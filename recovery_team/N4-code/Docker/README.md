# Nakuja N4 Codebase Overview

This repository contains the full software stack for the Nakuja N4 rocket, consisting of two main components: the ground station (Basestation) and the flight computer (Flight Software).

---

## Directory Structure

```
N4-code/
├── N4-Basestation/        # Ground station dashboard
│   ├── src/               # React frontend
│   ├── server.py          # Python Flask backend
│   ├── mosquitto.conf     # MQTT broker configuration
│   └── package.json
├── Docker/                # Docker Compose setup
│   ├── docker-compose.yml
│   ├── Dockerfile.backend
│   ├── Dockerfile.frontend
│   ├── nginx.conf
│   ├── start.sh           # Startup script (macOS/Linux)
│   ├── start.bat          # Startup script (Windows CMD)
│   └── start.ps1          # Startup script (Windows PowerShell)
└── N4-Flight-Software/    # Flight computer firmware
    └── n4-flight-software/ # PlatformIO project
```

---

## N4-Basestation (Ground Station)

### Overview

A web dashboard for real-time visualization of rocket telemetry data and pre-launch configuration.

### System Architecture

```
Browser
  │
  ├─── HTTP ──► Nginx (port 8080)      ← React dashboard
  ├─── HTTP ──► Flask (port 5001)      ← Serial data API
  └─── WS ────► Mosquitto (port 1783)  ← MQTT telemetry
                    │
                    └─── TCP (port 1883) ← Flight computer connection
```

### Component Roles

| Component | Technology | Role |
|---|---|---|
| **Frontend** | React + Vite + Tailwind CSS | Telemetry display, charts, map, arming controls |
| **Backend** | Python Flask | Reads data from ESP/Arduino serial port |
| **MQTT Broker** | Mosquitto | Messaging between flight computer and dashboard |

### MQTT Topics

| Topic | Direction | Content |
|---|---|---|
| `n4/flight-computer-1` | Subscribe | Telemetry data (JSON or CSV) |
| `n4/logs` | Subscribe | Log messages |
| `n4/commands` | Publish | `ARM` / `DISARM` commands |

### Port Reference

| Service | Port | Protocol |
|---|---|---|
| Dashboard (Nginx) | 8080 | HTTP |
| Flask backend | 5001 | HTTP |
| MQTT TCP | 1883 | TCP |
| MQTT WebSocket | 1783 | WebSocket |

---

## Running with Docker (Recommended)

Docker Compose starts the frontend, backend, and MQTT broker with a single command.

### Prerequisites

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) installed and running

### Steps

1. Navigate to the `Docker/` directory.

2. Run the startup script.

   **macOS / Linux:**
   ```bash
   ./start.sh
   ```

   **Windows (Command Prompt):**
   ```bat
   start.bat
   ```

   **Windows (PowerShell):**
   ```powershell
   .\start.ps1
   ```

   On the first run, a `.env` file is created automatically and the Docker images are built.

3. Once the build completes, open `http://localhost:8080` in your browser.

### Configuring .env

Customize the environment by editing the `.env` file.

```env
# MQTT host as seen by the browser (change to server IP for remote deployments)
VITE_MQTT_HOST=localhost
VITE_WS_PORT=1783

# Flask backend URL as seen by the browser
VITE_BACKEND_URL=http://localhost:5001

# Video stream URL (e.g. 192.168.1.10:8554) — leave empty to disable
VITE_VIDEO_URL=

# Host port mappings
FRONTEND_PORT=8080
BACKEND_PORT=5001
MQTT_TCP_PORT=1883
MQTT_WS_PORT=1783

# Serial port — only required when hardware is connected
# Linux example:  SERIAL_PORT=/dev/ttyUSB0
# macOS example:  SERIAL_PORT=/dev/cu.usbserial-XXXX
SERIAL_PORT=
```

> **Note:** `VITE_*` variables are baked into the static build at image-build time. After changing them, rebuild with `docker compose up --build -d`.

### Useful Commands

```bash
# Stream logs from all services
docker compose logs -f

# Stream logs from a single service
docker compose logs -f frontend

# Stop and remove all containers
docker compose down

# Rebuild and restart after a config change
docker compose up --build -d
```

---

## Running Locally (Without Docker)

To run each service individually without Docker:

### Prerequisites

- Node.js / npm
- Python 3.x
- Mosquitto

### Steps

**1. Start Mosquitto (MQTT broker):**
```bash
mosquitto -c mosquitto.conf
```

**2. Create a Python virtual environment and install dependencies:**
```bash
python3 -m venv venv_local
source venv_local/bin/activate       # macOS/Linux
# venv_local\Scripts\activate        # Windows
pip install flask flask-cors pyserial
```

**3. Start the Flask backend:**
```bash
python server.py
# → Running at http://localhost:5001
```

**4. Start the frontend:**
```bash
npm install
npm run dev
# → Running at http://localhost:5173
```

---

## Telemetry Data Format

### JSON Format

```json
{
  "state": 0,
  "operation_mode": 0,
  "gps_data": {
    "latitude": -1.1,
    "longitude": 37.01,
    "gps_altitude": 0
  },
  "alt_data": {
    "pressure": 101325,
    "temperature": 25,
    "AGL": 0,
    "velocity": 0
  },
  "acc_data": {
    "ax": 0,
    "ay": 0,
    "az": 9.8
  },
  "chute_state": {
    "pyro1_state": 0,
    "pyro2_state": 0
  },
  "battery_voltage": 12.0
}
```

### Flight States

| Value | State |
|---|---|
| 0 | Pre-Flight |
| 1 | Powered Flight (ascending) |
| 2 | Apogee |
| 3 | Drogue Deployed |
| 4 | Main Deployed |
| 5 | Rocket Descent |
| 6 | Post Flight (landed) |

---

## N4-Flight-Software (Flight Computer)

### Overview

Firmware for the ESP32-based onboard rocket computer, built with PlatformIO.

### Key Features

- Acceleration and velocity calculation with filtering
- Altitude above ground level (AGL) from barometric sensor
- Automatic flight state transitions
- Data logging to flash memory
- GPS position acquisition
- Telemetry transmission to ground station via MQTT
- ARM/DISARM command reception from ground station

### Build and Flash

```bash
cd N4-Flight-Software/n4-flight-software
# Build and flash using PlatformIO CLI or the VS Code PlatformIO extension
pio run --target upload
```

See [`N4-Flight-Software/README.md`](N4-Flight-Software/README.md) for details.

---

## Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| Dashboard does not open | Ensure Docker Desktop is running. If port 8080 is already in use, change `FRONTEND_PORT` in `.env` and rebuild. |
| Cannot connect to MQTT | Check that `VITE_MQTT_HOST` in `.env` is correct. For remote deployments, set it to the server's IP address. |
| No serial data received | Verify that `SERIAL_PORT` is set to the correct device path. On Linux, uncomment the `devices` section in `docker-compose.yml`. |
| macOS port 5000 conflict | macOS AirPlay occupies port 5000. This project uses port 5001 to avoid the conflict. |
| Frontend build fails | Run `npm install` to reinstall dependencies, then rebuild with `docker compose up --build -d`. |
