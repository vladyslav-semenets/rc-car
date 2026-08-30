# Legacy Desktop C Client (SDL2 + MAVLink)

A lightweight C desktop ground controller using **SDL2** for joystick input and POSIX sockets for UDP MAVLink transmission.

---

## 1. Overview

* **Input**: Captures analog axis inputs and button presses from connected gamepads using the SDL2 GameController/Joystick subsystem.
* **Telemetry & Commands**: Packages driver stick inputs into standard MAVLink `COMMAND_LONG` packets and transmits them over UDP.
* **Heartbeat Thread**: Generates background MAVLink heartbeats every $100\text{ ms}$ to maintain the safety link with the Raspberry Pi.

---

## 2. Directory Structure

```
client/c/
├── main.c           # Entry point, SDL event loop, heartbeat thread
├── rc-car.c / .h    # MAVLink command builders and joystick mapping
├── udp.c / .h       # POSIX UDP socket sender
├── libs/            # Embedded MAVLink v2 headers & cJSON
└── CMakeLists.txt   # CMake configuration
```

---

## 3. Build & Run

### Prerequisites
On macOS (via Homebrew):
```bash
brew install sdl2 cmake
```

On Ubuntu/Debian:
```bash
sudo apt update
sudo apt install -y libsdl2-dev cmake build-essential
```

### Build & Execute
```bash
cd client/c
cmake -S . -B build
cmake --build build
./build/rccarclient
```
