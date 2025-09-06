import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GLib
import cv2
import numpy as np

Gst.init(None)

pipeline_str = (
    'udpsrc port=6000 caps="application/x-rtp, media=video, clock-rate=90000, encoding-name=H264, payload=96" ! '
    'rtph264depay ! avdec_h264 ! videoconvert ! video/x-raw,format=BGR ! appsink name=mysink emit-signals=true max-buffers=1 drop=true'
)

pipeline = Gst.parse_launch(pipeline_str)
appsink = pipeline.get_by_name('mysink')

pipeline.set_state(Gst.State.PLAYING)

try:
    print("Receiving video... Press 'q' to exit.")
    while True:
        sample = appsink.emit('try-pull-sample', Gst.SECOND // 10)
        if sample:
            buffer = sample.get_buffer()
            success, map_info = buffer.map(Gst.MapFlags.READ)
            if not success:
                continue
            frame = np.frombuffer(map_info.data, dtype=np.uint8)

            caps = sample.get_caps()
            structure = caps.get_structure(0)
            width = structure.get_value('width')
            height = structure.get_value('height')

            frame = frame.reshape((height, width, 3))

            cv2.imshow('Front Camera', frame)
            buffer.unmap(map_info)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

except KeyboardInterrupt:
    print("Stopping pipeline...")

pipeline.set_state(Gst.State.NULL)
cv2.destroyAllWindows()
