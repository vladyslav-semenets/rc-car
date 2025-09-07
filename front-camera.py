import subprocess

cmd = [
    "gst-launch-1.0",
    "-v",
    "v4l2src", "device=/dev/video0",
    "!", "image/jpeg,width=1280,height=720,framerate=10/1",
    "!", "jpegdec",
    "!", "videoflip", "method=clockwise",
    "!", "videoconvert",
    "!", "video/x-raw,format=I420",
    "!", "x264enc", "tune=zerolatency", "bitrate=2000", "speed-preset=ultrafast", "key-int-max=20",
    "!", "rtph264pay", "config-interval=1", "pt=96",
    "!", "udpsink", "host=100.78.40.118", "port=5000", "sync=false"
]

print("Streaming front camera (1280x720 @10fps) via CLI...")
subprocess.run(cmd)
