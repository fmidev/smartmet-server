// ======================================================================
/*!
 * \brief Server HTTP Server for SmartMet (Half-Sync/Half-Async version)
 *
 * Based on Boost ASIO (2003-2012 Christopher M. Kohlhoff (chris at kohlhoff dot com))
 *
 */
// ======================================================================

#pragma once

#include "AsyncConnection.h"
#include "Server.h"
#include <boost/asio.hpp>
#include <spine/Options.h>
#include <spine/Reactor.h>
#include <spine/Thread.h>
#include <atomic>
#include <string>
#include <vector>

namespace SmartMet
{
namespace Server
{
// ======================================================================
/*!
 * \brief Declaration of the Async Server object
 *
 * This server is uses Half-Sync Half-Async (hahs) paradigm to handle
 * requests. Network operations (socket reading and writing, request
 * parsing) are done asynchronously with boost::asio IO service, while
 * content production is threadpooled (and in that sense, run synchronously).
 */
// ======================================================================
class AsyncServer : public Server
{
 public:
  constexpr static const std::size_t DEFAULT_ASYNC_THREAD_SIZE = 6;

  friend class AsyncConnection;

  // ======================================================================
  /*!
   * \brief Constructor
   *
   * Constructs a server using given options and the reactor object.
   */
  // ======================================================================

  explicit AsyncServer(SmartMet::Spine::Options& theOptions,
                       SmartMet::Spine::Reactor& theReactor,
                       std::size_t numThreads = DEFAULT_ASYNC_THREAD_SIZE);

  // ======================================================================
  /*!
   * \brief Starts the server.
   *
   * Fires up the server. The calling thread will block until shutdown
   * to the server instance has been signaled.
   */
  // ======================================================================

  void run() override;

 protected:
  void shutdown() override;

 private:
  // ======================================================================
  /*!
   * \brief Starts listening of incoming connections
   *
   */
  // ======================================================================

  void startAccept();

  // ======================================================================
  /*!
   * \brief Callback to be used when connection is accepted
   *
   */
  // ======================================================================

  void handleAccept(const boost::system::error_code& err);

  void serverThreadFunction(unsigned index);

  /// The next connection to be accepted.
  AsyncConnection::ConnectionPtr itsNewConnection;

  // Mutex for connection number update
  SmartMet::Spine::MutexType itsMutex;

  // Current number of connections
  std::size_t itsConnections = 0;

  // Number of threads for asynchronous reads and writes
  std::size_t numThreads;

  // Number of io_service worker threads currently running run(). shutdown() waits for this
  // to reach zero after stopping the io_service, so no worker can still be executing a
  // completion handler (which might schedule onto an executor) when the executors are torn
  // down. Set before the workers are spawned, decremented as each leaves io_service::run().
  std::atomic<std::size_t> itsRunningWorkers{0};

  // True while the connection limit is being hit, so the log gets one line per
  // episode instead of one per refused connection.
  std::atomic<bool> itsConnectionLimitReported{false};
};

}  // namespace Server
}  // namespace SmartMet
