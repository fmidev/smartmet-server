#include "Utility.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <macgyver/StringConversion.h>
#include <macgyver/Exception.h>
#include <array>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
const std::array<const char *,8> weekdays = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const std::array<const char *,13> months = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

}

namespace SmartMet
{

namespace Server
{
std::string makeDateString()
{
  try
  {

    auto u_time = boost::posix_time::second_clock::universal_time();
    auto date = u_time.date();
    auto time = u_time.time_of_day();

    int weekday = date.day_of_week();
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

bool response_is_compressable(const SmartMet::Spine::HTTP::Request& request,
                              const SmartMet::Spine::HTTP::Response& response,
                              std::size_t compressLimit)
{
  try
  {
    std::vector<std::string> non_compressable_mimes = {"image/png", "application/pdf"};

    auto content_header = response.getHeader("Content-Type");
    if (content_header)
    {
      for (const auto& mime : non_compressable_mimes)
      {
        if (content_header->find(mime) != std::string::npos)
          return false;
      }
    }

    auto header = request.getHeader("Accept-Encoding");
    if (!header)
      return false;

    if (header->find("gzip") != std::string::npos)
    {
      if (response.getContentLength() >= compressLimit)
        return true;
    }

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void gzip_response(SmartMet::Spine::HTTP::Response& response)
{
  try
  {
    namespace io = boost::iostreams;
    std::string content = response.getContent();

    // REF: http://lists.boost.org/boost-users/att-34361/main.cpp
    // This is supposed to be the fastest way to compress out
    // of the presented methods.

    std::string output;
    io::filtering_streambuf<io::output> tmp;
    tmp.push(io::gzip_compressor());
    tmp.push(io::back_inserter(output));
    io::copy(boost::make_iterator_range(content.begin(), content.end()), tmp);
    // Rewrite output

    response.setContent(output);

    response.setHeader("Content-Encoding", "gzip");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void reportError(const std::string& message)
{
  try
  {
    std::ostringstream os;
    os << boost::posix_time::second_clock::local_time()
       << " Incoming connection error: " << message;
    std::cerr << os.str() << std::endl;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void reportInfo(const std::string& message)
{
  try
  {
    std::ostringstream os;
    os << boost::posix_time::second_clock::local_time() << " Server info: " << message;
    std::cerr << os.str() << std::endl;
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

std::string dumpRequest(SmartMet::Spine::HTTP::Request& request)
{
  try
  {
    std::ostringstream ss;

    ss << "Received request: ";

    ss << request.getURI() << " (" << request.getClientIP() << ")\n";

    std::size_t contentLength = request.getContentLength();

    if (contentLength > 0)
    {
      // Request has content, dump 25 first characters of it in addition to the URI
      std::size_t maxCharacters = std::min(25UL, static_cast<unsigned long>(contentLength));
      std::string content = request.getContent();  // No support for streamable requests?
      ss << content.substr(maxCharacters) << "\n";
    }

    return ss.str();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Server
}  // namespace SmartMet
