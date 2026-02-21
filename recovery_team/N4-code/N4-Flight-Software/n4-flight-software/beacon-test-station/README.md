# N4 Beacon Test Station

A standalone PlatformIO project that transmits **synthetic beacon frames** at 1 Hz, allowing you to test your base station receiver and RSSI pipeline without a fully assembled flight computer.

---

## Quick Start

### Windows
```bat
quick-start.bat
```

### Manual
```bash
cd beacon-test-station
pio run --target upload
pio device monitor --baud 115200
```

---

## What It Transmits

The test station sends the same 25-field CSV telemetry format as the real flight computer, with incrementing timestamps and simulated sensor values.

```
1000,1,2,0.10,0.02,9.81,1.5,0.8,0.01,0.01,-0.01,0.0,0.0,1450.0,120000,98500.0,24.0,15.0,1.2,0,0,14.8,0,15.1,1.2
```

---

## Configuration

Edit `src/main.cpp` to change:
- Transmit interval (default 1000 ms)
- Simulated sensor values
- MAC addresses — must match `ROCKET_MAC` in base station firmware

---

## Expected Base Station Output

```
[BEACON] Received from AA:BB:CC:DD:EE:FF — RSSI: -45 dBm
{"timestamp":1000,"rssi":-45,"state":2,...}
```

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Base station receives nothing | MAC addresses match? Channel 1 on both? |
| RSSI too high (-20 dBm) | ESP32s too close — RF coupling, not real signal |
| Garbled CSV | Firmware version mismatch — update both sides |

See [../docs/BEACON_CONFIGURATION.md](../docs/BEACON_CONFIGURATION.md) for full beacon setup details.
