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
  friend class AsyncConnection;

  // ======================================================================
  /*!
   * \brief Constructor
   *
   * Constructs a server using given options and the reactor object.
   */
  // ======================================================================

  explicit AsyncServer(const SmartMet::Spine::Options& theOptions,
                       SmartMet::Spine::Reactor& theReactor);

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

  /// The next connection to be accepted.
  AsyncConnection::ConnectionPtr itsNewConnection;

  // Mutex for connection number update
  SmartMet::Spine::MutexType itsMutex;

  // Current number of connections
  std::size_t itsConnections = 0;
};

}  // namespace Server
}  // namespace SmartMet
