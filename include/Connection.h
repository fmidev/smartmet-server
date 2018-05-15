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
#include <boost/thread.hpp>

#include <macgyver/ThreadPool.h>

#include <spine/Reactor.h>
#include <spine/Thread.h>

namespace SmartMet
{
namespace Server
{
typedef Fmi::ThreadPool::ThreadPool<Fmi::ThreadPool::FifoScheduler> ThreadPoolType;

class Server;

class Connection
{
 public:
  explicit Connection(Server* theServer,
                      bool canGzipResponse,
                      std::size_t compressLimit,
                      std::size_t maxRequestSize,
                      long timeout,
                      bool dumpRequests,
                      boost::asio::io_service& io_service,
                      SmartMet::Spine::Reactor& serverReactor,
                      ThreadPoolType& slowExecutor,
                      ThreadPoolType& fastExecutor);

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

 protected:
  /// Handle to the server instance which spawned this connection
  Server* itsServer;

  /// Socket for the connection.
  boost::asio::ip::tcp::socket itsSocket;

  /// Its associated Io Service
  boost::asio::io_service& itsIoService;

  /// Slow thread pool
  ThreadPoolType& itsSlowExecutor;

  /// Fast thread pool
  ThreadPoolType& itsFastExecutor;

  /// The reactor contains request processing facilities.
  SmartMet::Spine::Reactor& itsReactor;

  /// Connection timeout timer
  std::unique_ptr<boost::asio::deadline_timer> itsTimeoutTimer;

  /// Socket reads into this buffer
  boost::array<char, 8192> itsSocketBuffer;

  /// Entire received data.
  std::string itsBuffer;

  /// The incoming request.
  std::unique_ptr<SmartMet::Spine::HTTP::Request> itsRequest;

  /// The reply to be sent back to the client.
  std::unique_ptr<SmartMet::Spine::HTTP::Response> itsResponse;

  /// Number of received bytes in the buffer
  std::size_t itsReceivedBytes;

  /// Response string to be written to socket
  std::string itsResponseString;

  /// Flag to say if we can (attempt to) gzip response
  bool itsCanGzipResponse;

  /// Response compression limit
  std::size_t itsCompressLimit;

  // Maximum request size (0=unlimited)
  std::size_t itsMaxRequestSize;

  /// Connection timeout in seconds
  long itsTimeout;

  /// Timeout flag
  bool hasTimedOut;

  /// Mutex for this connection
  SmartMet::Spine::MutexType itsMutex;

  // Flag to signal if query the connection is handling is fast or slow
  bool itsQueryIsFast;

  // Flag to see if requests should be dumped to stdout
  bool itsDumpRequests;

  // The current status of the client connection
  boost::system::error_code itsFinalStatus;
};

}  // namespace Server
}  // namespace SmartMet
