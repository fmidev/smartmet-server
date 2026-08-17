#!/bin/bash
#
# HTTP/1.1 conformance checks for the persistent-connection support (BS-3475).
#
# These go over a raw socket rather than through curl, because the interesting
# cases are exactly the ones a well-behaved client will not produce: a missing
# Host header, a withheld request body, a bodyless status code, a request
# pipelined into the same segment.  Each check states what it sends and what it
# expects, so a failure says which requirement regressed.
#
# Usage: conformance-test.sh [path-to-smartmetd]

waittime=60

cd `dirname $0`

smartmetd=../smartmetd
if [ "$1" ] ; then smartmetd=$1 ; fi
if [ ! -x "$smartmetd" ] ; then
    echo "Smartmet server at '$smartmetd' is not found or not executable"
    exit 127
fi

port=8079

cat > conformance.conf <<EOF
defaultlogging = false;
server_threads = 4;
port = $port;
keepalive: { enabled = true; timeout = 30; maxrequests = 1000; };
engines: { }
plugins: { }
EOF

touch conformance.out
timeout $waittime "$smartmetd" -d -v -c ./conformance.conf >conformance.out 2>&1 &
childpid=$!

echo "Waiting for server to start ..."
while ! grep -q "Launched.*server" conformance.out ; do
    sleep 1s
    if [ ! -d /proc/$childpid ]; then
        echo "Server exited before it was ready:"
        cat conformance.out
        exit 1
    fi
done
sleep 1s

python3 ./conformance-test.py $port
ret=$?

kill $childpid 2>/dev/null
wait 2>/dev/null

if [ "$ret" != "0" ] ; then
    echo
    echo "Server output:"
    cat conformance.out
    exit $ret
fi

rm -f conformance.out conformance.conf
echo "Conformance test OK"
exit 0
