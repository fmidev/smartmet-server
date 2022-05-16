//===============================================
/*
 * Definition of the Connection base class
 */
//===============================================

#include "Connection.h"
#include "Server.h"

#include <macgyver/Exception.h>

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
                       boost::asio::io_service& io_service,
                       SmartMet::Spine::Reactor& theReactor,
                       ThreadPoolType& slowExecutor,
                       ThreadPoolType& fastExecutor)
    : itsServer(theServer),
      itsEncryptionEnabled(encryptionEnabled),
      itsSocket(io_service,sslContext),
      itsIoService(io_service),
      itsSlowExecutor(slowExecutor),
      itsFastExecutor(fastExecutor),
      itsReactor(theReactor),
      itsSocketBuffer(),
      itsRequest(new SmartMet::Spine::HTTP::Request),
      itsResponse(new SmartMet::Spine::HTTP::Response),
      itsReceivedBytes(0),
      itsCanGzipResponse(canGzipResponse),
      itsCompressLimit(compressLimit),
      itsMaxRequestSize(maxRequestSize),
      itsTimeout(timeout),
      hasTimedOut(false),
      itsQueryIsFast(false),
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
#ifndef NDEBUG
    std::cout << "Slow pool queue size after connection: " << itsSlowExecutor.getQueueSize()
              << std::endl;
    std::cout << "Fast pool queue size after connection: " << itsFastExecutor.getQueueSize()
              << std::endl;
#endif
  }
}

}  // namespace Server
}  // namespace SmartMet
