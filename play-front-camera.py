import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GLib

# Initialize GStreamer
Gst.init(None)

# Pipeline string (uses ximagesink instead of autovideosink)
pipeline_str = (
    'udpsrc port=5000 caps="application/x-rtp, media=video, clock-rate=90000, encoding-name=H264, payload=96" ! '
    'rtph264depay ! avdec_h264 ! videoconvert ! ximagesink'
)

# Create the pipeline
pipeline = Gst.parse_launch(pipeline_str)
pipeline.set_state(Gst.State.PLAYING)

# Main loop to keep it running
loop = GLib.MainLoop()
try:
    print("Receiving video... Press Ctrl+C to stop.")
    loop.run()
except KeyboardInterrupt:
    print("Stopping pipeline...")
    pipeline.set_state(Gst.State.NULL)
