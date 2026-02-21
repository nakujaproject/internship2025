# MQTT Broker Setup

## Overview

In MQTT mode the flight computer connects as a **WiFi Station** and publishes telemetry to a broker running on the ground station laptop or Raspberry Pi.

---

## Broker Options

| Option | Notes |
|--------|-------|
| Mosquitto (Linux/Windows/macOS) | Recommended — lightweight, easy config |
| HiveMQ CE | Docker-friendly alternative |
| EMQX | Feature-rich, web dashboard |

### Install Mosquitto (Ubuntu / Raspberry Pi)
```bash
sudo apt-get install mosquitto mosquitto-clients
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

### Install Mosquitto (Windows)
Download the installer from https://mosquitto.org/download/ and run it.  
Default config listens on port 1883.

---

## WiFi Configuration

The N4 base station uses **WiFiManager** to configure WiFi credentials and broker IP at runtime — no hardcoding required.

1. Power on the base station ESP32
2. If no saved credentials, it creates an AP named `N4-BaseStation`
3. Connect your laptop to `N4-BaseStation`
4. Navigate to `192.168.4.1` in a browser
5. Enter SSID, password, broker IP, and MQTT port
6. Save — the base station reboots and connects

Full guide: [WiFiManager_BaseStation_Setup.md](WiFiManager_BaseStation_Setup.md)

---

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `n4/flight-computer-1` | Rocket → Broker | Telemetry JSON |
| `n4/commands` | Broker → Rocket | ARM, DISARM, mode commands |

Customise the topic names in `include/defs.h`:
```cpp
const char MQTT_TELEMETRY_TOPIC[30] = "n4/flight-computer-1";
const char MQTT_ARMING_TOPIC[30]    = "n4/commands";
```

---

## Subscribe and View Data

```bash
# Subscribe to all N4 topics
mosquitto_sub -h 192.168.100.248 -p 1883 -t "n4/#" -v

# Send an ARM command
mosquitto_pub -h 192.168.100.248 -p 1883 -t "n4/commands" -m "ARM"

# Switch to beacon mode
mosquitto_pub -h 192.168.100.248 -p 1883 -t "n4/commands" -m "BEACON_MODE"
```

---

## Python Data Logger with MQTT

Use `scripts/data-logger.py` to subscribe and save to CSV:

```bash
cd scripts
python data-logger.py --broker 192.168.100.248 --topic n4/flight-computer-1
```

---

## Compile-time MQTT Enable

In `include/defs.h`:
```cpp
#define MQTT 1   // Start in MQTT mode
#define XBEE 0   // Disable XBee at boot
```

Or leave defaults and switch at runtime:
```
MQTT_MODE
```

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Flight computer can't connect | Broker IP correct in WiFiManager? Broker running? |
| No messages arriving | Subscribed to correct topic? `n4/flight-computer-1` |
| Commands not received | Publishing to `n4/commands`? Flight computer subscribed? |
| WiFiManager AP not appearing | Hold boot button for 3 s to clear saved config |

Default broker IP: `192.168.100.248` (change via WiFiManager to match your network).
