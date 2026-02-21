# N4 Flight Software — docs/

Architecture guides, integration references, and hardware setup docs.

## Architecture & Integration

| File | Contents |
|------|----------|
| [COMMUNICATION_ARCHITECTURE.md](COMMUNICATION_ARCHITECTURE.md) | Full multi-mode comms design, UART assignments, FreeRTOS tasks, command system |
| [BEACON_CONFIGURATION.md](BEACON_CONFIGURATION.md) | Beacon mode, 4 km range, raw 802.11 frames, RSSI, antenna guide |
| [XBEE_INTEGRATION.md](XBEE_INTEGRATION.md) | XBee Pro 900HP wiring, XCTU settings (AP=0, BD=7), CSV format |
| [RSSI_TELEMETRY_INTEGRATION.md](RSSI_TELEMETRY_INTEGRATION.md) | How RSSI is captured and included in telemetry packets |
| [PYRO_CONTROL_SYSTEM.md](PYRO_CONTROL_SYSTEM.md) | PWM ejection control, timing, safety system |
| [PWM_CONFIG_COMMANDS.md](PWM_CONFIG_COMMANDS.md) | Runtime PWM configuration commands |
| [LOGGER_IMPROVEMENTS.md](LOGGER_IMPROVEMENTS.md) | SD card and flash logging improvements |
| [WiFiManager_BaseStation_Setup.md](WiFiManager_BaseStation_Setup.md) | Base station WiFi config via captive portal |

## Hardware Setup

| File | Contents |
|------|----------|
| [beacon-setup.md](beacon-setup.md) | Beacon hardware wiring, MAC address setup, test procedure |
| [mqtt-setup.md](mqtt-setup.md) | MQTT broker installation, WiFiManager, topic structure |
| [logging.md](logging.md) | SD card, flash, and system event logging guide |

---

For fix logs and changelogs see [../fixes/](../fixes/).  
For the project overview see [../README.md](../README.md).
