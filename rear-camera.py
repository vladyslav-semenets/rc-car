import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject

Gst.init(None)

pipeline_str = (
    "v4l2src device=/dev/video2 ! "
    "image/jpeg,width=1280,height=720,framerate=15/1 ! "  # keep MJPG at 1280x720 but lower fps
    "jpegdec ! "
    "videoflip method=clockwise ! "
    "videoscale ! video/x-raw,width=480,height=270,format=I420 ! "  # scale down small
    "x264enc tune=zerolatency bitrate=800 speed-preset=ultrafast ! "  # lower bitrate for small stream
    "rtph264pay config-interval=1 pt=96 ! "
    "udpsink host=100.78.40.118 port=5001 sync=false"
)

pipeline = Gst.parse_launch(pipeline_str)
pipeline.set_state(Gst.State.PLAYING)

try:
    loop = GObject.MainLoop()
    print("Streaming small rear camera... Ctrl+C to stop.")
    loop.run()
except KeyboardInterrupt:
    print("Stopping rear camera stream...")
    pipeline.set_state(Gst.State.NULL)
