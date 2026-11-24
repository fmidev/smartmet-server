#!/bin/bash
#
# Cancellation of requests which exceed the time limit of their plugin. The test plugin
# (cancel_plugin) runs each request for 60 seconds unless it is cancelled:
#
#   busy    computation checking Fmi::check_cancelled()
#   block   a call broken off only by a Fmi::CancellationCallback, like a database query
#   stream  a streamed response
#
# With a time limit of 1 second the first two get a 503, and the stream is cut off and the
# connection closed. A plugin specific limit of 0 lets a long stream finish although the
# server wide limit is 1 second.
#
# Usage: cancellation-test.sh [path-to-smartmetd]

waittime=60

cd `dirname $0`

smartmetd=../smartmetd
if [ "$1" ] ; then smartmetd=$1 ; fi
if [ ! -x "$smartmetd" ] ; then
    echo "Smartmet server at '$smartmetd' is not found or not executable"
    exit 127
fi

if [ ! -f cancel_plugin/cancel_plugin.so ] ; then
    echo "cancel_plugin/cancel_plugin.so is not built"
    exit 1
fi

port=8078
childpid=

# Start the server with the given server wide and plugin specific time limits
start_server() {
    cat > cancellation.conf <<EOF
defaultlogging = true;
accesslogdir = "$PWD/cancellation-logs";
server_threads = 4;
port = $port;
request_timeout = $1;
engines: { }
plugins:
{
    cancel:
    {
        configfile = "/dev/null";
        libfile = "$PWD/cancel_plugin/cancel_plugin.so";
        request_timeout = $2;
    };
};
EOF
    mkdir -p cancellation-logs
    timeout $waittime "$smartmetd" -d -v -c ./cancellation.conf >>cancellation.out 2>&1 &
    childpid=$!
    for i in $(seq 1 100) ; do
        if curl -s -o /dev/null "http://localhost:$port/cancel/none" ; then return ; fi
        sleep 0.2
    done
}

stop_server() {
    if [ -n "$childpid" ] ; then
        kill $childpid 2>/dev/null
        wait $childpid 2>/dev/null
        childpid=
    fi
}
trap stop_server EXIT

failures=0

check() {
    if [ "$1" = "ok" ] ; then
        echo "  PASS  $2"
    else
        echo "  FAIL  $2"
        failures=$((failures + 1))
    fi
}

# Status code and the time taken
request() {
    curl -s -o "cancellation-$1.body" -w '%{http_code} %{time_total}' --max-time 20 \
         "http://localhost:$port/cancel/$1$2"
}

within() {
    awk -v t="$1" -v limit="$2" 'BEGIN { exit !(t < limit) }'
}

rm -f cancellation.out

# Plugin specific time limit of 1 second
start_server 0 1

read status elapsed <<< "$(request busy)"
[ "$status" = 503 ] && within "$elapsed" 3 && result=ok || result=fail
check $result "a computation checking the token gets a 503 at the time limit ($status in $elapsed s)"

read status elapsed <<< "$(request block)"
[ "$status" = 503 ] && within "$elapsed" 3 && result=ok || result=fail
check $result "a blocking call is broken off by the callback, 503 ($status in $elapsed s)"

read status elapsed <<< "$(request stream)"
chunks=$(grep -c chunk cancellation-stream.body)
within "$elapsed" 3 && [ "$chunks" -gt 0 ] && result=ok || result=fail
check $result "a streamed response is cut off at the time limit ($chunks chunks in $elapsed s)"

sleep 0.5
grep -q "request was cancelled while streaming the response" cancellation.out && result=ok || result=fail
check $result "the cancelled stream is logged"

# The server keeps serving after the cancellations
read status elapsed <<< "$(request none)"
[ "$status" = 404 ] && result=ok || result=fail
check $result "the server still answers ($status)"

stop_server

# Server wide limit of 1 second, none for the plugin
start_server 1 0

read status elapsed <<< "$(request stream '?seconds=3')"
chunks=$(grep -c chunk cancellation-stream.body)
[ "$status" = 200 ] && [ "$chunks" -ge 25 ] && result=ok || result=fail
check $result "a plugin without a time limit streams for 3 seconds ($status, $chunks chunks in $elapsed s)"

read status elapsed <<< "$(request busy '?seconds=2')"
[ "$status" = 200 ] && result=ok || result=fail
check $result "a plugin without a time limit computes for 2 seconds ($status in $elapsed s)"

stop_server

rm -f cancellation-*.body

if [ $failures -ne 0 ] ; then
    echo
    echo "$failures cancellation checks failed, server output:"
    cat cancellation.out
    exit 1
fi
echo
echo "All cancellation checks passed"
