import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject

Gst.init(None)

pipeline_str = (
    "v4l2src device=/dev/video0 ! "
    "image/jpeg,width=640,height=360,framerate=15/1 ! "
    "jpegdec ! "
    "videoflip method=clockwise ! "
    "videoscale ! video/x-raw,width=640,height=360,format=I420 ! "
    "x264enc tune=zerolatency bitrate=1200 speed-preset=ultrafast key-int-max=30 ! "
    "rtph264pay config-interval=1 pt=96 ! "
    "udpsink host=100.78.40.118 port=5000 sync=false"
)

pipeline = Gst.parse_launch(pipeline_str)
pipeline.set_state(Gst.State.PLAYING)

try:
    loop = GObject.MainLoop()
    print("Streaming optimized video... Ctrl+C to stop.")
    loop.run()
except KeyboardInterrupt:
    print("Stopping pipeline...")
    pipeline.set_state(Gst.State.NULL)
