# N4 Flight Computer Firmware

Firmware for the **N4 rocket flight computer** built on an ESP32 DevKit.  
Manages sensor acquisition, state machine, ejection control, data logging, and multi-mode telemetry.

---

## Quick Navigation

| I want to… | Go to |
|------------|-------|
| Build & flash the firmware | [QUICKSTART.md](QUICKSTART.md) |
| Understand the communication system | [docs/COMMUNICATION_ARCHITECTURE.md](docs/COMMUNICATION_ARCHITECTURE.md) |
| Set up XBee radio | [docs/XBEE_INTEGRATION.md](docs/XBEE_INTEGRATION.md) |
| Set up beacon mode | [docs/BEACON_CONFIGURATION.md](docs/BEACON_CONFIGURATION.md) |
| Set up MQTT / WiFi | [docs/mqtt-setup.md](docs/mqtt-setup.md) |
| Configure base station WiFi | [docs/WiFiManager_BaseStation_Setup.md](docs/WiFiManager_BaseStation_Setup.md) |
| Understand pyro/ejection system | [docs/PYRO_CONTROL_SYSTEM.md](docs/PYRO_CONTROL_SYSTEM.md) |
| Understand data logging | [docs/LOGGER_IMPROVEMENTS.md](docs/LOGGER_IMPROVEMENTS.md) |
| Debug RSSI values | [fixes/BEACON_RSSI_DEBUG_GUIDE.md](fixes/BEACON_RSSI_DEBUG_GUIDE.md) |
| Review PWM commands | [docs/PWM_CONFIG_COMMANDS.md](docs/PWM_CONFIG_COMMANDS.md) |

---

## Project Layout

```
n4-flight-software/
│
├── src/                          # Flight computer source files
│   ├── main.cpp                  # Entry point, task creation
│   ├── states.cpp / states.h     # Flight state machine (9 states)
│   ├── communication_manager.cpp # Mode switching logic
│   ├── kalman_filter.cpp/h       # Kalman filter for altitude & velocity
│   ├── logger.cpp/h              # Data logging coordinator
│   ├── sd_logger.cpp/h           # SD card CSV logging
│   ├── system_logger.cpp/h       # Event / system log
│   ├── mpu.cpp/h                 # MPU6050 IMU driver
│   ├── espnow_beacon_transmitter.cpp/h   # Beacon / ESP-NOW transmitter
│   ├── wifi-config.cpp/h         # WiFiManager wrapper
│   ├── ring_buffer.cpp/h         # Lock-free ring buffer
│   └── data_types.h              # Shared data structures
│
├── include/                      # Header-only and shared headers
│   ├── defs.h                    # Pin assignments, compile flags, constants
│   ├── communication_manager.h   # CommunicationManager class
│   ├── state_machine.h/cpp       # State machine definitions
│   └── comm_utils.h              # Communication utilities
│
├── lib/
│   └── CustomSerialFlash/        # External flash memory driver
│
├── docs/                         # Topic guides and architecture docs
│   ├── README.md                 # Docs index
│   ├── COMMUNICATION_ARCHITECTURE.md
│   ├── BEACON_CONFIGURATION.md
│   ├── XBEE_INTEGRATION.md
│   ├── RSSI_TELEMETRY_INTEGRATION.md
│   ├── PYRO_CONTROL_SYSTEM.md
│   ├── PWM_CONFIG_COMMANDS.md
│   ├── WiFiManager_BaseStation_Setup.md
│   ├── LOGGER_IMPROVEMENTS.md
│   ├── beacon-setup.md
│   ├── mqtt-setup.md
│   └── logging.md
│
├── fixes/                        # Bug fix logs and changelogs
│   ├── README.md
│   ├── BEACON_RSSI_DEBUG_GUIDE.md
│   ├── BEACON_RSSI_UPDATE_COMPLETE.md
│   ├── XBEE_MODE_SWITCHING_FIX.md
│   ├── AUTO_SWITCHING_FIXES.md
│   ├── COMMUNICATION_MANAGER_FIX.md
│   ├── DATA_STREAM_FIXES.md
│   ├── PERFORMANCE_FIXES_REPORT.md
│   ├── PWM_DURATION_UPDATE_SUMMARY.md
│   └── SYSTEM_STATUS_FINAL.md
│
├── test/                         # Unit tests and test rigs
├── scripts/                      # Python data analysis scripts
├── log-data/                     # Sample flight logs and plots
│
├── base_station_xbee_fixed.cpp   # ★ Current base station firmware (XBee+BT)
├── platformio.ini                # PlatformIO build configuration
├── QUICKSTART.md                 # ★ Start here
└── README.md                     # This file
```

---

## Hardware Summary

| Component | Model / Notes |
|-----------|--------------|
| Microcontroller | ESP32 DevKit (38-pin) |
| IMU | MPU6050 (I2C, address 0x68) |
| Barometer | BMP280 |
| GPS | UART module, 9600 baud (pins 16/17) |
| Radio 1 | XBee Pro 900HP — 900 MHz UART (pins 32/34) |
| Radio 2 | ESP32 internal WiFi — Beacon / MQTT |
| SD card | SPI, CS on GPIO 26 |
| External flash | SPI (CustomSerialFlash library) |
| Drogue pyro | GPIO 25, PWM channel 3 |
| Main chute pyro | GPIO 12, PWM channel 4 |
| Supply voltage | ~15 V LiPo |

---

## Communication Modes

| Mode | Range | Transport | Use Case |
|------|-------|-----------|----------|
| MQTT | ~100 m | WiFi 2.4 GHz | Pad ops, pre-flight |
| Beacon | ~4 km | Raw 802.11 + ESP-NOW | Short/medium flights |
| XBee | 1–30 km | 900 MHz UART | Long-range flights |
| Triple | best | All three | Maximum redundancy |

Switch modes at runtime with serial commands: `XBEE_MODE`, `BEACON_MODE`, `MQTT_MODE`, `TRIPLE_MODE`.

---

## Flight State Machine

```
PRE_FLIGHT_GROUND (0)
    │ launch detected (>10 m rise)
    ▼
POWERED_FLIGHT (1)
    │ motor burnout / decel
    ▼
COASTING (2)
    │ apogee detected (velocity reversal)
    ▼
APOGEE (3)
    │ +1500 ms delay
    ▼
DROGUE_DEPLOY (4)  ──── fires GPIO 25 (5 s PWM)
    ▼
DROGUE_DESCENT (5)
    │ altitude < 500 m AGL
    ▼
MAIN_DEPLOY (6)  ──── fires GPIO 12 (5 s PWM)
    ▼
MAIN_DESCENT (7)
    │ near-zero velocity
    ▼
POST_FLIGHT_GROUND (8)
```

---

## Key Configuration Flags (`include/defs.h`)

| Flag | Default | Description |
|------|---------|-------------|
| `MQTT` | 0 | Enable MQTT mode at boot |
| `XBEE` | 1 | Enable XBee mode at boot |
| `TEST` | 1 | Transmit when disarmed |
| `DEBUGGING` | 1 | Enable Serial.print output |
| `DEBUG_TO_TERMINAL` | 1 | Enable debug task |
| `LOG_TO_MEMORY` | 0 | Flash logging (set 1 for flight) |
| `ENABLE_SD_LOGGING` | 1 | SD card logging |
| `USE_SIMULATION` | 0 | Simulated sensor data |
| `USE_KALMAN_FOR_STATE_DETECTION` | 1 | Use Kalman for state transitions |

> Set `DEBUGGING 0`, `DEBUG_TO_TERMINAL 0`, `LOG_TO_MEMORY 1`, `TEST 0` before flight.

---

## Base Station Firmware

The active base station file is **`base_station_xbee_fixed.cpp`** in this directory.

| UART | Pins | Device |
|------|------|--------|
| UART0 | USB | Debug + Python server |
| UART1 | 16/17 | Bluetooth HC-05/06 |
| UART2 | 32/34 | XBee Pro 900HP |

Both Serial (USB) and Bluetooth mirror all telemetry JSON and log messages.

---

## Documentation Index

### Getting Started
- [QUICKSTART.md](QUICKSTART.md) — build, flash, first test, pre-flight checklist

### Topic Guides (`docs/`)
- [docs/COMMUNICATION_ARCHITECTURE.md](docs/COMMUNICATION_ARCHITECTURE.md) — full comms system design, UART assignments, FreeRTOS tasks
- [docs/BEACON_CONFIGURATION.md](docs/BEACON_CONFIGURATION.md) — beacon mode, 4 km range, antennas, RSSI
- [docs/XBEE_INTEGRATION.md](docs/XBEE_INTEGRATION.md) — XBee wiring, XCTU settings (AP=0, BD=7), CSV format
- [docs/RSSI_TELEMETRY_INTEGRATION.md](docs/RSSI_TELEMETRY_INTEGRATION.md) — RSSI in telemetry
- [docs/PYRO_CONTROL_SYSTEM.md](docs/PYRO_CONTROL_SYSTEM.md) — ejection charge PWM control
- [docs/PWM_CONFIG_COMMANDS.md](docs/PWM_CONFIG_COMMANDS.md) — PWM runtime commands
- [docs/LOGGER_IMPROVEMENTS.md](docs/LOGGER_IMPROVEMENTS.md) — logging improvements
- [docs/WiFiManager_BaseStation_Setup.md](docs/WiFiManager_BaseStation_Setup.md) — base station WiFi config
- [docs/beacon-setup.md](docs/beacon-setup.md) — beacon hardware setup
- [docs/mqtt-setup.md](docs/mqtt-setup.md) — MQTT broker setup
- [docs/logging.md](docs/logging.md) — logging system guide
- [docs/README.md](docs/README.md) — docs folder index

### Fix Logs & Changelogs (`fixes/`)
- [fixes/BEACON_RSSI_DEBUG_GUIDE.md](fixes/BEACON_RSSI_DEBUG_GUIDE.md) — RSSI troubleshooting
- [fixes/BEACON_RSSI_UPDATE_COMPLETE.md](fixes/BEACON_RSSI_UPDATE_COMPLETE.md) — RSSI update changelog
- [fixes/XBEE_MODE_SWITCHING_FIX.md](fixes/XBEE_MODE_SWITCHING_FIX.md) — mode switching bug fix log
- [fixes/AUTO_SWITCHING_FIXES.md](fixes/AUTO_SWITCHING_FIXES.md) — auto mode-switching fixes
- [fixes/COMMUNICATION_MANAGER_FIX.md](fixes/COMMUNICATION_MANAGER_FIX.md) — comm manager fixes
- [fixes/DATA_STREAM_FIXES.md](fixes/DATA_STREAM_FIXES.md) — data stream bug fixes
- [fixes/PERFORMANCE_FIXES_REPORT.md](fixes/PERFORMANCE_FIXES_REPORT.md) — performance improvements
- [fixes/PWM_DURATION_UPDATE_SUMMARY.md](fixes/PWM_DURATION_UPDATE_SUMMARY.md) — pyro timing update
- [fixes/SYSTEM_STATUS_FINAL.md](fixes/SYSTEM_STATUS_FINAL.md) — system status report
- [fixes/README.md](fixes/README.md) — fixes folder index

---

## Scripts & Data

| Path | Purpose |
|------|---------|
| `scripts/data-logger.py` | Serial data logger — captures CSV from USB |
| `scripts/graph.py` | Plot telemetry fields from CSV |
| `scripts/apogee-check.py` | Post-flight apogee analysis |
| `log-data/` | Sample flight logs and MATLAB/Python plots |
