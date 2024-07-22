// ======================================================================
/*!
 * \brief Declaration of Connection object
 *
 * Connection represents a single connection established with the server
 */
// ======================================================================

#pragma once
#include "Connection.h"
#include <array>
#include <boost/asio.hpp>
#include <memory>
#include <macgyver/ThreadPool.h>
#include <spine/HTTP.h>
#include <spine/HandlerView.h>
#include <spine/Reactor.h>

namespace SmartMet
{
namespace Server
{
class AsyncServer;

// ======================================================================
/*!
 * \brief Connection class to handle single asynchronous connection
 *
 * This connection type is used by the AsyncServer server implementation.
 */
// ======================================================================
class AsyncConnection : public Connection, public std::enable_shared_from_this<AsyncConnection>
{
 public:
  using ConnectionPtr = std::shared_ptr<AsyncConnection>;

 private:
  // Implemented according to https://en.cppreference.com/w/cpp/memory/enable_shared_from_this (variant Best)
  // This dummy private structure is to disable acccess to constructor, but to still allow
  // it's use in static factory method
  struct Private{ explicit Private() = default; };

 public:

  explicit AsyncConnection(Private,
                           AsyncServer* serverInstance,
                           bool sslEnabled,
                           boost::asio::ssl::context& sslContext,
                           bool canGzipResponse,
                           std::size_t compressLimit,
                           std::size_t maxRequestSize,
                           long timeout,
                           bool dumpRequests,
                           boost::asio::io_service& io_service,
                           SmartMet::Spine::Reactor& theReactor,
                           ThreadPoolType& adminExecutor,
                           ThreadPoolType& slowExecutor,
                           ThreadPoolType& fastExecutor);

  AsyncConnection() = delete;
  AsyncConnection(const AsyncConnection& other) = delete;
  AsyncConnection(AsyncConnection&& other) = delete;
  AsyncConnection& operator=(const AsyncConnection& other) = delete;
  AsyncConnection& operator=(AsyncConnection&& other) = delete;

  // ======================================================================
  /*!
   * \brief Constructs a connection with given server members
   *
   * @param serverInstance Pointer to the parent server instance. Used to
   * communicate with the server.
   * @param canGzipResponse Flag to tell if responses can be gzipped by
   * the connection object.
   * @param compressLimit The lower limit of response size after which
   * compression can be applied
   * @param timeout Timeout in seconds for the incoming request to make sense
   * @param dumpRequests Flag to see if incoming requests should be dumped to log
   * @param io_service The IO Service supplied by the server for network and
   * timer handling
   * @param serverReactor The reactor supplied by the server for content handling
   * @param adminExecutor Thread pool to handle requests marked as 'admin'
   * @param slowExecutor Thread pool to handle requests marked as 'slow'
   * @param fastExecutor Thread pool to handle requests marked as 'fast'
   */
  // ======================================================================

  static ConnectionPtr create(AsyncServer* serverInstance,
                           bool sslEnabled,
                           boost::asio::ssl::context& sslContext,
                           bool canGzipResponse,
                           std::size_t compressLimit,
                           std::size_t maxRequestSize,
                           long timeout,
                           bool dumpRequests,
                           boost::asio::io_service& io_service,
                           SmartMet::Spine::Reactor& theReactor,
                           ThreadPoolType& adminExecutor,
                           ThreadPoolType& slowExecutor,
                           ThreadPoolType& fastExecutor);

  inline ConnectionPtr get_ptr() { return shared_from_this(); }

  // ======================================================================
  /*!
   * \brief Destructs a connection
   *
   * Frees resources and closes any sockets still open at this point.
   */
  // ======================================================================

  ~AsyncConnection() override;

  // ======================================================================
  /*!
   * \brief Gets the socket associated with this connection
   *
   * This is used by the server when accepting new connections.
   */
  // ======================================================================

  // boost::asio::ip::tcp::socket& socket();

  // ======================================================================
  /*!
   * \brief Initiates the connection processing
   *
   * This fires the request processing functionality. Called by the server
   * when new connection arrives.
   */
  // ======================================================================

  void start() override;

 private:
  void handleHandshake(const boost::system::error_code& error);

  // ======================================================================
  /*!
   * \brief Handle partial read from socket
   *
   * Reads some bytes from the socket and attempts to parse the incoming
   * request.
   */
  // ======================================================================

  void handleRead(const boost::system::error_code& e, std::size_t bytes_transferred);

  // ======================================================================
  /*!
   * \brief Handle completed request
   *
   * Will be invoked when a request has been successfully read from the
   * socket and parsed.
   * @param theView The handler view object which contains the content
   * handling functionality needed to handle this request
   */
  // ======================================================================

  void handleCompletedRead(SmartMet::Spine::HandlerView& theHandlerView);

  // ======================================================================
  /*!
   * \brief Function to set common server-originating HTTP headers
   */
  // ======================================================================

  void setServerHeaders();

  // ======================================================================
  /*!
   * \brief Function to send simple stock reply to the client
   */
  // ======================================================================

  void sendStockReply(SmartMet::Spine::HTTP::Status theStatus);

  // ======================================================================
  /*!
   * \brief Function to send simple prepared simple reply to the client
   */
  // ======================================================================

  void sendSimpleReply();

  // ======================================================================
  /*!
   * \brief Function to handle timeout timer asynchronously
   *
   * This will be called if timeout fires.
   */
  // ======================================================================

  void handleTimer(const boost::system::error_code& err);

  // ======================================================================
  /*!
   * \brief Function to start gateway writes
   *
   * Gateway writes do not get any HTTP encapsulation, they are sent
   * directly as byte stream. This is frontend functionality.
   */
  // ======================================================================

  void startGatewayReply();

  // ======================================================================
  /*!
   * \brief Function to start chunked stream writes
   *
   * Chunked replies are started when the response content is of stream type.
   * and the content size is unknown.
   */
  // ======================================================================

  void startChunkedReply();

  // ======================================================================
  /*!
   * \brief Function to handle chunked stream writes
   *
   * Writes a slice of chunked stream data.
   */
  // ======================================================================

  void writeChunkedReply(const boost::system::error_code& e, std::size_t bytes_transferred);

  // ======================================================================
  /*!
   * \brief Function to finalize chunked stream writes
   *
   * Finalizes chunked stream reply. Writes the necessary trailing
   * information.
   */
  // ======================================================================

  void finalizeChunkedReply(const boost::system::error_code& e, std::size_t bytes_transferred);

  // ======================================================================
  /*!
   * \brief Function to start unchunked stream writes
   *
   * Unchunked stream replies start when the response  content is stream
   * type and the content size is known.
   */
  // ======================================================================

  void startStreamReply();

  // ======================================================================
  /*!
   * \brief Function to handle unchunked stream writes
   *
   * Writes a slice of unchunked stream data.
   */
  // ======================================================================

  void writeStreamReply(const boost::system::error_code& e, std::size_t bytes_transferred);

  // ======================================================================
  /*!
   * \brief Function to obtain the next chunk from response stream
   *
   * Calls the appropriate thread pool to give the next chunk of data
   * to write.
   */
  // ======================================================================

  void getNextChunk();

  // ======================================================================
  /*!
   * \brief Function to obtain the next chunk from response stream
   *
   * Calls the appropriate thread pool to give the next chunk of data
   * to write. Does the HTTP chunking.
   */
  // ======================================================================

  void getNextChunkedChunk();

  // ======================================================================
  /*!
   * \brief Function to start regular (unstreamed) writes
   *
   * Regular replies are simple string responses.
   */
  // ======================================================================

  void startRegularReply();

  // ======================================================================
  /*!
   * \brief Function to handle regular (unstreamed) writes
   *
   * Writes a slice of regular reply data.
   */
  // ======================================================================

  void writeRegularReply(const boost::system::error_code& e, std::size_t bytes_transferred);

  // ======================================================================
  /*!
   * \brief Auxilary function to schedule chunk getter functor
   *
   */
  // ======================================================================

  void scheduleChunkGetter();

  // ======================================================================
  /*!
   * \brief Auxilary function to schedule chunked chunk getter functor
   *
   */
  // ======================================================================

  void scheduleChunkedChunkGetter();

  // ======================================================================
  /*!
   * \brief Auxilary function to see if client disconnected while
   * waiting in the queue
   *
   */
  // ======================================================================

  void notifyClientDisconnect(const boost::system::error_code& e, std::size_t bytes_transferred);

  /// Number of sent bytes so far
  std::size_t itsSentBytes;

  /// Handle to the server instance which spawned this connection
  /* AsyncServer* itsServer; */

  /// Boolean to see if client has disconnected while waiting in the queue
  bool itsPrematurelyDisconnected;

  /// Mutex for the client disconnect boolean
  mutable boost::mutex itsDisconnectMutex;
};

}  // namespace Server
}  // namespace SmartMet
