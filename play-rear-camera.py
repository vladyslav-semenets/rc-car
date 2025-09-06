import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GLib
import cv2
import numpy as np

Gst.init(None)

pipeline_str = (
    'udpsrc port=5000 caps="application/x-rtp, media=video, clock-rate=90000, encoding-name=H264, payload=96" ! '
    'rtph264depay ! avdec_h264 ! videoconvert ! video/x-raw,format=BGR ! '
    'appsink name=mysink emit-signals=true max-buffers=1 drop=true'
)

pipeline = Gst.parse_launch(pipeline_str)
appsink = pipeline.get_by_name('mysink')

pipeline.set_state(Gst.State.PLAYING)

zoom = 1.0  # 1.0 = no zoom

try:
    print("Receiving video... Press '+/-' to zoom, 'q' to exit.")
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
            buffer.unmap(map_info)

            # --- Apply zoom ---
            if zoom != 1.0:
                center_x, center_y = width // 2, height // 2
                new_w, new_h = int(width / zoom), int(height / zoom)
                x1 = max(center_x - new_w // 2, 0)
                y1 = max(center_y - new_h // 2, 0)
                x2 = min(center_x + new_w // 2, width)
                y2 = min(center_y + new_h // 2, height)
                cropped = frame[y1:y2, x1:x2]
                frame = cv2.resize(cropped, (width, height))

            cv2.imshow('Front Camera', frame)

        key = cv2.waitKey(1) & 0xFF
        if key == ord('q'):
            break
        elif key in (ord('+'), ord('=')):  # zoom in
            zoom = min(zoom + 0.1, 3.0)
            print(f"Zoom: {zoom:.1f}x")
        elif key == ord('-'):  # zoom out
            zoom = max(zoom - 0.1, 1.0)
            print(f"Zoom: {zoom:.1f}x")

except KeyboardInterrupt:
    print("Stopping pipeline...")

pipeline.set_state(Gst.State.NULL)
cv2.destroyAllWindows()
