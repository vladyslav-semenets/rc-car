#!/bin/bash
BITRATE=$(cat /tmp/rc_car_bitrate 2>/dev/null || echo "2500k")
echo "[stream] Starting ffmpeg with bitrate: $BITRATE"
exec ffmpeg \
  -f v4l2 -framerate 25 -video_size 1280x720 \
  -i /dev/video0 \
  -c:v h264_v4l2m2m \
  -b:v "$BITRATE" -maxrate "$BITRATE" -bufsize 500k \
  -g 25 -pix_fmt yuv420p \
  -fflags nobuffer -flags low_delay \
  -f rtsp rtsp://localhost:8554/front
