// ======================================================================
/*!
 * \brief Declaration of Connection object
 *
 * Connection represents a single connection established with the server
 * Actual connection objects derive from this
 */
// ======================================================================

#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/thread.hpp>
#include <macgyver/ThreadPool.h>
#include <spine/Reactor.h>
#include <spine/Thread.h>
#include <memory>

using ssl_socket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;

namespace SmartMet
{
namespace Server
{
using ThreadPoolType = Fmi::ThreadPool::ThreadPool<Fmi::ThreadPool::FifoScheduler>;

class Server;

class Connection
{
 public:
  using DeadlineTimer = boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

  explicit Connection(Server* theServer,
                      bool encryptionEnabled,
                      boost::asio::ssl::context& sslContext,
                      bool canGzipResponse,
                      std::size_t compressLimit,
                      std::size_t maxRequestSize,
                      long timeout,
                      bool dumpRequests,
                      boost::asio::io_context& io_service,
                      SmartMet::Spine::Reactor& theReactor,
                      ThreadPoolType& adminExecutor,
                      ThreadPoolType& slowExecutor,
                      ThreadPoolType& fastExecutor);

  Connection() = delete;
  Connection(const Connection& other) = delete;
  Connection(Connection&& other) = delete;
  Connection& operator=(const Connection& other) = delete;
  Connection& operator=(Connection&& other) = delete;

  // ======================================================================
  /*!
   * \brief Destructs a connection
   */
  // ======================================================================

  virtual ~Connection();

  // ======================================================================
  /*!
   * \brief Initiates the connection processing
   */
  // ======================================================================

  virtual void start() = 0;

  // ======================================================================
  /*!
   * \brief Gets the socket associated with this connection
   */
  // ======================================================================

  boost::asio::ip::tcp::socket& socket();

  void reportError(const std::string& message) const;
  void reportInfo(const std::string& message) const;

 protected:
  /// Handle to the server instance which spawned this connection
  Server* itsServer = nullptr;

  /// SSL / TLS enabled
  bool itsEncryptionEnabled = false;

  /// Socket for the connection. When SSL is disabled, we just use the socket-part
  boost::asio::ssl::stream<boost::asio::ip::tcp::socket> itsSocket;

  /// Its associated Io Service
  boost::asio::io_context& itsIoService;

  /// Admin thread pool
  ThreadPoolType& itsAdminExecutor;

  /// Slow thread pool
  ThreadPoolType& itsSlowExecutor;

  /// Fast thread pool
  ThreadPoolType& itsFastExecutor;

  /// The reactor contains request processing facilities.
  SmartMet::Spine::Reactor& itsReactor;

  /// Connection timeout timer
  std::unique_ptr<DeadlineTimer> itsTimeoutTimer;

  /// Socket reads into this buffer
  std::array<char, 8192> itsSocketBuffer;

  /// Entire received data.
  std::string itsBuffer;

  /// The incoming request.
  std::unique_ptr<SmartMet::Spine::HTTP::Request> itsRequest;

  /// The reply to be sent back to the client.
  std::unique_ptr<SmartMet::Spine::HTTP::Response> itsResponse;

  /// Number of received bytes in the buffer
  std::size_t itsReceivedBytes = 0;

  /// Response string to be written to socket
  std::string itsResponseString;

  /// Flag to say if we can (attempt to) gzip response
  bool itsCanGzipResponse = false;

  /// Response compression limit
  std::size_t itsCompressLimit = 0;

  // Maximum request size (0=unlimited)
  std::size_t itsMaxRequestSize;

  /// Connection timeout in seconds
  long itsTimeout = 0;

  /// Persistent connections enabled server wide
  bool itsKeepAliveEnabled = false;

  /// Seconds an idle persistent connection waits for the next request
  long itsKeepAliveTimeout = 0;

  /// Maximum number of requests to serve over this connection (0 = unlimited)
  std::size_t itsMaxKeepAliveRequests = 0;

  /// Maximum size of a request's header section in bytes (0 = unlimited)
  std::size_t itsMaxHeaderSize = 0;

  /// Number of requests parsed from this connection so far
  std::size_t itsRequestCount = 0;

  /// True if the connection is to be reused after the response currently being
  /// produced. Reset to false at the start of every request/response cycle, so
  /// any path that has not explicitly negotiated persistence closes the socket.
  bool itsKeepAlive = false;

  /// HTTP version to answer the current request with, "1.0" or "1.1"
  std::string itsResponseVersion{"1.0"};

  /// True while an already used persistent connection is waiting for the next request
  std::atomic<bool> itsIdle = false;

  /// True once this connection has been counted against the server's connection
  /// limit, so that the destructor decrements exactly what start() incremented.
  bool itsCounted = false;

  /// The server's shutdown flag. Co-owned because the connection may outlive the
  /// server object, see Server::getShutdownFlag().
  std::shared_ptr<const std::atomic<bool>> itsShutdownFlag;

  /// The server's live connection counter, co-owned for the same reason
  std::shared_ptr<std::atomic<std::size_t>> itsConnectionCount;

  // ======================================================================
  /*!
   * \brief Count this connection against the server's connection limit
   *
   * Called once the connection has actually been accepted, not when the object
   * is created: the server keeps one unused connection object around waiting
   * for the next accept, and that one must not occupy a slot.
   */
  // ======================================================================

  void registerConnection();

  /// Timeout flag
  std::atomic<bool> hasTimedOut = false;

  /// Mutex for this connection
  SmartMet::Spine::MutexType itsMutex;

  // Flag to signal if query the connection is handling is fast or slow
  bool itsQueryIsFast = false;

  // Flag to signal if the query the connection is handling is an admin query
  bool itsAdminQuery = false;

  // Flag to see if requests should be dumped to stdout
  bool itsDumpRequests = false;

  // The current status of the client connection
  boost::system::error_code itsFinalStatus;
};

}  // namespace Server
}  // namespace SmartMet
