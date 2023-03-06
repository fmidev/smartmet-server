// ======================================================================
/*!
 * \brief Server HTTP Server for SmartMet
 * Abstract base class for Server server types
 *
 * Based on Boost ASIO (2003-2012 Christopher M. Kohlhoff (chris at kohlhoff dot com))
 *
 */
// ======================================================================

#pragma once

#include "Connection.h"
#include <boost/asio.hpp>
#include <spine/Options.h>
#include <spine/Reactor.h>

namespace SmartMet
{
namespace Server
{
// ======================================================================
/*!
 * \brief Declaration of the Server object
 *
 * Server is the top level object in the server hierarchy. It contains
 * the Reactor (which in turn contains the server content functionality) along
 * with network communication and processing facilities.
 */
// ======================================================================
class Server
{
 public:
  Server() = delete;
  Server(const Server& other) = delete;
  Server(Server&& other) = delete;
  Server& operator=(const Server& other) = delete;
  Server& operator=(Server&& other) = delete;
  Server(const SmartMet::Spine::Options& theOptions, SmartMet::Spine::Reactor& theReactor);

  // ======================================================================
  /*!
   * \brief Starts the server.
   *
   * Fires up the server. The calling thread will block until shutdown
   * to the server instance has been signaled.
   */
  // ======================================================================

  virtual void run() = 0;
  void shutdownServer();
  bool isShutdownRequested() const;

  virtual ~Server() = default;

 protected:
  virtual void shutdown();
  virtual std::string getPassword() const;

  /// The io_service used to perform asynchronous operations.
  boost::asio::io_service itsIoService;

  bool itsEncryptionEnabled;
  std::string itsEncryptionPassword;

  boost::asio::ssl::context itsEncryptionContext;

  /// Acceptor used to listen for incoming connections.
  boost::asio::ip::tcp::acceptor itsAcceptor;

  /// This contains HTTP request handling functionality
  SmartMet::Spine::Reactor& itsReactor;

  /// The Slow Thread Pool Executor for asynchronous processing of slow requests
  ThreadPoolType itsSlowExecutor;

  /// The Fast Thread Pool Executor for asynchronous processing of fast requests
  ThreadPoolType itsFastExecutor;

  /// Flag to enable response gzip compression
  bool itsCanGzip;

  /// Compression limit for responses
  std::size_t itsCompressLimit;

  // Maximum request size (0=unlimited)
  std::size_t itsMaxRequestSize;

  /// Connection timeout in seconds
  long itsTimeout;

  /// Flag if requests should be dumped to stdout
  bool itsDumpRequests;

  /// This is true if the shutdown is requested. The server should not accept any more connections.
  bool itsShutdownRequested;
};

}  // namespace Server
}  // namespace SmartMet
