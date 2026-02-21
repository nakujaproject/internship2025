# Data Logging System

## Overview

The N4 flight computer has three simultaneous logging backends:

| Backend | File / Destination | Enable Flag |
|---------|-------------------|-------------|
| SD card CSV | `/flight_log_NNNN.csv` | `ENABLE_SD_LOGGING 1` |
| External SPI flash | Binary log | `ENABLE_FLASH_LOGGING 1` |
| RAM queue | In-memory circular buffer | always enabled |

All backends receive the same 25-field telemetry record on every cycle.

---

## SD Card Logging (`src/sd_logger.cpp`)

### Configuration
```cpp
// include/defs.h
#define SD_CS_PIN        26      // Chip select
#define ENABLE_SD_LOGGING 1      // Enable at compile time
```

### File Naming
Log files are created as `flight_log_0000.csv`, `flight_log_0001.csv`, etc.  
The index increments on each boot to avoid overwriting previous flights.

### CSV Format
The file header is written on first open:
```
timestamp,mode,state,ax,ay,az,pitch,roll,gx,gy,gz,lat,lon,gps_alt,gps_time,
pressure,temp,alt_agl,velocity,drogue,main,battery,rssi,kalman_alt,kalman_vel
```

### SD Card Requirements
- FAT32 formatted
- ≥ 2 GB capacity recommended
- Class 10 or faster (to keep up with 100 ms write cycle)

---

## External Flash Logging (`lib/CustomSerialFlash/`)

The `CustomSerialFlash` library provides raw binary logging to an SPI NOR flash chip.

Enable at compile time:
```cpp
#define ENABLE_FLASH_LOGGING 1
#define LOG_TO_MEMORY        1   // Must also set this
```

Flash is useful as a redundant backup if the SD card fails or ejects on landing.

---

## System Event Logger (`src/system_logger.cpp`)

A separate logger records system events (state transitions, arming events, errors) with timestamps.  
Events are written to both Serial and SD card.

Log levels are defined in `src/system_log_levels.h`:
```cpp
LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_CRITICAL
```

---

## Logging Queue

A FreeRTOS queue buffers log records before writing to storage:
```cpp
#define LOG_TO_MEM_QUEUE_LENGTH  64   // Queue depth (records)
```

If the queue fills (storage too slow), the oldest records are dropped.

---

## Post-Flight Data Extraction

### From SD Card
Remove the SD card and read `flight_log_NNNN.csv` on any computer.

### Via Serial (real-time)
```bash
# In scripts/ directory:
python data-logger.py --port COM3 --baud 115200 --output flight.csv
```

---

## Data Analysis Scripts

| Script | Usage |
|--------|-------|
| `scripts/graph.py` | Interactive plot of any telemetry field |
| `scripts/apogee-check.py` | Detect apogee and chute deployment from CSV |
| `log-data/plot_filtered_altitude.m` | MATLAB altitude plot with Kalman overlay |
| `log-data/python-raw-plot.py` | Python altitude plot |

### Quick Plot Example
```bash
cd scripts
python graph.py --file ../log-data/raw-log.csv --fields alt_agl,kalman_alt,velocity
```

---

## Pre-Flight Logging Checklist

- [ ] `ENABLE_SD_LOGGING 1` in `defs.h`
- [ ] `LOG_TO_MEMORY 1` in `defs.h`
- [ ] SD card inserted (FAT32 formatted)
- [ ] Verify log file created on boot (check Serial for `[SD] Log file: flight_log_NNNN.csv`)
- [ ] Confirm previous flight logs are backed up before new flight

---

## Related Documentation

- [LOGGER_IMPROVEMENTS.md](LOGGER_IMPROVEMENTS.md) — recent logging improvements
- [QUICKSTART.md](../QUICKSTART.md) — pre-flight checklist
