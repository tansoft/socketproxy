#!/bin/bash

if [ "$1" == "ts" ]; then
	format="mpegts"
else
	format="flv"
fi

ffmpeg -loglevel quiet -i http://vfx.mtime.cn/Video/2019/02/04/mp4/190204084208765161.mp4 -vf delogo=640:22:211:77:1 -b:v 500k -f $format -
