# N4 Flight Computer — Quick Start Guide

Get the N4 flight computer firmware compiled and running in under 15 minutes.

---

## Prerequisites

| Requirement | Version / Notes |
|-------------|-----------------|
| [PlatformIO IDE](https://platformio.org/) | VS Code extension or CLI |
| [VS Code](https://code.visualstudio.com/) | Recommended IDE |
| Python | 3.8+ (for data-logging scripts) |
| USB–UART driver | CP210x or CH340 depending on your board |
| ESP32 DevKit board | 38-pin variant |

---

## 1. Clone / Open the Project

Open the `n4-flight-software/` folder as your PlatformIO project root.  
The `platformio.ini` at that level is the authoritative build file.

```
n4-flight-software/
├── platformio.ini   ← project root
├── src/
├── include/
└── lib/
```

---

## 2. Configure Communication Mode

Edit `include/defs.h` before building:

```cpp
// === Compile-time defaults ===
#define MQTT  0   // 1 = default to MQTT, 0 = skip MQTT at boot
#define XBEE  1   // 1 = create XBee task and default to XBee mode
#define TEST  1   // 1 = transmit even when disarmed (bench testing)
```

> **XBee mode** is the recommended default for field use.  
> **MQTT mode** requires a WiFi broker reachable at the configured IP.

All three modes can be switched at runtime via serial commands — see [COMMUNICATION_ARCHITECTURE.md](docs/COMMUNICATION_ARCHITECTURE.md).

---

## 3. Set Pin Assignments (verify hardware)

Key pins defined in `include/defs.h`:

| Signal | GPIO | Notes |
|--------|------|-------|
| XBee RX | 34 | ESP32 RX ← XBee DOUT |
| XBee TX | 32 | ESP32 TX → XBee DIN |
| GPS TX  | 17 | UART2 |
| GPS RX  | 16 | UART2 |
| Drogue pyro | 25 | PWM channel 3 |
| Main pyro   | 12 | PWM channel 4 |
| SD CS   | 26 | |
| Green LED | 15 | Status indicator |
| Red LED   | 4  | Error indicator |
| Buzzer  | 33 | Audio alert |
| Remote arm switch | 27 | Pull-up input |
| Test mode pin | 14 | Set HIGH for TEST |
| Run mode pin  | 13 | Set HIGH for RUN |

Full pin documentation: [src/pin_assignment.MD](src/pin_assignment.MD)

---

## 4. MAC Address Configuration

Update the MAC addresses in `include/defs.h` to match your **actual hardware** before flight:

```cpp
// Replace with real MAC addresses from your ESP32s
static const uint8_t ROCKET_MAC[6] = {0x08, 0xD1, 0xF9, 0x15, 0x9C, 0x04};
static const uint8_t BASE_MAC[6]   = {0x10, 0x06, 0x1c, 0xa6, 0x11, 0xf0};
```

Read your ESP32 MAC address with:
```cpp
WiFi.macAddress()  // returns AA:BB:CC:DD:EE:FF
```

---

## 5. Build the Firmware

### PlatformIO CLI
```bash
cd n4-flight-software
pio run
```

### PlatformIO VS Code
- Click the **Build** (✓) button in the bottom status bar, or
- Run `PlatformIO: Build` from the command palette.

Expected output:
```
[SUCCESS] Took N.NN seconds
```

---

## 6. Flash the Firmware

```bash
pio run --target upload
```

Or click the **Upload** (→) button in VS Code.

> Hold the **BOOT** button on the ESP32 if upload fails on first attempt.

---

## 7. Monitor Serial Output

```bash
pio device monitor --baud 115200
```

Expected boot sequence:
```
[BOOT] N4 Flight Computer Starting...
[COMM] XBee mode active (UART1, 115200 baud)
[SENSORS] MPU calibration: 200 readings...
[TASKS] All tasks created OK
```

---

## 8. First Test — Bench Verification

With `TEST 1` and `XBEE 1` set in `defs.h`:

1. Open Serial Monitor at **115200 baud**
2. You should see CSV lines every ~100 ms:
   ```
   1250,1,2,0.15,0.02,9.81,2.5,1.3,0.01,0.02,-0.01,...
   ```
3. Send a mode query via serial:
   ```
   GET_MODE
   ```
4. Expected response:
   ```
   [COMM STATUS] Mode: XBEE_ONLY | MQTT: OFF | Beacon: OFF | XBee: ON
   ```

---

## 9. Ground Station Setup

Flash the base station firmware to a second ESP32:

- **XBee + Bluetooth**: `base_station_xbee_fixed.cpp` — use this for current field setup
- **MQTT only**: see [docs/mqtt-setup.md](docs/mqtt-setup.md)
- **Beacon only**: see [docs/beacon-setup.md](docs/beacon-setup.md)

WiFiManager setup for base station: [WiFiManager_BaseStation_Setup.md](docs/WiFiManager_BaseStation_Setup.md)

---

## 10. Pre-Flight Checklist

- [ ] `DEBUGGING 0` — disable serial printing for speed  
- [ ] `DEBUG_TO_TERMINAL 0` — disable terminal task  
- [ ] `LOG_TO_MEMORY 1` — enable flash logging  
- [ ] `ENABLE_SD_LOGGING 1` — enable SD card  
- [ ] `TEST 0` — disable test mode (requires arming to transmit)  
- [ ] `USE_SIMULATION 0` — use real sensors  
- [ ] MAC addresses match physical hardware  
- [ ] XBee baud rate matches ground station (both 115200)  
- [ ] XBee AP=0 (Transparent mode) confirmed in XCTU  
- [ ] Base altitude (`BASE_ALTITUDE`) set to launch site AGL  
- [ ] SD card inserted and formatted FAT32  
- [ ] Battery voltage > 12 V  
- [ ] Arm switch verified functional  
- [ ] Both pyro channels tested with continuity meter (NOT igniter installed)

---

## 11. Runtime Commands

Send via Serial, MQTT, or ESP-NOW:

| Command | Effect |
|---------|--------|
| `ARM` | Arm flight computer (requires altitude > 50 m AGL) |
| `DISARM` | Disarm flight computer |
| `MQTT_MODE` | Switch to MQTT only |
| `BEACON_MODE` | Switch to Beacon only |
| `XBEE_MODE` | Switch to XBee only |
| `DUAL_MODE` | MQTT + Beacon |
| `TRIPLE_MODE` | MQTT + Beacon + XBee |
| `AUTO_FALLBACK_ON` | Enable MQTT → Beacon fallback |
| `AUTO_FALLBACK_OFF` | Disable fallback |
| `GET_MODE` | Report current mode and statistics |
| `RESET` | Software reset |

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| No CSV output | `TEST 1`? Disarmed with TEST=0 silences output |
| XBee no data at base | XBee AP=0? Baud matches? RX/TX wired correctly? |
| Beacon not received | MAC addresses match? Channel matches? |
| MQTT not connecting | Broker IP correct? WiFi credentials set? |
| SD write fails | FAT32 format? CS pin 26? SD card inserted? |
| Stack overflow crash | Increase `STACK_SIZE` in defs.h (currently 2048 words) |
| Watchdog timeout | Reduce task delays; check blocking calls |

---

## Related Documentation

- [COMMUNICATION_ARCHITECTURE.md](docs/COMMUNICATION_ARCHITECTURE.md) — full comms system design
- [BEACON_CONFIGURATION.md](docs/BEACON_CONFIGURATION.md) — beacon mode deep-dive
- [XBEE_INTEGRATION.md](docs/XBEE_INTEGRATION.md) — XBee wiring, XCTU config, CSV format
- [PYRO_CONTROL_SYSTEM.md](docs/PYRO_CONTROL_SYSTEM.md) — ejection charge control
- [LOGGER_IMPROVEMENTS.md](docs/LOGGER_IMPROVEMENTS.md) — SD / flash logging
- [docs/mqtt-setup.md](docs/mqtt-setup.md) — MQTT broker setup
