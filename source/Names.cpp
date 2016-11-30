// ======================================================================
/*!
 * \brief Implementation of namespace SmartMet::Names
 */
// ======================================================================

#include "Names.h"

#include <boost/regex.hpp>

namespace SmartMet
{
namespace Server
{
namespace Names
{
// ----------------------------------------------------------------------
/*!
 * \brief Extract engine name from filename
 */
// ----------------------------------------------------------------------

std::string engine_name(const std::string& filename)
{
  const boost::regex e(".*/(.*?)\\.so");
  std::string name = boost::regex_replace(filename, e, "\\1");
  return name;
}

// ----------------------------------------------------------------------
/*!
 * \brief Extract plugin name from filename
 */
// ----------------------------------------------------------------------

std::string plugin_name(const std::string& filename)
{
  return engine_name(filename);
}

}  // namespace Names
}  // namespace Server
}  // namespace SmartMet

// ======================================================================
