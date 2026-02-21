# XBee Mode Switching Fix

## Problem Description

When sending mode switch commands (e.g., `CMD_BEACON_MODE` or `CMD_XBEE_MODE`), the base station's serial output would either change format or stop entirely.

## Root Causes Identified

### Issue 1: Base Station Mode Filtering 🔴
The base station was **filtering telemetry output based on its own internal mode**:

```cpp
// ❌ OLD BROKEN CODE
if (currentMode == MODE_XBEE) {
    sendTelemetryJSON();  // Only print if in XBee mode!
}
```

**Problem:** When you sent `CMD_BEACON_MODE` to the rocket:
1. Base station changed its own `currentMode` to `MODE_BEACON`
2. XBee telemetry still arrived from rocket (because rocket takes time to switch)
3. Base station **ignored** the XBee data because it wasn't in XBee mode anymore
4. Result: Serial output stopped or changed format

### Issue 2: Base Station Mode Commands Changed Local State 🔴
The base station was changing its **own** reception mode when sending commands to the rocket:

```cpp
// ❌ OLD BROKEN CODE
else if (command == "CMD_BEACON_MODE") {
    currentMode = MODE_BEACON;  // ← Base station changes its own mode!
    lastCommand = "CMD_BEACON_MODE";
    // Send command to rocket...
}
```

**Problem:** Mode commands should only affect the **rocket's transmission**, not the base station's **reception**.

### Issue 3: Missing CMD_XBEE_MODE Handler in Flight Computer 🔴
The flight computer wasn't checking for `CMD_XBEE_MODE` in the command parser:

```cpp
// ❌ OLD BROKEN CODE
if (strncmp(cmdBuffer, "CMD_MQTT_MODE", 13) == 0 ||
    strncmp(cmdBuffer, "CMD_BEACON_MODE", 15) == 0 ||
    // CMD_XBEE_MODE missing!
    strncmp(cmdBuffer, "CMD_AUTO_FALLBACK", 17) == 0) {
```

## Solutions Applied ✅

### Fix 1: Base Station Always Prints Telemetry
**Files Changed:** `base_station_xbee_fixed.cpp`

Removed mode checks from telemetry handlers:

```cpp
// ✅ NEW FIXED CODE - XBee Handler
if (parseCSV(xbeeBuffer.c_str(), telemetry)) {
    xbeePacketsReceived++;
    lastXBeePacketTime = millis();
    lastPacketTime = millis();
    dataReceived = true;
    
    // ALWAYS send telemetry regardless of mode
    sendTelemetryJSON();  // ← No mode check!
}

// ✅ NEW FIXED CODE - Beacon Handler
if (parseCSV(csv_data, telemetry)) {
    packetsReceived++;
    lastPacketTime = millis();
    dataReceived = true;
    updateConnectionStatus();
    
    // ALWAYS send telemetry regardless of mode
    sendTelemetryJSON();  // ← No mode check!
}
```

### Fix 2: Base Station Doesn't Change Own Mode
**Files Changed:** `base_station_xbee_fixed.cpp`

Mode commands now only forward to rocket without changing base station behavior:

```cpp
// ✅ NEW FIXED CODE
else if (command == "CMD_BEACON_MODE" || command == "BEACON_MODE" || command == "BEACON") {
    // DON'T change currentMode here!
    lastCommand = "CMD_BEACON_MODE";
    commandPending = true;
    commandSentTime = millis();
    sendLogMessage("INFO", "Sending CMD_BEACON_MODE to rocket", "BaseStation");
}

else if (command == "CMD_XBEE_MODE" || command == "XBEE_MODE" || command == "XBEE") {
    // DON'T change currentMode here!
    lastCommand = "CMD_XBEE_MODE";
    commandPending = true;
    commandSentTime = millis();
    sendLogMessage("INFO", "Sending CMD_XBEE_MODE to rocket", "BaseStation");
}
```

### Fix 3: Added CMD_XBEE_MODE Support
**Files Changed:** `src/main.cpp`, `base_station_xbee_fixed.cpp`

**Flight Computer:**
```cpp
// ✅ NEW FIXED CODE
if (strncmp(cmdBuffer, "CMD_MQTT_MODE", 13) == 0 ||
    strncmp(cmdBuffer, "CMD_BEACON_MODE", 15) == 0 ||
    strncmp(cmdBuffer, "CMD_XBEE_MODE", 13) == 0 ||  // ← Added!
    strncmp(cmdBuffer, "CMD_AUTO_FALLBACK", 17) == 0 ||
    strncmp(cmdBuffer, "CMD_GET_MODE", 12) == 0) {
    comm_manager.handleModeCommand(String(cmdBuffer), "ESP_NOW");
}
```

**Base Station ESP-NOW Sending:**
```cpp
// ✅ NEW FIXED CODE
else if (lastCommand == "CMD_XBEE_MODE") {
    result = esp_now_send(rocket_mac, (uint8_t*)"CMD_XBEE_MODE", 13);
}
else if (lastCommand == "CMD_AUTO_FALLBACK_ON") {
    result = esp_now_send(rocket_mac, (uint8_t*)"CMD_AUTO_FALLBACK_ON", 20);
}
else if (lastCommand == "CMD_AUTO_FALLBACK_OFF") {
    result = esp_now_send(rocket_mac, (uint8_t*)"CMD_AUTO_FALLBACK_OFF", 21);
}
```

## New Behavior ✅

### Base Station (Receiver)
- **Always accepts and prints** telemetry from ANY source (XBee, Beacon, MQTT)
- **Never filters** based on internal mode state
- Mode commands are **forwarded to rocket only**
- Serial output is **continuous and consistent**

### Flight Computer (Transmitter)
- Responds to all mode commands: `CMD_MQTT_MODE`, `CMD_BEACON_MODE`, `CMD_XBEE_MODE`
- Switches transmission method as commanded
- Debug logging shows mode changes: `[COMM MANAGER] Switched to XBee-only mode by ESP_NOW`

## Testing Commands

```bash
# Switch rocket to XBee transmission
XBEE

# Switch rocket to Beacon transmission  
BEACON

# Switch rocket to MQTT transmission
MQTT

# Check status
STATUS

# Test XBee UART
XBEE_TEST
```

## Expected Serial Output (After Fix)

```
LOG:{"level":"INFO","message":"Sending CMD_BEACON_MODE to rocket","source":"BaseStation","timestamp":12345}
[XBEE RX] Packet #45 | Rec#1250 | Alt=145.2m | Vel=340.0m/s
{"record_number":1250,"operation_mode":1,"state":2,...}|ESP32:N4_BASE_XBEE_FIXED
[XBEE RX] Packet #46 | Rec#1251 | Alt=148.7m | Vel=345.2m/s
{"record_number":1251,"operation_mode":1,"state":2,...}|ESP32:N4_BASE_XBEE_FIXED
```

**Telemetry continues streaming regardless of which mode command was sent!**

## Files Modified

1. ✅ `base_station_xbee_fixed.cpp` - Removed mode filtering, fixed command handling
2. ✅ `src/main.cpp` - Added CMD_XBEE_MODE parsing

## Summary

The base station now acts as a **universal receiver** that prints all telemetry regardless of source, while mode commands control the **rocket's transmission method** only. This ensures continuous, uninterrupted serial output for your dashboard.
