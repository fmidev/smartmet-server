#include "AsyncServer.h"
#include <spine/Exception.h>
#include <boost/thread.hpp>
#include <boost/bind.hpp>

namespace SmartMet
{
namespace Server
{
// Number of threads for asynchronous reads and writes
#ifndef ASYNC_THREAD_SIZE
#define ASYNC_THREAD_SIZE 6
#endif

AsyncServer::AsyncServer(const SmartMet::Spine::Options& theOptions,
                         SmartMet::Spine::Reactor& theReactor)
    : Server(theOptions, theReactor), itsNewConnection(), itsMutex(), itsConnections(0)

{
  try
  {
    // Fire the connect accept loop
    startAccept();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void AsyncServer::run()
{
  try
  {
    // Create some threads to handle connection accepting and asynchronous events
    boost::thread_group workerThreads;
    for (std::size_t i = 0; i < ASYNC_THREAD_SIZE; ++i)
    {
      workerThreads.add_thread(
          new boost::thread(boost::bind(&boost::asio::io_service::run, &itsIoService)));
    }

    // Start the Slow Thread Pool Executor
    itsSlowExecutor.start();

    // Start the Fast Thread Pool Executor
    itsFastExecutor.start();

    // Wait for all threads in the pool to exit.
    // Main thread waits here

    workerThreads.join_all();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void AsyncServer::shutdown()
{
  try
  {
    // STEP 1: Shutting down the server socket i.e we should not accept any more connections.

    itsAcceptor.close();

    // STEP 2: Shutdown the reactor (i.e. plugins and engines)

    itsReactor.shutdown();

    // STEP 3: Shutdown the thread pools

    // Let's sleep few seconds, because there might be still some
    // data that needs to be tranported.

    boost::this_thread::sleep(boost::posix_time::milliseconds(5000));

    itsSlowExecutor.setGracefulShutdown(true);
    itsFastExecutor.setGracefulShutdown(true);
    itsSlowExecutor.shutdown();
    itsFastExecutor.shutdown();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void AsyncServer::startAccept()
{
  try
  {
    if (itsShutdownRequested)
      return;

    // Make a new connection object and let it wait for an incoming connection
    // This should not need locking, since we accept connections from a single socket (handleAccepts
    // are implicity serialized)
    itsNewConnection.reset(new AsyncConnection(this,
                                               itsCanGzip,
                                               itsCompressLimit,
                                               itsTimeout,
                                               itsDumpRequests,
                                               itsIoService,
                                               itsReactor,
                                               itsSlowExecutor,
                                               itsFastExecutor));
    itsAcceptor.async_accept(
        itsNewConnection->socket(),
        boost::bind(&AsyncServer::handleAccept, this, boost::asio::placeholders::error));
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

void AsyncServer::handleAccept(const boost::system::error_code& e)
{
  try
  {
    if (itsShutdownRequested)
      return;

    if (!e)
    {
      // Start processing the new connection
      itsNewConnection->start();
    }

    // Go back to listen for the next connection
    startAccept();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception(BCP, "Operation failed!", NULL);
  }
}

}  // namespace Server
}  // namespace SmartMet
