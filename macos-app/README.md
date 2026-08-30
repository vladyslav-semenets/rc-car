# macOS Process Runner & WebRTC Dashboard (SwiftUI)

A dedicated macOS dashboard application that combines a real-time **WebRTC FPV video player** with an embedded **Process Runner and Log Console** to manage and monitor the background C client (`rccarclient`).

---

## 1. Features

* **Embedded C Process Runner**: Launches and monitors the compiled `rccarclient` binary directly from the GUI, capturing standard output and error streams in real time.
* **Live Log Terminal Console**: Scrollable log monitor with timestamps, exit code detection, and automatic restart handling.
* **WebRTC Live FPV Player**: Embedded low-latency video player rendering the H.264 camera feed from MediaMTX.
* **Preferences & Path Configuration**: Allows custom configuration of binary paths, working directories, and video stream endpoints.

---

## 2. Architecture

```
macos-app/RCCarApp/
├── RCCarApp.swift         # Application entry point
├── ContentView.swift      # Main split-screen UI (Video player + Console logs)
├── ClientManager.swift    # Foundation Process wrapper, stdout/stderr pipe handler
├── WebRTCView.swift       # WKWebView / WebRTC video surface
├── SettingsView.swift     # Path and streaming preferences
├── Assets.xcassets        # Application icons and colors
└── RCCarApp.entitlements  # App sandbox network entitlements
```

---

## 3. Build & Run (Xcode)

### Prerequisites
* macOS Sonoma 14.0 or newer.
* Xcode 15.0 or newer.
* Built C client binary (`client/c/build/rccarclient`).

### Instructions
1. Open `macos-app/RCCarApp/RCCarApp.xcodeproj` in Xcode.
2. Ensure `client/c/build/rccarclient` is built (see `client/c/README.md`).
3. Press `Cmd + R` to build and run the dashboard.
4. In Settings, verify the path to your `rccarclient` binary, then click **Start Client** in the main window.
