#include "Server.h"

#include <spine/Exception.h>

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
                                            (unsigned short)(theOptions.port));
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
      std::exit(1);
    }

#ifndef NDEBUG
    std::cout << "Bind completed to port " << theOptions.port << std::endl;
#endif
    // Start listening for connections
    itsAcceptor.listen();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

Server::~Server() {}

bool Server::isShutdownRequested()
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
    mallctl("prof.dump", NULL, NULL, NULL, 0);
    shutdown();
    mallctl("prof.dump", NULL, NULL, NULL, 0);
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void Server::shutdown()
{
  std::cout << "### Server::shutdown()\n";
}

}  // namespace Server
}  // namespace SmartMet
