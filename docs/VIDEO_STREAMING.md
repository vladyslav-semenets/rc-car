# FPV Video Streaming & ABR Architecture

This document describes the low-latency FPV video streaming pipeline, MediaMTX WebRTC server configuration, and the dynamic Adaptive Bitrate (ABR) controller running on the Raspberry Pi 4.

---

## 1. Streaming Pipeline Overview

```
[ Camera (MS2109 / Walksnail Avatar) ]
       │  (v4l2 /dev/video0)
       ▼
[ ffmpeg Hardware H.264 Encoder ]
       │  (libx264, zerolatency, superfast)
       ▼
[ MediaMTX Server (RTSP on :8554) ]
       │  (WebRTC relay)
       ▼
[ WebRTC Stream (:8889/front) ] ────(4G LTE / Tailscale)────► [ Android / macOS Client ]
```

---

## 2. MediaMTX Configuration (`mediamtx.yml`)

MediaMTX runs directly on the Raspberry Pi 4, receiving the local RTSP feed from ffmpeg and serving low-latency WebRTC streams.

### 2.1. ffmpeg Capture Pipeline
```yaml
paths:
  front:
    runOnInit: >
      ffmpeg -f v4l2 -input_format yuyv422 -video_size 1280x720 -framerate 30 -i /dev/video0
      -c:v libx264 -preset superfast -tune zerolatency
      -b:v $(cat /tmp/rc_car_bitrate 2>/dev/null || echo 2000k)
      -maxrate $(cat /tmp/rc_car_bitrate 2>/dev/null || echo 2000k)
      -bufsize 200k
      -g 30 -pix_fmt yuv420p
      -an
      -f rtsp rtsp://localhost:$RTSP_PORT/$MTX_PATH
    runOnInitRestart: yes
```

### 2.2. Critical Low-Latency Settings
* `-tune zerolatency`: Disables frame buffering and b-frame lookaheads.
* `-bufsize 200k`: Forces a small VBV buffer to prevent encoding lag.
* `-g 30`: Sends a keyframe (IDR) every 1 second (at 30 fps) for instant stream recovery upon packet loss.
* `runOnInitRestart: yes`: MediaMTX automatically relaunches ffmpeg when the ABR controller updates the bitrate file and terminates the active process.

---

## 3. Dynamic Adaptive Bitrate (ABR) Controller (`abr-controller.py`)

The ABR controller dynamically measures round-trip latency over the 4G LTE / Tailscale network and adjusts the video bitrate in real time to prevent video freezing.

### 3.1. How It Works
1. Continuous ICMP ping monitoring to the connected client.
2. If latency exceeds threshold ($> 120\text{ ms}$) or packet loss occurs, it lowers the bitrate step-by-step ($4000\text{k} \rightarrow 2500\text{k} \rightarrow 1500\text{k} \rightarrow 800\text{k}$).
3. When network quality stabilizes for $> 5\text{ seconds}$, it steps the bitrate back up.
4. Updates `/tmp/rc_car_bitrate` and sends `SIGTERM` to ffmpeg; MediaMTX immediately relaunches ffmpeg with the new bitrate.

### 3.2. Installing as a systemd Service
```bash
sudo cp abr-controller.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now abr-controller
sudo journalctl -u abr-controller -f
```

---

## 4. Client Video Viewing

* **In Android App**: Embedded WebRTC surface or browser preview at `http://<pi-ip>:8889/front`.
* **In macOS App**: Native WebRTC view rendered inside `WebRTCView.swift`.
* **In Web Browser**: Open `http://<pi-ip>:8889/front` directly in Chrome, Safari, or Firefox.
