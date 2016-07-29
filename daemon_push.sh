#!/bin/sh

nohup ./objs/testSrslibrtmp rtsp://10.196.230.149/000100 \
rtsp://10.196.230.138:8554/cam \
"rtsp://10.196.230.150/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif" \
"rtsp://10.196.230.139/stream?c=h264" \
>/dev/null 2>&1 &
