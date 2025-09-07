import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject

Gst.init(None)

pipeline_str = (
    "v4l2src device=/dev/video2 ! "
    "image/jpeg,width=960,height=540,framerate=30/1 ! "
    "jpegdec ! "
    "videoflip method=clockwise ! "
    "videoscale ! video/x-raw,width=960,height=540 ! "
    "clockoverlay time-format=\"%Y-%m-%d %H:%M:%S\" shaded-background=true font-desc=\"Sans, 24\" ! "
    "x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast ! "
    "rtph264pay config-interval=1 pt=96 ! "
    "udpsink host=100.78.40.118 port=5001"
)

pipeline = Gst.parse_launch(pipeline_str)
pipeline.set_state(Gst.State.PLAYING)

try:
    loop = GObject.MainLoop()
    print("Streaming with current time overlay... Ctrl+C to stop.")
    loop.run()
except KeyboardInterrupt:
    print("Stopping pipeline...")
    pipeline.set_state(Gst.State.NULL)
