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
