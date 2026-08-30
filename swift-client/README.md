# Native macOS Ground Control Client (Swift / SwiftUI)

Native desktop Ground Control Station for macOS built with **Swift**, **SwiftUI**, and the Apple **GameController** framework.

---

## 1. Features

* **Native Gamepad Integration**: Uses Apple's `GCController` API for plug-and-play support for DualShock 4, DualSense 5, Xbox Wireless, and MFi gamepads over Bluetooth and USB.
* **Direct WebRTC Video Player**: Embedded WebRTC client playing low-latency H.264 video streamed from MediaMTX on the car.
* **SSH Remote Daemon Management**: Automatically triggers, monitors, or restarts the Raspberry Pi video streaming services over SSH.
* **Low-Latency UDP MAVLink Link**: Streams vehicle controls directly to the Raspberry Pi over local Wi-Fi or Tailscale WireGuard.

---

## 2. Project Architecture

```
swift-client/RCCar/
├── RCCarApp.swift         # App lifecycle and main window initialization
├── ContentView.swift      # Main split-view dashboard (video + telemetry HUD)
├── CarController.swift    # Vehicle state machine, 30 Hz control loop, expo math
├── JoystickManager.swift  # Apple GameController listener & button routing
├── MAVLink.swift          # Swift MAVLink v2 encoder/parser
├── UDPManager.swift       # Async UDP socket transport
├── WebRTCView.swift       # Native WebRTC / WKWebView video surface
├── SSHManager.swift       # Libssh2 / Process wrapper for remote daemon control
├── PingMonitor.swift      # Continuous ICMP network latency tracker
└── SettingsView.swift     # IP address, port, and trim preferences
```

---

## 3. Build & Run (Xcode)

### Prerequisites
* macOS Sonoma 14.0 or newer.
* Xcode 15.0 or newer.

### Instructions
1. Open `swift-client/RCCar/RCCar.xcodeproj` in Xcode.
2. Select target **RCCar** -> **My Mac**.
3. Press `Cmd + R` to build and run.
