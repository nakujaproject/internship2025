# N4 Base Station - Quick Reference

## 📁 Main Directory Structure

```
N4-Basestation/
├── start_basestation_integrated.py    # 🚀 START HERE - Main launcher
├── start_basestation.py               # Legacy launcher
├── README.md                           # Project overview
├── research/                           # 🔬 All development files
│   ├── arduino_code/                   # ESP32 code (production + simulator)
│   ├── bluetooth/                      # Bluetooth tools & guides
│   ├── simulation/                     # Flight simulators & testing
│   ├── analysis/                       # Data analysis tools
│   ├── documentation/                  # Technical docs
│   ├── scripts/                        # Server & utility scripts
│   └── range_tests/                    # Range test results
├── src/                                # Frontend React code
├── public/                             # Static assets
├── telemetry_logs/                     # Recorded flight data
├── server.js                           # Node.js API server
├── vite.config.js                      # Vite build config
├── package.json                        # Node dependencies
├── mosquitto.conf                      # MQTT config
└── osm-2020-02-10-v3.11_africa_kenya.mbtiles  # Map tiles
```

---

## 🚀 Quick Start

### 1. Start Base Station (All Services)
```bash
python start_basestation_integrated.py
```

### 2. Open Dashboard
```
http://localhost:5173
```

### 3. Common Options
```bash
# Simulation mode (no hardware)
python start_basestation_integrated.py --simulation

# Force USB only
python start_basestation_integrated.py --force-usb

# Force Bluetooth only
python start_basestation_integrated.py --force-bluetooth

# Skip Bluetooth setup
python start_basestation_integrated.py --skip-bluetooth
```

---

## 📚 Documentation

All documentation is in `research/documentation/`:

- **SETUP.md** - Complete system setup
- **BLUETOOTH_SETUP.md** - Bluetooth configuration
- **COMMUNICATION_MODES.md** - USB/Bluetooth switching
- **COMMAND_INTERFACE_IMPLEMENTATION.md** - Command system
- **VISUAL_FEEDBACK_IMPLEMENTATION.md** - Dashboard features

---

## 🔧 Development Files

All development/research files are in `research/`:

### Arduino Code
```
research/arduino_code/Basestation_Code_6_Bluetooth.ino     # Production
research/arduino_code/Simulated_BaseStation_Integrated.ino  # Simulator
```

### Bluetooth Tools
```
research/bluetooth/HC05_AT_Command_Setup.ino               # AT commands
research/bluetooth/HC05_BAUDRATE_CONFIG.md                 # Baud guide
```

### Testing
```
research/simulation/SIMULATION_TESTING_GUIDE.md            # Test guide
research/simulation/flight_test_simulator.py               # Simulator
```

### Analysis
```
research/analysis/analyze_telemetry.py                     # Flight analysis
research/analysis/flight_curves.py                         # Visualizations
```

---

## 🎯 Common Tasks

### Upload Production Code
1. Open Arduino IDE
2. Load `research/arduino_code/Basestation_Code_6_Bluetooth.ino`
3. Select "ESP32 Dev Module"
4. Upload

### Upload Simulator Code
1. Open Arduino IDE
2. Load `research/arduino_code/Simulated_BaseStation_Integrated.ino`
3. Select "ESP32 Dev Module"
4. Upload

### Configure Bluetooth Module
1. Upload `research/bluetooth/HC05_AT_Command_Setup.ino`
2. Open Serial Monitor (115200 baud)
3. Type: `AT+UART=460800,0,0` (HC-05)
4. Or: `AT+BAUD9` (HC-06)

### Analyze Flight Data
```bash
python research/analysis/analyze_telemetry.py telemetry_logs/flight.csv
python research/analysis/flight_curves.py telemetry_logs/flight.csv
```

---

## 🐛 Troubleshooting

### Slow Telemetry
- Check: `research/bluetooth/HC05_BAUDRATE_CONFIG.md`
- Fix: Configure module to 460800 baud

### Bluetooth Not Working
- Check: `research/documentation/BLUETOOTH_SETUP.md`
- Fix: Power cycle module after pairing

### No Data Received
- Check: `research/documentation/COMMUNICATION_MODES.md`
- Fix: Try `--force-usb` or `--force-bluetooth`

---

## 📖 For New Developers

1. Read `README.md` (main overview)
2. Read `research/README.md` (research structure)
3. Read `research/documentation/SETUP.md` (setup guide)
4. Read `research/simulation/SIMULATION_TESTING_GUIDE.md` (testing)

---

## 📊 Project Organization

### Main Directory (Operational Files Only)
- Startup scripts
- Configuration files
- Frontend code
- Build files
- Map data

### Research Directory (Development Files)
- Arduino code
- Testing tools
- Analysis scripts
- Documentation
- Experiments

**Rule:** Only files needed to RUN the system belong in main directory.
Everything else goes in `research/`.

---

## 🔄 Git Workflow

```bash
# Check status
git status

# Stage changes
git add -A

# Commit
git commit -m "Your message"

# Push
git push origin exp_try1
```

---

## 📞 Need Help?

- **System Setup:** `research/documentation/SETUP.md`
- **Bluetooth Issues:** `research/bluetooth/README.md`
- **Simulation Testing:** `research/simulation/SIMULATION_TESTING_GUIDE.md`
- **Research Files:** `research/README.md`

---

**Last Updated:** 2026-01-17
