import subprocess

def rear_camera_stream():
    cmd = [
        "gst-launch-1.0", "-v",
        "v4l2src", "device=/dev/video2",
        "!", "image/jpeg",
        "!", "jpegdec",
        "!", "videoconvert",
        "!", "x264enc", "tune=zerolatency", "bitrate=800", "speed-preset=ultrafast",
        "!", "rtph264pay", "config-interval=1", "pt=96",
        "!", "udpsink", "host=100.78.40.118", "port=5001", "sync=false"
    ]
    print("Streaming rear camera via CLI... Ctrl+C to stop.")
    subprocess.run(cmd)

if __name__ == "__main__":
    rear_camera_stream()
