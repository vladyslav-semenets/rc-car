import subprocess
import signal
import sys

def main():
    front_cmd = [
        "gst-launch-1.0", "-v",
        "v4l2src", "device=/dev/video0",
        "!", "image/jpeg,width=1280,height=720,framerate=15/1",
        "!", "jpegdec",
        "!", "videoconvert",
        "!", "v4l2h264enc", "bitrate=2000",
        "!", "rtph264pay", "config-interval=1", "pt=96",
        "!", "udpsink", "host=100.78.40.118", "port=5000", "sync=false"
    ]

    rear_cmd = [
        "gst-launch-1.0", "-v",
        "v4l2src", "device=/dev/video2",
        "!", "image/jpeg,width=640,height=360,framerate=10/1",
        "!", "jpegdec",
        "!", "videoconvert",
        "!", "v4l2h264enc", "bitrate=800",
        "!", "rtph264pay", "config-interval=1", "pt=96",
        "!", "udpsink", "host=100.78.40.118", "port=5001", "sync=false"
    ]

    print("Starting front and rear camera streams with h264_v4l2m2m... Ctrl+C to stop.")

    front_proc = subprocess.Popen(front_cmd)
    rear_proc = subprocess.Popen(rear_cmd)

    def signal_handler(sig, frame):
        print("\nStopping streams...")
        front_proc.terminate()
        rear_proc.terminate()
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)

    front_proc.wait()
    rear_proc.wait()

if __name__ == "__main__":
    main()
