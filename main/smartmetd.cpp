#include "AsyncServer.h"

// #include <jemalloc/jemalloc.h>
#include <macgyver/AnsiEscapeCodes.h>
#include <spine/Exception.h>
#include <spine/HTTP.h>
#include <spine/Options.h>
#include <spine/Reactor.h>
#include <spine/SmartMet.h>
#include <sys/types.h>
#include <csignal>
#include <iostream>
#include <memory>
#include <new>

// libdw from elfutils-devel provides more details than libbfd
#define BACKWARD_HAS_DW 1
#include "backward.h"

std::unique_ptr<SmartMet::Server::Server> server;
std::unique_ptr<SmartMet::Spine::Reactor> reactor;

int last_signal = 0;

// Allowed core dump signals. Note that we choose to ignore SIGBUS though due to
// NFS problems, and SIGQUIT since we want to allow quitting via keyboard

std::vector<int> core_signals{SIGILL, SIGABRT, SIGFPE, SIGSYS, SIGSEGV, SIGXCPU, SIGXFSZ};

void signal_handler(int sig)
{
  last_signal = sig;
}
// Block all signals except profiling
void block_signals()
{
  sigset_t signal_set;
  sigfillset(&signal_set);

  // Enable debugging
  sigdelset(&signal_set, SIGPROF);

  // Enable core dumps from fatal errors
  for (auto sig : core_signals)
    sigdelset(&signal_set, sig);

  pthread_sigmask(SIG_BLOCK, &signal_set, nullptr);
}

void set_new_handler(const std::string& name)
{
  if (name == "default")  // system default may change in C++20
    ;
  else if (name == "bad_alloc")
    std::set_new_handler([] { throw std::bad_alloc(); });
  else if (name == "terminate")
    std::set_new_handler([] { std::terminate(); });
  else
    throw SmartMet::Spine::Exception(BCP, "Unknown new_handler").addParameter("name", name);
}

/* [[noreturn]] */ void run(int argc, char* argv[])
{
  try
  {
    // Parse options

    SmartMet::Spine::Options options;
    if (!options.parse(argc, argv))
      exit(1);

    // Use the system locale or autocomplete may not work properly (iconv requirement)
    std::setlocale(LC_ALL, "");

    // Set new_handler
    set_new_handler(options.new_handler);

    // Block signals before starting new threads

    block_signals();

    // Set special SIGTERM, SIGBUS and SIGWINCH handlers

    struct sigaction action;             // NOLINT(cppcoreguidelines-pro-type-member-init)
    action.sa_handler = signal_handler;  // NOLINT(cppcoreguidelines-pro-type-union-access)
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGWINCH, &action, nullptr);

    // We also want to record other common non-core dumping signals
    sigaction(SIGHUP, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    std::unique_ptr<backward::SignalHandling> sh;

    if (options.stacktrace)
      sh.reset(new backward::SignalHandling(core_signals));

    // Save heap profile if it has been enabled
    // mallctl("prof.dump", nullptr, nullptr, nullptr, 0);

    // Load engines and plugins

    reactor.reset(new SmartMet::Spine::Reactor(options));

    // Start the server

    server.reset(new SmartMet::Server::AsyncServer(options, *reactor));
    std::cout << ANSI_BG_GREEN << ANSI_BOLD_ON << ANSI_FG_WHITE << "Launched Synapse server"
              << ANSI_FG_DEFAULT << ANSI_BOLD_OFF << ANSI_BG_DEFAULT << std::endl;

    server->run();

    // When we are here, shutdown has been signaled (however, this is not the main thread).

    // Save heap profile if it has been enabled
    //  mallctl("prof.dump", nullptr, nullptr, nullptr, 0);

    //  exit(0);
  }
  catch (...)
  {
    SmartMet::Spine::Exception exception(BCP, "Operation failed!", nullptr);
    exception.printError();
    kill(getpid(), SIGKILL);  // If we use exit() we might get a core dump.
                              // exit(-1);
  }
}

int main(int argc, char* argv[])
{
  try
  {
    // Start the server thread
    boost::thread thr(boost::bind(&run, argc, argv));

    // Sleep until a signal comes, and then either handle it or exit

    while (true)
    {
      pause();

      std::cout << "\n"
                << ANSI_BG_RED << ANSI_BOLD_ON << ANSI_FG_WHITE << "Signal '"
                << strsignal(last_signal) << "' (" << last_signal << ") received ";

      if (last_signal == SIGBUS || last_signal == SIGWINCH)
      {
        std::cout << " - ignoring it!" << ANSI_FG_DEFAULT << ANSI_BOLD_OFF << ANSI_BG_DEFAULT
                  << std::endl;
        last_signal = 0;
      }
      else if (last_signal == SIGTERM)
      {
        std::cout << " - shutting down!" << ANSI_FG_DEFAULT << ANSI_BOLD_OFF << ANSI_BG_DEFAULT
                  << std::endl;

        if (server != nullptr)
          server->shutdownServer();

        // Save heap profile if it has been enabled
        // mallctl("prof.dump", nullptr, nullptr, nullptr, 0);
        return 0;
      }
      else
      {
        std::cout << " - exiting!" << ANSI_FG_DEFAULT << ANSI_BOLD_OFF << ANSI_BG_DEFAULT
                  << std::endl;
        break;
      }
    }

    return 666;
  }
  catch (...)
  {
    SmartMet::Spine::Exception exception(BCP, "Operation failed!", nullptr);
    exception.printError();

    return -1;
  }
}
