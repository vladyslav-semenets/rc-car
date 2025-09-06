import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject, GLib
import sys
import termios
import tty

Gst.init(None)

# Initial zoom level (scaling resolution)
zoom_levels = [(1280, 720), (960, 540), (640, 360)]  # normal → zoomed out more
current_zoom_index = 0

pipeline_str = (
    "v4l2src device=/dev/video2 ! "
    "image/jpeg,width=1280,height=720,framerate=30/1 ! "
    "jpegdec ! "
    "videoflip method=clockwise ! "
    "videoscale name=vs ! "
    "capsfilter name=cf caps=video/x-raw,width=1280,height=720 ! "
    "x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast ! "
    "rtph264pay config-interval=1 pt=96 ! "
    "udpsink host=100.78.40.118 port=5000"
)

pipeline = Gst.parse_launch(pipeline_str)

# Get references to elements
capsfilter = pipeline.get_by_name("cf")

pipeline.set_state(Gst.State.PLAYING)

# --- Keyboard input handler ---
def getch():
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        ch = sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
    return ch

def handle_input():
    global current_zoom_index
    ch = getch()
    if ch == "q":
        print("Exiting...")
        loop.quit()
    elif ch == "+":
        if current_zoom_index > 0:
            current_zoom_index -= 1
        set_zoom()
    elif ch == "-":
        if current_zoom_index < len(zoom_levels) - 1:
            current_zoom_index += 1
        set_zoom()
    return True

def set_zoom():
    width, height = zoom_levels[current_zoom_index]
    print(f"Setting resolution to {width}x{height}")
    caps = Gst.Caps.from_string(f"video/x-raw,width={width},height={height}")
    capsfilter.set_property("caps", caps)

# Setup main loop
loop = GLib.MainLoop()
GLib.io_add_watch(sys.stdin, GLib.IO_IN, lambda source, cond: handle_input())

try:
    print("Streaming video... Press '+' to zoom in, '-' to zoom out, 'q' to quit.")
    loop.run()
except KeyboardInterrupt:
    pass
finally:
    pipeline.set_state(Gst.State.NULL)
