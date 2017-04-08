#include "AsyncServer.h"

// libdw from elfutils-devel provides more details than libbfd
#define BACKWARD_HAS_DW 1
#include "backward.h"

#include <spine/Exception.h>
#include <spine/HTTP.h>
#include <spine/Options.h>
#include <spine/Reactor.h>
#include <spine/SmartMet.h>

#include <macgyver/AnsiEscapeCodes.h>

#include <jemalloc/jemalloc.h>

#include <sys/types.h>
#include <iostream>
#include <signal.h>

SmartMet::Server::Server* theServer = NULL;
SmartMet::Spine::Reactor* theReactor = NULL;

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

  pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
}

/* [[noreturn]] */ void run(int argc, char* argv[])
{
  try
  {
    using namespace SmartMet;

    // Parse options

    SmartMet::Spine::Options options;
    if (!options.parse(argc, argv))
      exit(1);

    // Block signals before starting new threads

    block_signals();

    // Set special SIGTERM, SIGBUS and SIGWINCH handlers

    struct sigaction action;
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGWINCH, &action, NULL);

    // We also want to record other common non-core dumping signals
    sigaction(SIGHUP, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    backward::SignalHandling sh(core_signals);

    // Save heap profile if it has been enabled
    mallctl("prof.dump", NULL, NULL, NULL, 0);

    // Load engines and plugins

    theReactor = new SmartMet::Spine::Reactor(options);

    // Start the server

    theServer = new Server::AsyncServer(options, *theReactor);
    std::cout << ANSI_BG_GREEN << ANSI_BOLD_ON << ANSI_FG_WHITE << "Launched Synapse server"
              << ANSI_FG_DEFAULT << ANSI_BOLD_OFF << ANSI_BG_DEFAULT << std::endl;

    theServer->run();

    // When we are here, shutdown has been signaled (however, this is not the main thread).

    // delete theServer;

    // Save heap profile if it has been enabled
    //  mallctl("prof.dump", NULL, NULL, NULL, 0);

    //  exit(0);
  }
  catch (...)
  {
    SmartMet::Spine::Exception exception(BCP, "Operation failed!", NULL);
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
        if (theServer)
        {
          theServer->shutdownServer();
          delete theServer;
        }

        // We cannot delete the reactor before the server is deleted. That's because
        // there are some methods in the server that try to call the reactor during
        // the delete operation.

        if (theReactor)
        {
          delete theReactor;
        }
        // Save heap profile if it has been enabled
        mallctl("prof.dump", NULL, NULL, NULL, 0);
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
    SmartMet::Spine::Exception exception(BCP, "Operation failed!", NULL);
    exception.printError();

    return -1;
  }
}
