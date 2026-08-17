//===================================
/*
 * Definition of the class Connection
 */
//===================================
#include "AsyncConnection.h"
#include "AsyncServer.h"
#include "Utility.h"
#include <boost/bind/bind.hpp>
#include <boost/move/make_unique.hpp>
#include <macgyver/Exception.h>
#include <spine/Convenience.h>
#include <sstream>
#include <vector>

namespace SmartMet
{
namespace Server
{
namespace
{
// Status codes whose responses are defined to carry no message body. Their
// framing comes from the status code, not from a header, so no body and no
// Content-Length may be sent - a client stops reading at the end of the header
// section and would take anything after it for the next response.
//
// Spine's Status enumerators are the numeric codes themselves, except for the
// two local ones (high_load, shutdown) which are far outside these ranges.
bool statusHasNoBody(SmartMet::Spine::HTTP::Status status)
{
  const int code = static_cast<int>(status);
  return (code >= 100 && code < 200) || code == 204 || code == 304;
}
}  // namespace

AsyncConnection::AsyncConnection(Private,
                                 AsyncServer* serverInstance,
                                 bool sslEnabled,
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
                                 ThreadPoolType& fastExecutor)
    : Connection(serverInstance,
                 sslEnabled,
                 sslContext,
                 canGzipResponse,
                 compressLimit,
                 maxRequestSize,
                 timeout,
                 dumpRequests,
                 io_service,
                 theReactor,
                 adminExecutor,
                 slowExecutor,
                 fastExecutor),
      itsSentBytes(0),
      itsPrematurelyDisconnected(false)
{
}

AsyncConnection::ConnectionPtr AsyncConnection::create(AsyncServer* serverInstance,
                                                       bool sslEnabled,
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
                                                       ThreadPoolType& fastExecutor)
{
  return std::make_shared<AsyncConnection>(Private(),
                                           serverInstance,
                                           sslEnabled,
                                           sslContext,
                                           canGzipResponse,
                                           compressLimit,
                                           maxRequestSize,
                                           timeout,
                                           dumpRequests,
                                           io_service,
                                           theReactor,
                                           adminExecutor,
                                           slowExecutor,
                                           fastExecutor);
}

// Initiate graceful Connection closure after the reply is written (connection is destructing)
AsyncConnection::~AsyncConnection()
{
  if (!hasTimedOut)
  {
    boost::system::error_code ignored_ec;
    socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    socket().close(ignored_ec);
  }
}

void AsyncConnection::handleTimer(const boost::system::error_code& err)
{
  try
  {
    SmartMet::Spine::WriteLock lock(itsMutex);  // Lock here, just in case
    if (err == boost::asio::error::operation_aborted)
      return;

    hasTimedOut = true;

    if (itsIdle)
    {
      // The keep-alive timeout of an idle persistent connection expired before the
      // next request line arrived. A server may close a persistent connection at any
      // time (RFC 9112 9.6), and it must do so silently here: the client may be
      // writing a request at this very moment, in which case an unsolicited 408 would
      // arrive in the middle of a request it still expects an answer to.
      boost::system::error_code ignored_ec;
      socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
      socket().close(ignored_ec);
      return;
    }

    // A request was being read (or is the first one on this connection) and did not
    // arrive in time. Report it and close.
    itsKeepAlive = false;
    sendStockReply(SmartMet::Spine::HTTP::Status::request_timeout);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void AsyncConnection::startRead()
{
  try
  {
    auto handler = [me = shared_from_this()](const boost::system::error_code& err,
                                             std::size_t bytes_transferred)
    { me->handleRead(err, bytes_transferred); };

    if (itsEncryptionEnabled)
      itsSocket.async_read_some(boost::asio::buffer(itsSocketBuffer), handler);
    else
      socket().async_read_some(boost::asio::buffer(itsSocketBuffer), handler);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Start a new connection

void AsyncConnection::start()
{
  try
  {
    // The connection has been accepted, so it now occupies a slot against the
    // server's connection limit until it is destroyed.
    registerConnection();

    // Start the timeout timer

    itsTimeoutTimer =
        std::make_unique<DeadlineTimer>(itsIoService, std::chrono::seconds(itsTimeout));

    itsTimeoutTimer->async_wait([me = shared_from_this()](const boost::system::error_code& err)
                                { me->handleTimer(err); });

    if (itsEncryptionEnabled)
    {
      // Begin the handshake process
      itsSocket.async_handshake(boost::asio::ssl::stream_base::server,
                                [me = shared_from_this()](const boost::system::error_code& err)
                                { me->handleHandshake(err); });
    }
    else
    {
      // Begin the reading process
      startRead();
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void AsyncConnection::handleHandshake(const boost::system::error_code& error)
{
  if (!error)
  {
    // Handshake is ok. Start socket reading
    startRead();
  }
  else
  {
    // printf("HANDSHAKE ERROR\n");
    // std::cout << error << "\n";

    // Handshake error. Closing the connection.
    boost::system::error_code ignored_ec;
    socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    socket().close(ignored_ec);
  }
}

void AsyncConnection::handleRead(const boost::system::error_code& e, std::size_t bytes_transferred)
{
  try
  {
    if (itsServer->isShutdownRequested())
    {
      sendStockReply(SmartMet::Spine::HTTP::Status::shutdown);
      return;
    }

    // Initialize the connection status.
    itsFinalStatus = e;

    if (!e)
    {
      if (itsIdle.exchange(false) && !hasTimedOut)
      {
        // First bytes of the next request on a persistent connection. Swap the timer
        // from the keep-alive idle timeout back to the request read timeout, so that a
        // client on a slow uplink gets the same time to deliver a request as it would
        // on a fresh connection - and so that an expiry from here on is reported as a
        // request timeout instead of quietly dropping the socket.
        itsTimeoutTimer->expires_after(std::chrono::seconds(itsTimeout));
        itsTimeoutTimer->async_wait([me = shared_from_this()](const boost::system::error_code& err)
                                    { me->handleTimer(err); });
      }

      itsReceivedBytes += bytes_transferred;

      if (itsMaxRequestSize > 0 && itsReceivedBytes > itsMaxRequestSize)
      {
        // Request is larger than the incoming buffer size.
        reportError("413 Entity too large");
        sendStockReply(SmartMet::Spine::HTTP::Status::request_entity_too_large);
        return;
      }

      // Copy incoming buffer into the received data buffer
      itsBuffer.append(itsSocketBuffer.data(), bytes_transferred);

      // std::cout << itsBuffer << "\n";

      // Try to parse the incoming message. parseOneRequest reads exactly one
      // message and says how much of the buffer it took, so anything a
      // pipelining client sent after it stays put for the next round instead of
      // being swallowed into this request's body.
      auto parsedRequest = SmartMet::Spine::HTTP::parseOneRequest(itsBuffer);

      if (parsedRequest.status == SmartMet::Spine::HTTP::ParsingStatus::COMPLETE)
      {
        // Successfully parsed the request, set connection request
        itsRequest = std::move(parsedRequest.request);

        // Drop the bytes this message occupied; what is left belongs to the
        // next request and is carried over by finishResponse().
        itsBuffer.erase(0, parsedRequest.consumed);
        itsReceivedBytes = itsBuffer.size();

        // Set client ip
        auto forwardHeader = itsRequest->getHeader("X-Forwarded-For");
        if (forwardHeader)
        {
          // Should we validate this?
          itsRequest->setClientIP(parseXForwardedFor(*forwardHeader));
        }
        else
        {
          try
          {
            itsRequest->setClientIP(socket().remote_endpoint().address().to_string());
          }
          catch (...)
          {
            Fmi::Exception exception(BCP, "Operation failed!", nullptr);
            reportError(std::string("Failed to obtain remote endpoint IP address:\n") +
                        exception.what());
            return;
          }
        }

#ifndef NDEBUG
        // DEBUGGIN OUTPUT************************************

        std::cout << "Incoming request from " << itsRequest->getClientIP() << '\n';
        std::cout << "Method: " << itsRequest->getMethodString() << '\n';
        std::cout << "URI: " << itsRequest->getURI() << '\n';
        std::cout << "Query string: " << itsRequest->getQueryString() << '\n';
        std::cout << "Headers: \n";
        auto hmappi = itsRequest->getHeaders();
        for (const auto& header : hmappi)
        {
          std::cout << header.first << " : " << header.second << '\n';
        }

        std::cout << "Parsed parameters: \n";
        auto mappi = itsRequest->getParameterMap();
        for (const auto& elem : mappi)
        {
          std::cout << elem.first << " : " << elem.second << '\n';
        }
        std::cout << "Content: \"" << itsRequest->getContent() << "\"\n\n";

// DEBUGGIN OUTPUT************************************
#endif

        ++itsRequestCount;

        // Answer in the client's own HTTP version and negotiate the persistence of
        // the connection for this request/response cycle.
        itsResponseVersion = negotiateVersion(itsRequest->getVersion());
        itsKeepAlive = evaluateKeepAlive();

        // RFC 9112 3.2: an HTTP/1.1 request without a Host header cannot be
        // routed to a target resource and must be rejected. The check lives here
        // rather than in the parser because HTTP/1.0 requests legitimately have
        // no Host. The connection is closed: a client that gets this wrong is
        // not one whose framing should be trusted for a second request.
        if (itsResponseVersion == "1.1" && !itsRequest->getHeader("Host"))
        {
          reportError("400 Missing Host header on an HTTP/1.1 request");
          itsKeepAlive = false;
          sendStockReply(SmartMet::Spine::HTTP::Status::bad_request);
          return;
        }

        // Must come after the keep-alive negotiation, which reads Connection
        stripHopByHopHeaders();

        // RFC 9110 9.3.2: the response to HEAD is the response to GET with the
        // content left out. The plugins do not know the method - several answer
        // only GET and POST and would reject it - so the request is handed on as
        // a GET and the body is dropped when the reply is written. The cost is
        // that a HEAD shows up as a GET in the per-handler access log.
        itsHeadRequest = (itsRequest->getMethod() == SmartMet::Spine::HTTP::RequestMethod::HEAD);
        if (itsHeadRequest)
          itsRequest->setMethod(SmartMet::Spine::HTTP::RequestMethod::GET);

        // Check whether we have 'OPTIONS' request
        if (itsRequest->getMethodString() == "OPTIONS" && itsRequest->getResource() == "*")
        {
          *itsResponse = SmartMet::Spine::HTTP::Response::stockOptionsResponse();
          setServerHeaders();
          sendSimpleReply();
          return;
        }

        // Determine where to put the handler function
        auto* handlerView = itsReactor.getHandlerView(*itsRequest);
        if (!handlerView)
        {
          // Couldn't find handler for the request
          sendStockReply(SmartMet::Spine::HTTP::Status::not_found);
          return;
        }

        itsAdminQuery = (!handlerView->isCatchNoMatch() && handlerView->isAdminQuery(*itsRequest) &&
                         itsAdminExecutor.getPoolSize() > 0);

        // Handle high load situations
        if (!itsAdminQuery && itsReactor.isLoadHigh())
        {
          std::cout << Spine::log_time_str() << " Too many active requests, reporting high load\n";
          sendStockReply(SmartMet::Spine::HTTP::Status::high_load);
          return;
        }

        bool scheduled = false;

        // Handle frontends and backends separately

        if (handlerView->isCatchNoMatch())
        {
          // frontend always uses the fast executor
          itsQueryIsFast = true;
          itsAdminQuery = false;
          scheduled = itsFastExecutor.schedule([me = shared_from_this(), handlerView]()
                                               { me->handleCompletedRead(*handlerView); });

          if (!scheduled)
          {
            // Task queue was full, send busy response
            sendStockReply(SmartMet::Spine::HTTP::Status::service_unavailable);
            reportInfo("Frontend request queue was full");
          }
        }

        else
        {
          // backend

          itsQueryIsFast =
              (itsSlowExecutor.getPoolSize() == 0 || handlerView->queryIsFast(*itsRequest));

          if (itsAdminQuery)
          {
            scheduled = itsAdminExecutor.schedule([me = shared_from_this(), handlerView]()
                                                  { me->handleCompletedRead(*handlerView); });
          }
          else if (itsQueryIsFast)
          {
            scheduled = itsFastExecutor.schedule([me = shared_from_this(), handlerView]()
                                                 { me->handleCompletedRead(*handlerView); });
          }
          else
          {
            scheduled = itsSlowExecutor.schedule([me = shared_from_this(), handlerView]()
                                                 { me->handleCompletedRead(*handlerView); });
          }

          if (!scheduled)
          {
            // Task queue was full, send high load response to frontend
            sendStockReply(SmartMet::Spine::HTTP::Status::high_load);
            reportInfo("Backend request queue was full, returning high load notification");
          }
        }
      }
      else if (parsedRequest.status == SmartMet::Spine::HTTP::ParsingStatus::FAILED)
      {
        // Failed parse, something (fundamentally) wrong with the request
        sendStockReply(SmartMet::Spine::HTTP::Status::bad_request);
      }
      else
      {
        // Bound the header section. Without this a client can hold a connection
        // - and a slot against the connection limit - open indefinitely by
        // dribbling header bytes that never reach the terminating blank line.
        if (itsMaxHeaderSize > 0 && itsBuffer.size() > itsMaxHeaderSize &&
            itsBuffer.find("\r\n\r\n") == std::string::npos)
        {
          reportError("431 Request header fields too large");
          itsKeepAlive = false;
          sendStockReply(SmartMet::Spine::HTTP::Status::request_header_fields_too_large);
          return;
        }

        // Request is not succesfully parsed, attempt to get more data from socket and try
        // again - unless the client is waiting for us to accept its body first.
        if (handleExpectContinue())
          startRead();
      }
    }
    else if (e == boost::asio::error::eof)
    {
      // Peer closed the socket or timeout has occurred
      // Abort this connection
      return;
    }
    else if (itsIdle)
    {
      // An idle persistent connection went away: either the client closed it, or the
      // keep-alive timeout reaped it here. Both are the expected end of a reused
      // connection and are not worth a log entry.
      return;
    }
    else if (e == boost::asio::error::operation_aborted)
    {
      // This connection has been timed out
      reportInfo("Connection timeout");
    }
    else
    {
      // Some other error occurred in reading, handle it somehow
      std::stringstream ss;
      ss << e.message();
      reportInfo("Error occurred while reading socket: " + ss.str());
    }
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::handleRead aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::handleRead aborted\n";
  }
}

bool AsyncConnection::handleExpectContinue()
{
  try
  {
    if (itsExpectHandled)
      return true;

    const auto head = peekRequestHead(itsBuffer);
    if (!head.complete)
      return true;  // Header section is still on its way

    itsExpectHandled = true;  // One decision per request, whatever it is

    if (head.expect.empty())
      return true;

    // "Expect" is an HTTP/1.1 mechanism. An interim response must never be sent
    // to an HTTP/1.0 client (RFC 9112 9.4): it has no notion of one and would
    // read the 100 as the final answer to its request.
    if (negotiateVersion(head.version) != "1.1")
      return true;

    if (hasHeaderToken(head.expect, "100-continue"))
    {
      // Tell the client to go ahead and send the body it is holding back.
      // Without this it waits for its own timeout and the request never
      // completes - which is why this cannot wait for a parseable request.
      static const std::string continueReply = "HTTP/1.1 100 Continue\r\n\r\n";

      boost::system::error_code err;
      if (itsEncryptionEnabled)
        boost::asio::write(itsSocket, boost::asio::buffer(continueReply), err);
      else
        boost::asio::write(socket(), boost::asio::buffer(continueReply), err);

      if (err)
      {
        itsFinalStatus = err;
        itsKeepAlive = false;
        return false;
      }

      return true;
    }

    // An expectation we do not understand cannot be met (RFC 9110 10.1.1).
    reportInfo("Unsupported expectation '" + head.expect + "', rejecting the request");
    itsKeepAlive = false;
    sendStockReply(SmartMet::Spine::HTTP::Status::expectation_failed);
    return false;
  }
  catch (...)
  {
    Fmi::Exception ex(
        BCP, "Operation failed! AsyncConnection::handleExpectContinue aborted", nullptr);
    std::cerr << ex.getStackTrace();
    return false;
  }
}

void AsyncConnection::stripHopByHopHeaders()
{
  try
  {
    // Connection-management headers. They apply to the single hop the request
    // arrived over and must not reach a handler, nor be forwarded by the
    // frontend to a backend (RFC 9110 7.6.1).
    //
    // Transfer-Encoding is not in this list because parseOneRequest has already
    // removed it: a chunked body is decoded during parsing and the header is
    // replaced by the Content-Length of the decoded body, so what reaches a
    // handler - or a backend, via the frontend - is an ordinary message.
    static const std::array<const char*, 7> hopByHop = {"Connection",
                                                        "Keep-Alive",
                                                        "Proxy-Connection",
                                                        "Proxy-Authenticate",
                                                        "Proxy-Authorization",
                                                        "TE",
                                                        "Trailer"};

    // Whatever the Connection header itself names is hop-by-hop too, so collect
    // those before the header is removed.
    std::vector<std::string> listed;
    const auto connection = itsRequest->getHeader("Connection");
    if (connection)
    {
      std::size_t pos = 0;
      while (pos <= connection->size())
      {
        std::size_t end = connection->find(',', pos);
        if (end == std::string::npos)
          end = connection->size();

        const std::size_t first = connection->find_first_not_of(" \t", pos);
        if (first != std::string::npos && first < end)
        {
          const std::size_t last = connection->find_last_not_of(" \t", end - 1);
          listed.push_back(connection->substr(first, last - first + 1));
        }
        pos = end + 1;
      }
    }

    for (const auto* name : hopByHop)
      itsRequest->removeHeader(name);

    for (const auto& name : listed)
      itsRequest->removeHeader(name);
  }
  catch (...)
  {
    Fmi::Exception ex(
        BCP, "Operation failed! AsyncConnection::stripHopByHopHeaders aborted", nullptr);
    std::cerr << ex.getStackTrace();
  }
}

void AsyncConnection::rejectConnection()
{
  try
  {
    // The connection was never started, so there is no timer to cancel and no
    // request to report. Answer with a framed 503 so a legitimate client backs
    // off instead of seeing an unexplained reset, then close.
    itsKeepAlive = false;
    itsResponseVersion = "1.0";  // Nothing has been read, so the version is unknown
    sendStockReply(SmartMet::Spine::HTTP::Status::service_unavailable);
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::rejectConnection aborted", nullptr);
    std::cerr << ex.getStackTrace();
  }
}

bool AsyncConnection::evaluateKeepAlive() const
{
  try
  {
    if (!itsKeepAliveEnabled)
      return false;

    // Do not invite a client to reuse a socket the server is about to close anyway
    if (itsServer->isShutdownRequested())
      return false;

    // Recycle the connection after a while so that clients are periodically
    // redistributed over the backends and no socket is held forever.
    if (itsMaxKeepAliveRequests > 0 && itsRequestCount >= itsMaxKeepAliveRequests)
      return false;

    const auto connection = itsRequest->getHeader("Connection");

    if (itsResponseVersion == "1.1")
    {
      // HTTP/1.1: persistent by default, the client opts out with "Connection: close"
      return !(connection && hasHeaderToken(*connection, "close"));
    }

    // HTTP/1.0: no persistent connections in the standard, so the client has to opt in
    // with the keep-alive extension. Some HTTP/1.0 era clients and proxies send that
    // request in "Proxy-Connection" instead; the header never had any meaning beyond
    // this HTTP/1.0 hack and is deliberately not consulted for HTTP/1.1 requests.
    if (connection)
      return hasHeaderToken(*connection, "keep-alive");

    const auto proxyConnection = itsRequest->getHeader("Proxy-Connection");
    return proxyConnection && hasHeaderToken(*proxyConnection, "keep-alive");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Single exit point for a response that was delivered completely and successfully.
// May be called from an io_service thread or from a thread pool thread.
void AsyncConnection::finishResponse()
{
  try
  {
    if (!itsKeepAlive || hasTimedOut || itsServer->isShutdownRequested())
    {
      // Not reusing the socket. The caller drops the last reference to this connection
      // right after returning, and the destructor shuts the socket down.
      return;
    }

    // Reset the per-request state. The client IP is carried over: it identifies the
    // connection in the error reports and in the connection-finished hooks also while
    // no request is being handled.
    const std::string clientIP = itsRequest->getClientIP();

    // itsBuffer is deliberately not cleared: handleRead() erased exactly the
    // bytes this message occupied, so whatever remains is the next request,
    // already delivered by a pipelining client.
    itsSentBytes = 0;
    itsTotalStreamedBytes = 0;
    itsDeclaredContentLength = 0;
    itsResponseString.clear();
    itsRequest = std::make_unique<SmartMet::Spine::HTTP::Request>();
    itsRequest->setClientIP(clientIP);
    itsResponse = std::make_unique<SmartMet::Spine::HTTP::Response>();
    itsResponseVersion = "1.0";
    itsQueryIsFast = false;
    itsAdminQuery = false;
    itsKeepAlive = false;
    itsExpectHandled = false;
    itsHeadRequest = false;
    itsBackendNotified = false;

    // A pipelined next request is already here, so the connection is not idle
    // and the timer bounds reading that request rather than waiting for one.
    // Must be decided before arming the timer, which is what tells handleTimer
    // whether an expiry means "drop a stale idle connection" or "408".
    const bool pipelined = !itsBuffer.empty();
    itsIdle = !pipelined;

    // The timer was cancelled when the request was handed to a plugin, so that a slow
    // plugin is not killed by the request timeout. Rearm it for whichever wait comes
    // next.
    itsTimeoutTimer->expires_after(
        std::chrono::seconds(pipelined ? itsTimeout : itsKeepAliveTimeout));
    itsTimeoutTimer->async_wait([me = shared_from_this()](const boost::system::error_code& err)
                                { me->handleTimer(err); });

    if (pipelined)
    {
      // Answer the buffered request without going to the socket. Posted rather
      // than called directly so that a client which pipelines many requests
      // does not nest one whole request/response cycle inside the previous
      // one's stack frame. Responses stay in request order because a connection
      // only ever has one request in flight.
      boost::asio::post(itsIoService,
                        [me = shared_from_this()]()
                        { me->handleRead(boost::system::error_code(), 0); });
      return;
    }

    startRead();
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::finishResponse aborted", nullptr);
    std::cerr << ex.getStackTrace();
  }
}

// Calls SmartMet plugins. This function is always called from within the thread pool
void AsyncConnection::handleCompletedRead(SmartMet::Spine::HandlerView& theHandlerView)
{
  try
  {
    // Read was successful, cancel timeout
    itsTimeoutTimer->cancel();

    // See if client has prematurely disconnected
    {
      boost::lock_guard<boost::mutex> lock(itsDisconnectMutex);
      if (!itsPrematurelyDisconnected)
      {
        // Cancel the client disconnect notify handler
        socket().cancel();
      }
      else
      {
        if (theHandlerView.isCatchNoMatch())
        {
          // Say nothing here, we are not interested if connection was closed before frontend
          // handling
        }
        else
        {
          reportInfo("Client '" + itsRequest->getClientIP() +
                     "' has already disconnected, not calling the plugin " +
                     theHandlerView.getPluginName());
        }
        return;
      }
    }

    // Call connection started - hooks
    itsReactor.callClientConnectionStartedHooks(itsRequest->getClientIP());

    if (itsDumpRequests)
    {
      auto requestString = dumpRequest(*itsRequest);
      reportInfo(requestString);
    }

    // Handle the request
    bool success = theHandlerView.handle(itsReactor, *itsRequest, *itsResponse);
    if (!success)
    {
      // Incoming IP not if filter whitelist

      sendStockReply(SmartMet::Spine::HTTP::Status::bad_request);

      return;
    }

    if (itsHeadRequest)
    {
      // No body is produced at all: a streamed or chunked response is collapsed
      // here rather than started, so the plugin's streamer is never pulled.
      this->setServerHeaders();
      this->startRegularReply();
      return;
    }

    if (itsResponse->hasStreamContent())
    {
      if (itsResponse->isGatewayResponse)
      {
        this->startGatewayReply();
      }

      else if (itsResponse->getChunked())
      {
        this->setServerHeaders();
        this->startChunkedReply();
      }
      else
      {
        this->setServerHeaders();
        this->startStreamReply();
      }
    }
    else
    {
      this->setServerHeaders();
      this->startRegularReply();
    }
  }
  catch (...)
  {
    // Dump stack trace to find possible causes
    Fmi::Exception ex(
        BCP, "Operation failed! AsyncConnection::handleCompletedRead aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // Must not continue throwing here or the server will terminate
    // std::cerr << "Operation failed! AsyncConnection::handleCompletedRead aborted\n";
  }
}

// Handle gateway writes. This function is always called from within the thread pool
void AsyncConnection::startGatewayReply()
{
  try
  {
    // This response is a gateway response, simply stream its content to client without any
    // modifications.
    //
    // The frontend forwards the backend's status line, headers and body verbatim, so this
    // server neither knows how the body is framed nor is able to inject a Connection
    // header of its own. The connection therefore cannot be reused. This is consistent
    // with what the client actually sees: the frontend asks its backends for
    // "Connection: close", so the forwarded headers announce the close as well.
    itsKeepAlive = false;

    // Get first chunk

    this->getNextChunk();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// start chunked write. This function is always called from within the thread pool
void AsyncConnection::startChunkedReply()
{
  try
  {
    // Content is to be sent using chunked content encoding. A message must never
    // carry both framings (RFC 9112 6.1): a recipient that honoured the
    // Content-Length instead would stop mid-stream and read the remaining chunks
    // as the next response, so drop anything a plugin may have set.
    itsResponse->setHeader("Transfer-Encoding", "chunked");
    itsResponse->removeHeader("Content-Length");

    // Headers are currently written synchronously
    try
    {
      boost::system::error_code e;
      auto headerbuffer = itsResponse->headersToBuffer();
      // Write headers
      if (itsEncryptionEnabled)
        boost::asio::write(itsSocket, headerbuffer, e);
      else
        boost::asio::write(socket(), headerbuffer, e);

      if (e)
      {
        reportInfo("Unable to send chunk response headers to " + itsRequest->getClientIP() +
                   ". Reason: " + e.message());
        itsFinalStatus = e;
        return;
      }
    }
    catch (const std::runtime_error&)
    {
      boost::system::error_code e;
      reportInfo("Response status not set, defaulting to 501 Not Implemented");
      itsResponse->setStatus(SmartMet::Spine::HTTP::Status::not_implemented, true);

      auto headerbuffer = itsResponse->headersToBuffer();
      // Write headers
      if (itsEncryptionEnabled)
        boost::asio::write(itsSocket, headerbuffer, e);
      else
        boost::asio::write(socket(), headerbuffer, e);

      if (e)
      {
        reportInfo("Unable to send chunk response headers to " + itsRequest->getClientIP() +
                   ". Reason: " + e.message());
        itsFinalStatus = e;
        return;
      }
    }

    // Get first chunk
    this->getNextChunkedChunk();
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::startChunkedReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::startChunkedReply aborted\n";
  }
}

// Response is streamed without chunked encoding. This function is always called from within the
// thread pool
void AsyncConnection::startStreamReply()
{
  try
  {
    // Set Content-Length header, just to be sure
    itsDeclaredContentLength = itsResponse->getContentLength();
    itsResponse->setHeader(
        "Content-Length",
        std::to_string(static_cast<long long unsigned int>(itsDeclaredContentLength)));

    // Currently headers a written syncronously
    try
    {
      boost::system::error_code e;
      auto headerbuffer = itsResponse->headersToBuffer();
      // Write headers
      if (itsEncryptionEnabled)
        boost::asio::write(itsSocket, headerbuffer, e);
      else
        boost::asio::write(socket(), headerbuffer, e);

      if (e)
      {
        reportInfo("Unable to send stream response headers to " + itsRequest->getClientIP() +
                   ". Reason: " + e.message());
        itsFinalStatus = e;
        return;
      }
    }
    catch (const std::runtime_error&)
    {
      boost::system::error_code e;
      reportInfo("Response status not set, defaulting to 501 Not Implemented");
      itsResponse->setStatus(SmartMet::Spine::HTTP::Status::not_implemented, true);

      auto headerbuffer = itsResponse->headersToBuffer();
      // Write headers
      if (itsEncryptionEnabled)
        boost::asio::write(itsSocket, headerbuffer, e);
      else
        boost::asio::write(socket(), headerbuffer, e);

      if (e)
      {
        reportInfo("Unable to send stream response headers to " + itsRequest->getClientIP() +
                   ". Reason: " + e.message());
        itsFinalStatus = e;
        return;
      }
    }

    // Get first chunk

    this->getNextChunk();
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::startStreamReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::startStreamReply aborted\n";
  }
}

// perform a single chunked write
void AsyncConnection::writeChunkedReply(const boost::system::error_code& e,
                                        std::size_t bytes_transferred)
{
  try
  {
    itsFinalStatus = e;

    if (!e)
    {
      itsSentBytes += bytes_transferred;

      if (itsSentBytes < itsResponseString.size())
      {
        // Still something left to send

        // Start async write to socket
        auto remaining_buffer = boost::asio::buffer(itsResponseString) + itsSentBytes;
        if (itsEncryptionEnabled)
        {
          itsSocket.async_write_some(boost::asio::buffer(remaining_buffer),
                                     [me = shared_from_this()](const boost::system::error_code& err,
                                                               std::size_t bytes_transferred)
                                     { me->writeChunkedReply(err, bytes_transferred); });
        }
        else
        {
          socket().async_write_some(boost::asio::buffer(remaining_buffer),
                                    [me = shared_from_this()](const boost::system::error_code& err,
                                                              std::size_t bytes_transferred)
                                    { me->writeChunkedReply(err, bytes_transferred); });
        }
      }
      else
      {
        scheduleChunkedChunkGetter();
      }
    }
    else
    {
      // The body was cut short, so the client cannot tell where the next response would
      // begin even if the socket still worked. Never reuse the connection.
      itsKeepAlive = false;
      sendStockReply(SmartMet::Spine::HTTP::Status::service_unavailable);
      reportInfo("Error in chunked reply send to " + itsRequest->getClientIP() +
                 ". Reason: " + e.message() + ". Code: " + Fmi::to_string(e.value()));
      // Log the aborted stream with the bytes delivered so far.
      finalizeStreamLogging();
    }
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::writeChunkedReply", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::writeChunkedReply aborted\n";
  }
}

// finalize chunked response write
void AsyncConnection::finalizeChunkedReply(const boost::system::error_code& e,
                                           std::size_t bytes_transferred)
{
  try
  {
    itsFinalStatus = e;

    if (!e)
    {
      itsSentBytes += bytes_transferred;

      if (itsSentBytes < itsResponseString.size())
      {
        // Still something left to send

        // Start async write to socket
        auto remaining_buffer = boost::asio::buffer(itsResponseString) + itsSentBytes;
        if (itsEncryptionEnabled)
        {
          itsSocket.async_write_some(boost::asio::buffer(remaining_buffer),
                                     [me = shared_from_this()](const boost::system::error_code& err,
                                                               std::size_t bytes_transferred)
                                     { me->finalizeChunkedReply(err, bytes_transferred); });
        }
        else
        {
          socket().async_write_some(boost::asio::buffer(remaining_buffer),
                                    [me = shared_from_this()](const boost::system::error_code& err,
                                                              std::size_t bytes_transferred)
                                    { me->finalizeChunkedReply(err, bytes_transferred); });
        }
      }
      else
      {
        // Final chunk has been sent — the streamed response is complete.
        finalizeStreamLogging();
        finishResponse();
      }
    }
    else
    {
      itsKeepAlive = false;
      sendStockReply(SmartMet::Spine::HTTP::Status::service_unavailable);
      reportInfo("Error in finalizing chunked reply send to " + itsRequest->getClientIP() +
                 ". Reason: " + e.message() + ". Code: " + Fmi::to_string(e.value()));
      // Log the aborted stream with the bytes delivered so far.
      finalizeStreamLogging();
    }
  }
  catch (...)
  {
    Fmi::Exception ex(
        BCP, "Operation failed! AsyncConnection::finalizeChunkedReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::finalizeChunkedReply aborted\n";
  }
}

// This function is always called from within the thread pool
void AsyncConnection::getNextChunk()
{
  try
  {
    auto streamStatus = itsResponse->getStreamingStatus();

    if (streamStatus == SmartMet::Spine::HTTP::ContentStreamer::StreamerStatus::OK)
    {
      std::string contentstring = itsResponse->getContent();

      if (!contentstring.empty())
      {
        // Account this chunk's body bytes for the deferred access log.
        itsTotalStreamedBytes += contentstring.size();

        // Send received data
        itsResponseString = contentstring;

        // Reset sent bytes counter, this is a new chunk
        itsSentBytes = 0;

        // Schedule next async write operation
        if (itsEncryptionEnabled)
        {
          itsSocket.async_write_some(boost::asio::buffer(itsResponseString),
                                     [me = shared_from_this()](const boost::system::error_code& err,
                                                               std::size_t bytes_transferred)
                                     { me->writeStreamReply(err, bytes_transferred); });
        }
        else
        {
          socket().async_write_some(boost::asio::buffer(itsResponseString),
                                    [me = shared_from_this()](const boost::system::error_code& err,
                                                              std::size_t bytes_transferred)
                                    { me->writeStreamReply(err, bytes_transferred); });
        }
      }
      else
      {
        // Empty chunk but stream is ok, reschedule for later
        scheduleChunkGetter();
      }
    }
    else
    {
      // Stream status is EXIT, finalize the send. The streamed response is
      // complete: write the deferred access-log entry with the real size.
      finalizeStreamLogging();

      // Tell the backend heartbeat hooks how the backend conversation ended.
      // Keyed on the backend having been recorded rather than on the response
      // being a raw gateway passthrough: the frontend now re-emits a backend
      // response as an ordinary framed response, and the heartbeat must still
      // see it.
      notifyBackendFinished(streamStatus);

      if (itsKeepAlive && itsTotalStreamedBytes != itsDeclaredContentLength)
      {
        // Reusing the connection requires the body to be framed exactly as announced.
        // The streamer stopped after a different number of bytes than the Content-Length
        // header promised, so the client cannot find the start of the next response and
        // the socket has to be closed instead.
        reportInfo("Streamed response delivered " + Fmi::to_string(itsTotalStreamedBytes) +
                   " bytes instead of the announced " + Fmi::to_string(itsDeclaredContentLength) +
                   ", closing the connection");
        itsKeepAlive = false;
      }

      finishResponse();
    }
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::getNextChunk aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::getNextChunk aborted\n";
  }
}

//	This function is always called from within the thread pool
void AsyncConnection::getNextChunkedChunk()
{
  try
  {
    auto streamStatus = itsResponse->getStreamingStatus();

    if (streamStatus == SmartMet::Spine::HTTP::ContentStreamer::StreamerStatus::OK)
    {
      std::string contentstring = itsResponse->getContent();

      if (!contentstring.empty())
      {
        std::size_t length = contentstring.size();

        // Account this chunk's body bytes for the deferred access log.
        // The hex length + CRLF framing is transport overhead and is
        // deliberately excluded so the logged size matches the response
        // body size, like non-chunked responses.
        itsTotalStreamedBytes += length;

        std::string hexlength = convertToHex(length);
        std::string responsestring = hexlength + "\r\n" + contentstring + "\r\n";

        // This is new chunk, zero the counter and set response string
        itsResponseString = responsestring;

        itsSentBytes = 0;

        // Schedule the next asynchronous write
        if (itsEncryptionEnabled)
        {
          itsSocket.async_write_some(boost::asio::buffer(itsResponseString),
                                     [me = shared_from_this()](const boost::system::error_code& err,
                                                               std::size_t bytes_transferred)
                                     { me->writeChunkedReply(err, bytes_transferred); });
        }
        else
        {
          socket().async_write_some(boost::asio::buffer(itsResponseString),
                                    [me = shared_from_this()](const boost::system::error_code& err,
                                                              std::size_t bytes_transferred)
                                    { me->writeChunkedReply(err, bytes_transferred); });
        }
      }
      else
      {
        // Empty chunk received but stream is ok, reschedule for later
        scheduleChunkedChunkGetter();
      }
    }
    else
    {
      // Finalize the chunked send
      std::string endstring("0\r\n\r\n");

      // This is new, final chunk. zero the counter and set response string
      itsResponseString = endstring;

      itsSentBytes = 0;

      if (itsEncryptionEnabled)
      {
        itsSocket.async_write_some(boost::asio::buffer(itsResponseString),
                                   [me = shared_from_this()](const boost::system::error_code& err,
                                                             std::size_t bytes_transferred)
                                   { me->finalizeChunkedReply(err, bytes_transferred); });
      }
      else
      {
        socket().async_write_some(boost::asio::buffer(itsResponseString),
                                  [me = shared_from_this()](const boost::system::error_code& err,
                                                            std::size_t bytes_transferred)
                                  { me->finalizeChunkedReply(err, bytes_transferred); });
      }
    }
  }
  catch (...)
  {
    Fmi::Exception ex(
        BCP, "Operation failed! AsyncConnection::getNextChunkedChunk aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::getNextChunkedChunk aborted\n";
  }
}

// Does a single write from the current stream buffer
void AsyncConnection::writeStreamReply(const boost::system::error_code& e,
                                       std::size_t bytes_transferred)
{
  try
  {
    itsFinalStatus = e;

    if (!e)
    {
      itsSentBytes += bytes_transferred;

      if (itsSentBytes < itsResponseString.size())
      {
        // Still something left to send

        // Start async write to socket
        auto remaining_buffer = boost::asio::buffer(itsResponseString) + itsSentBytes;
        if (itsEncryptionEnabled)
        {
          itsSocket.async_write_some(boost::asio::buffer(remaining_buffer),
                                     [me = shared_from_this()](const boost::system::error_code& err,
                                                               std::size_t bytes_transferred)
                                     { me->writeStreamReply(err, bytes_transferred); });
        }
        else
        {
          socket().async_write_some(boost::asio::buffer(remaining_buffer),
                                    [me = shared_from_this()](const boost::system::error_code& err,
                                                              std::size_t bytes_transferred)
                                    { me->writeStreamReply(err, bytes_transferred); });
        }
      }
      else
      {
        scheduleChunkGetter();
      }
    }
    else
    {
      itsKeepAlive = false;
      sendStockReply(SmartMet::Spine::HTTP::Status::service_unavailable);
      reportInfo("Error in stream reply send to " + itsRequest->getClientIP() +
                 ". Reason: " + e.message());
      // Log the aborted stream with the bytes delivered so far.
      finalizeStreamLogging();
      notifyBackendFinished(itsResponse->getStreamingStatus());
    }
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::writeStreamReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::writeStreamReply aborted\n";
  }
}

void AsyncConnection::writeRegularReply(const boost::system::error_code& e,
                                        std::size_t bytes_transferred)
{
  try
  {
    itsFinalStatus = e;

    if (!e)
    {
      itsSentBytes += bytes_transferred;

      if (itsSentBytes < itsResponseString.size())
      {
        // Still something left to send

        // Start async write to socket
        auto remaining_buffer = boost::asio::buffer(itsResponseString) + itsSentBytes;
        if (itsEncryptionEnabled)
        {
          itsSocket.async_write_some(boost::asio::buffer(remaining_buffer),
                                     [me = shared_from_this()](const boost::system::error_code& err,
                                                               std::size_t bytes_transferred)
                                     { me->writeRegularReply(err, bytes_transferred); });
        }
        else
        {
          socket().async_write_some(boost::asio::buffer(remaining_buffer),
                                    [me = shared_from_this()](const boost::system::error_code& err,
                                                              std::size_t bytes_transferred)
                                    { me->writeRegularReply(err, bytes_transferred); });
        }
        return;
      }

      // If we are here, everything has been sent. Either wait for the next request on
      // this connection or let it close.
      notifyBackendFinished(SmartMet::Spine::HTTP::ContentStreamer::StreamerStatus::EXIT_OK);
      finishResponse();
    }
    else
    {
      itsKeepAlive = false;
      sendStockReply(SmartMet::Spine::HTTP::Status::service_unavailable);
      reportInfo("Error in reply send to " + itsRequest->getClientIP() +
                 ". Reason: " + e.message());
    }
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::writeRegularReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::writeRegularReply aborted\n";
  }
}

// Fire the response's deferred access-log finalizer with the total body
// bytes streamed. Runs at most once (the handler removes itself), so this
// is safe to call from every stream terminal path: normal completion, send
// errors, and client disconnects. No-op for responses that did not register
// a finalizer (non-streamed, or logging disabled for the handler).
// Report the outcome of a backend conversation to the hooks that track backend
// health (sputnik's heartbeat). Runs at most once per response, and only for
// responses that actually came from a backend.
void AsyncConnection::notifyBackendFinished(
    SmartMet::Spine::HTTP::ContentStreamer::StreamerStatus theStatus)
{
  try
  {
    if (itsBackendNotified || !itsResponse || itsResponse->itsOriginatingBackend.empty())
      return;

    itsBackendNotified = true;
    itsReactor.callBackendConnectionFinishedHooks(
        itsResponse->itsOriginatingBackend, itsResponse->itsBackendPort, theStatus);
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::notifyBackendFinished", nullptr);
    std::cerr << ex.getStackTrace();
  }
}

void AsyncConnection::finalizeStreamLogging()
{
  try
  {
    if (itsResponse)
      itsResponse->runStreamCompletionHandler(itsTotalStreamedBytes);
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::finalizeStreamLogging", nullptr);
    std::cerr << ex.getStackTrace();
  }
}

// Prepare unstreamed writes
void AsyncConnection::startRegularReply()
{
  try
  {
    // A streamed response only reaches this function for a HEAD request, where
    // there is no buffered body to compress and asking the streamer for one
    // would run the plugin's producer for a body nobody will read.
    const bool streamed = itsResponse->hasStreamContent();

    // Regular response
    // Compress response if its greater than limit and client accepts
    if (itsCanGzipResponse && !streamed)
    {
      auto encoding = select_content_encoding(*itsRequest, *itsResponse, itsCompressLimit);
      if (!encoding.empty())
        compress_response(*itsResponse, encoding);
    }

    if (statusHasNoBody(itsResponse->getStatus()))
    {
      // 1xx, 204 and 304 are framed by the status code itself: the message ends
      // at the header section, whatever the headers claim (RFC 9110 15.3.5,
      // 15.4.5). Sending a body - or a Content-Length promising one - would
      // desynchronise the next response on a persistent connection, since the
      // client stops reading at the end of the headers.
      itsResponse->setContent(std::string());
      itsResponse->removeHeader("Content-Length");
      itsResponse->removeHeader("Transfer-Encoding");
    }
    else if (itsHeadRequest && itsResponse->getChunked())
    {
      // The GET would have been sent chunked, which announces no length, so
      // neither does the answer to HEAD.
      itsResponse->removeHeader("Content-Length");
      itsResponse->removeHeader("Transfer-Encoding");
    }
    else
    {
      // Set Content-Length header, as it may change during compression
      itsResponse->setHeader(
          "Content-Length",
          std::to_string(static_cast<long long unsigned int>(itsResponse->getContentLength())));
    }

    std::string headers;
    std::string content;

    try
    {
      headers = itsResponse->headersToString();
    }
    catch (const std::runtime_error&)
    {
      reportInfo("Response status not set, defaulting to 501 Not Implemented");
      itsResponse->setStatus(SmartMet::Spine::HTTP::Status::not_implemented, true);

      headers = itsResponse->headersToString();
    }

    // RFC 9110 9.3.2: a HEAD response carries the header fields describing the
    // content GET would have returned, but not the content itself.
    if (itsHeadRequest)
      itsResponseString = headers;
    else
    {
      content = itsResponse->getContent();
      itsResponseString = headers + content;
    }

    // Start async write to socket
    if (itsEncryptionEnabled)
    {
      itsSocket.async_write_some(boost::asio::buffer(itsResponseString),
                                 [me = shared_from_this()](const boost::system::error_code& err,
                                                           std::size_t bytes_transferred)
                                 { me->writeRegularReply(err, bytes_transferred); });
    }
    else
    {
      socket().async_write_some(boost::asio::buffer(itsResponseString),
                                [me = shared_from_this()](const boost::system::error_code& err,
                                                          std::size_t bytes_transferred)
                                { me->writeRegularReply(err, bytes_transferred); });
    }
  }
  catch (...)
  {
    Fmi::Exception ex(BCP, "Operation failed! AsyncConnection::startRegularReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::startRegularReply aborted\n";
  }
}

// Sets common server-based headers
void AsyncConnection::setServerHeaders()
{
  try
  {
    // Put additional server-based headers here
    itsResponse->setHeader("Server", "SmartMet Server (" __TIME__ " " __DATE__ ")");
    itsResponse->setHeader("Vary", "Accept-Encoding");
    itsResponse->setHeader("Date", makeDateString());

    // --- Persistent connection (keep-alive) negotiation --------------------------
    //
    // The reply is sent in the client's own HTTP version, because the two versions
    // disagree on what a missing Connection header means:
    //
    //   HTTP/1.1  Connections are persistent by default (RFC 9112 9.3). Only the end
    //             of persistence has to be announced, with "Connection: close". The
    //             response version matters: answering an HTTP/1.1 request with an
    //             HTTP/1.0 status line would make the client assume a non-persistent
    //             connection no matter what we do with the headers.
    //
    //   HTTP/1.0  Has no persistent connections at all; keep-alive is an extension
    //             (RFC 1945 appendix D.1.1). The client asks for it with
    //             "Connection: keep-alive" and the server MUST echo the same header
    //             to confirm, otherwise the client reads the body until EOF and
    //             would hang on our reused socket.
    //
    // A chunked response is HTTP/1.1 by definition (Spine forces the version when a
    // stream of unknown length is set as the content), so its version is left alone.
    // Chunked transfer encoding does not exist in HTTP/1.0, so an HTTP/1.0 client
    // cannot be given a reusable connection in that case.
    if (itsResponse->getChunked())
    {
      if (itsResponseVersion != "1.1")
        itsKeepAlive = false;
    }
    else
    {
      itsResponse->setVersion(itsResponseVersion);
    }

    // A plugin may veto the reuse of the connection on its own terms
    auto connectionHeader = itsResponse->getHeader("Connection");
    if (connectionHeader && hasHeaderToken(*connectionHeader, "close"))
      itsKeepAlive = false;

    if (!itsKeepAlive)
    {
      itsResponse->setHeader("Connection", "close");
      itsResponse->removeHeader("Keep-Alive");
    }
    else if (itsResponseVersion == "1.1")
    {
      // Persistence is the default, saying so would only waste bytes
      itsResponse->removeHeader("Connection");
      itsResponse->removeHeader("Keep-Alive");
    }
    else
    {
      itsResponse->setHeader("Connection", "keep-alive");

      // The Keep-Alive header is informational: it tells an HTTP/1.0 client how long the
      // socket will be held and how many more requests it may still send. "max" is left
      // out entirely when the number of requests is unlimited, since "max=0" would read
      // as "no more requests allowed".
      std::string keepAlive = "timeout=" + Fmi::to_string(itsKeepAliveTimeout);
      if (itsMaxKeepAliveRequests > 0)
        keepAlive += ", max=" + Fmi::to_string(itsMaxKeepAliveRequests - itsRequestCount);
      itsResponse->setHeader("Keep-Alive", keepAlive);
    }

    // Append stale-while-revalidate and stale-if-error to cacheable responses.
    //
    // Why here rather than in each individual plugin:
    //
    //   This method is the single post-plugin hook that runs on every response before it is
    //   written to the socket. Plugins (~10 of them) already independently construct their
    //   Cache-Control header with a plugin-specific max-age, but none of them add the stale
    //   directives because those values are a server-level policy concern, not a
    //   data-product concern.  Adding the directives here means:
    //
    //   1. One code change covers all existing plugins and every future plugin automatically.
    //   2. The stale durations are tuned server-wide in smartmet.conf alongside other
    //      server-level knobs (timeout, compress, etc.), keeping operator config cohesive.
    //   3. The frontend plugin's ResponseCache stores Cache-Control verbatim and replays it
    //      on cache hits, so the stale directives propagate to CDN/browser caches with no
    //      extra code.
    //
    // Guard: only touch headers that already carry "max-age" (i.e. genuinely cacheable
    // responses).  Responses with "no-cache" or no Cache-Control header at all are left
    // unchanged.  A plugin that needs different stale values can set its own stale-*
    // directives before returning; the second guard prevents double-appending.
    //
    // Rationale for default values (configurable via smartmet.conf):
    //   stalewhilerevalidate = 60     -- serve at most 60 s stale while fetching fresh data
    //   staleiferror         = 86400  -- serve up to 24 h stale when the backend is down
    //
    const auto& opts = itsReactor.getOptions();
    if (opts.staleWhileRevalidate > 0 || opts.staleIfError > 0)
    {
      auto cc = itsResponse->getHeader("Cache-Control");
      if (cc && cc->find("max-age") != std::string::npos && cc->find("stale-") == std::string::npos)
      {
        std::string updated = *cc;
        if (opts.staleWhileRevalidate > 0)
          updated += ", stale-while-revalidate=" + std::to_string(opts.staleWhileRevalidate);
        if (opts.staleIfError > 0)
          updated += ", stale-if-error=" + std::to_string(opts.staleIfError);
        itsResponse->setHeader("Cache-Control", updated);
      }
    }
  }
  catch (...)
  {
    std::cerr << "Operation failed! AsyncConnection::setServerHeaders aborted\n";
  }
}

void AsyncConnection::sendStockReply(const SmartMet::Spine::HTTP::Status theStatus)
{
  try
  {
    itsResponse->setStatus(theStatus, true);
    itsResponse->setHeader(
        "Content-Length",
        std::to_string(static_cast<long long unsigned int>(itsResponse->getContentLength())));
    setServerHeaders();  // Set the rest of server headers

    sendSimpleReply();
  }
  catch (...)
  {
    std::cerr << "Operation failed! AsyncConnection::sendStockReply aborted\n";
  }
}

void AsyncConnection::sendSimpleReply()
{
  try
  {
    boost::system::error_code err;

    auto headerbuffer = itsResponse->headersToBuffer();
    auto contentbuffer = itsResponse->contentToBuffer();

    // No error checking here at the moment
    if (itsEncryptionEnabled)
    {
      boost::asio::write(itsSocket, headerbuffer, err);
      boost::asio::write(itsSocket, contentbuffer, err);
    }
    else
    {
      boost::asio::write(socket(), headerbuffer, err);
      boost::asio::write(socket(), contentbuffer, err);
    }

    itsFinalStatus = err;

    if (err)
      itsKeepAlive = false;  // The socket is not usable for another request

    if (itsKeepAlive)
    {
      // Stock replies are fully framed by their Content-Length header, so the client
      // knows where this response ends and the connection can serve another request.
      finishResponse();
      return;
    }

    // Shutdown immediately to avoid lingering CLOSE_WAIT sockets
    boost::system::error_code ignored_ec;
    socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    socket().close(ignored_ec);
  }
  catch (...)
  {
    std::cerr << "Operation failed! AsyncConnection::sendStockReply aborted\n";
  }
}

void AsyncConnection::scheduleChunkGetter()
{
  try
  {
    bool scheduled = false;
    // Put the chunk getter function in the appropriate pool

    if (itsAdminQuery)
    {
      scheduled = itsAdminExecutor.schedule([me = shared_from_this()]() { me->getNextChunk(); });
    }
    else if (itsQueryIsFast)
    {
      scheduled = itsFastExecutor.schedule([me = shared_from_this()]() { me->getNextChunk(); });
    }
    else
    {
      scheduled = itsSlowExecutor.schedule([me = shared_from_this()]() { me->getNextChunk(); });
    }

    // Task queue was full, send busy response
    if (!scheduled)
    {
      // Aborting in the middle of the body leaves the response unframed
      itsKeepAlive = false;
      sendStockReply(SmartMet::Spine::HTTP::Status::service_unavailable);
      reportInfo("Request queue was full, aborting chunked transfer");
    }
  }
  catch (...)
  {
    Fmi::Exception ex(
        BCP, "Operation failed! AsyncConnection::scheduleChunkGetter aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::scheduleChunkGetter aborted\n";
  }
}

void AsyncConnection::scheduleChunkedChunkGetter()
{
  try
  {
    bool scheduled = false;
    // Put the chunk getter function in the appropriate pool
    if (itsAdminQuery)
    {
      scheduled =
          itsAdminExecutor.schedule([me = shared_from_this()]() { me->getNextChunkedChunk(); });
    }
    else if (itsQueryIsFast)
    {
      scheduled =
          itsFastExecutor.schedule([me = shared_from_this()]() { me->getNextChunkedChunk(); });
    }
    else
    {
      scheduled =
          itsSlowExecutor.schedule([me = shared_from_this()]() { me->getNextChunkedChunk(); });
    }

    // Task queue was full, send busy response
    if (!scheduled)
    {
      // Aborting in the middle of the body leaves the response unframed
      itsKeepAlive = false;
      sendStockReply(SmartMet::Spine::HTTP::Status::service_unavailable);
      reportInfo("Request queue was full, aborting chunked transfer");
    }
  }
  catch (...)
  {
    Fmi::Exception ex(
        BCP, "Operation failed! AsyncConnection::scheduleChunkedChunkGetter aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::scheduleChunkedChunkGetter aborted\n";
  }
}

void AsyncConnection::notifyClientDisconnect(const boost::system::error_code& e,
                                             std::size_t /* bytes_transferred */)
{
  try
  {
    // If this function is called, either client sent something after the fully parsed request or
    // client disconnect
    // has been signaled
    boost::lock_guard<boost::mutex> lock(itsDisconnectMutex);
    if (e)
    {
      // Some error occurred, the client may have disconnected
      if (e == boost::asio::error::operation_aborted)
      {
        // Pass
      }
      else
      {
        // This handler was not cancelled, the client has definitely disconnected
        itsPrematurelyDisconnected = true;
      }
    }
    else
    {
      // Client sent something, ignore it and go back to listen
      if (itsEncryptionEnabled)
      {
        itsSocket.async_read_some(boost::asio::buffer(itsSocketBuffer),
                                  [me = shared_from_this()](const boost::system::error_code& err,
                                                            std::size_t bytes_transferred)
                                  { me->notifyClientDisconnect(err, bytes_transferred); });
      }
      else
      {
        socket().async_read_some(boost::asio::buffer(itsSocketBuffer),
                                 [me = shared_from_this()](const boost::system::error_code& err,
                                                           std::size_t bytes_transferred)
                                 { me->notifyClientDisconnect(err, bytes_transferred); });
      }
    }
  }
  catch (...)
  {
    Fmi::Exception ex(
        BCP, "Operation failed! AsyncConnection::notifyClientDisconnect aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::notifyClientDisconnect aborted\n";
  }
}

}  // namespace Server
}  // namespace SmartMet
