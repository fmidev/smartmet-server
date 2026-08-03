// ======================================================================
/*!
 * \brief Utility functions for the server
 */
// ======================================================================

#pragma once
#include <spine/HTTP.h>

namespace SmartMet
{
namespace Server
{
std::string convertToHex(std::size_t theNumber);

// Choose the Content-Encoding to use for the response based on the request's
// Accept-Encoding header, the response mime type and size. Returns "zstd", "gzip"
// or an empty string when the response should not be compressed.
std::string select_content_encoding(const SmartMet::Spine::HTTP::Request& request,
                                    const SmartMet::Spine::HTTP::Response& response,
                                    std::size_t compressLimit);

// Compress the response body in place using the given encoding ("zstd" or "gzip")
// and set the Content-Encoding header accordingly.
void compress_response(SmartMet::Spine::HTTP::Response& response, const std::string& encoding);

std::string makeDateString();

std::string parseXForwardedFor(const std::string& input);

// ======================================================================
/*!
 * \brief Test whether a comma separated HTTP header value contains a token
 *
 * Several HTTP headers ("Connection", "Proxy-Connection", ...) carry a comma
 * separated list of case insensitive tokens, e.g. "keep-alive, Upgrade".
 * Surrounding whitespace around each token is ignored.
 *
 * \param headerValue The raw header value
 * \param token The token to look for, in lower case
 */
// ======================================================================
bool hasHeaderToken(const std::string& headerValue, const std::string& token);

// ======================================================================
/*!
 * \brief Resolve the HTTP version to answer a request with
 *
 * The reply must speak the same HTTP version as the request, since the
 * default connection semantics differ between versions. Anything the request
 * parser reports as 1.1 or newer is answered as 1.1 (the highest version this
 * server implements), everything else as 1.0.
 *
 * \param requestVersion Version string of the request, e.g. "1.1"
 * \return Either "1.1" or "1.0"
 */
// ======================================================================
std::string negotiateVersion(const std::string& requestVersion);

// ======================================================================
/*!
 * \brief Selected fields of a request whose header section has arrived
 *
 * Read straight from the raw socket buffer, i.e. before the request can be
 * parsed. "Expect: 100-continue" has to be answered at exactly that point:
 * the client is deliberately withholding the body until the server says it
 * wants it, so waiting for a complete request would deadlock.
 */
// ======================================================================
struct RawRequestHead
{
  /// True once the CRLFCRLF terminating the header section has been seen
  bool complete = false;

  /// Version from the request line, e.g. "1.1". Empty when unparseable.
  std::string version;

  /// Value of the Expect header, empty when absent
  std::string expect;
};

RawRequestHead peekRequestHead(const std::string& buffer);

std::string dumpRequest(SmartMet::Spine::HTTP::Request& request);

// ======================================================================
/*!
 * \brief Read password from a file
 * \param filename Name of the password file
 * \return The password read
 * \throw Fmi::Exception File cannot be opened or is empty
 */
// ======================================================================
std::string readPassword(const std::string& filename);

}  // namespace Server
}  // namespace SmartMet
