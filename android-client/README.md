# Android Ground Control Hub

Native Android Ground Control application for the Long-Range RC Car System built with **Kotlin** and **Jetpack Compose**. It acts as the primary ground relay, receiving gamepad inputs from a **DualShock 4 / DualSense 5 controller** over Bluetooth Classic HID and streaming low-latency control packets to the **Ground TX LoRa module** over BLE (Nordic UART Service).

---

## 1. Architecture Overview

```
[ DualShock 4/5 Gamepad ]
       │  (Bluetooth Classic HID / USB-C OTG)
       ▼
[ JoystickManager.kt ]
       │  (Deadband normalization: 7% triggers, 8% sticks, vibration)
       ▼
[ CarController.kt ]
       │  (Anti-rollover speed-sensitive damping, 50 Hz active state loop)
       ▼
 ┌─────┴─────────────────────────┐
 │                               │
 ▼                               ▼
[ RcPacket.kt ]            [ MAVLink.kt ]
 (8-Byte Compact Frame)     (MAVLink v2 Fallback)
 │                               │
 ▼                               ▼
[ BleManager.kt ]          [ UDPManager.kt ]
 (BLE NUS to Ground TX)     (Direct Wi-Fi/4G UDP)
```

---

## 2. Key Modules

### 2.1. `MainActivity.kt`
* **Fullscreen Compose UI**: Immersive dark-themed control interface displaying connection statuses, battery, signal RSSI, telemetry counters, and video stream placeholder.
* **Settings Dialog**: Real-time sliders for:
  * Steering Exponential (`steeringExpo`, default `0.5`).
  * Gimbal Exponential (`gimbalExpo`, default `0.3`).
  * High-Speed Steering Damping (`highSpeedSteeringDamping`, 0%–85%, default `50%–75%`).
  * Motor trim and slew configuration parameters.
* **Dynamic Permissions**: Automatically handles Bluetooth Scan, Connect, and Location permissions for Android 12+ (API 31+) and legacy Android versions.

### 2.2. `CarController.kt`
* **Active 50 Hz Control Loop**: Background coroutine streaming stick state every 20 ms.
* **Instant Neutral Deceleration**: Automatically pushes neutral ($0\%$) the moment triggers enter deadband.
* **Speed-Sensitive Dynamic Steering (Anti-Rollover)**:
  $$\text{damping} = \left(\max(\text{throttleFrac}, \text{gearFrac})^{0.75} \times \text{highSpeedSteeringDamping}\right).\text{coerceIn}(0\text{f}, 0.85\text{f})$$
  Progressively narrows steering throw as speed increases so high-speed turns do not flip the vehicle.

### 2.3. `JoystickManager.kt`
* **Deadband & Linear Normalization**:
  * Trigger deadband: `7%` with linear remap $[0.07..1.0] \rightarrow [0.0..1.0]$.
  * Stick deadband: `8%` with linear remap $[0.08..1.0] \rightarrow [0.0..1.0]$.
* **Input Routing**: Reads analog sticks (steering, gimbal pan), triggers (forward gas, reverse brake), D-pad HAT axes, and buttons.
* **Haptic Feedback**: Drives gamepad vibration motors proportionally to throttle and steering inputs.

### 2.4. `BleManager.kt`
* **Service**: Nordic UART Service (NUS) `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`.
* **TX Characteristic**: `6E400003-...` (Telemetry notifications from Ground TX).
* **RX Characteristic**: `6E400002-...` (High-speed writes with `WRITE_TYPE_NO_RESPONSE`).
* **MTU Request**: Requests 256-byte MTU on connection for zero-fragmentation delivery.

### 2.5. `RcPacket.kt`
* Encodes the high-speed 8-byte binary frame:
  `[0xAA, Seq, Throttle, Steering, GimbalYaw, GimbalPitch, Flags, CRC-8]`.
* Computes polynomial `0x07` CRC-8 for hardware verification on the Pi.

---

## 3. Gamepad Button Mapping

| Button / Axis | Action |
| :--- | :--- |
| **R2 Trigger (GAS)** | Proportional Forward Acceleration |
| **L2 Trigger (BRAKE)** | Proportional Reverse / Braking |
| **Left Stick X** | Steering (with expo and speed-sensitive dynamic damping) |
| **Right Stick X** | Camera Gimbal Yaw (Pan) |
| **L1 / R1 Bumpers** | Camera Gimbal Pitch Up / Down |
| **R3 (Right Stick Click)** | Shift Transmission Gear Up (Gears 1–8) |
| **L3 (Left Stick Click)** | Shift Transmission Gear Down |
| **D-Pad Up** | Initialize Car / Recenter Center Trim |
| **D-Pad Down** | Toggle Active MPU-6050 Gyro ESP Counter-Steering |
| **D-Pad Left / Right** | Adjust Center Trim Degree (`degreeOfTurns` $\pm 1^\circ$) |
| **B Button** | Emergency Stop / Neutral |
| **Y Button** | Start FPV Video Stream |
| **A Button** | Stop FPV Video Stream |

---

## 4. Build and Installation

### Requirements
* Android Studio Iguana (2023.2.1) or newer.
* Android SDK 34 (Android 14) / Minimum SDK 26 (Android 8.0).
* Gradle 8.2+ with Kotlin 1.9+.

### Steps
1. Open `android-client/` in Android Studio.
2. Allow Gradle sync to complete.
3. Connect an Android smartphone via USB or Wireless ADB.
4. Click **Run 'app'** (`Shift + F10`) to build and deploy.
