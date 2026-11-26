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
# connection closed. The plugin sets a default limit of 2 seconds for its handler,
# plugins.cancel.request_timeout overrides it, and a limit of 0 lets a long stream finish.
# The server wide request_timeout is not used. The settimeout admin request changes the
# limit, and serviceinfo shows it through the admin URI only.
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

# Start the server with the given server wide and plugin specific settings
start_server() {
    cat > cancellation.conf <<EOF
defaultlogging = true;
accesslogdir = "$PWD/cancellation-logs";
server_threads = 4;
port = $port;
admin: { uri = "/admin"; user = "foo"; password = "bar"; };
$1
engines: { }
plugins:
{
    cancel:
    {
        configfile = "/dev/null";
        libfile = "$PWD/cancel_plugin/cancel_plugin.so";
        $2
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

# Plugin specific time limit of 1 second, overriding the default of the plugin
start_server "" "request_timeout = 1;"

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

# The default of the plugin (2 seconds) applies, the server wide setting is not used
start_server "request_timeout = 1;" ""

read status elapsed <<< "$(request busy '?seconds=5')"
[ "$status" = 503 ] && within 1.8 "$elapsed" && within "$elapsed" 4 && result=ok || result=fail
check $result "the default time limit of the plugin applies ($status in $elapsed s)"

grep -q "server wide request_timeout setting is not used" cancellation.out && result=ok || result=fail
check $result "the unused server wide request_timeout is reported"

stop_server

# No time limit for the plugin
start_server "" "request_timeout = 0;"

read status elapsed <<< "$(request stream '?seconds=3')"
chunks=$(grep -c chunk cancellation-stream.body)
[ "$status" = 200 ] && [ "$chunks" -ge 25 ] && result=ok || result=fail
check $result "a plugin without a time limit streams for 3 seconds ($status, $chunks chunks in $elapsed s)"

read status elapsed <<< "$(request busy '?seconds=3')"
[ "$status" = 200 ] && result=ok || result=fail
check $result "a plugin without a time limit computes for 3 seconds ($status in $elapsed s)"

# The settimeout admin request sets a limit
status=$(curl -s -o /dev/null -w '%{http_code}' "http://localhost:$port/admin?what=settimeout&uri=/cancel&timeout=1")
[[ "$status" == 4?? ]] && result=ok || result=fail
check $result "settimeout requires authentication ($status)"

status=$(curl -s -o /dev/null -w '%{http_code}' -u foo:bar "http://localhost:$port/admin?what=settimeout&uri=/cancel&timeout=1")
[ "$status" = 200 ] && result=ok || result=fail
check $result "settimeout sets the time limit of /cancel ($status)"

read status elapsed <<< "$(request busy '?seconds=5')"
[ "$status" = 503 ] && within "$elapsed" 3 && result=ok || result=fail
check $result "the time limit set with settimeout applies ($status in $elapsed s)"

status=$(curl -s -o /dev/null -w '%{http_code}' -u foo:bar "http://localhost:$port/admin?what=settimeout&uri=/nosuchuri&timeout=1")
[[ "$status" == [45]?? ]] && result=ok || result=fail
check $result "settimeout of an unknown URI fails ($status)"

# serviceinfo shows the time limits through the admin URI only
curl -s "http://localhost:$port/admin?what=serviceinfo&format=json" > cancellation-admin.body
curl -s "http://localhost:$port/info?what=serviceinfo&format=json" > cancellation-info.body
grep -q -E '"/cancel"[^}]*Request timeout *":"?1"?[,}]' cancellation-admin.body && result=ok || result=fail
check $result "serviceinfo through /admin shows the time limit"
grep -q '"/cancel"' cancellation-info.body && ! grep -q "Request timeout" cancellation-info.body && result=ok || result=fail
check $result "serviceinfo through /info does not show the time limits"

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
