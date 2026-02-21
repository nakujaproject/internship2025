
## N4 Flight Software Documentation
![Static Badge](https://img.shields.io/badge/Version-N4-blue)
![Static Badge](https://img.shields.io/badge/License-MIT-red)
![Static Badge](https://img.shields.io/badge/Status-development-orange)

### Code documentation
The complete code documentation can be found here [N4 Flight Software Documentation](https://nakujaproject.com/N4-Flight-Software/)

---

## Table of Contents

1. [Hardware Overview](#hardware-overview)
2. [Flight Software Requirements](#n4-flight-software-requirements)
3. [Communication Architecture](#communication-architecture)
4. [Tasks and Task Creation](#tasks-and-task-creation)
5. [Telemetry Packet Structure](#telemetry-packet-structure)
6. [Data Logging and Storage](#data-logging-and-storage)
7. [GPS Operations](#gps-operations)
8. [State Machine](#state-machine-logic-and-operation)
9. [IMU](#imu)
10. [Data Filtering](#data-filtering)
11. [Pyro / Ejection Control](#pyro--ejection-control)
12. [Utility Scripts](#utility-scripts)
13. [Documentation Index](#documentation-index)
14. [References](#references-and-error-fixes)

---

## Hardware Overview

| Component | Model / Detail |
|-----------|---------------|
| Microcontroller | ESP32 DevKit (38-pin) |
| IMU | MPU6050 (I2C, 0x68) — accel ±16 g, gyro ±1000 °/s |
| Barometer | BMP280 — altitude and temperature |
| GPS | UART module, 9600 baud, pins 16/17 |
| Long-range radio | XBee Pro 900HP — 900 MHz, UART1, pins 32/34 |
| Short-range radio | ESP32 internal WiFi — Beacon (ESP-NOW + raw 802.11) |
| WiFi/MQTT | ESP32 internal WiFi — 2.4 GHz station mode |
| SD card | SPI, CS GPIO 26, FAT32 |
| External flash | WINBOND W25Q32JVSIQ — 32 Mbit SPI NOR |
| Drogue pyro | GPIO 25, LEDC channel 3, 500 Hz PWM |
| Main chute pyro | GPIO 12, LEDC channel 4, 500 Hz PWM |
| Supply voltage | ~15 V LiPo |
| Green LED | GPIO 15 |
| Red LED | GPIO 4 |
| Buzzer | GPIO 33 |
| Arm switch | GPIO 27 (remote) |

> Pin assignments are defined in `n4-flight-software/include/defs.h`.

---

### N4 Flight software requirements 

---

#### 1. Rocket kinematics (acceleration, velocity)

a) Perform accurate calculation of acceleration and velocity from sensor data

b) Perform data filtering to get close to ideal simulated data

#### 2. Rocket altitude above ground level (AGL)

a) Determine the rocket's instantaneous AGL during flight

#### 3. Flight state transitions 

a) Accurately switch to the corresponding flight state based on evaluated sensor data 

#### 4. Data logging and storage 

a) Collect and aggregate all sensor data and store it in an external flash memory for post-flight data recovery

b) Perform onboard system logging to indicate all events that occur during flight and store this in a separate system log file

#### 5. Onboard systems diagnostics 

a) Troubleshoot onboard subsystems such as the sensors, batteries etc. and log to the system file 

b) Package the system diagnostics results into telemetry packets for transmission to ground

#### 6. GPS location 

a)  Accurately determine the latitude, longitude and timestamp of the rocket using GPS for post flight recovery

#### 7. Rocket attitude (orientation ) determination

a) Calculate the roll and pitch of the rocket in space during flight 

#### 8. Command and data handling 

a) Receive commands sent from ground station

b) Decode commands sent from ground station

c) Acknowledge and perform command sent from the ground station

#### 9. Telemetry transmission

a) Reliably transmit the rocket's data to the ground station 

b) Perform error detection and correction on the telemetry packets 

#### 10. Video capture and streaming**

a) Capture video stream during flight

b) Record video stream to an onboard SD card for post-flight analysis

b) Transmit video stream to ground**



### Tasks and task creation

---

The firmware runs under **FreeRTOS** on the ESP32's dual cores. Tasks are created in `src/main.cpp` via `xCreateAllTasks()`.

| Task Name | Core | Priority | Stack | Purpose |
|-----------|------|----------|-------|---------|
| `altimeter_task` | 0 | 3 | STACK_SIZE | Read BMP280 → altimeter queue |
| `gyroscope_task` | 0 | 3 | STACK_SIZE | Read MPU6050 → gyroscope queue |
| `gps_task` | 0 | 2 | STACK_SIZE | Parse UART GPS → GPS queue |
| `kalman_task` | 0 | 3 | STACK_SIZE | Kalman filter → filtered data queue |
| `state_machine` | 0 | 4 | STACK_SIZE | Evaluate queues → flight state transitions |
| `mqtt_telemetry` | 1 | 2 | STACK_SIZE×4 | Publish CSV to MQTT broker |
| `xbee_telemetry` | 1 | 2 | STACK_SIZE×4 | Write CSV to XBee UART |
| `beacon_transmit` | 1 | 2 | STACK_SIZE×4 | Inject raw 802.11 beacon frames |
| `debug_terminal` | 1 | 1 | STACK_SIZE | Serial debug print (disable pre-flight) |

`STACK_SIZE` = 2048 words (defined in `defs.h`). Disable `debug_terminal` before flight by setting `DEBUG_TO_TERMINAL 0`.

### Data queues and task communication

---

Tasks communicate via FreeRTOS queues. All queues are defined with lengths in `include/defs.h`:

| Queue | Length | Producers → Consumers |
|-------|--------|-----------------------|
| `altimeter_queue` | 10 | altimeter_task → kalman_task, state_machine |
| `gyroscope_queue` | 10 | gyroscope_task → state_machine |
| `gps_queue` | 24 | gps_task → telemetry tasks |
| `telemetry_data_queue` | 10 | state_machine → mqtt/xbee/beacon tasks |
| `filtered_data_queue` | 10 | kalman_task → state_machine, telemetry |
| `flight_states_queue` | 1 | state_machine → telemetry tasks |
| `log_to_mem_queue` | 64 | all tasks → sd_logger / flash_logger |



## Communication Architecture

---

The N4 flight computer supports **four communication modes** that can be switched at runtime:

| Mode | Transport | Range | Use Case |
|------|-----------|-------|----------|
| **MQTT** | WiFi 2.4 GHz (STA mode) | ~100 m | Pad ops, pre-flight, post-recovery |
| **Beacon** | Raw 802.11 + ESP-NOW | ~4 km LOS | Short-to-medium range flights |
| **XBee** | 900 MHz UART (XBee Pro 900HP) | 1–30 km | Long-range flights |
| **Triple** | All three simultaneously | best available | Maximum redundancy |

### Runtime Mode Commands

Send via Serial, MQTT (`n4/commands`), or ESP-NOW:

| Command | Effect |
|---------|--------|
| `MQTT_MODE` | MQTT only |
| `BEACON_MODE` | Beacon only |
| `XBEE_MODE` | XBee only |
| `DUAL_MODE` | MQTT + Beacon |
| `TRIPLE_MODE` | MQTT + Beacon + XBee |
| `AUTO_FALLBACK_ON` | Auto-switch MQTT→Beacon on 10 s timeout |
| `GET_MODE` | Report current mode and statistics |
| `ARM` / `DISARM` | Arm/disarm the flight computer |

### UART Assignments

**Flight Computer**

| UART | Pins | Device | Baud |
|------|------|--------|------|
| UART0 | USB | Debug console | 115200 |
| UART1 | TX=32, RX=34 | XBee Pro 900HP | 115200 |
| UART2 | TX=17, RX=16 | GPS module | 9600 |

**Base Station** (`base_station_xbee_fixed.cpp`)

| UART | Pins | Device | Baud |
|------|------|--------|------|
| UART0 | USB | Python server | 115200 |
| UART1 | TX=17, RX=16 | Bluetooth HC-05/06 | 115200 |
| UART2 | TX=32, RX=34 | XBee Pro 900HP | 115200 |

Full architecture details: [n4-flight-software/docs/COMMUNICATION_ARCHITECTURE.md](n4-flight-software/docs/COMMUNICATION_ARCHITECTURE.md)

---

### Telemetry and transmission to ground

----

#### Link budget calculation



#### Telemetry packet structure

| Data                  | Data type | Size (bytes) | Description                                              |
| --------------------- | --------- | ------------ | -------------------------------------------------------- |
| record_number         | uint32_t  | 4            | record number count                                      |
| state                 | uint8_t   | 1            | current flight state                                     |
| operation_mode        | uint8_t   | 1            | current flight mode, whether SAFE or ARMED               |
| ax                    | float     | 4            | acceleration in the x-axis (m/s^2)                       |
| ay                    | float     | 4            | acceleration in the y-axis (m/s^2)                       |
| az                    | float     | 4            | acceleration in the z-axis (m/s^2)                       |
| pitch                 | float     | 4            | pitch angle (deg)                                        |
| roll                  | float     | 4            | roll angle (deg)                                         |
| gx                    | float     | 4            | angular velocity along the x-axis (deg/sec)              |
| gy                    | float     | 4            | angular velocity along the y-axis (deg/sec)              |
| gz                    | float     | 4            | angular velocity along the z-axis (deg/sec)              |
| latitude              | double    | 8            | geographical distance N or S of equator (deg)            |
| longitude             | double    | 8            | geographical distance E or W of Greenwich Meridian (deg) |
| gps_altitude          | uint16_t  | 2            | altitude read by the onboard GPS (m)                     |
| gps_time              | time_t    | 4            | current time from the GPS (UTC)                          |
| pressure              | float     | 4            | pressure from the altimeter (mb)                         |
| temperature           | uint8_t   | 1            | temperature from the altimeter (deg C)                   |
| altitude_agl          | uint16_t  | 2            | height above ground level                                |
| velocity              | float     | 4            | velocity derived from the altimeter                      |
| pyro1_state           | uint8_t   | 1            | state of main chute pyro (whether ejected or active)     |
| pyro2_state           | uint8_t   | 1            | state of drogue chute pyro (whether ejected or active)   |
| battery_voltage       | float     | 4            | voltage of the battery during flight (V)                 |
| rssi                  | int32_t   | 4            | signal strength at base station (dBm); beacon-mode value captured at receiver |
| kalman_altitude       | float     | 4            | Kalman-filtered altitude AGL (m)                         |
| kalman_vertical_vel   | float     | 4            | Kalman-filtered vertical velocity (m/s)                  |
|                       |           |              |                                                          |
| **Total packet size** |           | **86 BYTES** | *(25 fields — 3 added: rssi, kalman_alt, kalman_vel)*   |



### Data Logging and storage 

---

For logging and storage, we use **two simultaneous backends** for redundancy:

1. **SD card** (primary, in-flight): FAT32 CSV files written every cycle. Files named `flight_log_NNNN.csv` with auto-increment on boot. Enabled with `ENABLE_SD_LOGGING 1` in `defs.h`.
2. **External SPI flash** (backup): WINBOND W25Q32JVSIQ2135, 32 Mbit (4 MB). Binary log written in parallel. Enabled with `ENABLE_FLASH_LOGGING 1` and `LOG_TO_MEMORY 1`.

Both backends receive the same 25-field telemetry record via a FreeRTOS queue (`log_to_mem_queue`, depth 64). A separate **system event logger** (`src/system_logger.cpp`) records state transitions, arming events, and errors with timestamps to a second log file.

See [n4-flight-software/docs/LOGGER_IMPROVEMENTS.md](n4-flight-software/docs/LOGGER_IMPROVEMENTS.md) for recent logging improvements.

The logging flowchart is shown below:

![logger-flowchart](./imgs/logger-flowchart.png)



#### Flash chip hardware tests 

Using this library [SerialFlashLib](https://github.com/PaulStoffregen/SerialFlash/tree/master), we carried out flash chip hardware tests to make sure the MCU communicates as desired with the memory chip. The circuit diagram is shown below:

![flash-cct](./imgs/flash-mem.png)



#### PCB layout for the flash memory

To ensure maximum reliability of the flash memory on the PCB, follow the following techniques during layout:



The following snapshot from serial monitor shows that ESP32 was able to recognize the chip over the SPI channel.

![flash-test](./imgs/flash-test.png)

However, there is a discrepancy when we use this library to recognize this memory chip. This may be because the chip is a fake and therefore not recognized by this library. By default, the lib shows the size of the chip as 1MB, which is wrong. 

If we use the [SparkFun_SPI_SerialFlashChip library](https://github.com/sparkfun/SparkFun_SPI_SerialFlash_Arduino_Library/tree/main), we are able to recognize the chip as shown below.

![flash-SFE](./imgs/flash-mem-SFE.png)

The flash chip is working okay from the tests above. 

Now, since we want to access the flash memory in a file-system kind of way, where we can read and write FILES, we use the [SerialFlash Library](https://github.com/PaulStoffregen/SerialFlash), even if the flash memory is not recognized by it. This will make it easier for us to access huge blocks of memory in chunks and avoid accessing the memory directly. In addition, we can erase files and use SD-like methods to access data.

The demonstration below received data from the serial monitor, and writes it to a file inside the flash memory. 

First we test for file R/W. 

##### Known issue 

When using SPI protocol on breadboard, it might fail to communicate with the peripheral device. This is because SPI is high-speed and is expected to be used with short traces on PCB. When testing this part, I experienced errors before i realized this issue. To correct this, I reduced the SPI communication speed from 50MHz to 20MHz so that I could access the R/W functions using the breadboard. More details are in reference #8 below. 

Note: Make sure you change the speed to 20MHz for your files to be created. Change the speed in the SerialFlashChip.cpp near the top of the file (SPICONFIG)




The image below shows the response after I reduced the SPI speed: 
![flash](./imgs/file-ready.png)

Testing method
1. I created a file 4KB in size and named it ```test.csv```. 
2. Then generated dummy data using random() functions in Arduino. 
3. I then appended this random data to the file, while checking the size being occupied by the file
4. Running the ```flash_read.ino``` file after data is done recording displays all the recorded data on the flash memory



#### How to recover the data 

Use ```Nakuja Flight Data Recovery Tool``` to dump the recorded data as follows: 



The image below shows the response after I reduced the SPI speed: 
![flash](./imgs/file-ready.png)

### GPS Operations 

GPS is used to give us accurate location in terms of longitude, latitude, time and altitude. This data is useful for post-flight recovery and for apogee detection and verification. However, because of the low sample rate of GPS modules (1 Hz), we cannot use it reliably to log altitude data since rocketry is high speed. 

![gps](./imgs/GPS-MODULE.jfif)

#### Reading GPS data algorithm 

We read GPS data using the [TinyGPSPlus Library](https://github.com/mikalhart/TinyGPSPlus). The data of interest is the latitude, longitude, time and altitude. The algorithm is as follows:

1. Create GPS data queue
2. Create the ```readGPS``` task
3. Inside the task, create a local ```gps_type_t``` variable to hold the sampled data
4. Read the latitude, longitude, time and altitude into the ```gps_type_t``` variable
5. Send this data to ```telemetry_queue```

#### GPS  fix time issues

The start of GPS can be cold or warm. Cold start means the GPS is starting from scratch, no prior satellite data exists, and here it takes much time to lock satellites and download satellite data. Once you initially download satellite data, the following connections take less time, referred to as warm-starts.

When using GPS, you will find that the time it takes to acquire a fix to GPS satellites depends on the cloud cover. If the cloud cover is too high, it takes longer to acquire a signal and vice-versa. During one of the tests of the GPS, it took ~2 min at 45% cloud cover to acquire signal. 

During launch, we do not want to wait for infinity to get a GPS lock, so we implement a timeout as follows:

```c
Consider the GPS_WAIT_TIME as 2 minutes (2000ms):

1. Initialize a timeout variable and a lock_acquired boolean value
2. Check the value of the timeout_variable
3. Is it less than the GPS_WAIT_TIME?
4. If less than the wait time,  continue waiting for GPS fix, if more than the GPS_WAIT_TIME, stop waiting for fix and return false
5. If the GPS data is available and successfully encoded via serial, set the lock_acquired booelan value to true
```

This timeout will ensure we do not delay other sub-systems of the flight software from starting.

##### Flowchart 

![gps-flowchart](./imgs/gps-lock-flow.png)



#### GPS tests

The following screenshots show the results of GPS tests during development. In the image below, the raw GPS coordinates are read and printed on the serial debugger:

![gps-data](./imgs/gps-test-altitude.png)





### State machine logic and operation

---

#### States

Defined in `n4-flight-software/src/states.h`:

| Value | State | Description |
|-------|-------|-------------|
| 0 | `PRE_FLIGHT_GROUND` | On pad, waiting for launch detection |
| 1 | `POWERED_FLIGHT` | Motor burning, positive acceleration |
| 2 | `COASTING` | Motor off, decelerating |
| 3 | `APOGEE` | Zero vertical velocity, peak altitude |
| 4 | `DROGUE_DEPLOY` | Drogue ejection charge fired |
| 5 | `DROGUE_DESCENT` | Descending under drogue, awaiting main altitude |
| 6 | `MAIN_DEPLOY` | Main chute ejection charge fired |
| 7 | `MAIN_DESCENT` | Descending under main chute |
| 8 | `POST_FLIGHT_GROUND` | Landed, logging stopped |

#### State transition conditions

![N4 Flight State Machine](n4-flight-software/diagrams/output/state_machine_diagram.png)

Apogee and launch detection use **Kalman-filtered altitude and velocity** when `USE_KALMAN_FOR_STATE_DETECTION 1` (default). The `ARM` command requires filtered altitude > 50 m AGL (`ARM_ALTITUDE_THRESHOLD`) to prevent pad arming.

#### State functions handling

State logic is evaluated in the `state_machine` FreeRTOS task. On each state transition:
- The new state is written to `flight_states_queue` for telemetry tasks
- An event is logged to the system log via `system_logger`
- Ejection flags (`DROGUE_DEPLOY_FLAG`, `MAIN_CHUTE_EJECT_FLAG`) are set one-way (never cleared)

Full pyro control details: [n4-flight-software/docs/PYRO_CONTROL_SYSTEM.md](n4-flight-software/docs/PYRO_CONTROL_SYSTEM.md)

### IMU

#### Calculating acceleration from accelerometer



#### Calculating velocity from accelerometer
The initial idea is to use integration. 
Since velocity is the first integral of acceleration. From the equation: 
``` v = u + at ```

So what we do to calculate the velocity is keep track of time, acceleration in the requires axis and then update the initial velocity. Consider the X axis: 

``` Vx = Ux + ACCx*Sample_time ```  
``` Ux = Vx  ```

(Let the sample tme be 1ms (0.001 s))

Known issue is velocity drift: where the velocity does not get to zero even when the sensor is stationary. small errors in the measurement of acceleration and angular velocity are integrated into progressively larger errors in velocity, which are compounded into still greater errors in position

Article: [IMU Velocity drift](https://en.wikipedia.org/wiki/Inertial_navigation_system#Drift_rate)

However, after extensive research online, it was concluded that getting velocity from accelerometer is very innacurate and unreliable. Check out this reddit thread:
[Acceleration & velocity with MPU6050](https://www.reddit.com/r/embedded/comments/138jnhu/acceleration_velocity_with_mpu6050/)

Check this arduinoForum article too (ArduinForum)
[https://forum.arduino.cc/t/integrating-acceleration-to-get-velocity/954731/8]

Following this, we decide to keep the accelerometer for measuring the acceleration and the rocket orientation.

### Data Filtering 

---

#### Complementary filter

A complementary filter fuses accelerometer and barometric altitude data to reduce noise. The **Kalman filter** (`src/kalman_filter.cpp`) has superseded the complementary filter for state detection and is used for both altitude and vertical velocity estimation in-flight.

---

## Pyro / Ejection Control

---

Both ejection channels use **PWM voltage scaling** to deliver ~6 V to the bridgewire from the 15 V supply:

$$\text{duty} = \frac{6}{15} \times 255 = 102 \quad (\approx 40\%\text{ at 500 Hz, 8-bit})$$

| Channel | GPIO | LEDC Channel | Event | Pulse Duration |
|---------|------|-------------|-------|----------------|
| Drogue  | 25   | 3 | Apogee + 1500 ms | 5000 ms |
| Main chute | 12 | 4 | Descent through 500 m AGL | 5000 ms |

See [n4-flight-software/docs/PYRO_CONTROL_SYSTEM.md](n4-flight-software/docs/PYRO_CONTROL_SYSTEM.md) for full details, safety system, and pre-flight checks.

---

### Utility scripts
During development the following scripts might (and will) be useful.

##### 1. HEX converter
Converts string to HEX string and back. Built with python
###### Requirements
1. Python > 3.10

The screenshot below shows the program running:
![hex-converter](./imgs/hex-converter.png)

###### Usage 
Open a terminal window in the folder containing the ```hex-converter.py``` file and run the following command:

```c
python hex-converter.py
```
The screenshot above appears. Select your option and proceed. The program will output your string in HEX format.


## Documentation Index

---

All detailed guides live in the `n4-flight-software/` subdirectory.

### Getting Started
- [n4-flight-software/QUICKSTART.md](n4-flight-software/QUICKSTART.md) — build, flash, first test, full pre-flight checklist
- [n4-flight-software/README.md](n4-flight-software/README.md) — firmware project overview and file layout

### Topic Guides (`n4-flight-software/docs/`)
- [docs/COMMUNICATION_ARCHITECTURE.md](n4-flight-software/docs/COMMUNICATION_ARCHITECTURE.md) — full multi-mode comms design, UART assignments, FreeRTOS tasks
- [docs/XBEE_INTEGRATION.md](n4-flight-software/docs/XBEE_INTEGRATION.md) — XBee Pro 900HP wiring, XCTU settings (AP=0, BD=7), CSV format
- [docs/BEACON_CONFIGURATION.md](n4-flight-software/docs/BEACON_CONFIGURATION.md) — beacon mode, 4 km range, antennas, RSSI
- [docs/RSSI_TELEMETRY_INTEGRATION.md](n4-flight-software/docs/RSSI_TELEMETRY_INTEGRATION.md) — how RSSI is captured and included in telemetry
- [docs/WiFiManager_BaseStation_Setup.md](n4-flight-software/docs/WiFiManager_BaseStation_Setup.md) — base station WiFi config via captive portal
- [docs/mqtt-setup.md](n4-flight-software/docs/mqtt-setup.md) — MQTT broker installation and topic structure
- [docs/beacon-setup.md](n4-flight-software/docs/beacon-setup.md) — beacon hardware wiring and MAC setup
- [docs/PYRO_CONTROL_SYSTEM.md](n4-flight-software/docs/PYRO_CONTROL_SYSTEM.md) — PWM ejection control, timing, safety
- [docs/PWM_CONFIG_COMMANDS.md](n4-flight-software/docs/PWM_CONFIG_COMMANDS.md) — runtime PWM commands
- [docs/LOGGER_IMPROVEMENTS.md](n4-flight-software/docs/LOGGER_IMPROVEMENTS.md) — SD + flash logging improvements
- [docs/logging.md](n4-flight-software/docs/logging.md) — logging system guide and post-flight data extraction

### Fix Logs & Changelogs (`n4-flight-software/fixes/`)
- [fixes/BEACON_RSSI_DEBUG_GUIDE.md](n4-flight-software/fixes/BEACON_RSSI_DEBUG_GUIDE.md) — RSSI troubleshooting
- [fixes/BEACON_RSSI_UPDATE_COMPLETE.md](n4-flight-software/fixes/BEACON_RSSI_UPDATE_COMPLETE.md) — RSSI implementation changelog
- [fixes/XBEE_MODE_SWITCHING_FIX.md](n4-flight-software/fixes/XBEE_MODE_SWITCHING_FIX.md) — mode-switching bug fix log
- [fixes/AUTO_SWITCHING_FIXES.md](n4-flight-software/fixes/AUTO_SWITCHING_FIXES.md) — auto mode-switching fixes
- [fixes/COMMUNICATION_MANAGER_FIX.md](n4-flight-software/fixes/COMMUNICATION_MANAGER_FIX.md) — comm manager fixes
- [fixes/DATA_STREAM_FIXES.md](n4-flight-software/fixes/DATA_STREAM_FIXES.md) — data stream bug fixes
- [fixes/PERFORMANCE_FIXES_REPORT.md](n4-flight-software/fixes/PERFORMANCE_FIXES_REPORT.md) — performance improvements
- [fixes/PWM_DURATION_UPDATE_SUMMARY.md](n4-flight-software/fixes/PWM_DURATION_UPDATE_SUMMARY.md) — pyro timing update notes
- [fixes/SYSTEM_STATUS_FINAL.md](n4-flight-software/fixes/SYSTEM_STATUS_FINAL.md) — system status report

---

### References and Error fixes

1. (Wire LIbrary Device Lock) [Confusing overload of `Wire::begin` · Issue #6616 · espressif/arduino-esp32 · GitHub](https://github.com/espressif/arduino-esp32/issues/6616)
2. (Estimating velocity and altitude) [https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4179067/]
3. [rocket orientation and velocity] (https://www.reddit.com/r/rocketry/comments/10q7j8m/using_accelerometers_for_rocket_attitude/)
4. https://cdn.shopify.com/s/files/1/1014/5789/files/Standard-ASCII-Table_large.jpg?10669400161723642407
5. https://www.codeproject.com/Articles/99547/Hex-strings-to-raw-data-and-back
6. https://cdn.shopify.com/s/files/1/1014/5789/files/Standard-ASCII-Table_large.jpg?10669400161723642407
7. https://www.geeksforgeeks.org/convert-a-string-to-hexadecimal-ascii-values/
8. (SPI Flash memory file creation issue on breadboard) https://forum.arduino.cc/t/esp32-and-winbond-w25q128jv-serial-flash-memory/861315/3



