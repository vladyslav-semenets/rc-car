# GPS Telemetry & Live Map Interface (Angular + Mapbox GL)

A real-time GPS tracking and vehicle positioning map interface built with **Angular 19**, **Mapbox GL JS** (`ngx-mapbox-gl`), and **Capacitor 6**.

---

## 1. Features

* **Interactive Mapbox 2D/3D Mapping**: Vector tiles, satellite imagery, terrain layers, and smooth camera tracking.
* **Live Telemetry & Heading Visualization**: Renders the vehicle's real-time coordinates, orientation, and heading trajectory.
* **Integrated Gamepad & Controls**: Allows vehicle operation directly from the map interface using connected gamepads.
* **Capacitor Mobile Ready**: Packageable as a native Android or iOS application for field tracking.

---

## 2. Directory Structure

```
map/
├── src/
│   ├── app/
│   │   ├── app.component.ts   # Mapbox map container, markers, and layers
│   │   ├── app.service.ts     # Telemetry WebSocket listener & IP configuration
│   │   ├── app.routes.ts      # Application routes
│   │   └── app.config.ts      # Application config
│   ├── assets/                # Controller layouts and assets
│   └── main.ts                # Bootstrap file
├── angular.json               # Angular workspace configuration
├── capacitor.config.ts        # Capacitor configuration
└── package.json               # Dependencies and scripts
```

---

## 3. Getting Started

### 3.1. Prerequisites
* Node.js 18+ or 20+
* Yarn or npm
* Mapbox Access Token (from [mapbox.com](https://www.mapbox.com/))

### 3.2. Configuration
Copy `.env.example` to `.env.local`:
```bash
cp .env.example .env.local
```
Set your Mapbox token and Raspberry Pi IP in `.env.local`:
```env
NG_APP_MAPBOX_ACCESS_TOKEN="pk.your_mapbox_token_here"
NG_APP_RASPBERRY_PI_IP="192.168.1.100"
```

### 3.3. Run Development Server
```bash
yarn install
yarn start
```
Navigate to `http://localhost:4200` to view the live map.
