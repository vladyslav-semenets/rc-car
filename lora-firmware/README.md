# LoRa 868 MHz Control System (Seeed XIAO ESP32-S3 + Wio-SX1262)

This directory contains two PlatformIO firmwares for the **Seeed Studio XIAO ESP32-S3 + Wio-SX1262** kit, providing an ultra-low latency, long-range 868 MHz LoRa RF control link between the **Android Smartphone (Ground Control Hub)** and the **Raspberry Pi 4 (Onboard Rover)**.

---

## 1. System Topology & Latency

```
[ DualShock 4/5 Controller ]
       │  ~5 ms  (Bluetooth Classic HID / USB OTG)
       ▼
[ Android Smartphone Client ]
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
[ ESCs, Steering Servo, 2-Axis Gimbal, MPU-6050 ]
```

* **Packet Protocol**: Compact 8-byte binary frame (ExpressLRS/CRSF style) + MAVLink dual-mode fallback.
* **Over-The-Air Airtime**: **~5.5 ms** (SF7, BW 500 kHz, 22 dBm).
* **Streaming Rate**: **50 Hz** continuous state stream.
* **End-to-End Latency**: **~23 – 28 ms** (stick input $\rightarrow$ servo/motor response).

---

## 2. Hardware Pinout (Seeed XIAO ESP32-S3 + Wio-SX1262)

Both Ground TX and Rover RX use identical pinout mappings matching the Seeed Studio board-to-board (B2B) interface:

| Function | Pin Name | XIAO Pin | ESP32-S3 GPIO | Description |
| :--- | :--- | :--- | :--- | :--- |
| **SPI CS** | NSS | `D4` | `GPIO 41` | LoRa Chip Select |
| **IRQ** | DIO1 | `D1` | `GPIO 39` | Packet RX/TX Interrupt |
| **Reset** | NRST | `D2` | `GPIO 42` | LoRa Chip Reset |
| **Busy** | BUSY | `D3` | `GPIO 40` | LoRa Busy Status Line |
| **RF Switch** | RF_SW | `D5` | `GPIO 38` | Internal RF TX/RX Path Switch |
| **SPI SCK** | SCK | `D8` | `GPIO 7` | Hardware SPI Clock |
| **SPI MISO**| MISO | `D9` | `GPIO 8` | Hardware SPI MISO |
| **SPI MOSI**| MOSI | `D10` | `GPIO 9` | Hardware SPI MOSI |
| **User LED**| LED | `LED_BUILTIN` | `GPIO 21` | Status / Packet Activity Indicator |

> [!WARNING]
> Always attach an appropriate **868 MHz LoRa antenna** to the U.FL / IPEX connector before powering on the boards to prevent RF power amplifier damage.

---

## 3. High-Speed 8-Byte Packet Format (ELRS Style)

```
[ 0xAA ] [ Seq ] [ Throttle ] [ Steering ] [ GimbalYaw ] [ GimbalPitch ] [ Flags ] [ CRC-8 ]
  1 Byte  1 Byte    1 Byte       1 Byte       1 Byte        1 Byte       1 Byte   1 Byte
```

* **`0xAA`**: Sync header byte.
* **`Seq`**: Incremental packet sequence counter (0–255).
* **`Throttle`**: Signed 8-bit integer (`-100` to `+100`%).
* **`Steering`**: 8-bit unsigned degrees (`0` to `180`°, center ~`86`°).
* **`GimbalYaw`**: Signed 8-bit degrees (`-90` to `+90`°).
* **`GimbalPitch`**: Signed 8-bit degrees (`-45` to `+45`°).
* **`Flags`**: Bit 0 = Gyro ESP On/Off, Bit 1 = Unstuck active, Bits 3..6 = Gear level (1–8).
* **`CRC-8`**: Standard polynomial `0x07` checksum.

---

## 4. Firmwares Overview

### 4.1. `tx-ground-controller/` (Ground TX)
* **Role:** BLE NUS Peripheral (Server) + LoRa 868 MHz Uplink Transmitter.
* **BLE Device Name:** `RCCAR-GROUND-TX`
* **BLE Service UUID:** `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
* **Uplink:** Receives 8-byte binary control frames from the Android app over BLE NUS and immediately transmits them over 868 MHz LoRa at 50 Hz.
* **Downlink:** Listens for incoming rover telemetry frames over LoRa and relays them to the phone via BLE notifications.

### 4.2. `rx-pi-bridge/` (Rover RX)
* **Role:** LoRa 868 MHz Receiver + USB CDC-ACM Serial Bridge to Pi 4.
* **Connection to Pi:** Plugged into any USB port of the Raspberry Pi 4 with a USB-C cable (enumerates as `/dev/ttyACM0`).
* **Uplink:** Receives 868 MHz LoRa packets and writes raw binary frames directly to `Serial` at 115200 baud.
* **Downlink:** Reads telemetry bytes from `Serial` (from the Pi) and transmits them back over LoRa.

---

## 5. Flashing Instructions (PlatformIO)

Make sure you have [PlatformIO CLI](https://platformio.org/install/cli) installed (`brew install platformio` or via VS Code PlatformIO extension).

### 5.1. Flash Ground TX Module
1. Connect the **Ground TX XIAO ESP32-S3** to your computer via USB-C.
2. Build and upload:
   ```bash
   cd lora-firmware/tx-ground-controller
   pio run -t upload
   ```
3. (Optional) Open serial monitor:
   ```bash
   pio device monitor -b 115200
   ```

### 5.2. Flash Rover RX Module
1. Connect the **Rover RX XIAO ESP32-S3** to your computer via USB-C.
2. Build and upload:
   ```bash
   cd lora-firmware/rx-pi-bridge
   pio run -t upload
   ```
3. (Optional) Open serial monitor:
   ```bash
   pio device monitor -b 115200
   ```

---

## 6. Quick Start Guide

1. **Rover Setup:**
   * Plug the **Rover RX XIAO ESP32-S3** into a USB port on the Raspberry Pi 4.
   * Start the C firmware on the Pi:
     ```bash
     cd raspberry-pi-client/c
     cmake -S . -B build && cmake --build build
     sudo ./build/raspberrypiclient
     ```
   * The Pi will report: `[Serial] Connected successfully on /dev/ttyACM0` and `Reader thread active (Dual-Mode: Compact 8B + MAVLink)`.

2. **Ground TX Setup:**
   * Power on the **Ground TX XIAO ESP32-S3** (via USB power bank, phone OTG, or battery).
   * Its LED will blink slowly, indicating BLE advertising.

3. **Android Client Operation:**
   * Pair your **DualShock 4 / DualSense 5** controller to the Android phone via Bluetooth (or connect with a USB-C OTG cable).
   * Open the **RC Car Android App**.
   * Tap **Scan / Connect** to connect to `RCCAR-GROUND-TX`.
   * Once both Gamepad and LoRa TX are connected, Drive Mode is unlocked.
