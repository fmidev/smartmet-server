#!/bin/bash

waittime=60  # What is the timeout limit

cd `dirname $0`

smartmetd=../smartmetd
if [ "$1" ] ; then smartmetd=$1 ; fi
if [ ! -x "$smartmetd" ] ; then
    echo "Smartmet server at '$smartmetd' is not found or not executable"
    exit 127
fi

function waitloop {
    echo "Waiting for server to start ..."
    while ! grep -q "Launched.*server" test.out ; do
	   # Server not fully started yet
    	sleep 1s
    	# If server stopped(timeout or crash), return
    	if [ ! -d /proc/$childpid ]; then
    	   wait
    	   ret=$?
    	   if [ "$ret" != "0" ] ; then return $ret ; fi
    	   echo "Failure to detect subprocess existence"
    	   return 1
    	fi
    done
    echo "Server started, shutting it down ..."
    # Detected started string, wait a bit
    sleep 1s
    curl -i -X OPTIONS --request-target '*' --max-time 1 --verbose http://127.0.0.1:8080 >options.out 2>&1
    # Two requests in one curl invocation: a persistent HTTP/1.1 connection must
    # serve both without curl having to open a second TCP connection
    curl -s --max-time 5 --verbose http://127.0.0.1:8080/one http://127.0.0.1:8080/two >keepalive11.out 2>&1
    # HTTP/1.0 keep-alive is an extension the client has to ask for and the server
    # has to confirm with a Connection header of its own
    curl -si --http1.0 -H 'Connection: keep-alive' --max-time 5 http://127.0.0.1:8080/ka >keepalive10.out 2>&1
    # ... whereas a plain HTTP/1.0 request must be answered with a close
    curl -si --http1.0 --max-time 5 http://127.0.0.1:8080/noka >nokeepalive10.out 2>&1
    sleep 1s
    # Kill server
    kill $childpid
    echo "Waiting for server to stop ..."
    wait
    return $? # Normal return,  
}

touch test.out # Avoid missing file warnings when grepping
timeout $waittime "$smartmetd" -d -v -c ./minimal.conf >test.out 2>&1 &
childpid=$!

waitloop
ret=$?

if [ "$ret" != "0" ] ; then echo "Abnormal end." ; fi

# The rest should fail if any of the steps fail
set -e
echo ; echo "Server exited with status $ret, output:"
cat test.out
echo
echo "Checking for output correctness"
grep "Launched.*server" test.out
grep "SmartMet.*stopping" test.out

# Check OPTIONS request result. The reply is sent in the client's own HTTP version,
# and curl speaks HTTP/1.1 by default.
grep '^HTTP/1.1 204 ' options.out || exit 1

# HTTP/1.1 is persistent by default: two requests, one TCP connection. curl reports
# every connection attempt with a "Trying <host>:<port>" line.
echo "Checking HTTP/1.1 keep-alive"
test "$(grep -c 'Trying 127.0.0.1:8080' keepalive11.out)" = "1" || exit 1
test "$(grep -c '^< HTTP/1.1 ' keepalive11.out)" = "2" || exit 1
# ... and persistence being the default, it must not be announced with a Connection header
if grep -q '^< Connection:' keepalive11.out ; then
    echo "Unexpected Connection header on a persistent HTTP/1.1 response"
    exit 1
fi

# HTTP/1.0 keep-alive must be explicitly confirmed by the server
echo "Checking HTTP/1.0 keep-alive"
grep -q '^HTTP/1.0 ' keepalive10.out || exit 1
grep -qi '^Connection: keep-alive' keepalive10.out || exit 1
grep -qi '^Keep-Alive: timeout=' keepalive10.out || exit 1

# HTTP/1.0 without the extension is non-persistent
echo "Checking HTTP/1.0 default close"
grep -qi '^Connection: close' nokeepalive10.out || exit 1

rm -f test.out options.out keepalive11.out keepalive10.out nokeepalive10.out
echo
# Normal ecit/shutdown
exit $ret


