# Beacon Mode Hardware Setup

## Required Hardware

| Item | Notes |
|------|-------|
| 2× ESP32 DevKit (38-pin) | One for rocket, one for base station |
| 2× 2.4 GHz antenna | U.FL to SMA pigtail recommended |
| Yagi or directional antenna | Ground station; 9 dBi or higher for range |
| USB cable | Base station to laptop |

---

## Rocket Side Wiring

The beacon transmitter uses the ESP32's **internal WiFi radio** — no external radio module is needed.

| Connection | Detail |
|------------|--------|
| Antenna | 2.4 GHz omni attached to ESP32 U.FL port |
| Power | 3.3 V from flight computer rail |

Ensure the antenna is routed **outside any metal airframe section** for RF clearance.

---

## Base Station Wiring

The base station also uses only the internal WiFi radio for beacon reception.  
For the current combined base station (`base_station_xbee_fixed.cpp`), the full pinout is:

| Signal | Pin | Notes |
|--------|-----|-------|
| USB / UART0 | — | Python server connection |
| Bluetooth TX | GPIO 17 | To HC-05/06 RX |
| Bluetooth RX | GPIO 16 | From HC-05/06 TX |
| XBee TX | GPIO 32 | To XBee DIN |
| XBee RX | GPIO 34 | From XBee DOUT |

---

## MAC Address Setup

Edit `include/defs.h` on **both** rocket and base station before flashing:

```cpp
static const uint8_t ROCKET_MAC[6] = {/* your rocket ESP32 MAC */};
static const uint8_t BASE_MAC[6]   = {/* your base station ESP32 MAC */};
```

Read MAC with:
```cpp
WiFi.mode(WIFI_STA);
Serial.println(WiFi.macAddress());
```

---

## Testing

1. Flash base station firmware
2. Flash rocket firmware with `TEST 1` in `defs.h`
3. Open base station Serial Monitor at 115200 baud
4. You should see beacon packets with RSSI values within 5–10 seconds

For detailed RSSI troubleshooting see [BEACON_RSSI_DEBUG_GUIDE.md](../fixes/BEACON_RSSI_DEBUG_GUIDE.md).  
For full beacon architecture see [BEACON_CONFIGURATION.md](BEACON_CONFIGURATION.md).
