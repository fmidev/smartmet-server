#!/bin/bash

waittime=60  # What is the timeout limit

cd `dirname $0`

function waitloop {
    echo "Waiting for server to start ..."
    while ! grep -q "Launched.*server" test.out ; do
	# Server not fully started yet
	sleep 1s
	# If server stopped(timeout or crash), return
	if [ -e "test.status" ] ; then return `cat test.status` ; fi
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

rm -f test.status  # Appearance of this file signals smartmetd exited
touch test.out # Avoid missing file warnings when grepping
( timeout $waittime ../smartmetd -d -v -c ./minimal.conf >test.out 2>&1 ; echo $? > test.status ) &
childpid=$!

waitloop
ret=$?
tmret=0
if [ -r test.status ] ; then
    tmret=`cat test.status`
fi

if [ "$ret" != "0" ] ; then echo "Abnormal end." ; fi

# The rest should fail if any of the steps fail
set -e
echo ; echo "Server exited with status $tmret, output:"
cat test.out
rm -f test.out

# Likely timeout or crash
if [ "$tmret" != "0" ] ; then exit $tmret ; fi

# Normal ecit/shutdown
exit $ret


