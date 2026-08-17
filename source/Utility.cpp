#include "Utility.h"
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <macgyver/DateTime.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <array>
#include <fstream>
#include <sstream>

namespace
{
const std::array<const char*, 8> weekdays = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const std::array<const char*, 13> months = {
    "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

}  // namespace

namespace SmartMet
{
namespace Server
{
std::string makeDateString()
{
  try
  {
    auto u_time = Fmi::SecondClock::universal_time();
    auto date = u_time.date();
    auto time = u_time.time_of_day();

    int weekday = date.day_of_week().iso_encoding();
    int day = date.day();
    int month = date.month();
    int year = date.year();

    int hour = time.hours();
    int minute = time.minutes();
    int second = time.seconds();

    std::string date_string = fmt::sprintf("%s, %02ld %s %d %02ld:%02ld:%02ld GMT",
                                           weekdays[weekday],  // NOLINT
                                           day,
                                           months[month],  // NOLINT
                                           year,
                                           hour,
                                           minute,
                                           second);

    return date_string;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::string convertToHex(std::size_t theNumber)
{
  try
  {
    return Fmi::to_string("%x", static_cast<unsigned long>(theNumber));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::string select_content_encoding(const SmartMet::Spine::HTTP::Request& request,
                                    const SmartMet::Spine::HTTP::Response& response,
                                    std::size_t compressLimit)
{
  try
  {
    std::vector<std::string> non_compressable_mimes = {
        "image/png", "image/webp", "application/pdf"};

    auto content_header = response.getHeader("Content-Type");
    if (content_header)
    {
      for (const auto& mime : non_compressable_mimes)
      {
        if (content_header->find(mime) != std::string::npos)
          return "";
      }
    }

    // The gzip=1 request parameter forces gzip compression regardless of size.
    auto gzip = request.getParameter("gzip");
    if (gzip && *gzip == "1")
      return "gzip";

    auto header = request.getHeader("Accept-Encoding");
    if (!header)
      return "";

    if (response.getContentLength() < compressLimit)
      return "";

    // Prefer zstd over gzip: at their default/fast levels it is both faster and
    // compresses better. Fall back to gzip for clients that do not support zstd.
    if (header->find("zstd") != std::string::npos)
      return "zstd";

    if (header->find("gzip") != std::string::npos)
      return "gzip";

    return "";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void compress_response(SmartMet::Spine::HTTP::Response& response, const std::string& encoding)
{
  try
  {
    namespace io = boost::iostreams;
    std::string content = response.getContent();

    std::string output;
    io::filtering_streambuf<io::output> tmp;

    if (encoding == "zstd")
    {
      // Use zstd's default level (3). Higher levels switch to slower search
      // strategies for little extra gain on typical responses.
      tmp.push(io::zstd_compressor());
    }
    else
    {
      // gzip level 3: the highest level still using zlib's fast deflate algorithm.
      // The default level 6 uses the much slower deflate_slow lazy matcher.
      constexpr int gzip_fast_level = 3;
      tmp.push(io::gzip_compressor(io::gzip_params(gzip_fast_level)));
    }

    tmp.push(io::back_inserter(output));
    io::copy(boost::make_iterator_range(content.begin(), content.end()), tmp);

    response.setContent(output);

    response.setHeader("Content-Encoding", encoding);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::string parseXForwardedFor(const std::string& input)
{
  try
  {
    std::size_t loc = input.find(',');

    if (loc == std::string::npos)
      return input;  // No comma, ip is the entre token

    return input.substr(0, loc);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool hasHeaderToken(const std::string& headerValue, const std::string& token)
{
  try
  {
    std::size_t pos = 0;
    while (pos <= headerValue.size())
    {
      std::size_t end = headerValue.find(',', pos);
      if (end == std::string::npos)
        end = headerValue.size();

      // Trim the linear whitespace allowed around a list element
      std::size_t first = headerValue.find_first_not_of(" \t", pos);
      if (first != std::string::npos && first < end)
      {
        std::size_t last = headerValue.find_last_not_of(" \t", end - 1);
        if (Fmi::ascii_tolower_copy(headerValue.substr(first, last - first + 1)) == token)
          return true;
      }

      pos = end + 1;
    }

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::string negotiateVersion(const std::string& requestVersion)
{
  try
  {
    const std::size_t dot = requestVersion.find('.');
    if (dot != std::string::npos)
    {
      try
      {
        const unsigned long major = Fmi::stoul(requestVersion.substr(0, dot));
        const unsigned long minor = Fmi::stoul(requestVersion.substr(dot + 1));
        if (major > 1 || (major == 1 && minor >= 1))
          return "1.1";
      }
      catch (...)
      {
        // Unparseable version, fall through to the conservative default
      }
    }

    return "1.0";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

RawRequestHead peekRequestHead(const std::string& buffer)
{
  try
  {
    RawRequestHead head;

    const std::size_t end = buffer.find("\r\n\r\n");
    if (end == std::string::npos)
      return head;  // Header section is still on its way

    head.complete = true;

    // Request line: METHOD SP request-target SP HTTP/<major>.<minor>
    const std::size_t eol = buffer.find("\r\n");
    const std::size_t versionPos = buffer.rfind("HTTP/", eol);
    if (versionPos != std::string::npos)
      head.version = buffer.substr(versionPos + 5, eol - versionPos - 5);

    // Header lines. Obsolete line folding is rejected by the request parser,
    // so every header occupies exactly one line here.
    std::size_t pos = eol + 2;
    while (pos < end)
    {
      const std::size_t lineEnd = buffer.find("\r\n", pos);
      if (lineEnd == std::string::npos || lineEnd > end)
        break;

      const std::size_t colon = buffer.find(':', pos);
      if (colon != std::string::npos && colon < lineEnd &&
          Fmi::ascii_tolower_copy(buffer.substr(pos, colon - pos)) == "expect")
      {
        const std::size_t first = buffer.find_first_not_of(" \t", colon + 1);
        if (first != std::string::npos && first < lineEnd)
        {
          const std::size_t last = buffer.find_last_not_of(" \t", lineEnd - 1);
          head.expect = buffer.substr(first, last - first + 1);
        }
        break;
      }

      pos = lineEnd + 2;
    }

    return head;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::string dumpRequest(SmartMet::Spine::HTTP::Request& request)
{
  try
  {
    std::ostringstream ss;

    ss << "Received request: ";

    ss << request.getURI() << " (" << request.getClientIP() << ")";

    std::size_t contentLength = request.getContentLength();

    if (contentLength > 0)
    {
      // Request has content, dump 25 first characters of it in addition to the URI
      std::size_t maxCharacters = std::min(25UL, static_cast<unsigned long>(contentLength));
      std::string content = request.getContent();  // No support for streamable requests?
      ss << content.substr(0, maxCharacters) << "\n";
    }

    return ss.str();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::string readPassword(const std::string& file)
{
  std::ifstream is{file};
  if (!is)
  {
    Fmi::Exception ex(BCP, "Cannot open the password file!");
    ex.addParameter("password_file", file);
    throw ex;
  }

  std::string s;
  std::getline(is, s);
  if (s.empty() && is.eof())
  {
    Fmi::Exception ex(BCP, "Cannot read the password!");
    ex.addParameter("password_file", file);
    throw ex;
  }
  return s;
}

}  // namespace Server
}  // namespace SmartMet
