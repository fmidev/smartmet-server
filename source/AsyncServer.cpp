#include "AsyncServer.h"
#include <boost/thread.hpp>
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/ThreadName.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace SmartMet
{
namespace Server
{
// Number of threads for asynchronous reads and writes

AsyncServer::AsyncServer(SmartMet::Spine::Options& theOptions,
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
    // Create some threads to handle connection accepting and asynchronous events.
    // Publish the worker count before spawning so shutdown() can wait for them to drain.
    itsRunningWorkers.store(numThreads);
    boost::thread_group workerThreads;
    for (std::size_t i = 0; i < numThreads; ++i)
    {
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      workerThreads.add_thread(new boost::thread(&AsyncServer::serverThreadFunction, this, i));
    }

    // Start the Admin Thread Pool Executor
    itsAdminExecutor->start();

    // Start the Slow Thread Pool Executor
    itsSlowExecutor->start();

    // Start the Fast Thread Pool Executor
    itsFastExecutor->start();

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

    // Wait for the io_service worker threads to leave io_service::run() before touching the
    // executors. A completion handler still running after stop() may call executor->schedule()
    // (via AsyncConnection::scheduleChunkGetter); letting that race with the reset() below
    // would be a use-after-free on the pool. Bounded so shutdown itself cannot hang here.
    for (int waited = 0; itsRunningWorkers.load() > 0 && waited < 2000; ++waited)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Shutdown the thread pools
    itsAdminExecutor->setGracefulShutdown(true);
    itsSlowExecutor->setGracefulShutdown(true);
    itsFastExecutor->setGracefulShutdown(true);
    itsAdminExecutor->shutdown();
    itsSlowExecutor->shutdown();
    itsFastExecutor->shutdown();

    // Destroy the pools before the reactor unloads the plugins. Destroying a pool also
    // destroys its task queue, dropping any request task still queued there. Each such task
    // pins an AsyncConnection -> Response -> plugin-created response streamer; releasing them
    // now, while the plugins' shared libraries are still mapped, avoids destroying that
    // plugin code after it has been dlclose()'d (which crashed at process exit otherwise).
    // Safe because the io_service workers have stopped, so nothing can schedule onto the
    // pools anymore.
    itsAdminExecutor.reset();
    itsSlowExecutor.reset();
    itsFastExecutor.reset();

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
                                               *itsAdminExecutor,
                                               *itsSlowExecutor,
                                               *itsFastExecutor);
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
  Fmi::set_thread_name(fmt::format("srv-wrk-{:04}", index));
}

}  // namespace

void AsyncServer::serverThreadFunction(unsigned index)
{
  // Signal to shutdown() that this worker has left io_service::run(), on both normal and
  // exceptional exit, so it can safely tear the executors down afterwards.
  struct WorkerExitGuard
  {
    std::atomic<std::size_t>& count;
    ~WorkerExitGuard() { --count; }
  } guard{itsRunningWorkers};

  try
  {
    setThreadName(index);
    itsIoService.run();
  }
  catch (...)
  {
    auto error = Fmi::Exception::Trace(BCP, "Operation failed!");
    std::cerr << "Async server thread " << index << " terminated with exception: " << error.what()
              << '\n';
    throw error;
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Server
}  // namespace SmartMet
