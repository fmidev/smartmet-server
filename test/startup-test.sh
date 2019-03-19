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
    sleep 2s
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

rm -f test.out
echo
# Normal ecit/shutdown
exit $ret


