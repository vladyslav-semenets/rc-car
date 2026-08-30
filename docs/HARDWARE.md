# Hardware Architecture, Pinouts & Wiring Guide

This document specifies the electronic pinouts, power wiring, sensor orientation, and RF transceiver configurations for the Long-Range RC Car System.

---

## 1. Raspberry Pi 4 Model B GPIO Pinout

The Raspberry Pi 4 uses the **pigpio** library for hardware DMA-timed PWM generation at 50 Hz.

```
                         Raspberry Pi 4 Header
                             ┌─────────────┐
                    3.3V (1) │  ●       ●  │ (2)  5.0V (Relay Power)
                 I2C1_SDA (3) │  ●       ●  │ (4)  5.0V (Relay Power)
                 I2C1_SCL (5) │  ●       ●  │ (6)  GND
                              │  ●       ●  │ (8)  TXD
                          GND │  ●       ●  │ (10) RXD
        CAR_TURNS_SERVO (11) │  ● (17)  ●  │ (12) (18)
                              │  ●       ●  │ (14) GND
   CAR_CAMERA_GIMBAL_P3 (15) │  ● (22)  ●  │ (16) (23) CAR_ESC_FRONT
                              │  ●       ●  │ (18) (24) CAR_CAMERA_GIMBAL_P4
         CAR_ESC_REAR   (19) │  ● (25)  ●  │ (20) GND
                              │  ●       ●  │ (22)
                              │  ●       ●  │ (24)
                              │  ●       ●  │ (26)
   CAR_CAMERA_GIMBAL_P1 (27) │  ● (27)  ●  │ (28)
                              │  ●       ●  │ (30) GND
                              │  ●       ●  │ (32)
                              │  ●       ●  │ (34) GND
                              │  ● (16)  ●  │ (36)
                              │  ●       ●  │ (38)
                          GND │  ●       ●  │ (40)
                             └─────────────┘
```

### 1.1. Pin Mapping Table

| GPIO Number | Physical Pin | Function / Target | Signal Type | Range / Voltage |
| :--- | :---: | :--- | :--- | :--- |
| **GPIO 17** | Pin 11 | Steering Servo Signal | PWM (50 Hz) | 500 – 2500 µs (3.3V) |
| **GPIO 23** | Pin 16 | Front Axle ESC Signal | PWM (50 Hz) | 1000 – 2000 µs (3.3V) |
| **GPIO 25** | Pin 19 | Rear Axle ESC Signal | PWM (50 Hz) | 1000 – 2000 µs (3.3V) |
| **GPIO 16** | Pin 36 | ESC Power Relay Line | Digital Output | 0 = Active / ON, 1 = OFF |
| **GPIO 27** | Pin 27 | Gimbal Base Power | Digital Output | 0 / 1 (3.3V) |
| **GPIO 22** | Pin 15 | Gimbal Pitch Servo | PWM (50 Hz) | 1000 – 2000 µs (3.3V) |
| **GPIO 24** | Pin 18 | Gimbal Yaw Servo | PWM (50 Hz) | 1000 – 2000 µs (3.3V) |
| **GPIO 2 (SDA)** | Pin 3 | MPU-6050 I2C Data | I2C Bus 1 | 3.3V Pull-Up |
| **GPIO 3 (SCL)** | Pin 5 | MPU-6050 I2C Clock | I2C Bus 1 | 3.3V Pull-Up |

---

## 2. MPU-6050 Gyroscope Mounting & Orientation

### 2.1. Physical Mounting Configuration
* The MPU-6050 breakout board is mounted **vertically on its side** screwed against the black chassis tower heatsink.
* The 8-pin header (`VCC, GND, SCL, SDA, XDA, XCL, AD0, INT`) runs vertically along the left edge.
* The printed text `MPU-6050` runs vertically.

### 2.2. Axis Mathematical Alignment
* In standard horizontal mounting, yaw rotation is on the $Z$-axis (`0x47`).
* In this **vertical mount**, horizontal chassis yaw rotation corresponds directly to the **$Y$-axis gyro (`GYRO_YOUT_H = 0x45`)**.
* Sensitivity: $131.0\text{ LSB} / (^\circ/\text{s})$ at full-scale range $\pm 250^\circ/\text{s}$.

```
Standard Horizontal:             Vertical Heatsink Mount (Current):
   Chassis Yaw = Z-Axis             Chassis Yaw = Y-Axis (Register 0x45)
```

---

## 3. LoRa Modules: Seeed XIAO ESP32-S3 + Wio-SX1262

Both **Ground TX** and **Rover RX** modules use identical pinouts matching the Seeed board-to-board (B2B) pin mapping.

### 3.1. B2B Pinout Table

| Function | Pin Name | XIAO Pin | ESP32-S3 GPIO | Description |
| :--- | :--- | :--- | :--- | :--- |
| **SPI Chip Select** | `NSS` | `D4` | `GPIO 41` | Active-low SPI select |
| **Interrupt / IRQ** | `DIO1`| `D1` | `GPIO 39` | Packet RX/TX completion interrupt |
| **Hardware Reset** | `NRST`| `D2` | `GPIO 42` | Active-low radio reset |
| **Busy Status** | `BUSY`| `D3` | `GPIO 40` | Radio busy line |
| **RF Switch Control**| `RF_SW`| `D5` | `GPIO 38` | Controls internal RF path switch |
| **SPI Clock** | `SCK` | `D8` | `GPIO 7` | Hardware SPI Clock |
| **SPI MISO** | `MISO`| `D9` | `GPIO 8` | Hardware SPI Data In |
| **SPI MOSI** | `MOSI`| `D10`| `GPIO 9` | Hardware SPI Data Out |
| **Built-in LED** | `LED` | `LED_BUILTIN` | `GPIO 21` | Active LOW status LED |

### 3.2. RF Configuration
* **Frequency**: 868.0 MHz (EU ISM Band).
* **Bandwidth**: 500.0 kHz.
* **Spreading Factor**: SF7.
* **Coding Rate**: 4/5.
* **Output Power**: +22 dBm (SX1262 High-Power PA).
* **Sync Word**: `0x12` (Private LoRa Network).
* **Preamble Length**: 12 symbols.

> [!WARNING]
> Never apply power to the SX1262 without an **868 MHz tuned antenna** connected to the U.FL / IPEX port. Operating without an antenna will damage the RF power amplifier.

---

## 4. Power & Relay Isolation System

```
[ Main LiPo Battery (7.4V - 11.1V) ]
          │
          ├───► [ 5V 5A High-Efficiency BEC / Buck Converter ]
          │            │
          │            ├───► Raspberry Pi 4 (USB-C 5V Input)
          │            ├───► Servos (Steering & Gimbal Power Rails)
          │            └───► Rover RX LoRa Module
          │
          └───► [ High-Current Relay Module (GPIO 16) ]
                       │
                       └───► [ Dual Hobbywing QuicRun 1625 ESCs ]
                                    │
                                    ├───► Front Axle Motor
                                    └───► Rear Axle Motor
```

* **Relay Safety Feature**: The Raspberry Pi keeps GPIO 16 LOW (`0`) to enable ESC battery power. On software exit, crash, or `SIGINT`, GPIO 16 is pulled HIGH (`1`), instantly cutting power to both ESCs to prevent accidental runaway.
