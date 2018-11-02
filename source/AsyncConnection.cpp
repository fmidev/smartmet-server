//===================================
/*
 * Definition of the class Connection
 */
//===================================

#include "AsyncConnection.h"
#include "AsyncServer.h"
#include "Utility.h"
#include <boost/bind.hpp>
#include <boost/move/make_unique.hpp>
#include <spine/Convenience.h>
#include <spine/Exception.h>
#include <sstream>
#include <vector>

namespace SmartMet
{
namespace Server
{
AsyncConnection::AsyncConnection(AsyncServer* serverInstance,
                                 bool canGzipResponse,
                                 std::size_t compressLimit,
                                 std::size_t maxRequestSize,
                                 long timeout,
                                 bool dumpRequests,
                                 boost::asio::io_service& io_service,
                                 SmartMet::Spine::Reactor& theReactor,
                                 ThreadPoolType& slowExecutor,
                                 ThreadPoolType& fastExecutor)
    : Connection(serverInstance,
                 canGzipResponse,
                 compressLimit,
                 maxRequestSize,
                 timeout,
                 dumpRequests,
                 io_service,
                 theReactor,
                 slowExecutor,
                 fastExecutor),
      itsSentBytes(0),
      itsPrematurelyDisconnected(false)
{
}

// Initiate graceful Connection closure after the reply is written (connection is destructing)
AsyncConnection::~AsyncConnection()
{
  if (!hasTimedOut)
  {
    boost::system::error_code ignored_ec;
    itsSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
    itsSocket.close(ignored_ec);
  }
}

boost::asio::ip::tcp::socket& AsyncConnection::socket()
{
  return itsSocket;
}
void AsyncConnection::handleTimer(const boost::system::error_code& err)
{
  try
  {
    SmartMet::Spine::WriteLock lock(itsMutex);  // Lock here, just in case
    if (err != boost::asio::error::operation_aborted)
    {
      hasTimedOut = true;

      sendStockReply(SmartMet::Spine::HTTP::Status::request_timeout);

      boost::system::error_code ignored_ec;
      itsSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
      itsSocket.close(ignored_ec);
    }
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// Start a new connection
void AsyncConnection::start()
{
  try
  {
    // Start the timeout timer

    itsTimeoutTimer = boost::movelib::make_unique<boost::asio::deadline_timer>(
        itsIoService, boost::posix_time::seconds(itsTimeout));

    itsTimeoutTimer->async_wait(boost::bind(&AsyncConnection::handleTimer, shared_from_this(), _1));

    // Begin the reading process

    itsSocket.async_read_some(boost::asio::buffer(itsSocketBuffer),
                              boost::bind(&AsyncConnection::handleRead,
                                          shared_from_this(),
                                          boost::asio::placeholders::error,
                                          boost::asio::placeholders::bytes_transferred));
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception::Trace(BCP, "Operation failed!");
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

    if (itsReactor.isLoadHigh())
    {
      std::cout << Spine::log_time_str() << " Too many active requests, reporting high load"
                << std::endl;
      sendStockReply(SmartMet::Spine::HTTP::Status::high_load);
      return;
    }

    // Initialize the connection status.
    itsFinalStatus = e;

    if (!e)
    {
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

      // Try to parse the incoming message
      auto parsedRequest =
          SmartMet::Spine::HTTP::parseRequest(itsBuffer);  // Of type (tribool, parsed request)

      if (parsedRequest.first == SmartMet::Spine::HTTP::ParsingStatus::COMPLETE)
      {
        // Successfully parsed the request, set connection request
        itsRequest = std::move(parsedRequest.second);

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
            itsRequest->setClientIP(itsSocket.remote_endpoint().address().to_string());
          }
          catch (...)
          {
            SmartMet::Spine::Exception exception(BCP, "Operation failed!", nullptr);
            reportError(std::string("Failed to obtain remote endpoint IP address:\n") +
                        exception.what());
            return;
          }
        }

#ifndef NDEBUG
        // DEBUGGIN OUTPUT************************************

        std::cout << "Incoming request from " << itsRequest->getClientIP() << std::endl;
        std::cout << "Method: " << itsRequest->getMethodString() << std::endl;
        std::cout << "URI: " << itsRequest->getURI() << std::endl;
        std::cout << "Query string: " << itsRequest->getQueryString() << std::endl;
        std::cout << "Headers: " << std::endl;
        auto hmappi = itsRequest->getHeaders();
        for (const auto& header : hmappi)
        {
          std::cout << header.first << " : " << header.second << std::endl;
        }

        std::cout << "Parsed parameters: " << std::endl;
        auto mappi = itsRequest->getParameterMap();
        for (const auto& elem : mappi)
        {
          std::cout << elem.first << " : " << elem.second << std::endl;
        }
        std::cout << "Content: \"" << itsRequest->getContent() << "\"" << std::endl << std::endl;

// DEBUGGIN OUTPUT************************************
#endif

        // Determine where to put the handler function
        auto handlerView = itsReactor.getHandlerView(*itsRequest);
        if (!handlerView)
        {
          // Couldn't find handler for the request
          sendStockReply(SmartMet::Spine::HTTP::Status::not_found);

          return;
        }

        bool scheduled = false;

        // Handle frontends and backends separately

        if (handlerView->isCatchNoMatch())
        {
          // frontend
          itsQueryIsFast = true;
          scheduled = itsFastExecutor.schedule(boost::bind(
              &AsyncConnection::handleCompletedRead, shared_from_this(), boost::ref(*handlerView)));

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

          itsQueryIsFast = handlerView->queryIsFast(*itsRequest);

          if (itsQueryIsFast)
          {
            scheduled = itsFastExecutor.schedule(boost::bind(&AsyncConnection::handleCompletedRead,
                                                             shared_from_this(),
                                                             boost::ref(*handlerView)));
          }
          else
          {
            scheduled = itsSlowExecutor.schedule(boost::bind(&AsyncConnection::handleCompletedRead,
                                                             shared_from_this(),
                                                             boost::ref(*handlerView)));
          }

          if (!scheduled)
          {
            // Task queue was full, send high load response to frontend
            sendStockReply(SmartMet::Spine::HTTP::Status::high_load);
            reportInfo("Backend request queue was full, returning high load notification");
          }
        }
      }
      else if (parsedRequest.first == SmartMet::Spine::HTTP::ParsingStatus::FAILED)
      {
        // Failed parse, something (fundamentally) wrong with the request

        sendStockReply(SmartMet::Spine::HTTP::Status::bad_request);
      }
      else
      {
        // Request is not succesfully parsed, attempt to get more data from socket and try again
        itsSocket.async_read_some(boost::asio::buffer(itsSocketBuffer),
                                  boost::bind(&AsyncConnection::handleRead,
                                              shared_from_this(),
                                              boost::asio::placeholders::error,
                                              boost::asio::placeholders::bytes_transferred));
      }
    }
    else if (e == boost::asio::error::eof)
    {
      // Peer closed the socket or timeout has occurred
      // Abort this connection
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
    Spine::Exception ex(BCP, "Operation failed! AsyncConnection::handleRead aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::handleRead aborted" << std::endl;
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
        itsSocket.cancel();
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
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::handleCompletedRead aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // Must not continue throwing here or the server will terminate
    // std::cerr << "Operation failed! AsyncConnection::handleCompletedRead aborted" << std::endl;
  }
}

// Handle gateway writes. This function is always called from within the thread pool
void AsyncConnection::startGatewayReply()
{
  try
  {
    // This response is a gateway response, simply stream its content to client without any
    // modifications
    // Get first chunk

    this->getNextChunk();
  }
  catch (...)
  {
    throw SmartMet::Spine::Exception::Trace(BCP, "Operation failed!");
  }
}

// start chunked write. This function is always called from within the thread pool
void AsyncConnection::startChunkedReply()
{
  try
  {
    // Content is to be sent using chunked content encoding
    itsResponse->setHeader("Transfer-Encoding", "chunked");

    // Headers are currently written synchronously
    try
    {
      boost::system::error_code e;
      auto headerbuffer = itsResponse->headersToBuffer();
      // Write headers
      boost::asio::write(itsSocket, headerbuffer, e);
      if (e != nullptr)
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
      boost::asio::write(itsSocket, headerbuffer, e);
      if (e != nullptr)
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
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::startChunkedReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::startChunkedReply aborted" << std::endl;
  }
}

// Response is streamed without chunked encoding. This function is always called from within the
// thread pool
void AsyncConnection::startStreamReply()
{
  try
  {
    // Set Content-Length header, just to be sure
    itsResponse->setHeader(
        "Content-Length",
        std::to_string(static_cast<long long unsigned int>(itsResponse->getContentLength())));

    // Currently headers a written syncronously
    try
    {
      boost::system::error_code e;
      auto headerbuffer = itsResponse->headersToBuffer();
      // Write headers
      boost::asio::write(itsSocket, headerbuffer, e);
      if (e != nullptr)
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
      boost::asio::write(itsSocket, headerbuffer, e);
      if (e != nullptr)
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
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::startStreamReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::startStreamReply aborted" << std::endl;
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
        itsSocket.async_write_some(boost::asio::buffer(remaining_buffer),
                                   boost::bind(&AsyncConnection::writeChunkedReply,
                                               shared_from_this(),
                                               boost::asio::placeholders::error,
                                               boost::asio::placeholders::bytes_transferred));
      }
      else
      {
        scheduleChunkedChunkGetter();
      }
    }
    else
    {
      boost::system::error_code ignored_ec;
      itsSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
      itsSocket.close();
      reportInfo("Error in chunked reply send to " + itsRequest->getClientIP() +
                 ". Reason: " + e.message() + ". Code: " + Fmi::to_string(e.value()));
    }
  }
  catch (...)
  {
    Spine::Exception ex(BCP, "Operation failed! AsyncConnection::writeChunkedReply", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::writeChunkedReply aborted" << std::endl;
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
        itsSocket.async_write_some(boost::asio::buffer(remaining_buffer),
                                   boost::bind(&AsyncConnection::finalizeChunkedReply,
                                               shared_from_this(),
                                               boost::asio::placeholders::error,
                                               boost::asio::placeholders::bytes_transferred));
      }
      // Implicit else here, this was the final chunk so stop scheduling async writes
    }
    else
    {
      boost::system::error_code ignored_ec;
      itsSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
      itsSocket.close();
      reportInfo("Error in finalizing chunked reply send to " + itsRequest->getClientIP() +
                 ". Reason: " + e.message() + ". Code: " + Fmi::to_string(e.value()));
    }
  }
  catch (...)
  {
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::finalizeChunkedReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::finalizeChunkedReply aborted" << std::endl;
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
        // Send received data
        itsResponseString = contentstring;

        // Reset sent bytes counter, this is a new chunk

        itsSentBytes = 0;

        // Schedule next async write operation
        itsSocket.async_write_some(boost::asio::buffer(itsResponseString),
                                   boost::bind(&AsyncConnection::writeStreamReply,
                                               shared_from_this(),
                                               boost::asio::placeholders::error,
                                               boost::asio::placeholders::bytes_transferred));
      }
      else
      {
        // Empty chunk but stream is ok, reschedule for later
        scheduleChunkGetter();
      }
    }
    else
    {
      // Stream status is EXIT, finalize the send
      if (itsResponse->isGatewayResponse)
      {
        // If the response is a gateway response (sent by frontend plugin) call the associated hooks

        itsReactor.callBackendConnectionFinishedHooks(
            itsResponse->itsOriginatingBackend, itsResponse->itsBackendPort, streamStatus);
      }
    }
  }
  catch (...)
  {
    Spine::Exception ex(BCP, "Operation failed! AsyncConnection::getNextChunk aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::getNextChunk aborted" << std::endl;
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
        std::string hexlength = convertToHex(length);
        std::string responsestring = hexlength + "\r\n" + contentstring + "\r\n";

        // This is new chunk, zero the counter and set response string
        itsResponseString = responsestring;

        itsSentBytes = 0;

        // Schedule the next asynchronous write
        itsSocket.async_write_some(boost::asio::buffer(itsResponseString),
                                   boost::bind(&AsyncConnection::writeChunkedReply,
                                               shared_from_this(),
                                               boost::asio::placeholders::error,
                                               boost::asio::placeholders::bytes_transferred));
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

      itsSocket.async_write_some(boost::asio::buffer(itsResponseString),
                                 boost::bind(&AsyncConnection::finalizeChunkedReply,
                                             shared_from_this(),
                                             boost::asio::placeholders::error,
                                             boost::asio::placeholders::bytes_transferred));
    }
  }
  catch (...)
  {
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::getNextChunkedChunk aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::getNextChunkedChunk aborted" << std::endl;
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
        itsSocket.async_write_some(boost::asio::buffer(remaining_buffer),
                                   boost::bind(&AsyncConnection::writeStreamReply,
                                               shared_from_this(),
                                               boost::asio::placeholders::error,
                                               boost::asio::placeholders::bytes_transferred));
      }
      else
      {
        scheduleChunkGetter();
      }
    }
    else
    {
      reportInfo("Error in stream reply send to " + itsRequest->getClientIP() +
                 ". Reason: " + e.message());
      if (itsResponse->isGatewayResponse)
      {
        // If the response is a gateway response (sent by frontend plugin) call the associated hooks

        itsReactor.callBackendConnectionFinishedHooks(itsResponse->itsOriginatingBackend,
                                                      itsResponse->itsBackendPort,
                                                      itsResponse->getStreamingStatus());
      }
      boost::system::error_code ignored_ec;
      itsSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
      itsSocket.close();
    }
  }
  catch (...)
  {
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::writeStreamReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::writeStreamReply aborted" << std::endl;
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
        itsSocket.async_write_some(boost::asio::buffer(remaining_buffer),
                                   boost::bind(&AsyncConnection::writeRegularReply,
                                               shared_from_this(),
                                               boost::asio::placeholders::error,
                                               boost::asio::placeholders::bytes_transferred));
        return;
      }

      // If we are here, everything has been sent and we can close the connection (stop setting
      // asynchronous writes)
    }
    else
    {
      reportInfo("Error in reply send to " + itsRequest->getClientIP() +
                 ". Reason: " + e.message());
      boost::system::error_code ignored_ec;
      itsSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
      itsSocket.close();
    }
  }
  catch (...)
  {
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::writeRegularReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::writeRegularReply aborted" << std::endl;
  }
}

// Prepare unstreamed writes
void AsyncConnection::startRegularReply()
{
  try
  {
    // Regular response
    // Compress response if its greater than limit and client accepts
    if (itsCanGzipResponse && response_is_compressable(*itsRequest, *itsResponse, itsCompressLimit))
    {
      gzip_response(*itsResponse);
    }

    // Set Content-Length header, as it may change during compression
    itsResponse->setHeader(
        "Content-Length",
        std::to_string(static_cast<long long unsigned int>(itsResponse->getContentLength())));

    std::string headers, content;

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

    content = itsResponse->getContent();

    itsResponseString = headers + content;

    // Start async write to socket
    itsSocket.async_write_some(boost::asio::buffer(itsResponseString),
                               boost::bind(&AsyncConnection::writeRegularReply,
                                           shared_from_this(),
                                           boost::asio::placeholders::error,
                                           boost::asio::placeholders::bytes_transferred));
  }
  catch (...)
  {
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::startRegularReply aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::startRegularReply aborted" << std::endl;
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

    if (itsResponse->getVersion() == "1.1")
    {
      itsResponse->setHeader("Connection",
                             "close");  // Current implementation is one-request-per-connection
    }
  }
  catch (...)
  {
    std::cerr << "Operation failed! AsyncConnection::setServerHeaders aborted" << std::endl;
  }
}

void AsyncConnection::sendStockReply(const SmartMet::Spine::HTTP::Status theStatus)
{
  try
  {
    boost::system::error_code err;

    itsResponse->setStatus(theStatus, true);
    itsResponse->setHeader(
        "Content-Length",
        std::to_string(static_cast<long long unsigned int>(itsResponse->getContentLength())));
    setServerHeaders();  // Set the rest of server headers

    auto headerbuffer = itsResponse->headersToBuffer();
    auto contentbuffer = itsResponse->contentToBuffer();

    // No error checking here at the moment
    boost::asio::write(itsSocket, headerbuffer, err);
    boost::asio::write(itsSocket, contentbuffer, err);

    itsFinalStatus = err;
  }
  catch (...)
  {
    std::cerr << "Operation failed! AsyncConnection::sendStockReply aborted" << std::endl;
  }
}

void AsyncConnection::scheduleChunkGetter()
{
  try
  {
    bool scheduled = false;
    // Put the chunk getter function in the appropriate pool
    if (itsQueryIsFast)
    {
      while (!scheduled)
      {
        scheduled = itsFastExecutor.schedule(
            boost::bind(&AsyncConnection::getNextChunk, shared_from_this()));
        if (!scheduled)
        {
          // If SmartMet Reactor queue is full, sleep for a while to let it clear
          boost::this_thread::sleep(boost::posix_time::milliseconds(10));
        }
      }
    }
    else
    {
      while (!scheduled)
      {
        scheduled = itsSlowExecutor.schedule(
            boost::bind(&AsyncConnection::getNextChunk, shared_from_this()));
        if (!scheduled)
        {
          // If SmartMet Reactor queue is full, sleep for a while to let it clear
          boost::this_thread::sleep(boost::posix_time::milliseconds(10));
        }
      }
    }
  }
  catch (...)
  {
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::scheduleChunkGetter aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::scheduleChunkGetter aborted" << std::endl;
  }
}

void AsyncConnection::scheduleChunkedChunkGetter()
{
  try
  {
    bool scheduled = false;
    // Put the chunk getter function in the appropriate pool
    if (itsQueryIsFast)
    {
      while (!scheduled)
      {
        scheduled = itsFastExecutor.schedule(
            boost::bind(&AsyncConnection::getNextChunkedChunk, shared_from_this()));
        if (!scheduled)
        {
          // If the queue is full, sleep for a while to let it clear
          boost::this_thread::sleep(boost::posix_time::milliseconds(10));
        }
      }
    }
    else
    {
      while (!scheduled)
      {
        scheduled = itsSlowExecutor.schedule(
            boost::bind(&AsyncConnection::getNextChunkedChunk, shared_from_this()));
        if (!scheduled)
        {
          // If the queue is full, sleep for a while to let it clear
          boost::this_thread::sleep(boost::posix_time::milliseconds(10));
        }
      }
    }
  }
  catch (...)
  {
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::scheduleChunkedChunkGetter aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::scheduleChunkedChunkGetter aborted" <<
    // std::endl;
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
    if (e != nullptr)
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
      itsSocket.async_read_some(boost::asio::buffer(itsSocketBuffer),
                                boost::bind(&AsyncConnection::notifyClientDisconnect,
                                            shared_from_this(),
                                            boost::asio::placeholders::error,
                                            boost::asio::placeholders::bytes_transferred));
    }
  }
  catch (...)
  {
    Spine::Exception ex(
        BCP, "Operation failed! AsyncConnection::notifyClientDisconnect aborted", nullptr);
    std::cerr << ex.getStackTrace();
    // std::cerr << "Operation failed! AsyncConnection::notifyClientDisconnect aborted" <<
    // std::endl;
  }
}

}  // namespace Server
}  // namespace SmartMet
