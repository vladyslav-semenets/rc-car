import subprocess

cmd = [
    "gst-launch-1.0",
    "-v",
    "v4l2src", "device=/dev/video2",
    "!", "image/jpeg,width=1280,height=720,framerate=15/1",
    "!", "jpegdec",
    "!", "videoflip", "method=clockwise",
    "!", "videoscale",
    "!", "video/x-raw,width=480,height=270,format=I420",
    "!", "x264enc", "tune=zerolatency", "bitrate=800", "speed-preset=ultrafast",
    "!", "rtph264pay", "config-interval=1", "pt=96",
    "!", "udpsink", "host=100.78.40.118", "port=5001", "sync=false"
]

print("Streaming small rear camera via CLI...")
subprocess.run(cmd)
