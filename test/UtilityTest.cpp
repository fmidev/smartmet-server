// ======================================================================
/*!
 * \file
 * \brief Unit tests for utility functions
 */
// ======================================================================

#define BOOST_TEST_MODULE Utility tester
#include "Utility.h"
#include <macgyver/Exception.h>
#include <boost/test/included/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

using namespace boost::unit_test;
using namespace SmartMet::Server;

BOOST_AUTO_TEST_CASE(test_read_password)
{
  const auto pw{readPassword("testpass1.txt")};
  BOOST_CHECK_EQUAL(pw, "X5QqQwML I4ysFwHx\txCD7rO8z");
}

BOOST_AUTO_TEST_CASE(test_read_password_no_newline)
{
  const auto pw{readPassword("testpass2.txt")};
  BOOST_CHECK_EQUAL(pw, "1234");
}

BOOST_AUTO_TEST_CASE(test_read_password_empty_password)
{
  BOOST_CHECK_EQUAL(readPassword("newline.txt"), "");
}

BOOST_AUTO_TEST_CASE(test_read_password_nonexistent_file)
{
  BOOST_CHECK_THROW(readPassword(""), Fmi::Exception);
}

BOOST_AUTO_TEST_CASE(test_read_password_empty_file)
{
  BOOST_CHECK_THROW(readPassword("empty.txt"), Fmi::Exception);
}
