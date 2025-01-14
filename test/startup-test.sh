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
    sleep 1s
    # Kill server
    kill $childpid
    echo "Waiting for server to stop ..."
    wait
    return $? # Normal return,  
}

touch test.out # Avoid missing file warnings when grepping
mkdir -p log
timeout $waittime "$smartmetd" -d -v -c ./minimal.conf -a $(pwd)/log >test.out 2>&1 &
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

# Check OPTIONS request result
grep '^HTTP/1.0 204 ' options.out || exit 1

rm -f test.out options.out
echo
# Normal ecit/shutdown
exit $ret


