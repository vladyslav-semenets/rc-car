# Raspberry Pi 4 Rover Firmware (C)

High-performance onboard C firmware for the Raspberry Pi 4 Model B. It manages PWM generation for dual ESCs, digital steering, 2-axis camera gimbal servos, MPU-6050 gyro counter-steering, and MediaMTX FPV video streaming.

---

## 1. Architecture

```
                       ┌────────────────────────────────┐
                       │           main.c               │
                       │  (GPIO Setup, Signals, Loops)  │
                       └───────┬────────────────┬───────┘
                               │                │
            ┌──────────────────┴──┐          ┌──┴──────────────────┐
            │      serial.c       │          │       udp.c         │
            │  (Dual-Mode Reader) │          │ (UDP Port 8565 Srv) │
            └──────────┬──────────┘          └──────────┬──────────┘
                       │                                │
                       ▼                                ▼
                 8-Byte Frame /                   MAVLink v1/v2
                 CRC-8 Checked                    Frame Ingestion
                       │                                │
                       └───────────────┬────────────────┘
                                       │
                                       ▼
                       ┌────────────────────────────────┐
                       │          rc-car.c              │
                       │  - Slew-rate Motor Thread      │
                       │  - Gyro ESP Correction Thread  │
                       │  - Unstuck Recovery Thread     │
                       └───────────────┬────────────────┘
                                       │
                                       ▼
                       ┌────────────────────────────────┐
                       │        pigpio Library          │
                       │  (Hardware DMA 50 Hz PWM)      │
                       └────────────────────────────────┘
```

---

## 2. Key Modules & Subsystems

### 2.1. Dual-Mode Input Ingestion (`serial.c` & `udp.c`)
* **LoRa Serial Bridge (`/dev/ttyACM0`)**:
  * Reads raw byte stream at 115200 baud.
  * Detects `0xAA` header and extracts 8-byte frames:
    `[0xAA, Seq, Throttle, Steering, GimbalYaw, GimbalPitch, Flags, CRC-8]`.
  * Verifies polynomial `0x07` CRC-8. On match, dispatches directly to `processCompactRcPacket()` with zero parsing overhead.
  * On non-matching bytes (e.g. standard MAVLink packets), falls back transparently to `mavlink_parse_char()`.
* **UDP Server (`udp.c`)**:
  * Listens on UDP port `8565` for direct Wi-Fi or 4G LTE MAVLink commands.

### 2.2. ESC Powertrain Protection & Slew Limiter (`rc-car.c`)
* **Slew Rate Ramp (`motorThread`)**:
  * Runs every $10\text{ ms}$ (`ESC_SLEW_INTERVAL_MS`).
  * Limits pulse width changes to $\le 25\text{ µs} / 10\text{ ms}$ ($2500\text{ µs/s}$), reaching full throttle in $\sim 200\text{ ms}$ to prevent gear stripping.
  * When decelerating to neutral ($1500\text{ µs}$), slews $3\times$ faster ($75\text{ µs} / 10\text{ ms}$) to ensure rapid, predictable stopping.
* **Direction Change Neutral Hold**:
  * When crossing $1500\text{ µs}$ from forward to backward, holds neutral for $150\text{ ms}$ (`ESC_DIR_CHANGE_HOLD_MS`) to protect the ESCs from destructive back-EMF spikes.
* **Rear-First Axle Sequencing Lag**:
  * Rear motor leads by $30\text{ ms}$ (`ESC_FRONT_LAG_STEPS = 3`), eliminating front torque steer during hard launches.
* **Hobbywing Reverse Arming**:
  * Automates the standard Hobbywing QuicRun brake-to-reverse double-tap pulse sequence ($250\text{ ms}$ brake $\rightarrow$ $120\text{ ms}$ neutral $\rightarrow$ reverse).

### 2.3. MPU-6050 Gyro ESP Counter-Steering
* **Vertical Heatsink Mounting**:
  * Because the sensor is mounted vertically on its side against the chassis heatsink, vehicle yaw rotation is read on the **$Y$-axis gyro register (`GYRO_YOUT_H = 0x45`)** instead of the standard Z-axis.
* **Low-Pass Filter**:
  * Exponential smoothing ($\alpha = 0.10$) eliminates mechanical vibration noise.
* **Zero-CPU Idle Sleep**:
  * When the driver actively turns the steering wheel ($|\text{angle} - \text{center}| > 2^\circ$), the gyro thread enters a clean $20\text{ ms}$ sleep (`usleep(20000)`), avoiding 100% CPU core lockup.

### 2.4. Self-Recovery Unstuck Routine
* Background thread executes a 3-cycle escalating forward/backward power rocking routine ($40\% \rightarrow 70\% \rightarrow 100\%$) to free stuck wheels without blocking telemetry.
* Any manual throttle command from the driver immediately cancels the sequence.

---

## 3. GPIO Pin Assignments

| GPIO Pin | Function | Pulse Width Range | Description |
| :--- | :--- | :--- | :--- |
| **GPIO 17** | Steering Servo | 500 – 2500 µs | Digital steering servo (center ~86°) |
| **GPIO 23** | Front Axle ESC | 1000 – 2000 µs | Front Hobbywing QuicRun 1625 ESC |
| **GPIO 25** | Rear Axle ESC | 1000 – 2000 µs | Rear Hobbywing QuicRun 1625 ESC |
| **GPIO 16** | ESC Power Relay | Digital Output | 0 = Power ON, 1 = Power OFF (failsafe) |
| **GPIO 27** | Gimbal Base Power | Digital Output | Camera gimbal power enable |
| **GPIO 22** | Gimbal Pitch Servo | 1000 – 2000 µs | Camera tilt up/down |
| **GPIO 24** | Gimbal Yaw Servo | 1000 – 2000 µs | Camera pan left/right |
| **I2C Bus 1** | MPU-6050 IMU | I2C `0x68` | Gyroscope / accelerometer |

---

## 4. Build & Run Instructions

### Prerequisites
On Raspberry Pi OS (Debian):
```bash
sudo apt update
sudo apt install -y build-essential cmake pigpio libpigpio-dev
sudo systemctl enable --now pigpiod
```

### Build
```bash
cd raspberry-pi-client/c
cmake -S . -B build
cmake --build build
```

### Run
```bash
# Run with root privileges required for pigpio DMA PWM
sudo ./build/raspberrypiclient
```

*(Note: On macOS, CMake automatically builds against `pigpio-mock.c` for desktop testing and simulation).*
