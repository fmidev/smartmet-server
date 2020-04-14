#! /bin/sh

#Run this to obtain Brainstorm test suites

mkdir -p suites
if [ "$(ls -A suites)" ]; then
rm suites/*
fi

curl -s "brainstormgw.cisse.fmi.fi:8080/admin?what=lastrequests&minutes=120"|awk -F'":"' 'BEGIN{RS="},{" } /RequestString/ {gsub(/\"/,"",$3); print $3}'|head -n -1 > reqs

awk '/timeseries/||/pointforecast/' < reqs > suites/timeseries
awk '/observe/' < reqs > suites/observe
awk '/wfs/' < reqs > suites/wfs
awk '/wms/' < reqs > suites/wms
awk '/dali/' < reqs > suites/dali
awk '/avi/' < reqs > suites/avi
awk '/textgen/' < reqs > suites/textgen
mv reqs suites/all


