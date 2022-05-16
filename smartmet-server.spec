%define DIRNAME server
%define SPECNAME smartmet-%{DIRNAME}
Summary: SmartMet HTTP server
Name: %{SPECNAME}
Version: 22.4.28
Release: 1%{?dist}.fmi
License: MIT
Group: System Environment/Daemons
URL: https://github.com/fmidev/smartmet-server
Source0: smartmet-server.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: rpm-build
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: boost169-devel
BuildRequires: elfutils-devel
BuildRequires: fmt-devel >= 7.1.3
BuildRequires: openssl-devel
BuildRequires: jemalloc-devel
BuildRequires: systemd
BuildRequires: smartmet-library-macgyver-devel >= 21.10.4
BuildRequires: smartmet-library-spine-devel >= 22.5.16
Requires: boost169-date-time
Requires: boost169-filesystem
Requires: boost169-iostreams
Requires: boost169-program-options
Requires: boost169-regex
Requires: boost169-system
Requires: boost169-thread
Requires: fmt >= 7.1.3
Requires: glibc
Requires: jemalloc
Requires: openssl-libs
Requires: smartmet-library-macgyver >= 21.10.4
Requires: smartmet-library-spine >= 22.5.16
Provides: smartmetd
Obsoletes: smartmet-brainstorm-server < 16.11.1
Obsoletes: smartmet-brainstorm-server-debuginfo < 16.11.1
#TestRequires: /bin/bash
#TestRequires: gcc-c++
#TestRequires: make
#TestRequires: smartmet-library-macgyver-devel >= 20.10.7

Summary: SmartMet server
%description
SmartMet server

%prep

%setup -q -n %{SPECNAME}

%build
make %{_smp_mflags}

%install
%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(0755,root,root,0755)
%{_sbindir}/smartmetd
%defattr(0644,root,root,0755)
%config(noreplace) %{_sysconfdir}/logrotate.d/smartmet-server
%config(noreplace) %{_sysconfdir}/smartmet/smartmetd.env
%{_unitdir}/smartmet-server.service
%{_sysconfdir}/smartmet

%post
mkdir -p /var/log/smartmet

if [ $1 -eq 1 ]; then
   systemctl daemon-reload
   systemctl enable smartmet-server
fi


%preun
if [ $1 -eq 0 ]; then
   systemctl stop smartmet-server
   systemctl disable smartmet-server
fi


%changelog
* Thu Apr 28 2022 Andris Pavenis <andris.pavenis@fmi.fi> 22.4.28-1.fmi
- Repackage due to SmartMet::Spine::Reactor ABI changes

* Thu Nov 25 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.11.25-1.fmi
- Removed unnecessary empty lines from output when logrequests=true

* Tue Sep  7 2021 Andris Pavēnis <andris.pavenis@fmi.fi> 21.9.7-1.fmi
- Rebuild due to dependence changes

* Fri Jul  9 2021 Andris Pavēnis <andris.pavenis@fmi.fi> 21.7.9-1.fmi
- Try to stop gracefully on SIGINT

* Thu Jun  3 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.6.3-1.fmi
- Repackaged with jemalloc 5.2

* Thu Jan 14 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.14-1.fmi
- Repackaged smartmet to resolve debuginfo issues

* Tue Jan  5 2021 Mika Heiskanen <mika.heiskanen@fmi.fi> - 21.1.5-1.fmi
- Upgraded fmt dependency

* Wed Oct 28 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.10.28-1.fmi
- Rebuild due to fmt upgrade

* Wed Oct 21 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.10.21-1.fmi
- Rebuild due to part of changes missing in earlier 20.10.20-1.fmi

* Tue Oct 20 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.10.20-1.fmi
- Rebuild due to libconfig upgrade to version 1.7.2

* Mon Oct 12 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.12-1.fmi
- Use lambdas instead of boost::bind to avoid memory leaks
- Silenced some clang analyzer warnings

* Thu Oct  8 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.10.8-1.fmi
- Start handling requests while engine and plugin initioalization is still ongoing
- Build update: use makefile.inc from smartmet-library-macgyver

* Wed Oct  7 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.10.7-1.fmi
- Repackaged since Options ABI changed

* Wed Sep 23 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.23-1.fmi
- Use Fmi::Exception instead of Spine::Exception

* Mon Sep 14 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.9.14-1.fmi
- New option for running a script when active requests limit is broken

* Wed Sep  9 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.9.9-1.fmi
- Ensure reactor shutdown when SIGTERM received before creating AsyncServer object

* Tue Aug 25 2020 Andris Pavenis <andris.pavenis@fmi.fi> - 20.8.25-1.fmi
- Adapt to SmartMet::Spine::Reactor changes (separate init method)

* Fri Aug 21 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.21-1.fmi
- Upgrade to fmt 6.2

* Mon Aug 10 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.8.10-1.fmi
- Repackaged since Spine::Options changed
- Allow /admin requests during high load

* Fri Jul 31 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.7.31-1.fmi
- Repackaged due to libpqxx upgrade

* Sat Apr 18 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.4.18-1.fmi
- Upgraded to Boost 1.69

* Thu Feb 13 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.2.13-2.fmi
- Fixed dependency to be on jemalloc instead of jemalloc-debug

* Thu Feb 13 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.2.13-1.fmi
- Rebuilt since Reactor object size changed

* Wed Jan 15 2020 Mika Heiskanen <mika.heiskanen@fmi.fi> - 20.1.15-1.fmi
- New command line options for throttling

* Tue Oct  1 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.10.1-1.fmi
- Added option --stacktrace

* Thu Sep 26 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.9.26-1.fmi
- Added support for ASAN and TSAN builds

* Tue Sep 17 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.9.17-1.fmi
- Repackaged since Reactor object size changed

* Fri Aug  9 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.8.9-1.fmi
- Use the system locale globally in the server for proper character conversions

* Thu Jun 20 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.6.20-1.fmi
- Added smartmetd.env

* Tue Mar 19 2019 Mika Heiskanen <mika.heiskanen@fmi.fi> - 19.3.19-1.fmi
- Improved error handling if socket is already in use

* Fri Dec 14 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.12.14-1.fmi
- Start server after /smartmet/data has been mounted - if it is present

* Thu Nov  8 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.11.8-1.fmi
- Let admin requests pass despite a high load

* Mon Nov  5 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.11.5-1.fmi
- Do not wait for queues if unable to schedule a request, return 503 instead
- Added shutdown calls for sockets about to be closed to avoid WAIT-states
- Fixed debuginfo package to include symbols
- Improved shutdown sequence to prevent segmentation faults

* Sat Sep 29 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.9.29-1.fmi
- Upgraded to newer fmt

* Wed Sep 12 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.9.12-1.fmi
- Added new_handler for OOM situations

* Tue Sep 11 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.9.11-1.fmi
- Silenced CodeChecker warnings

* Wed Aug 22 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.22-1.fmi
- Fixed systemd file not to be a configuration file

* Wed Aug  8 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.8-1.fmi
- Removed several CodeChecker warnings

* Wed Aug  1 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.8.1-1.fmi
- Print stack traces for async connection errors

* Mon Jul 30 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.7.30-2.fmi
- Fixed the build system to detect modified files

* Mon Jul 30 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.7.30-1.fmi
- Silenced CodeChecker warning by using std::move

* Wed Jul 25 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.7.25-2.fmi
- Return high load HTTP response code 1234 if backend request queue is full

* Wed Jul 25 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.7.25-1.fmi
- Prefer nullptr over NULL

* Wed Jun  6 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.6.6-1.fmi
- Removed incorrect dependencies on devel-packages

* Tue May 15 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.5.15-1.fmi
- Added option --maxrequestsize

* Mon May 14 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.5.14-1.fmi
- Added handling for high load situations

* Wed May  9 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.4.9-1.fmi
- Repackaged since Reactor ABI changed

* Sat Apr  7 2018 Mika Heiskanen <mika.heiskanen@fmi.fi> - 18.4.7-1.fmi
- Upgrade to boost 1.66

* Mon Aug 28 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.8.28-1.fmi
- Upgrade to boost 1.65

* Sat Apr  8 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.4.8-1.fmi
- Simplified error handling

* Fri Apr  7 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.4.7-1.fmi
- Improved signal handling

* Wed Mar 15 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.3.15-1.fmi
- Recompiled since Spine::Exception changed

* Tue Mar 14 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.3.14-1.fmi
- Changed to use macgyver StringConversion.h

* Wed Jan 25 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.25-1.fmi
- Fixed docker vs smartmet-server startup sequence

* Wed Jan 18 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.18-1.fmi
- Upgrade from cppformat-library to fmt

* Wed Jan  4 2017 Mika Heiskanen <mika.heiskanen@fmi.fi> - 17.1.4-1.fmi
- Updated to using renamed newbase and macgyver libraries

* Mon Dec 19 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.12.19-1.fmi
- Removed cache expiration headers added for load testing purposes

* Wed Nov 30 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.11.30-1.fmi
- Removed installation of smartmet.conf

* Thu Nov  3 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.11.1-2.fmi
- Renamed cross-section to cross_section

* Tue Nov  1 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.11.1-1.fmi
- Namespace changed. Binary name changed: brainstorm -> smartmetd

* Fri Sep 30 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.9.30-1.fmi
- Recompiled with a ThreadPool from macgyver which now catches also Brainstorm::Exception

* Wed Sep 28 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.9.28-1.fmi
- Do not let exceptions lead to std::terminate

* Tue Sep  6 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.9.6-1.fmi
- Added a new exeception handling mechanism. The idea is that the methods
- cannot let any exception passing through without a control. So, the methods
- must catch all exception and most of the cases they just throw their own
- "BrainStorm::Exception" instead. In this way get an exception hierarchy that works 
- like a stack trace. Notice that it is very easy to add additional details and
- parameters related to the exception when using BrainStorm::Exception class.
- Ignore window resize signals
- Closing the server socket after the reactor shutdown.

* Tue Aug 30 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.8.23-1.fmi
- Added stack trace generation when core is dumped

* Mon Aug 15 2016 Markku Koskela <mika.heiskanen@fmi.fi> - 16.8.15-1.fmi
- Added the server pointer into the Connection class. Connection obects
- can use this pointer for checking if the shutdown is requested.
- The server shutdown() method was modified
- Deletion of the Server object and the Reactor object were moved, because they
- were deleted too early.

* Tue Jun 28 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.28-1.fmi
- Rewrote the signal handler
- Catch bus errors

* Tue Jun 14 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.14-1.fmi
- Full recompile release

* Mon Jun  6 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.2-1.fmi
- Store logs into /var/log/smartmet instead of /smartmet/logs/brainstorm

* Thu Jun  2 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.2-1.fmi
- Full recompile

* Wed Jun  1 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.6.1-1.fmi
- Added graceful shutdown
- Added jemalloc profiling

* Tue May 17 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.5.17-1.fmi
- Removed docker-dependency. It does not apply to frontend servers.

* Mon May  9 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.5.9-1.fmi
- Disabled HTTP caching headers for testing purposes

* Wed May  4 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.5.4-1.fmi
- Added docker as systemd-dependency and fixed date-header

* Wed Apr 20 2016 Tuomo Lauri <tuomo.lauri@fmi.fi> - 16.4.20-1.fmi
- Added lazy linking parameter

* Mon Jan 18 2016 Mika Heiskanen <mika.heiskanen@fmi.fi> - 16.1.18-1.fmi
- newbase API changed, full recompile

* Wed Nov 18 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.11.18-1.fmi
- Recompiled just in case, Spine HTTP API changed

* Tue Nov 10 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.11.10-1.fmi
- Avoid string streams to avoid global std::locale locks

* Tue Aug 25 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.25-1.fmi
- Use unique_ptr instead of shared_ptr when possible for speed

* Tue Aug 18 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.18-1.fmi
- Recompile forced by HTTP API changes

* Mon Aug 17 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.8.17-1.fmi
- Use -fno-omit-frame-pointer to improve perf use

* Tue Apr 21 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.4.21-1.fmi
- Fixed problems with logrotation

* Thu Apr  9 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.9-2.fmi
- Fixed brainstorm to start only once mounts are final (in multi-user mode)

* Thu Apr  9 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.9-1.fmi
- newbase API changed

* Wed Apr  8 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.4.8-1.fmi
- Dynamic linking of smartmet libraries

* Tue Mar 10 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.3.10-1.fmi
- Rebuild agains the new spine

* Mon Feb 16 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.2.16-1.fmi
- Added LimitCORE directive to brainstorm.service to enable coredumps

* Wed Jan 28 2015 Tuomo Lauri <tuomo.lauri@fmi.fi> - 15.1.28-1.fmi
- Migrated from init to systemd

* Wed Jan  7 2015 Mika Heiskanen <mika.heiskanen@fmi.fi> - 15.1.7-1.fmi
- Recompiled

* Thu Dec 18 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.12.18-1.fmi
- Recompiled due to spine API changes

* Tue Oct  7 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.10.7-1.fmi
- Failed bind of listening socket no longer causes uncaught exception

* Mon Jun 30 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.6.30-1.fmi
- Recompiled due to spine API changes

* Wed May 14 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.14-1.fmi
- Use shared macgyver and locus libraries

* Tue May  6 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.7-1.fmi
- Timeparser hotfix

* Fri May  2 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.5.2-1.fmi
- Removed /smartmet/logs/brainstorm/access_logs, replaced by a post-section mkdir

* Mon Apr 28 2014 Mika Heiskanen <mika.heiskanen@fmi.fi> - 14.4.28-1.fmi
- Full recompile of all packages due to large API changes

* Wed Apr 16 2014 Tuomo Lauri <tuomo.lauri@fmi.fi> - 14.4.16-1.fmi
- Recompile due to the new logging feature

* Wed Mar 19 2014 Mikko Visa <mikko.visa@fmi.fi> - 14.3.19-1.fmi
- Now returning 404 Not Found status if handler is not found

* Mon Jan 13 2014 Santeri Oksman <santeri.oksman@fmi.fi> - 14.1.13-1.fmi
- Now sending Brainstorm headers also when request fails
- Updated to use the simplified HTTP API

* Tue Nov  5 2013 Tuomo Lauri <tuomo.lauri@fmi.fi> - 13.11.5-5.fmi
- Removed client connection status sniffing due to leaking sockets

* Tue Nov  5 2013 Tuomo Lauri <tuomo.lauri@fmi.fi> - 13.11.5-4.fmi
- Now reporting which client disconnected prematurely only in the backend case

* Tue Nov  5 2013 Tuomo Lauri <tuomo.lauri@fmi.fi> - 13.11.5-3.fmi
- Now reporting which client disconnected prematurely

* Tue Nov  5 2013 Tuomo Lauri <tuomo.lauri@fmi.fi> - 13.11.5-2.fmi
- Hotfixed segfault issue when handling frontend requests

* Tue Nov  5 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.11.5-1.fmi
- Server no longer calls the plugin handler if client has disconnected while in queue
- Added process status dumping script for debugging purposes
- Added colour to the lauch message to spot it more easily

* Wed Oct 9 2013 Tuomo Lauri     <tuomo.lauri@fmi.fi>    - 13.10.9-1.fmi
- Rebuilt against the new Spine
- Removed threaded server type as unused
- Built against the new Spine

* Wed Sep 18 2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.9.18-1.fmi
- Streamer content calls can now be rescheduled for later if needed

* Mon Sep  9 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.9.9-1.fmi
- Rewrote signal handling to properly block threads from handling signals

* Thu Aug 15 2013 Tuomo Lauri    <tuomo.lauri@fmi.fi>    - 13.8.15-1.fmi
- Added callback messaging with Reactor to Connection classes

* Thu Jul 25 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.7.25-1.fmi
- Linked with jemalloc instead of tcmalloc, tests show it is more efficient

* Wed Jul 24 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.7.24-1.fmi
- Linked with tcmalloc to improve efficiency

* Tue Jul 23 2013 Mika Heiskanen <mika.heiskanen@fmi.fi> - 13.7.23-1.fmi
- Recompiled due to thread safety fixes in newbase & macgyver

* Wed Jul  3 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.7.3-1.fmi
- Update to boost 1.54
- Fixed option parsing

* Mon Jun 17 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.6.17-1.fmi
- Fixed broken --logrequests - option

* Fri Jun  7 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.6.7-1.fmi
- More verbose output in case of abnormal connection terminations

* Wed May 22 2013 tervo    <roope.tervo@fmi.fi>    - 13.5.22-1.fmi
- More gracefull shutdown
- Better error reporting
- Now reporting explicitly when server queue is full

* Wed May 15 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.5.15-1.fmi
- Modifications to conform with the new Spine

* Tue Apr 30 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.4.30-1.fmi
- Slightly better error reporting when something is wrong with the client socket

* Wed Apr 24 2013 lauri    <tuomo.lauri@fmi.fi>    - 13.4.24-1.fmi
- Fixed bug when obtaining IP address from an already closed remote endpoint

* Mon Apr 22 2013 mheiskan <mika.heiskanen@fmi.fi> - 13.4.22-1.fmi
- New Brainstorm API release

* Wed Apr 17 2013 tervo <roope.tervo@fmi.fi>    - 13.4.17-1.fmi
- Added ulimit -n 999999 to brainstormloop

* Tue Apr 16 2013 lauri <tuomo.lauri@fmi.fi>    - 13.4.16-1.fmi
- X-Forwarded-For - header now affects the reported client ip

* Fri Apr 12 2013 lauri <tuomo.lauri@fmi.fi>    - 13.4.12-1.fmi
- Built against the new Spine release

* Wed Apr  3 2013 lauri   <tuomo.lauri@fmi.fi>    - 13.4.3-1.fmi
- Changed socket thread insertion sleep from 100 to 10 ms
- Increased number of socket threads to 6

* Wed Mar 20 2013 lauri   <tuomo.lauri@fmi.fi>    - 13.3.20-1.fmi
- Removed incoming IP reporting from server

* Wed Mar  6 2013 lauri   <tuomo.lauri@fmi.fi>     - 13.3.6-1.fmi
- Fixed bug in Date-header string

* Mon Mar  4 2013 lauri   <tuomo.lauri@fmi.fi>     - 13.3.4-1.fmi
- Server now automatically sets Date-header for all responses

* Thu Feb 28 2013 lauri   <tuomo.lauri@fmi.fi>     - 13.2.28-1.fmi
- Added LC_CTYPE environment variable to brainstormloop

* Fri Feb 15 2013 lauri   <tuomo.lauri@fmi.fi>     - 13.2.15-1.fmi
- Fixed EOF-related bug in AsyncConnection
- Removed strand from AsyncConnection as unnecessary

* Wed Feb 6  2013 lauri   <tuomo.lauri@fmi.fi>     - 13.2.6-1.fmi
- Switch to Synapse HTTP Server

* Wed Nov  7 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.11.7-1.fmi
- Upgrade to boost 1.52
- Refactored common parts into spine library
- Using dynamic linking for brainstorm-spine library

* Fri Oct 19 2012 lauri    <tuomo.lauri@fmi.fi>    - 12.10.19-2.el6.fmi
- Added startup logging status config option. Defaults to true

* Fri Oct 19 2012 lauri    <tuomo.lauri@fmi.fi>    - 12.10.19-1.el6.fmi
- Added logging status reporting to ContentEngine

* Fri Oct 12 2012 lauri    <tuomo.lauri@fmi.fi>    - 12.10.12-1.el6.fmi
- Added last minute request tracking to ContentEngine

* Mon Sep 24 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.9.24-1.el6.fmi
- Always add a Vary header with value Accept-Encoding

* Wed Aug 15 2012 lauri    <tuomo.lauri@fmi.fi>    - 12.8.15-1.el6.fmi
- Added optional request logging functionality to ContentEngine

* Mon Jul 23 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.23-1.el6.fmi
- Added ApparentTemperature to common

* Mon Jul  9 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.9-3.el6.fmi
- Improved diagnostics

* Mon Jul  9 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.9-2.el6.fmi
- Fixed Reactor to be a global which is initialized only once

* Mon Jul  9 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.9-1.el6.fmi
- Fixed ContentEngine to use a mutex

* Thu Jul  5 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.7.5-1.el6.fmi
- Migration to boost 1.50

* Tue Jun 26 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.6.26-1.el6.fmi
- Recompiled with latest brainstorm-common

* Tue Jun 19 2012 lauri    <tuomo.lauri@fmi.fi>    - 12.6.19-1.el6.fmi
- New API where Reactor is explicitly given to plugins
- Removed Dataengine as unused
- Added virtual handler function to BrainStormPlugin

* Wed Jun  6 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.6.6-1.el6.fmi
- Added creation of static devel library for regression tests

* Wed Apr  4 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.4.4-1.el6.fmi
- common lib changed

* Mon Apr  2 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.4.2-1.el5.fmi
- macgyver change forced recompile

* Sat Mar 31 2012 mheiskan <mika.heiskanen@fmi.fi> - 12.3.31-1.el5.fmi
- Upgraded to boost 1.49
- Upgraded to newbase without NFmiRawData locking

* Thu Dec 29 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.12.29-1.el5.fmi
- Added support for gzip compression

* Mon Dec 12 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.12.21-1.el6.fmi
- New release since common changed significantly

* Tue Aug 16 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.8.16-1.el5.fmi
- Upgrade to boost 1.47

* Tue Apr 26 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.4.26-1.el5.fmi
- Added ulimit -n 2048 to the init.d script (doubles the default value)

* Fri Mar 25 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.3.25-2.el5.fmi
- Fixed daemon startup
- Fixed shutdown to use -HUP signal

* Thu Mar 24 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.3.24-1.el5.fmi
- Upgrade to boost 1.46

* Mon Feb 28 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.2.28-1.el5.fmi
- Improved logging features

* Mon Feb 14 2011 mheiskan <mika.heiskanen@fmi.fi> - 11.2.14-1.el5.fmi
- Added mysql_library_init call for thread safety

* Thu Oct 28 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.10.28-1.el5.fmi
- Added autocomplete regression tests

* Tue Sep 14 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.9.14-1.el5.fmi
- Upgrade to boost 1.44

* Mon Jul  5 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.7.5-1.el5.fmi
- Removed newbase linkage as unnecessary

* Tue May 11 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.5.11-2.el5.fmi
- tapsa contained old boost 1.33 and 1.36 which messed up dependencies, fixed to use 1.41

* Tue May 11 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.5.11-1.el5.fmi
- Release for RHEL 5.5

* Thu Apr 22 2010 westerba <antti.westerberg@fmi.fi> - 10.4.22-1.el5.fmi
- Bug fixes

* Thu Apr  8 2010 westerba <antti.westerberg@fmi.fi> - 10.4.8-1.el5.fmi
- New main loop now starts the server in a separate thread and goes into loop

* Tue Mar 23 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.3.23-1.el5.fmi
- Updates to signal handling

* Fri Feb 12 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.2.12-1.el5.fmi
- Log file will show the host name

* Fri Jan 15 2010 mheiskan <mika.heiskanen@fmi.fi> - 10.1.15-1.el5.fmi
- Upgrade to boost 1.41

* Fri Jan  1 2010 westerba <antti.westerberg@fmi.fi> - 10.1.7-1.el5.fmi
- setuid/setgid implemented. Configuration and command line options parsing.

* Tue Aug 11 2009 mheiskan <mika.heiskanen@fmi.fi> - 9.8.11-2.el5.fmi
- SIGPIPE no longer crashes the program

* Tue Aug 11 2009 mheiskan <mika.heiskanen@fmi.fi> - 9.8.11-1.el5.fmi
- Added signal handling

* Tue Jul 14 2009 mheiskan <mika.heiskanen@fmi.fi> - 9.7.14-1.el5.fmi
- Upgrade to boost 1.39

* Thu Jun 11 2009 westerba <antti.westerberg@fmi.fi> - 9.6.11-1.el5.fmi
- Build against new spserver

* Wed May 27 2009 westerba <antti.westerberg@fmi.fi> - 9.5.27-1.el5.fmi
- Full Brainstorm recompile release  

* Wed Mar 25 2009 mheiskan <mika.heiskanen@fmi.fi> - 9.3.25-1.el5.fmi
- Full Brainstorm recompile release

* Fri Dec 19 2008 mheiskan <mika.heiskanen@fmi.fi> - 8.12.19-1.el5.fmi
- common library update forced recompile

* Fri Dec 12 2008 mheiskan <mika.heiskanen@fmi.fi> - 8.12.12-1.el5.fmi
- Prevent server from hanging for invalid URIs

* Wed Nov 19 2008 westerba <antti.westerberg@fmi.fi> - 8.10.1-1.el5.fmi
- Using new spserver and new API interface. Not backwards compatible.

* Wed Oct 01 2008 westerba <antti.westerberg@fmi.fi> - 8.10.1-1.el5.fmi
- Removed library from the build. Nothing should link against the server.

* Thu Jul 31 2008 westerba <antti.westerberg@fmi.fi> - 8.7.31-1.el5.fmi
- Single-binary platform merge known as BrainStorm core

* Wed Jun 11 2008 westerba <antti.westerberg@fmi.fi> - 8.6.11-1.el5.fmi
- Numerous new improvements: frontend, backend and develop packages

* Mon Apr 14 2008 mheiskan <mika.heiskanen@fmi.fi> - 8.4.14-1.el5.fmi
- Numerous improvements

* Fri Nov 16 2007 mheiskan <mika.heiskanen@fmi.fi> - 1.0.1-1.el5.fmi
- Initial build
