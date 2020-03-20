#!/bin/sh

echo "Start processing..."
i=0
total=200
while [[ $i -le $total ]];
do
echo $i very very very very very very very very very very very very long line...
((i++))
sleep 0.1
done
echo "{\"status\":200}"
