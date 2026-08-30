# Hybrid Web & Mobile Client (Angular + Capacitor)

A cross-platform web and mobile ground control interface built with **Angular 19** and **Capacitor 6**. It allows vehicle control and live FPV video monitoring from any modern desktop browser, tablet, or Android device.

---

## 1. Features

* **Angular 19 Standalone Architecture**: High-performance UI with Angular Material components.
* **HTML5 Gamepad API Support**: Integrates `gamecontroller.js` for browser-based gamepad reading (DualShock 4, Xbox controllers).
* **Real-Time WebSocket Link**: Bidirectional communication with the car's WebSocket server over port `8585`.
* **Integrated WebRTC Video**: Direct low-latency FPV video rendering from MediaMTX.
* **Capacitor 6 Android Support**: Native Android wrapper allowing the Angular application to be installed and run as an Android APK.

---

## 2. Directory Structure

```
client/javascript/
├── src/
│   ├── app/
│   │   ├── app.component.ts   # Main controller view & gamepad listeners
│   │   ├── app.service.ts     # WebSocket connection & MediaMTX endpoint provider
│   │   ├── app.routes.ts      # Angular routing configuration
│   │   └── app.config.ts      # Application providers
│   ├── assets/                # Controller UI layouts and scripts
│   └── main.ts                # Application bootstrap
├── android/                   # Capacitor Android wrapper project
├── angular.json               # Angular workspace configuration
├── capacitor.config.ts        # Capacitor mobile configuration
└── package.json               # Dependencies and build scripts
```

---

## 3. Getting Started

### 3.1. Prerequisites
* Node.js 18+ or 20+
* Yarn or npm

### 3.2. Configuration
Copy `.env.example` to `.env.local` and set your Raspberry Pi's IP address:
```bash
cp .env.example .env.local
```
Inside `.env.local`:
```env
NG_APP_RASPBERRY_PI_IP="192.168.1.100"
```

### 3.3. Run in Web Browser
```bash
yarn install
yarn start
```
Open `http://localhost:4200` in your browser.

### 3.4. Build for Android (Capacitor)
```bash
yarn build
npx cap sync android
npx cap open android
```
This opens the project in Android Studio for native APK compilation and device deployment.
