//===============================================
/*
 * Definition of the Connection base class
 */
//===============================================

#include "Connection.h"
#include "Server.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <spine/HostInfo.h>

namespace SmartMet
{
namespace Server
{
Connection::Connection(Server* theServer,
                       bool encryptionEnabled,
                       boost::asio::ssl::context& sslContext,
                       bool canGzipResponse,
                       std::size_t compressLimit,
                       std::size_t maxRequestSize,
                       long timeout,
                       bool dumpRequests,
                       boost::asio::io_context& io_service,
                       SmartMet::Spine::Reactor& theReactor,
                       ThreadPoolType& adminExecutor,
                       ThreadPoolType& slowExecutor,
                       ThreadPoolType& fastExecutor)
    : itsServer(theServer),
      itsEncryptionEnabled(encryptionEnabled),
      itsSocket(io_service, sslContext),
      itsIoService(io_service),
      itsAdminExecutor(adminExecutor),
      itsSlowExecutor(slowExecutor),
      itsFastExecutor(fastExecutor),
      itsReactor(theReactor),
      itsRequest(new SmartMet::Spine::HTTP::Request),
      itsResponse(new SmartMet::Spine::HTTP::Response),
      itsCanGzipResponse(canGzipResponse),
      itsCompressLimit(compressLimit),
      itsMaxRequestSize(maxRequestSize),
      itsTimeout(timeout),
      itsDumpRequests(dumpRequests),
      itsFinalStatus(0, boost::system::system_category())
{
}

boost::asio::ip::tcp::socket& Connection::socket()
{
  return (boost::asio::ip::tcp::socket&)itsSocket.lowest_layer();
}

Connection::~Connection()
{
  if (!itsServer->isShutdownRequested())
  {
    // Call the client connection finished - hooks
    itsReactor.callClientConnectionFinishedHooks(itsRequest->getClientIP(), itsFinalStatus);
  }
}

void Connection::reportError(const std::string& message) const
{
  try
  {
    auto msg = fmt::format("{} Connection error: {}\n  - IP: {}\n  -HostName: {}\n  - URI: {}\n",
                           Fmi::to_iso_string(Fmi::SecondClock::local_time()),
                           message,
                           itsRequest->getClientIP(),
                           Spine::HostInfo::getHostName(itsRequest->getClientIP()),
                           itsRequest->getURI());
    std::cerr << msg << std::flush;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Connection::reportInfo(const std::string& message) const
{
  try
  {
    auto msg = fmt::format("{} Server info: {}\n  - IP: {}\n  -HostName: {}\n  - URI: {}\n",
                           Fmi::to_iso_string(Fmi::SecondClock::local_time()),
                           message,
                           itsRequest->getClientIP(),
                           Spine::HostInfo::getHostName(itsRequest->getClientIP()),
                           itsRequest->getURI());
    std::cerr << msg << std::flush;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Server
}  // namespace SmartMet
