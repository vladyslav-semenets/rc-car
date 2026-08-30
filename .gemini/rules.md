# Workspace Rules & Architecture Guide

This document defines architecture, pinouts, protocols, and standards for this repository.

---

## 1. System Overview

Professional Long-Range Remote-Controlled (RC) Car System featuring:
1. **Ultra-Low Latency LoRa 868 MHz Link** (5.5 ms airtime, ~23–28 ms total latency) via **Seeed Studio XIAO ESP32-S3 + Wio-SX1262**.
2. **Android Ground Control Hub** (Kotlin / Jetpack Compose) bridging **DualShock 4/5 Gamepad (Bluetooth Classic HID)** -> **Ground TX (BLE NUS)**.
3. **Raspberry Pi 4 Onboard Rover Firmware (C)** driving dual ESCs (slew-rate limited, rear-first sequencing), steering servo, 2-axis camera gimbal, and MPU-6050 gyro counter-steering (vertical mount).
4. **Low-Latency FPV Video Pipeline** with MediaMTX + WebRTC and adaptive bitrate (ABR) over 4G LTE / Tailscale.
5. **Multi-Client Support**: Android Client, macOS C Client (`client/c`), Swift macOS App (`swift-client/`).

```
[ DualShock 4/5 Gamepad ]
       │  ~5 ms  (Bluetooth Classic HID / USB OTG)
       ▼
[ Android Smartphone App ]
       │  ~6 ms  (BLE Nordic UART Service - NUS)
       ▼
[ Ground TX: Seeed XIAO ESP32-S3 + Wio-SX1262 ]
       │  ~5.5 ms (868.0 MHz LoRa RF Link - RadioLib)
       ▼
[ Rover RX: Seeed XIAO ESP32-S3 + Wio-SX1262 ]
       │  ~0.3 ms (USB CDC-ACM Serial /dev/ttyACM0)
       ▼
[ Raspberry Pi 4 (Rover Firmware) ]
       │  ~6 ms  (50 Hz pigpio DMA PWM)
       ▼
[ Dual ESCs, Steering Servo, 2-Axis Gimbal, MPU-6050 ]
```

---

## 2. Repository Structure

```
rc-car-repo/
├── android-client/               # Android App (Kotlin / Jetpack Compose)
│   └── app/src/main/java/com/rccar/android/
│       ├── MainActivity.kt       # Fullscreen Compose UI & Settings Dialog
│       ├── CarController.kt      # 50 Hz active state stream, anti-rollover damping
│       ├── JoystickManager.kt    # DualShock HID input, 7% deadzone normalization, vibration
│       ├── BleManager.kt         # BLE NUS client for Ground TX
│       ├── RcPacket.kt           # ExpressLRS-style 8-byte binary frame encoder
│       ├── MAVLink.kt            # MAVLink v2 encoder/parser (UDP/fallback)
│       └── UDPManager.kt         # Direct UDP Wi-Fi / 4G socket client
├── lora-firmware/                # PlatformIO firmwares for Seeed XIAO ESP32-S3 + SX1262
│   ├── tx-ground-controller/     # BLE NUS Server -> 868 MHz LoRa TX
│   ├── rx-pi-bridge/             # 868 MHz LoRa RX -> USB CDC Serial (/dev/ttyACM0)
│   └── README.md                 # Hardware pinout and flashing guide
├── raspberry-pi-client/c/        # Raspberry Pi 4 Rover Firmware (C)
│   ├── main.c                    # Entry point, signal handling, dual serial/UDP startup
│   ├── rc-car.c / rc-car.h       # ESC slew rate limiter, MPU-6050 gyro, gimbal, unstuck
│   ├── serial.c / serial.h       # Dual-mode parser (8-byte compact RC + MAVLink fallback)
│   ├── udp.c / udp.h             # UDP MAVLink server (port 8565)
│   └── pigpio-mock.c / .h        # Mock pigpio for macOS development
├── client/c/                     # Legacy macOS Desktop Client (SDL2 + UDP MAVLink)
├── swift-client/ / macos-app/    # Native Swift / macOS Clients
├── mediamtx.yml                  # MediaMTX WebRTC / RTSP video streaming configuration
├── abr-controller.py             # Dynamic ffmpeg video bitrate controller for 4G LTE
└── abr-controller.service        # systemd unit for ABR controller
```

---

## 3. Communication Protocols

### 3.1. ExpressLRS-Style 8-Byte High-Speed Binary Protocol (LoRa Primary)
Designed for ultra-low latency (**5.5 ms airtime** vs 22 ms MAVLink) streamed continuously at **50 Hz**:

```
[ 0xAA ] [ Seq ] [ Throttle ] [ Steering ] [ GimbalYaw ] [ GimbalPitch ] [ Flags ] [ CRC-8 ]
  1 Byte  1 Byte    1 Byte       1 Byte       1 Byte        1 Byte       1 Byte   1 Byte
```
* **`0xAA`**: Sync header byte.
* **`Seq`**: Incremental packet counter (0–255).
* **`Throttle`**: Signed 8-bit integer (`-100` to `+100`%).
* **`Steering`**: 8-bit unsigned degrees (`0` to `180`°, center ~`86`°).
* **`GimbalYaw`**: Signed 8-bit degrees (`-90` to `+90`°).
* **`GimbalPitch`**: Signed 8-bit degrees (`-45` to `+45`°).
* **`Flags`**: Bit 0 = Gyro ESP, Bit 1 = Unstuck active, Bits 3..6 = Gear level (1–8).
* **`CRC-8`**: Standard polynomial `0x07` checksum.

### 3.2. MAVLink Protocol (UDP / Direct Wi-Fi & Parameter Config)
Used over UDP port `8565` or for full parameter calibration:

| Command ID | Command Name | Parameters |
|---|---|---|
| `1` | `INIT` | `p1=speed, p2=centerDegree` |
| `2` | `CHANGE_DEGREE_OF_TURNS` | `p1=degrees` |
| `3` | `RESET_TURNS` | `p1=degrees` |
| `4` | `TURN_TO` | `p1=degrees` |
| `5` | `FORWARD` | `p1=speed (0-100%)` |
| `6` | `BACKWARD` | `p1=speed (0-100%)` |
| `7` | `SET_ESC_NEUTRAL` | — |
| `8` | `START_CAMERA` | — |
| `9` | `STOP_CAMERA` | — |
| `10` | `CAMERA_GIMBAL_TURN_TO` | `p1=yaw degrees (-90..+90)` |
| `11` | `CAMERA_GIMBAL_SET_PITCH` | `p1=pitch degrees (-45..+45)` |
| `12` | `RESET_CAMERA_GIMBAL` | — |
| `13` | `STEERING_CALIBRATION_ON` | MPU-6050 Gyro ESP ON |
| `14` | `STEERING_CALIBRATION_OFF`| MPU-6050 Gyro ESP OFF |
| `15` | `SET_MOTOR_CONFIG` | `p1=frontTrim, p2=rearTrim, p3=slewMax, p4=dirHold, p5=frontLag, p6=brakeMs, p7=neutralMs` |
| `16` | `UNSTUCK` | `p1=centerDegree` (rocking self-recovery) |

---

## 4. Hardware Configuration & Pinouts

### 4.1. Raspberry Pi 4 GPIO Pinout
```c
CAR_TURNS_SERVO_PIN   = 17   // Steering servo (500 - 2500 µs PWM)
CAR_ESC_PIN           = 23   // Front axle ESC (1000 - 2000 µs PWM)
CAR_ESC_SECOND_PIN    = 25   // Rear axle ESC (1000 - 2000 µs PWM)
CAR_ESC_ENABLE_PIN    = 16   // ESC power relay (0 = ON, 1 = OFF)
CAR_CAMERA_GIMBAL_PIN1= 27   // Gimbal base power
CAR_CAMERA_GIMBAL_PIN3= 22   // Pitch servo (1000 - 2000 µs PWM)
CAR_CAMERA_GIMBAL_PIN4= 24   // Yaw servo (1000 - 2000 µs PWM)
```

### 4.2. MPU-6050 Gyroscope (Vertical Mount)
* **Mounting**: Mounted vertically against the chassis tower.
* **Axis**: Chassis yaw rate is read on the **$Y$-axis gyro (`GYRO_YOUT_H = 0x45`)** instead of the standard Z-axis.
* **I2C Address**: `0x68` on I2C bus 1.

### 4.3. Seeed XIAO ESP32-S3 + Wio-SX1262 LoRa Pinout
* **`NSS`**: GPIO 41 (`D4`)
* **`DIO1` (IRQ)**: GPIO 39 (`D1`)
* **`NRST`**: GPIO 42 (`D2`)
* **`BUSY`**: GPIO 40 (`D3`)
* **`RF_SW`**: GPIO 38 (`D5`)
* **`SCK` / `MISO` / `MOSI`**: GPIO 7 / 8 / 9 (`D8` / `D9` / `D10`)
* **RF Settings**: 868.0 MHz, SF7, BW 500 kHz, CR 4/5, Power 22 dBm.

---

## 5. Build & Execution Instructions

### 5.1. Raspberry Pi C Client
```bash
cd raspberry-pi-client/c
cmake -S . -B build
cmake --build build
sudo ./build/raspberrypiclient
```
*(On macOS, CMake automatically builds against `pigpio-mock.c` for local development).*

### 5.2. LoRa Firmwares (PlatformIO)
```bash
# Ground TX
cd lora-firmware/tx-ground-controller
pio run -t upload

# Rover RX
cd lora-firmware/rx-pi-bridge
pio run -t upload
```

### 5.3. Android App
Open `android-client/` in **Android Studio** and click **Run** (or install via Gradle).

---

## 6. Coding Standards & Guidelines

* **Strict Explicit Braces**: Always wrap every `if`, `else if`, `else`, `for`, and `while` block in explicit curly braces `{ ... }` across multiple lines.
* **Thread Safety**: Always protect shared state with POSIX mutexes (`motorMutex`, `steeringWheelCorrectionMutex`, `writeMutex`).
* **PWM & Angle Clamping**: Always clamp motor targets (`1000–2000 µs`), steering angles (`0–180°`), and gimbal angles.
* **Zero Busy-Waiting**: Use `usleep(20000)` or thread condition variables instead of empty while loops.
