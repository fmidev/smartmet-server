//===============================================
/*
 * Definition of the Connection base class
 */
//===============================================

#include "Connection.h"
#include "Server.h"

#include <spine/Exception.h>

namespace SmartMet
{
namespace Server
{
Connection::Connection(Server* theServer,
                       bool canGzipResponse,
                       std::size_t compressLimit,
                       long timeout,
                       bool dumpRequests,
                       boost::asio::io_service& io_service,
                       SmartMet::Spine::Reactor& theReactor,
                       ThreadPoolType& slowExecutor,
                       ThreadPoolType& fastExecutor)
    : itsServer(theServer),
      itsSocket(io_service),
      itsIoService(io_service),
      itsSlowExecutor(slowExecutor),
      itsFastExecutor(fastExecutor),
      itsReactor(theReactor),
      itsTimeoutTimer(),
      itsSocketBuffer(),
      itsBuffer(),
      itsRequest(new SmartMet::Spine::HTTP::Request),
      itsResponse(new SmartMet::Spine::HTTP::Response),
      itsReceivedBytes(0),
      itsResponseString(),
      itsCanGzipResponse(canGzipResponse),
      itsCompressLimit(compressLimit),
      itsTimeout(timeout),
      hasTimedOut(false),
      itsMutex(),
      itsQueryIsFast(false),
      itsDumpRequests(dumpRequests),
      itsFinalStatus(0, boost::system::system_category())
{
}

boost::asio::ip::tcp::socket& Connection::socket()
{
  return itsSocket;
}

Connection::~Connection()
{
  try
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
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

}  // namespace Server
}  // namespace SmartMet
