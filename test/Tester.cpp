#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <spine/HTTP.h>

#include <boost/algorithm/string.hpp>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>

#include <macgyver/ThreadPool.h>

typedef Fmi::ThreadPool::ThreadPool<> pool;

using namespace std;

namespace fs = boost::filesystem;

namespace ip = boost::asio::ip;

namespace bp = boost::posix_time;

namespace po = boost::program_options;

pair<string, string> parseStatus(const boost::iterator_range<char*>& message)
{
  std::vector<boost::iterator_range<std::string::const_iterator> > headerTokens;
  vector<string> statusLineTokens(3);

  auto header_boundary = boost::algorithm::find_first(message, "\r\n\r\n");

  auto header_range = boost::make_iterator_range(message.begin(), header_boundary.begin());
  boost::algorithm::split(
      headerTokens, header_range, boost::is_any_of("\r\n"), boost::token_compress_on);

  if (headerTokens.empty())
  {
    return pair<string, string>();
  }

  auto startIt = headerTokens.begin();
  boost::algorithm::split(statusLineTokens, *startIt, boost::is_space(), boost::token_compress_on);

  string messageBody = string(header_boundary.end(), message.end());

  boost::algorithm::replace_all(messageBody, "\n", " ");
  boost::algorithm::replace_all(messageBody, "\r", " ");

  string code;
  try
  {
    code = statusLineTokens.at(1);
  }
  catch (...)
  {
    return pair<string, string>();
  }

  return make_pair(code, messageBody);
}

struct Request
{
  Request(string host, unsigned short port, string URI) : hostName(host), port(port), URI(URI) {}
  friend ostream& operator<<(ostream& stream, const Request& theReq)
  {
    stream << theReq.hostName << ":" << theReq.port << theReq.URI;
    return stream;
  }

  string hostName;

  unsigned short port;

  string URI;
};

struct Result
{
  Result(bp::ptime resTime, string URI, bp::time_duration duration, string status, string response)
      : resTime(resTime), URI(URI), duration(duration), status(status), response(response)
  {
  }

  friend ostream& operator<<(ostream& stream, const Result& theRes)
  {
    stream << theRes.resTime << "###" << theRes.URI << "###"
           << bp::to_simple_string(theRes.duration) << "###" << theRes.status << "###"
           << theRes.response;
    return stream;
  }

  bp::ptime resTime;

  string URI;

  bp::time_duration duration;

  string status;

  string response;
};

class Tester
{
 public:
  Tester(const string& requestFile,
         unsigned int threads,
         const string& hostname,
         unsigned short port)
      : itsHostName(hostname),
        itsPort(port),
        itsRequestFilePath(requestFile),
        itsThreadPool(new pool(threads)),
        itsSignals(itsIO),
        itsCurrentResultInd(0),
        itsPrintResults(false)
  {
    if (!fs::exists(itsRequestFilePath))
    {
      throw runtime_error("Request file not found");
    }

    readReqFile();

    itsResults.reserve(itsRequests.size());

    // Fill the pool prior to starting
    BOOST_FOREACH (const auto& req, itsRequests)
    {
      itsThreadPool->schedule(boost::bind(&Tester::performRequest, this, req));
    }

    itsSignals.add(SIGQUIT);
    itsSignals.add(SIGALRM);
    itsSignals.async_wait(boost::bind(&Tester::handleSignal, this, _1, _2));

    itsTimer.reset(new boost::asio::deadline_timer(itsIO));

    itsThread.reset(
        new boost::thread(boost::bind(&boost::asio::io_service::run, boost::ref(itsIO))));
  }

  ~Tester()
  {
    if (itsThreadPool != nullptr)
    {
      delete itsThreadPool;
      itsThreadPool = nullptr;
    }
  }

  void run()
  {
    itsTimer->expires_from_now(bp::seconds(5));

    itsTimer->async_wait(boost::bind(&Tester::printProgress, this, _1));

    itsThreadPool->start();

    itsThreadPool->join();
  }

  void printResults()
  {
    boost::lock_guard<boost::mutex> lock(itsResultMutex);
    BOOST_FOREACH (const auto& res, itsResults)
    {
      cout << res << endl;
    }
  }

  bool getPrintResults() { return itsPrintResults; }
  void dumpRequests()
  {
    BOOST_FOREACH (const auto& req, itsRequests)
    {
      cout << req << endl;
    }
  }

  void probeRemote()
  {
    ip::tcp::resolver resolver(itsIO);

    ip::tcp::resolver::query query(itsHostName, to_string((long long unsigned int)itsPort));
    auto end_iterator = resolver.resolve(query);

    ip::tcp::socket socket(itsIO);
    boost::asio::connect(socket, end_iterator);

    string reqString = "GET / HTTP/1.0\r\n\r\n";

    // Send request
    boost::asio::write(socket, boost::asio::buffer(reqString));

    // Read at least something
    boost::array<char, 512> retArray;
    std::size_t size = 0;
    string tmp;
    string response;
    size =
        boost::asio::read(socket, boost::asio::buffer(retArray), boost::asio::transfer_at_least(8));

    // If no exceptions are thrown, the remote is succesfully contacted
  }

 private:
  const string itsHostName;

  unsigned int itsPort;

  fs::path itsRequestFilePath;

  pool* itsThreadPool;

  boost::asio::io_service itsIO;

  boost::asio::signal_set itsSignals;

  long itsCurrentResultInd;

  std::unique_ptr<boost::thread> itsThread;

  std::unique_ptr<boost::asio::deadline_timer> itsTimer;

  vector<Request> itsRequests;

  vector<Result> itsResults;

  boost::mutex itsResultMutex;

  bool itsPrintResults;

  void readReqFile()
  {
    ifstream inFile(itsRequestFilePath.string());

    string line;

    if (inFile.is_open())
    {
      while (getline(inFile, line))
      {
        boost::algorithm::replace_all(line, " ", "+");  //(At least spaces must be encoded
        itsRequests.emplace_back(itsHostName, itsPort, line);
      }

      // Shuffle the requests
      random_shuffle(itsRequests.begin(), itsRequests.end());
    }

    inFile.close();
  }

  void handleSignal(const boost::system::error_code& err, int signal_number)
  {
    itsIO.stop();
    itsThreadPool->shutdown();
    if (signal_number == SIGQUIT)  // SigQuit should print results
    {
      itsPrintResults = true;
    }
  }

  void printProgress(const boost::system::error_code& err)
  {
    if (err != boost::asio::error::operation_aborted)
    {
      std::vector<Result> theseResults;
      {
        boost::lock_guard<boost::mutex> lock(itsResultMutex);
        theseResults.assign(itsResults.begin() + itsCurrentResultInd, itsResults.end());
        itsCurrentResultInd = itsResults.size();
      }

      if (!theseResults.empty())
      {
        std::size_t reqs = theseResults.size();

        long average_duration = 0;

        BOOST_FOREACH (const auto& res, theseResults)
        {
          average_duration += res.duration.total_milliseconds();
        }

        average_duration /= reqs;

        std::cerr << bp::second_clock::local_time() << " Completed " << reqs
                  << " requests with average response time of " << average_duration << " ms."
                  << std::endl;
      }
      else
      {
        std::cerr << bp::second_clock::local_time() << " No completed requests in this time slice"
                  << std::endl;
      }

      itsTimer->expires_from_now(bp::seconds(5));
      itsTimer->async_wait(boost::bind(&Tester::printProgress, this, _1));
    }
  }

  void performRequest(Request theReq)
  {
    ip::tcp::resolver resolver(itsIO);
    ip::tcp::resolver::query query(theReq.hostName, to_string((long long unsigned int)theReq.port));
    auto end_iterator = resolver.resolve(query);

    ip::tcp::socket socket(itsIO);
    boost::asio::connect(socket, end_iterator);

    string reqString = "GET " + theReq.URI + " HTTP/1.0\r\n\r\n";

    // Send request
    auto before = bp::microsec_clock::universal_time();
    boost::system::error_code err;
    boost::asio::write(socket, boost::asio::buffer(reqString), err);

    // Read at least something
    boost::array<char, 512> retArray;
    std::size_t size = 0;
    string errorCode;
    string response;
    if (!err)
    {
      size = boost::asio::read(
          socket, boost::asio::buffer(retArray), boost::asio::transfer_at_least(8), err);
      auto parsedResult =
          parseStatus(boost::make_iterator_range(retArray.begin(), retArray.begin() + size));
      errorCode = parsedResult.first;
      response = parsedResult.second;
    }
    else
    {
      // Don't include erroneus connects
      return;
    }

    auto after = bp::microsec_clock::universal_time();
    // This request is done

    boost::lock_guard<boost::mutex> lock(itsResultMutex);
    itsResults.emplace_back(before, theReq.URI, after - before, errorCode, response);
  }
};

void testRequest(Request theReq)
{
  boost::asio::io_service itsIO;
  ip::tcp::resolver resolver(itsIO);
  ip::tcp::resolver::query query(theReq.hostName, to_string((long long unsigned int)theReq.port));
  auto end_iterator = resolver.resolve(query);

  ip::tcp::socket socket(itsIO);
  boost::asio::connect(socket, end_iterator);

  string reqString = "GET " + theReq.URI + " HTTP/1.0\r\n\r\n";

  boost::asio::write(socket, boost::asio::buffer(reqString));

  boost::array<char, 512> retArray;

  // Read until retArray is full
  auto size =
      boost::asio::read(socket, boost::asio::buffer(retArray), boost::asio::transfer_at_least(8));
  // This request is done

  std::cout << string(retArray.begin(), retArray.begin() + size) << std::endl;
}

int main(int argc, const char* argv[])
{
  unsigned int threads;
  string host;
  unsigned short port;
  string suite_file;
  unsigned int timeout;

  po::options_description desc("Allowed Options");

  desc.add_options()("help,h", "Print this help")(
      "host,H", po::value<string>(&host)->default_value("brainstormgw.fmi.fi"), "Host")(
      "port,p", po::value<unsigned short>(&port)->default_value(80), "Port")(
      "suite,s", po::value<string>(&suite_file), "File from which to read the suite")(
      "threads,t", po::value<unsigned int>(&threads)->default_value(5), "Number of worker threads")(
      "timeout,T",
      po::value<unsigned int>(&timeout)->default_value(0),
      "Run for this many seconds and exit");

  po::variables_map vmap;

  po::store(po::command_line_parser(argc, argv).options(desc).run(), vmap);

  po::notify(vmap);

  if (vmap.count("help"))
  {
    std::cout << "Usage: "
              << "tester [options]" << std::endl;
    std::cout << desc << std::endl;
    return 1;
  }

  if (vmap.count("suite") == 0)
  {
    std::cout << "Please provide the test suite file" << std::endl;
    std::cout << "Usage: "
              << "tester [options]" << std::endl;
    std::cout << desc << std::endl;

    return 1;
  }

  Tester theTester(vmap["suite"].as<string>(),
                   vmap["threads"].as<unsigned int>(),
                   vmap["host"].as<string>(),
                   vmap["port"].as<unsigned short>());

  std::cerr << "Probing the remote..." << std::endl;

  theTester.probeRemote();

  std::cerr << "Succesfully probed, running test..." << std::endl;

  if (timeout > 0)
  {
    std::cerr << "Running a maximum of " << timeout << " seconds" << std::endl;
    alarm(timeout);
  }
  theTester.run();

  if (theTester.getPrintResults())
  {
    theTester.printResults();
  }

  std::cerr << "Testing concluded" << std::endl;
}
