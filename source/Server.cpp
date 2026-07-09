#include "Server.h"
#include <macgyver/Exception.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
#define SMARTMETD_SSL_METHOD boost::asio::ssl::context::tlsv13
#else
#define SMARTMETD_SSL_METHOD boost::asio::ssl::context::tlsv12
#endif

namespace SmartMet
{
namespace Server
{
Server::Server(SmartMet::Spine::Options& theOptions, SmartMet::Spine::Reactor& theReactor)
    : itsEncryptionEnabled(theOptions.encryptionEnabled),
      itsEncryptionPassword(theOptions.encryptionPassword),
      itsEncryptionContext(SMARTMETD_SSL_METHOD),
      itsAcceptor(itsIoService),
      itsMemoryLogTimer(itsIoService),
      itsReactor(theReactor),
      itsAdminExecutor(theOptions.adminpool.minsize, theOptions.adminpool.maxrequeuesize),
      itsSlowExecutor(theOptions.slowpool.minsize, theOptions.slowpool.maxrequeuesize),
      itsFastExecutor(theOptions.fastpool.minsize, theOptions.fastpool.maxrequeuesize),
      itsCanGzip(theOptions.compress),
      itsCompressLimit(theOptions.compresslimit),
      itsMaxRequestSize(theOptions.maxrequestsize),
      itsTimeout(theOptions.timeout),
      itsDumpRequests(theOptions.logrequests),
      itsShutdownRequested(false)
{
  try
  {
// Bind to the given port using given protocol
#ifndef NDEBUG
    std::cout << "Attempting to bind to port " << theOptions.port << std::endl;
#endif

    if (itsEncryptionEnabled)
    {
      if (!theOptions.encryptionPasswordFile.empty())
      {
        FILE* file = fopen(theOptions.encryptionPasswordFile.c_str(), "r");
        if (file == nullptr)
        {
          Fmi::Exception ex(BCP, "Cannot open the password file!");
          ex.addParameter("password_file", theOptions.encryptionPasswordFile);
          throw ex;
        }

        std::array<char, 100> st;
        if (fgets(st.data(), 100, file) == nullptr)
        {
          Fmi::Exception ex(BCP, "Cannot read the password!");
          ex.addParameter("password_file", theOptions.encryptionPasswordFile);
          throw ex;
        }

        char* p = strstr(st.data(), "\n");
        if (p != nullptr)
          *p = '\0';

        itsEncryptionPassword = st.data();
        std::cout << "[" << itsEncryptionPassword << "]\n";
        static_cast<void>(fclose(file));
      }

      itsEncryptionContext.set_options(boost::asio::ssl::context::tlsv13);

      itsEncryptionContext.set_password_callback(boost::bind(&Server::getPassword, this));
      itsEncryptionContext.use_certificate_chain_file(theOptions.encryptionCertificateFile);
      itsEncryptionContext.use_private_key_file(theOptions.encryptionPrivateKeyFile,
                                                boost::asio::ssl::context::pem);
      // itsEncryptionContext.use_tmp_dh_file("dh512.pem");
    }

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(),
                                            static_cast<unsigned short>(theOptions.port));
    itsAcceptor.open(endpoint.protocol());
    itsAcceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(
        true));  // Allows multiple servers to use same port
    try
    {
      itsAcceptor.bind(endpoint);
    }
    catch (const boost::system::system_error& err)
    {
      std::cout << "Error: Unable to bind listening socket to port " << theOptions.port << '\n';
      throw;
    }

    if (!theOptions.port)
    {
      theOptions.port = itsAcceptor.local_endpoint().port();
    }

#ifndef NDEBUG
    std::cout << "Bind completed to port " << theOptions.port << '\n';
#endif
    // Start listening for connections
    itsAcceptor.listen();

    // Optional periodic memory usage logging
    if (theOptions.itsConfig.exists("logmemoryuse"))
      theOptions.itsConfig.lookupValue("logmemoryuse", itsMemoryLogPeriod);
    if (theOptions.itsConfig.exists("logmemoryfields"))
    {
      const auto& arr = theOptions.itsConfig.lookup("logmemoryfields");
      itsMemoryLogFields.clear();
      for (int i = 0; i < arr.getLength(); ++i)
        itsMemoryLogFields.emplace_back(static_cast<const char*>(arr[i]));
    }
    if (itsMemoryLogPeriod > 0)
      scheduleMemoryLogging();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::string Server::getPassword() const
{
  try
  {
    return itsEncryptionPassword;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool Server::isShutdownRequested() const
{
  return itsShutdownRequested;
}

void Server::shutdownServer()
{
  try
  {
    // std::cout << "### Server::shutdownServer()\n";
    itsShutdownRequested = true;

    // Take heap snapshots before and after engine+plugin shutdown if profiling is enabled
    // mallctl("prof.dump", nullptr, nullptr, nullptr, 0);
    shutdown();
    // mallctl("prof.dump", nullptr, nullptr, nullptr, 0);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Server::shutdown()
{
  std::cout << "### Server::shutdown()\n";
}

namespace
{
using mallctl_t = int (*)(const char*, void*, std::size_t*, void*, std::size_t);

// jemalloc's runtime soname. jemalloc is never linked directly; in production it is
// injected via LD_PRELOAD (and is absent when e.g. libasan is preloaded instead).
constexpr const char* JEMALLOC_SONAME = "libjemalloc.so.2";

// Resolve jemalloc's mallctl() at runtime, or nullptr when jemalloc is not the active
// allocator. Resolved once.
//
// We deliberately look mallctl() up in the jemalloc library specifically rather than in
// the global symbol scope: another allocator could also export a mallctl() with an
// incompatible prototype, and calling that through our signature would crash. RTLD_NOLOAD
// returns a handle only if libjemalloc.so.2 is *already* resident (it never loads it), so
// a non-null handle both proves jemalloc is active and gives us jemalloc's own mallctl().
mallctl_t getMallctl()
{
  static const mallctl_t mallctl_ptr = []() -> mallctl_t
  {
    void* handle = dlopen(JEMALLOC_SONAME, RTLD_LAZY | RTLD_NOLOAD);
    if (handle == nullptr)
      return nullptr;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* ptr = reinterpret_cast<mallctl_t>(dlsym(handle, "mallctl"));
    dlclose(handle);
    return ptr;
  }();
  return mallctl_ptr;
}

// Read a size_t-valued jemalloc statistic via mallctl. Returns false if unavailable.
bool jemallocStat(mallctl_t mallctl, const char* name, std::size_t& value)
{
  std::size_t sz = sizeof(value);
  return mallctl(name, &value, &sz, nullptr, 0) == 0;
}

// Append jemalloc allocator/fragmentation statistics to the memory log line. A no-op
// when jemalloc is not the active allocator or was built without statistics support.
void appendJemallocStats(std::ostream& out)
{
  const mallctl_t mallctl = getMallctl();
  if (mallctl == nullptr)
    return;

  // jemalloc caches statistics and only refreshes them when the "epoch" is advanced.
  std::uint64_t epoch = 1;
  std::size_t epoch_sz = sizeof(epoch);
  if (mallctl("epoch", &epoch, &epoch_sz, &epoch, sizeof(epoch)) != 0)
    return;  // stats not enabled in this jemalloc build

  std::size_t allocated = 0;
  std::size_t active = 0;
  std::size_t resident = 0;
  std::size_t mapped = 0;
  std::size_t retained = 0;
  if (!jemallocStat(mallctl, "stats.allocated", allocated))
    return;  // built without --enable-stats
  jemallocStat(mallctl, "stats.active", active);
  jemallocStat(mallctl, "stats.resident", resident);
  jemallocStat(mallctl, "stats.mapped", mapped);
  jemallocStat(mallctl, "stats.retained", retained);

  out << " jemalloc.allocated=" << allocated << " jemalloc.active=" << active
      << " jemalloc.resident=" << resident << " jemalloc.mapped=" << mapped
      << " jemalloc.retained=" << retained;

  // External fragmentation: the fraction of page-backed (active) memory not actually
  // requested by the application. active is allocated rounded up to whole pages, so
  // active >= allocated holds for consistent stats.
  if (active > 0 && active >= allocated)
  {
    const double frag =
        100.0 * static_cast<double>(active - allocated) / static_cast<double>(active);
    std::array<char, 32> buf;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::snprintf(buf.data(), buf.size(), "%.1f%%", frag);
    out << " jemalloc.fragmentation=" << buf.data();
  }
}

void readAndLogMemoryUsage(const std::vector<std::string>& fields)
{
  std::ifstream status("/proc/self/status");
  if (!status)
    return;

  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(status, line))
  {
    const auto pos = line.find(':');
    if (pos == std::string::npos)
      continue;
    const std::string key = line.substr(0, pos);
    for (const auto& field : fields)
    {
      if (key == field)
      {
        std::string val = line.substr(pos + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        values[key] = val;
        break;
      }
    }
  }

  std::cout << "Memory:";
  for (const auto& field : fields)
  {
    const auto it = values.find(field);
    // Output empty value for missing fields so the user can spot typos in config
    std::cout << ' ' << field << '=' << (it != values.end() ? it->second : std::string{});
  }
  appendJemallocStats(std::cout);
  std::cout << '\n';
}
}  // namespace

void Server::scheduleMemoryLogging()
{
  itsMemoryLogTimer.expires_after(std::chrono::minutes(itsMemoryLogPeriod));
  itsMemoryLogTimer.async_wait([this](const boost::system::error_code& ec)
                               { handleMemoryLogTimer(ec); });
}

void Server::handleMemoryLogTimer(const boost::system::error_code& ec)
{
  if (ec || itsShutdownRequested)
    return;
  try
  {
    readAndLogMemoryUsage(itsMemoryLogFields);
  }
  catch (...)
  {
    // Silently ignore any errors during memory logging
  }
  scheduleMemoryLogging();
}

}  // namespace Server
}  // namespace SmartMet
