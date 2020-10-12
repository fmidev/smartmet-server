#include "Server.h"

#include <macgyver/Exception.h>

#include <jemalloc/jemalloc.h>

#include <cmath>

namespace SmartMet
{
namespace Server
{
Server::Server(const SmartMet::Spine::Options& theOptions, SmartMet::Spine::Reactor& theReactor)
    : itsIoService(),
      itsAcceptor(itsIoService),
      itsReactor(theReactor),
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
      std::cout << "Error: Unable to bind listening socket to port " << theOptions.port
                << std::endl;
      throw;
    }

#ifndef NDEBUG
    std::cout << "Bind completed to port " << theOptions.port << std::endl;
#endif
    // Start listening for connections
    itsAcceptor.listen();
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

}  // namespace Server
}  // namespace SmartMet
