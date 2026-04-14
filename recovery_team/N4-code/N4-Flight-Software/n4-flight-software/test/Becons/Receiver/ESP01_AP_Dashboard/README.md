ESP-01 AP Dashboard Bridge

Purpose
- Replaces Bluetooth in the base-station path.
- ESP32 receiver sends telemetry/log/status over UART2 (pins 16/17) to ESP-01.
- ESP-01 hosts an AP + dashboard, sends commands back to ESP32, and forwards CSV to laptop server.

ESP32 receiver wiring (this project)
- ESP32 TX2 GPIO17 -> ESP-01 RX
- ESP32 RX2 GPIO16 <- ESP-01 TX
- Common GND
- ESP-01 powered from stable 3.3V supply (do not use 5V)

ESP-01 flash target
- File: ESP01_AP_Dashboard.ino
- Board: Generic ESP8266 Module (ESP-01)
- Upload speed: 115200
- Serial baud: 115200

AP credentials
- SSID: N4_BaseStation_AP
- Password: n4flight2026
- Dashboard URL: http://192.168.4.1/

Server forwarding
- Default URL: http://192.168.4.2:3001/api/telemetry-csv
- Change from dashboard "Server Forwarding" box.

Command path
- Dashboard buttons POST commands to ESP-01
- ESP-01 writes command line over UART to ESP32 receiver
- ESP32 receiver forwards via ESP-NOW to flight computer

Notes
- ESP32 receiver now emits prefixed CSV frames as: CSV:<line>
- ESP-01 forwards only CSV frames to the configured server URL
