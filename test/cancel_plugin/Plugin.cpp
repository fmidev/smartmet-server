// A plugin for testing the cancellation of requests (cancellation-test.sh)
//
//   /cancel/busy    computes without boost interruption points, checks Fmi::check_cancelled()
//   /cancel/block   waits in a call which only a Fmi::CancellationCallback breaks off, like a
//                   database query
//   /cancel/stream  streams a chunk every 100 ms, checking Fmi::check_cancelled()
//
// Each runs for 60 seconds, or for the number of seconds given by the 'seconds' option,
// unless cancelled.

#include <macgyver/Cancellation.h>
#include <macgyver/Exception.h>
#include <spine/HTTP.h>
#include <spine/Reactor.h>
#include <spine/SmartMetPlugin.h>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace
{
class Streamer : public SmartMet::Spine::HTTP::ContentStreamer
{
 public:
  explicit Streamer(std::chrono::steady_clock::time_point theEnd) : itsEnd(theEnd) {}

  std::string getChunk() override
  {
    if (std::chrono::steady_clock::now() >= itsEnd)
    {
      setStatus(StreamerStatus::EXIT_OK);
      return "";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Fmi::check_cancelled();
    return "chunk\n";
  }

 private:
  std::chrono::steady_clock::time_point itsEnd;
};

class Plugin : public SmartMetPlugin
{
 public:
  Plugin(SmartMet::Spine::Reactor* theReactor, const char* /* theConfig */) : itsReactor(theReactor)
  {
  }

  const std::string& getPluginName() const override
  {
    static const std::string name = "cancel";
    return name;
  }

  int getRequiredAPIVersion() const override { return SMARTMET_API_VERSION; }

  bool queryIsFast(const SmartMet::Spine::HTTP::Request& /* theRequest */) const override
  {
    return false;
  }

 protected:
  void init() override
  {
    if (!itsReactor->addContentHandler(
            this,
            SmartMet::Spine::Reactor::HandlerOptions("/cancel").IsPrefix(true),
            [this](SmartMet::Spine::Reactor& theReactor,
                   const SmartMet::Spine::HTTP::Request& theRequest,
                   SmartMet::Spine::HTTP::Response& theResponse)
            { requestHandler(theReactor, theRequest, theResponse); }))
      throw Fmi::Exception(BCP, "Failed to register the cancel content handler");
  }

  void shutdown() override {}

  void requestHandler(SmartMet::Spine::Reactor& /* theReactor */,
                      const SmartMet::Spine::HTTP::Request& theRequest,
                      SmartMet::Spine::HTTP::Response& theResponse) override
  {
    const auto resource = theRequest.getResource();
    const auto seconds = theRequest.getParameter("seconds");
    const auto end =
        std::chrono::steady_clock::now() + std::chrono::seconds(seconds ? std::stoi(*seconds) : 60);

    if (resource == "/cancel/busy")
    {
      volatile double sum = 0;
      for (unsigned long i = 0; std::chrono::steady_clock::now() < end; i++)
      {
        sum = sum + 1.0 / static_cast<double>(i + 1);
        if ((i & 1023) == 0)
          Fmi::check_cancelled();
      }
    }
    else if (resource == "/cancel/block")
    {
      std::mutex mutex;
      std::condition_variable cond;
      bool broken_off = false;
      Fmi::CancellationCallback callback(
          [&]()
          {
            std::lock_guard<std::mutex> lock(mutex);
            broken_off = true;
            cond.notify_one();
          });
      Fmi::check_cancelled();
      std::unique_lock<std::mutex> lock(mutex);
      cond.wait_until(lock, end, [&]() { return broken_off; });
      lock.unlock();
      Fmi::check_cancelled();
    }
    else if (resource == "/cancel/stream")
    {
      theResponse.setStatus(SmartMet::Spine::HTTP::Status::ok);
      theResponse.setHeader("Content-Type", "text/plain");
      theResponse.setContent(std::make_shared<Streamer>(end));
      return;
    }
    else
    {
      theResponse.setStatus(SmartMet::Spine::HTTP::Status::not_found);
      return;
    }

    theResponse.setStatus(SmartMet::Spine::HTTP::Status::ok);
    theResponse.setContent("completed\n");
  }

 private:
  SmartMet::Spine::Reactor* itsReactor;
};

}  // namespace

extern "C" SmartMetPlugin* create(SmartMet::Spine::Reactor* theReactor, const char* theConfig)
{
  return new Plugin(theReactor, theConfig);
}

extern "C" void destroy(SmartMetPlugin* thePlugin)
{
  delete thePlugin;
}
