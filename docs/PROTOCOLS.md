# Communication Protocols Specification

This document provides the byte-level technical specification for both the **ExpressLRS-Style 8-Byte Binary RC Protocol** and the **Custom MAVLink v2 Command Protocol** used in the RC Car ecosystem.

---

## 1. High-Speed 8-Byte Binary RC Protocol (LoRa Primary)

### 1.1. Motivation & Airtime Math
Standard MAVLink `COMMAND_LONG` packets require 41 bytes. At 868.0 MHz with Spreading Factor 7 and Bandwidth 500 kHz, transmitting 41 bytes takes $\sim 21.8\text{ ms}$ of RF Time-on-Air (ToA).

The custom **8-Byte Binary Protocol** compresses all essential real-time driving channels into exactly 8 bytes, dropping LoRa airtime down to **5.5 ms** (a $4\times$ reduction) and enabling a steady **50 Hz** update rate with total latency under $28\text{ ms}$.

### 1.2. Packet Layout

| Byte Offset | Field Name | Type | Range / Units | Description |
| :---: | :--- | :--- | :--- | :--- |
| **0** | `sync` | `uint8_t` | `0xAA` | Frame synchronization magic byte |
| **1** | `seq` | `uint8_t` | `0 .. 255` | Rolling packet sequence counter |
| **2** | `throttle` | `int8_t` | `-100 .. +100` | Throttle percentage (negative = reverse, positive = forward) |
| **3** | `steering` | `uint8_t` | `0 .. 180` | Steering servo angle in degrees (center calibrated ~86°) |
| **4** | `gimbalYaw` | `int8_t` | `-90 .. +90` | Camera gimbal pan angle in degrees |
| **5** | `gimbalPitch`| `int8_t` | `-45 .. +45` | Camera gimbal tilt angle in degrees |
| **6** | `flags` | `uint8_t` | Bitmask | Auxiliary controls and transmission gear (see bitfield) |
| **7** | `crc` | `uint8_t` | `0x00 .. 0xFF` | CRC-8 checksum over bytes 0 through 6 |

### 1.3. Flags Bitmask Structure (Byte 6)

```
Bit 7      Bit 6      Bit 5      Bit 4      Bit 3      Bit 2      Bit 1      Bit 0
┌──────────┬─────────────────────────────────────────┬──────────┬──────────┬──────────┐
│ Reserved │           Transmission Gear             │  Camera  │ Unstuck  │ Gyro ESP │
│ (Unused) │               (1 .. 8)                  │  On/Off  │  Active  │  On/Off  │
└──────────┴─────────────────────────────────────────┴──────────┴──────────┴──────────┘
```

* **Bit 0 (`0x01`)**: `Gyro ESP` — 1 = MPU-6050 Active Stabilization ON, 0 = OFF.
* **Bit 1 (`0x02`)**: `Unstuck` — 1 = Trigger automated rocking recovery, 0 = Normal.
* **Bit 2 (`0x04`)**: `Camera` — 1 = MediaMTX stream enabled, 0 = Disabled.
* **Bits 3..6 (`0x78`)**: `Gear Level` — 4-bit unsigned integer (values `1` through `8`).
* **Bit 7 (`0x80`)**: Reserved for future telemetry flags.

### 1.4. CRC-8 Calculation Algorithm
Standard polynomial: $P(x) = x^8 + x^2 + x + 1$ (`0x07`), Initial Value: `0x00`.

```c
uint8_t computeCrc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if ((crc & 0x80) != 0) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}
```

---

## 2. MAVLink v2 Command Protocol (UDP & Parameter Config)

All MAVLink communications use `MAVLINK_MSG_ID_COMMAND_LONG` (Message ID 76).

### 2.1. Command Dictionary

| Command ID | Enum Name | Parameters | Description |
| :---: | :--- | :--- | :--- |
| **1** | `MAVLINK_INIT_COMMAND` | `param1=speed`, `param2=centerAngle` | Initializes ESCs, centers steering, arms camera gimbal |
| **2** | `MAVLINK_CHANGE_DEGREE_OF_TURNS` | `param1=degrees` | Sets runtime steering center angle |
| **3** | `MAVLINK_RESET_TURNS` | `param1=degrees` | Centers steering to specified degree |
| **4** | `MAVLINK_TURN_TO` | `param1=degrees (0..180)` | Sets steering servo angle |
| **5** | `MAVLINK_FORWARD` | `param1=speed (0..100)` | Drives forward at target speed percentage |
| **6** | `MAVLINK_BACKWARD` | `param1=speed (0..100)` | Drives reverse at target speed percentage |
| **7** | `MAVLINK_SET_ESC_TO_NEUTRAL` | — | Immediately sets ESCs to neutral ($1500\text{ µs}$) |
| **8** | `MAVLINK_START_CAMERA` | — | Forks and executes `mediamtx` daemon |
| **9** | `MAVLINK_STOP_CAMERA` | — | Sends `SIGTERM` to `mediamtx` process |
| **10** | `MAVLINK_CAMERA_GIMBAL_TURN_TO` | `param1=yawDegrees (-90..+90)` | Sets camera gimbal pan servo angle |
| **11** | `MAVLINK_CAMERA_GIMBAL_SET_PITCH` | `param1=pitchDegrees (-45..+45)` | Sets camera gimbal tilt servo angle |
| **12** | `MAVLINK_RESET_CAMERA_GIMBAL` | — | Centers camera gimbal pan and tilt |
| **13** | `MAVLINK_STEERING_CALIBRATION_ON` | — | Calibrates MPU-6050 and starts gyro ESP correction thread |
| **14** | `MAVLINK_STEERING_CALIBRATION_OFF` | — | Disables gyro thread and closes I2C handle |
| **15** | `MAVLINK_SET_MOTOR_CONFIG` | `p1=frontTrim`, `p2=rearTrim`, `p3=slewMax`, `p4=dirHold`, `p5=frontLag`, `p6=brakeMs`, `p7=neutralMs` | Configures powertrain slew, trim, and braking dynamics |
| **16** | `MAVLINK_UNSTUCK` | `param1=centerAngle` | Triggers 3-cycle automated rocking self-recovery |

---

## 3. End-to-End Latency Comparison

| Transport Mode | Packet Size | Uplink Medium | Airtime / Transit | Total End-to-End Latency |
| :--- | :---: | :---: | :---: | :---: |
| **LoRa Compact 8B Frame** | **8 Bytes** | **868.0 MHz LoRa (SX1262)** | **5.5 ms** | **~23 – 28 ms** |
| **LoRa MAVLink Frame** | 41 Bytes | 868.0 MHz LoRa (SX1262) | 21.8 ms | ~45 – 50 ms |
| **Direct Wi-Fi UDP** | 41 Bytes | 2.4 / 5 GHz 802.11 | ~2.0 ms | ~15 – 20 ms |
| **4G LTE Cellular (Tailscale)** | 41 Bytes | LTE Carrier Network | ~25 – 45 ms | ~45 – 70 ms |
