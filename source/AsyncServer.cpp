#include "AsyncServer.h"
#include <boost/thread.hpp>
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <algorithm>

namespace SmartMet
{
namespace Server
{
// Number of threads for asynchronous reads and writes

AsyncServer::AsyncServer(const SmartMet::Spine::Options& theOptions,
                         SmartMet::Spine::Reactor& theReactor,
                         std::size_t numThreads)
    : Server(theOptions, theReactor),
      itsConnections(0),
      numThreads(std::max(numThreads, std::size_t{1U}))
{
  try
  {
    // Fire the connect accept loop
    startAccept();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void AsyncServer::run()
{
  try
  {
    // Create some threads to handle connection accepting and asynchronous events
    boost::thread_group workerThreads;
    for (std::size_t i = 0; i < numThreads; ++i)
    {
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      workerThreads.add_thread(new boost::thread(&AsyncServer::serverThreadFunction, this, i));
    }

    // Start the Admin Thread Pool Executor
    itsAdminExecutor.start();

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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void AsyncServer::shutdown()
{
  try
  {
    // Shutting down the server socket i.e we should not accept any more connections.
    itsAcceptor.close();

    // Shutdown the IO service - does not block (is this necessary after the above?)
    itsIoService.stop();

    // Shutdown the thread pools
    itsAdminExecutor.setGracefulShutdown(true);
    itsSlowExecutor.setGracefulShutdown(true);
    itsFastExecutor.setGracefulShutdown(true);
    itsAdminExecutor.shutdown();
    itsSlowExecutor.shutdown();
    itsFastExecutor.shutdown();

    // Shutdown the reactor (i.e. plugins and engines)
    itsReactor.shutdown();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
    itsNewConnection = AsyncConnection::create(this,
                                               itsEncryptionEnabled,
                                               itsEncryptionContext,
                                               itsCanGzip,
                                               itsCompressLimit,
                                               itsMaxRequestSize,
                                               itsTimeout,
                                               itsDumpRequests,
                                               itsIoService,
                                               itsReactor,
                                               itsAdminExecutor,
                                               itsSlowExecutor,
                                               itsFastExecutor);
    itsAcceptor.async_accept(itsNewConnection->socket(),
                             [this](const boost::system::error_code& err)
                             { this->handleAccept(err); });
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

namespace
{

void setThreadName(unsigned index)
{
#if defined(BOOST_THREAD_PLATFORM_PTHREAD)
  const std::string name = fmt::format("std-wrk-{:04}", index);
  const std::string thread_name = name.substr(0, 15).c_str();
  pthread_setname_np(pthread_self(), thread_name.c_str());
#endif
}

}  // namespace

void AsyncServer::serverThreadFunction(unsigned index)
try
{
  setThreadName(index);
  itsIoService.run();
}
catch (...)
{
  auto error = Fmi::Exception::Trace(BCP, "Operation failed!");
  std::cerr << "Async server thread " << index << " terminated with exception: " << error.what()
            << std::endl;
  throw error;
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Server
}  // namespace SmartMet
