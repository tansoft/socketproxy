#!/bin/bash
echo "\n-------------------start------------------"
#文件检测
#inputFile1=$1
#if [[ -f $inputFile1 ]]; then
#	echo " "
#else
#	echo "File "$inputFile1" can't found!!!"
#	exit
#fi
inputFile1=http://vfx.mtime.cn/Video/2019/02/04/mp4/190204084208765161.mp4
# CRF: 21~24
CUR_CRF=24
TempPath=scripts
#获取源视频信息
echo "Meidia file 1:["$inputFile1"]"
iw1=$( ffprobe -hide_banner $inputFile1 -show_streams -select_streams v 2>&1 | grep width | grep -v coded_width)
iw1=${iw1##*=}

ih1=$( ffprobe -hide_banner $inputFile1 -show_streams -select_streams v 2>&1 | grep height | grep -v coded_height)
ih1=${ih1##*=}

fps=$( ffprobe -hide_banner $inputFile1 -show_streams -select_streams v 2>&1 | grep avg_frame_rate)
fps=${fps##*=}

bitrate=$( ffprobe -hide_banner $inputFile1 -show_streams -select_streams v 2>&1 | grep bit_rate | grep -v max_bit_rate)
bitrate=${bitrate##*=}

src_filesize=$( wc -c $inputFile1 | awk '{print $1}')

echo "source meidia width:[$iw1] height:[$ih1] avg_frame_rate:[$fps] bit_rate:[$[$bitrate/1000]kbps] file_size:[$[$src_filesize/1024]KB]"

outCRFName="out_crf_$CUR_CRF.mp4"
	crf_psnr=$(ffmpeg -i $inputFile1 -crf $CUR_CRF -x264opts psy=0:ref=5:keyint=250:min-keyint=25:scenecut=40:chroma_qp_offset=0:aq_mode=2:threads=36:lookahead-threads=4 -maxrate 4000000 -bufsize 8000000 -async 1 -b:a 48k -ar 44100 -ac 2 -acodec copy -psnr $TempPath/$outCRFName -y 2>&1 | grep "PSNR Mean" | grep -v "QP:")
	crf_psnr=${crf_psnr##*"Avg:"}
	crf_psnr=${crf_psnr%%" "*}
	crf_bitrate=$( ffprobe -hide_banner $TempPath/$outCRFName -show_streams -select_streams v 2>&1 | grep bit_rate | grep -v max_bit_rate)
	crf_bitrate=${crf_bitrate##*=}
	crf_filesize=$( wc -c $TempPath/$outCRFName | awk '{print $1}')
	resutInfo="crf=$CUR_CRF \tpsnr = $crf_psnr  \tbitrate = $[$crf_bitrate/1000]kb/s  \tfilesize = $[$crf_filesize/1024] KB"
echo "result: $resutInfo"
