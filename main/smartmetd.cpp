#include "AsyncServer.h"

#include <macgyver/AnsiEscapeCodes.h>
#include <macgyver/AsyncTaskGroup.h>
#include <macgyver/Exception.h>
#include <macgyver/StaticCleanup.h>
#include <spine/Backtrace.h>
#include <spine/Convenience.h>
#include <spine/HTTP.h>
#include <spine/Options.h>
#include <spine/Reactor.h>
#include <spine/SmartMet.h>
#include <sys/select.h>
#include <sys/types.h>
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <new>
#include <thread>

// libdw from elfutils-devel provides more details than libbfd
#define BACKWARD_HAS_DW 1
#include "backward.h"

namespace
{
std::unique_ptr<SmartMet::Server::Server> server;
std::unique_ptr<SmartMet::Spine::Reactor> reactor;
std::unique_ptr<Fmi::AsyncTaskGroup> tasks;

std::atomic<int> last_signal(0);

// Allowed core dump signals. Note that we choose to ignore SIGBUS though due to
// NFS problems, and SIGQUIT since we want to allow quitting via keyboard

std::vector<int> core_signals{SIGILL, SIGABRT, SIGFPE, SIGSYS, SIGSEGV, SIGXCPU, SIGXFSZ};

void crash_signal_handler(int sig)
{
  std::cout << "---------------------------------------------------------\n";
  std::cout << SmartMet::Spine::log_time_str() << " THE SYSTEM IS GOING DOWN (signal=" << sig
            << ")\n";
  std::cout << "---------------------------------------------------------\n";
  auto requests = reactor->getActiveRequests();
  for (const auto& request : requests)
  {
    std::cout << request.second.request.getURI() << "\n";
    std::cout << "---------------------------------------------------------\n";
  }
  static_cast<void>(signal(sig, SIG_DFL));
  static_cast<void>(raise(sig));
}

void signal_handler(int sig)
{
  last_signal = sig;
}
// Block all signals except profiling
void block_signals()
{
  sigset_t signal_set;
  sigfillset(&signal_set);

  // Enable debugging
  sigdelset(&signal_set, SIGPROF);

  // Enable core dumps from fatal errors
  for (auto sig : core_signals)
    sigdelset(&signal_set, sig);

  pthread_sigmask(SIG_BLOCK, &signal_set, nullptr);
}

template <std::size_t maxCount>
class MaxEventFreq
{
 public:
  MaxEventFreq(std::size_t timeWindowInSec) : itsCount(0), itsTimeWindowInSec(timeWindowInSec) {}

  void check()
  {
    std::lock_guard<std::mutex> lock(itsMutex);
    const auto now = std::chrono::steady_clock::now();
    const auto oldest = now - std::chrono::seconds(itsTimeWindowInSec);

    std::size_t i;
    for (i = 0; i < itsCount && itsTimePoints[i] < oldest; ++i)
    {
    }

    for (std::size_t j = 0; j + i < itsCount; ++j)
    {
      itsTimePoints[j] = itsTimePoints[i + j];
    }

    itsCount -= i;
    if (itsCount >= maxCount)
      throw std::runtime_error("Too many errors in time window");

    itsTimePoints[itsCount++] = now;
  }

 private:
  std::mutex itsMutex;
  std::size_t itsCount;
  std::size_t itsTimeWindowInSec;
  std::array<std::chrono::steady_clock::time_point, maxCount> itsTimePoints;
};

MaxEventFreq<10> maxEventFreq(60);

[[noreturn]] void bad_alloc_new_handler()
try
{
  using namespace SmartMet::Spine;

  maxEventFreq.check();

  // Print out the active requests (only once in case of multiple terminate calls)
  const ActiveRequests::Requests requests = reactor->getActiveRequests();
  std::cout << log_time_str() << ANSI_BOLD_ON << ANSI_FG_RED
            << " Active requests at the time of throwing std::bad_alloc" << ANSI_BOLD_OFF
            << ANSI_FG_DEFAULT << '\n';
  std::cout << requests << '\n';

  const std::string backtrace = Backtrace::make_backtrace();
  std::cout << log_time_str() << ANSI_BOLD_ON << ANSI_FG_RED
            << " Backtrace at the time of throwing std::bad_alloc" << ANSI_BOLD_OFF
            << ANSI_FG_DEFAULT << '\n';
  std::cout << backtrace << '\n';

  throw std::bad_alloc();
}
catch (...)
{
  // This is a fallback in case the std::bad_alloc handler fails
  std::cerr << "Failed to throw std::bad_alloc from terminate_new_handler or too many out of "
               "memory errors in short time\n";
  std::terminate();
}

[[noreturn]] void terminate_new_handler()
try
{
  // Throw std::bad_alloc to have it in exception trace for terminate handler
  throw std::bad_alloc();
}
catch (...)
{
  // std::terminate handler will handle loggin - no need to log anything here
  std::terminate();
}

void set_new_handler(const std::string& name)
{
  if (name == "default")  // system default may change in C++20
    std::set_new_handler(&bad_alloc_new_handler);
  else if (name == "bad_alloc")
    std::set_new_handler(&bad_alloc_new_handler);
  else if (name == "terminate")
    std::set_new_handler(&terminate_new_handler);
  else
    throw Fmi::Exception(BCP, "Unknown new_handler").addParameter("name", name);
}

}  // anonymous namespace

int main(int argc, char* argv[])
{
  try
  {
    // Ensure that the registrated static object cleanup is done before the exit
    Fmi::StaticCleanup::AtExit cleanup;

    // Parse options

    SmartMet::Spine::Options options;
    if (!options.parse(argc, argv))
      exit(1);  // NOLINT - no threads yet

    // Use the system locale or autocomplete may not work properly (iconv requirement)
    static_cast<void>(std::setlocale(LC_ALL, ""));  // NOLINT - no threads yet

    // Set new_handler
    set_new_handler(options.new_handler);

    const auto signals_init = []()
    {
      // Block signals before starting new threads

      block_signals();

      // Set special SIGTERM, SIGBUS and SIGWINCH handlers

      struct sigaction action;             // NOLINT(cppcoreguidelines-pro-type-member-init)
      action.sa_handler = signal_handler;  // NOLINT(cppcoreguidelines-pro-type-union-access)
      sigemptyset(&action.sa_mask);
      action.sa_flags = 0;
      sigaction(SIGBUS, &action, nullptr);
      sigaction(SIGTERM, &action, nullptr);
      sigaction(SIGWINCH, &action, nullptr);

      // We also want to record other common non-core dumping signals
      sigaction(SIGHUP, &action, nullptr);
      sigaction(SIGINT, &action, nullptr);

      struct sigaction action2;                   // NOLINT(cppcoreguidelines-pro-type-member-init)
      action2.sa_handler = crash_signal_handler;  // NOLINT(cppcoreguidelines-pro-type-union-access)
      sigemptyset(&action2.sa_mask);
      action2.sa_flags = 0;
      sigaction(SIGSEGV, &action2, nullptr);
    };

    std::unique_ptr<backward::SignalHandling> sh;
    if (options.stacktrace)
      sh.reset(new backward::SignalHandling(core_signals));

    reactor.reset(new SmartMet::Spine::Reactor(options));

    //
    // Default handler for shutdown timeout is to abort the process.
    // We do not however want to cause coredump in this case.
    //
    reactor->onShutdownTimedOut([]() -> void { kill(getpid(), SIGKILL); });

    unsigned server_threads = SmartMet::Server::AsyncServer::DEFAULT_ASYNC_THREAD_SIZE;
    if (options.itsConfig.exists("server_threads"))
    {
      options.itsConfig.lookupValue("server_threads", server_threads);
      if (server_threads < 1)
        server_threads = SmartMet::Server::AsyncServer::DEFAULT_ASYNC_THREAD_SIZE;
    }

    server.reset(new SmartMet::Server::AsyncServer(options, *reactor, server_threads));

    tasks.reset(new Fmi::AsyncTaskGroup);

    tasks->on_task_error(
        [](const std::string& /* dummy*/)
        {
          if (server)
            server->shutdownServer();
          else
            throw;
        });

    tasks->stop_on_error(true);

    tasks->add("SmartMet::Spine::Reactor::init",
               [signals_init]()
               {
                 signals_init();
                 reactor->init();
               });

    tasks->add("Run SmartMet::Server::AsyncServer",
               [signals_init]()
               {
                 signals_init();
                 std::this_thread::sleep_for(std::chrono::seconds(3));
                 std::cout << ANSI_BG_GREEN << ANSI_BOLD_ON << ANSI_FG_WHITE
                           << "Launched Synapse server" << ANSI_FG_DEFAULT << ANSI_BOLD_OFF
                           << ANSI_BG_DEFAULT << '\n';
                 server->run();
               });

    // Sleep until a signal comes, and then either handle it or exit

    double sigint_cnt = 0;

    while (true)
    {
      struct timeval tv;
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      errno = 0;
      if (select(0, nullptr, nullptr, nullptr, &tv) >= 0)
      {
        tasks->handle_finished();
        // FIXME: handle case when all tasks have ended
      }
      else if (errno != EINTR)
      {
        std::array<char, 1024> msg;
        if (strerror_r(errno, msg.data(), 1024) == nullptr)
        {
          std::cout << ANSI_BG_RED << ANSI_BOLD_ON << ANSI_FG_WHITE
                    << "Unexpected error code from select(): " << msg.data() << ANSI_FG_DEFAULT
                    << ANSI_BOLD_OFF << ANSI_BG_DEFAULT << '\n';
        }
      }

      int sig = last_signal;

      if (SmartMet::Spine::Reactor::isShutdownFinished())
      {
        tasks->stop();
        server->shutdownServer();
        tasks->wait();
        return 0;
      }

      if (sig != 0)
      {
        std::cout << "\n"
                  << ANSI_BG_RED << ANSI_BOLD_ON << ANSI_FG_WHITE << "Signal '" << strsignal(sig)
                  << "' (" << sig << ") received ";

        if (sig == SIGBUS || sig == SIGWINCH)
        {
          std::cout << " - ignoring it!" << ANSI_FG_DEFAULT << ANSI_BOLD_OFF << ANSI_BG_DEFAULT
                    << '\n';
          last_signal = 0;
        }
        else if (sig == SIGTERM)
        {
          std::cout << " - shutting down!" << ANSI_FG_DEFAULT << ANSI_BOLD_OFF << ANSI_BG_DEFAULT
                    << '\n';

          tasks->stop();
          server->shutdownServer();
          tasks->wait();

          // Save heap profile if it has been enabled
          // mallctl("prof.dump", nullptr, nullptr, nullptr, 0);
          return 0;
        }
        else if (sig == SIGINT)
        {
          last_signal = 0;
          Fmi::AsyncTask shutdown_timer(
              "Shutdown timer",
              [&sigint_cnt]()
              {
                for (int i = 0; i < 1200; i++)
                {
                  boost::this_thread::sleep_for(boost::chrono::milliseconds(50));
                  if (last_signal == SIGINT || last_signal == SIGTERM)
                  {
                    last_signal = 0;
                    sigint_cnt++;
                    if (sigint_cnt > 5)
                    {
                      std::cout << "*** Too many SIGINT or SIGTERM signals after first SIGINT. "
                                   "Commiting suicide\n";
                      abort();
                    }
                  }
                  else
                  {
                    sigint_cnt = std::max(0.0, sigint_cnt - 0.05);
                  }
                }
                std::cout << "*** Timed out waiting for server to shut down after SIGINT\n";
                abort();
              });
          std::cout << " - shutting down!" << ANSI_FG_DEFAULT << ANSI_BOLD_OFF << ANSI_BG_DEFAULT
                    << '\n';

          tasks->stop();
          server->shutdownServer();
          tasks->wait();
          shutdown_timer.cancel();

          // Save heap profile if it has been enabled
          // mallctl("prof.dump", nullptr, nullptr, nullptr, 0);
          return 0;
        }
        else
        {
          std::cout << " - exiting!" << ANSI_FG_DEFAULT << ANSI_BOLD_OFF << ANSI_BG_DEFAULT << '\n';
          break;
        }
      }
    }

    return 666;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
    exception.printError();

    return -1;
  }
}
