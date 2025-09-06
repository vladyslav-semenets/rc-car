import subprocess
import signal
import sys

mediamtx_process = subprocess.Popen(["mediamtx", "-c", "mediamtx.yaml"])

def shutdown(signal_received, frame):
    print("Shutting down MediaMTX...")
    mediamtx_process.terminate()
    mediamtx_process.wait()
    sys.exit(0)

signal.signal(signal.SIGINT, shutdown)
signal.signal(signal.SIGTERM, shutdown)

print("MediaMTX is running... Press Ctrl+C to stop.")
signal.pause()
