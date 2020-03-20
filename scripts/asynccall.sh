#!/bin/sh

echo "receive async job: $@" >> scripts/asynclog.txt
scripts/forker "nohup sleep 10 > /dev/null 2>&1 &"
echo "{\"status\":200}"
