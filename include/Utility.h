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

bool response_is_compressable(const SmartMet::Spine::HTTP::Request& request,
                              const SmartMet::Spine::HTTP::Response& response,
                              std::size_t compressLimit);

void gzip_response(SmartMet::Spine::HTTP::Response& response);

void reportError(const std::string& message);

void reportInfo(const std::string& message);

std::string makeDateString();

std::string parseXForwardedFor(const std::string& input);

std::string dumpRequest(SmartMet::Spine::HTTP::Request& request);

}  // namespace Server
}  // namespace SmartMet
