# PWM Configuration via ESP-NOW Commands

## Overview
The flight computer supports dynamic PWM voltage **and duration** configuration via ESP-NOW JSON commands. This allows real-time adjustment of drogue and main chute deployment voltages and on-times without reflashing the firmware.

## Command Format

### Full Configuration Structure (with durations)
```
CMD_SET_PWM_CONFIG:<json_payload>
```

### JSON Payload Schema
```json
{
  "vcc": 17.8,           // Battery supply voltage (0-20V)
  "drogue_v": 3.0,       // Drogue pyro voltage (0-vcc)
  "main_v": 10.0,        // Main pyro voltage (0-vcc)
  "drogue_time": 5000,   // Drogue PWM on-time in milliseconds (100-60000)
  "main_time": 5000      // Main PWM on-time in milliseconds (100-60000)
}
```

### Complete Command Examples

#### Full configuration (voltages + durations):
```
CMD_SET_PWM_CONFIG:{"vcc":14.8,"drogue_v":9.0,"main_v":10.0,"drogue_time":3000,"main_time":5000}
```

#### Voltage only (uses default 5000ms durations):
```
CMD_SET_PWM_CONFIG:{"vcc":14.8,"drogue_v":9.0,"main_v":10.0}
```

#### Duration only (preserves existing voltages):
```
CMD_SET_PWM_CONFIG:{"drogue_time":2000,"main_time":8000}
```

## Parameters

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| `vcc` | float | 0-20V | 17.8V | Battery input voltage |
| `drogue_v` | float | 0-vcc | 3.0V | Drogue pyro output voltage |
| `main_v` | float | 0-vcc | 10.0V | Main pyro output voltage |
| `drogue_time` | int | 100-60000ms | 5000ms | Drogue PWM on-time (milliseconds) |
| `main_time` | int | 100-60000ms | 5000ms | Main PWM on-time (milliseconds) |

## Base Station Integration

### Method 1: Serial Command (Quick Testing)
Add to base station `handleSerialCommands()`:

```cpp
else if (command.startsWith("SET_PWM:")) {
    // Example: SET_PWM:{"vcc":14.8,"drogue_v":9.0,"main_v":10.0,"drogue_time":3000,"main_time":5000}
    String jsonPayload = command.substring(8); // Remove "SET_PWM:" prefix
    jsonPayload.trim();
    
    // Validate JSON structure
    StaticJsonDocument<256> testDoc;
    DeserializationError error = deserializeJson(testDoc, jsonPayload);
    
    if (error) {
        sendLogMessage("ERROR", ("Invalid JSON format: " + String(error.c_str())).c_str(), "BaseStation");
        sendLogMessage("INFO", "Format: SET_PWM:{\"vcc\":14.8,\"drogue_v\":9.0,\"main_v\":10.0,\"drogue_time\":5000,\"main_time\":5000}", "BaseStation");
        return;
    }
    
    String fullCommand = "CMD_SET_PWM_CONFIG:" + jsonPayload;
    lastCommand = fullCommand;
    commandPending = true;
    commandSentTime = millis();
    sendLogMessage("INFO", ("PWM config command: " + jsonPayload).c_str(), "BaseStation");
}
```

### Method 2: Direct ESP-NOW Function
Add to base station as a dedicated function:

```cpp
void sendPWMConfig(float vcc, float drogue_v, float main_v, 
                   unsigned long drogue_time_ms, unsigned long main_time_ms) {
    // Build JSON command with durations
    DynamicJsonDocument doc(256);
    doc["vcc"] = vcc;
    doc["drogue_v"] = drogue_v;
    doc["main_v"] = main_v;
    doc["drogue_time"] = drogue_time_ms;
    doc["main_time"] = main_time_ms;
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    String fullCommand = "CMD_SET_PWM_CONFIG:" + jsonPayload;
    
    esp_err_t result = esp_now_send(rocket_mac, 
                                     (uint8_t*)fullCommand.c_str(), 
                                     fullCommand.length());
    
    if (result == ESP_OK) {
        sendLogMessage("INFO", ("✅ PWM config sent: " + jsonPayload).c_str(), "BaseStation");
    } else {
        sendLogMessage("ERROR", "❌ PWM config send failed", "BaseStation");
    }
}

// Usage examples:
// sendPWMConfig(14.8, 9.0, 10.0, 3000, 5000);  // Full config
// sendPWMConfig(14.8, 9.0, 10.0, 5000, 5000);  // Standard durations
```

### Method 3: Add to Command Menu
Update `handleSerialCommands()` with interactive menu:

```cpp
else if (command == "PWM_CONFIG" || command == "CONFIG") {
    sendLogMessage("INFO", "📝 PWM Configuration Menu", "BaseStation");
    sendLogMessage("INFO", "Current format: SET_PWM:<json>", "BaseStation");
    sendLogMessage("INFO", "Example: SET_PWM:{\"vcc\":14.8,\"drogue_v\":9.0,\"main_v\":10.0,\"drogue_time\":3000,\"main_time\":5000}", "BaseStation");
}
```

## Response Handling

### Success Response (with durations)
Flight computer sends back:
```
PWM_CONFIG_OK:Vcc=17.8,Drogue=9.0V(3000ms),Main=10.0V(5000ms)
```

### Error Response
```
PWM_CONFIG_ERROR:Invalid_JSON
PWM_CONFIG_ERROR:Invalid_drogue_duration
PWM_CONFIG_ERROR:Invalid_Vcc_range
```

### Update Base Station Callback
Modify `onESPNowDataReceived()`:

```cpp
void onESPNowDataReceived(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    String response = "";
    for (int i = 0; i < len; i++) {
        response += (char)incomingData[i];
    }
    response.trim();

    // Handle PWM config responses with durations
    if (response.startsWith("PWM_CONFIG_OK:")) {
        // Parse: "PWM_CONFIG_OK:Vcc=17.8,Drogue=9.0V(3000ms),Main=10.0V(5000ms)"
        sendLogMessage("INFO", ("✅ " + response).c_str(), "BaseStation");
        
        // Optional: Extract and store values for status display
        int vccStart = response.indexOf("Vcc=") + 4;
        if (vccStart > 3) {
            // Update local PWM status tracking
            pwm_status.config_received = true;
            pwm_status.last_update_time = millis();
        }
    }
    else if (response.startsWith("PWM_CONFIG_ERROR:")) {
        sendLogMessage("ERROR", ("❌ " + response).c_str(), "BaseStation");
    }
    // ... existing response handlers ...
}
```

## Validation Rules

### Flight Computer Constraints
- **Vcc**: 0V - 20V (battery supply voltage)
- **Drogue voltage**: 0V - Vcc
- **Main voltage**: 0V - Vcc
- **Drogue duration**: 100ms - 60000ms (0.1s - 60s)
- **Main duration**: 100ms - 60000ms (0.1s - 60s)
- All voltages and durations must be within valid ranges
- Invalid configs are rejected with error response

### Safety Considerations
1. **Test on ground first** - Verify voltages and durations with multimeter/oscilloscope before flight
2. **Document configurations** - Log all PWM settings used during tests
3. **Use conservative values** - Start with lower voltages and shorter durations, increase as needed
4. **Pre-flight checklist** - Confirm PWM config matches expected values
5. **Duration testing** - Verify auto-shutdown works correctly with configured durations

## Example Usage Scenarios

### Scenario 1: Battery Voltage Changed (14.8V → 17.8V)
```
Serial input: SET_PWM:{"vcc":17.8,"drogue_v":9.0,"main_v":10.0}
```

### Scenario 2: Lower Drogue Voltage for Testing with Short Duration
```
Serial input: SET_PWM:{"vcc":14.8,"drogue_v":5.0,"main_v":10.0,"drogue_time":1000,"main_time":3000}
```

### Scenario 3: Full Power Configuration with Extended Duration
```
Serial input: SET_PWM:{"vcc":17.8,"drogue_v":12.0,"main_v":15.0,"drogue_time":3000,"main_time":8000}
```

### Scenario 4: Adjust Only Durations (Quick E-Match Test)
```
Serial input: SET_PWM:{"drogue_time":500,"main_time":500}
```

### Scenario 5: Extended Backup Deployment Duration
```
Serial input: SET_PWM:{"main_time":10000}
```
*Note: Drogue voltage and other parameters remain unchanged*

## Testing Procedure

### Step 1: Connect Base Station Serial Monitor
```
Baud: 115200
Commands available: HELP
```

### Step 2: Send Test Configuration with Short Durations
```
SET_PWM:{"vcc":14.8,"drogue_v":3.0,"main_v":10.0,"drogue_time":1000,"main_time":1000}
```

### Step 3: Verify Response
Look for:
```
LOG:{"level":"INFO","message":"✅ PWM Config Updated: Vcc=14.8V, Drogue=3.0V (1000ms), Main=10.0V (1000ms)"}
```

### Step 4: Test Deployment with Duration (SAFE MODE)
```
1. Send: MAIN_ON
2. Measure voltage on main pyro pin with oscilloscope
3. Verify: ~10.0V output (± 0.5V tolerance)
4. Verify: Auto-shutdown after 1000ms
5. Observe: Serial output "⏰ MAIN PYRO auto-shutdown after 1000ms (PWM=0)"
```

### Step 5: Test Different Durations
```
# Short burst for e-match testing
SET_PWM:{"drogue_time":500,"main_time":500}
DROGUE_ON  # Should turn off after 500ms

# Extended duration for backup deployment
SET_PWM:{"main_time":8000}
MAIN_ON    # Should turn off after 8000ms
```

## Flight Computer Logging

All PWM config changes are logged to SPIFFS with durations:
```
[2024-12-10 14:30:15] INFO - PWM Config: Vcc=14.8V, Drogue=9.0V (3000ms), Main=10.0V (5000ms)
```

## Troubleshooting

### Issue: No Response from Flight Computer
- **Check**: ESP-NOW peer connection established
- **Check**: Rocket MAC address matches base station config
- **Try**: Send simpler command first (e.g., "ARM")

### Issue: "Invalid_JSON" Error
- **Check**: JSON syntax is valid (use online JSON validator)
- **Check**: No extra spaces in command
- **Check**: All quotes are double quotes (")
- **Check**: Duration values are integers, not floats

### Issue: "Invalid_drogue_duration" or "Invalid_main_duration" Error
- **Check**: Duration is within 100-60000ms range
- **Try**: Use 1000-5000ms for typical e-match applications
- **Note**: Minimum 100ms prevents accidental instant cutoff

### Issue: Config Accepted but Wrong Voltage Output
- **Check**: PWM frequency is 1kHz (ESP32PWM settings)
- **Check**: Battery Vcc measurement is accurate
- **Measure**: Actual output voltage with multimeter
- **Note**: PWM duty cycle = (desiredV / Vcc) * 255

### Issue: PWM Turns Off Too Quickly or Stays On
- **Check**: Configured duration matches expectation (use PWM_STATUS command)
- **Verify**: Auto-shutdown message appears in serial log
- **Test**: Bench test with oscilloscope to measure exact duration
- **Note**: Duration starts from moment of activation (DROGUE_ON/MAIN_ON or deployment)

## Recommended Settings by E-Match Type

### Standard Estes E-Match (1-2A activation):
```json
{
  "vcc": 14.8,
  "drogue_v": 9.0,
  "drogue_time": 2000,
  "main_v": 10.0,
  "main_time": 3000
}
```

### High-Current Pyro (Copperhead, 3-5A):
```json
{
  "vcc": 17.8,
  "drogue_v": 12.0,
  "drogue_time": 1500,
  "main_v": 14.0,
  "main_time": 2500
}
```

### Conservative/Backup Settings:
```json
{
  "vcc": 17.8,
  "drogue_v": 11.0,
  "drogue_time": 5000,
  "main_v": 12.0,
  "main_time": 8000
}
```

### Quick Bench Test (Safe):
```json
{
  "vcc": 3.3,
  "drogue_v": 3.0,
  "drogue_time": 500,
  "main_v": 3.0,
  "main_time": 500
}
```

## Integration Checklist

- [ ] Add `#include <ArduinoJson.h>` to base station (if not already present)
- [ ] Update `handleSerialCommands()` with SET_PWM command parser (with duration support)
- [ ] Update `onESPNowDataReceived()` to handle PWM responses (with durations)
- [ ] Test with oscilloscope to verify duration accuracy
- [ ] Test with multimeter to verify voltage output
- [ ] Document final PWM config values (voltage + duration) in flight log
- [ ] Verify auto-shutdown timing with different duration values
- [ ] Test e-match firing with configured durations before flight

## Default Values (Current Firmware)
```cpp
float Vcc = 17.8f;                      // 17.8V battery
float desiredDrogueV = 3.0f;            // 3.0V drogue
float desiredMainV   = 10.0f;           // 10.0V main
unsigned long droguePWMDuration = 5000; // 5 seconds drogue
unsigned long mainPWMDuration = 5000;   // 5 seconds main
```

## Deployment Sequence Behavior

### Automatic Flight Deployment:
```
1. Apogee detected → State: APOGEE
2. Wait 500ms delay → State: DROGUE_DEPLOY
3. Drogue PWM starts at configured voltage
4. Auto-shutdown after droguePWMDuration expires
5. Descend to main altitude → State: MAIN_DEPLOY
6. Main PWM starts at configured voltage
7. Auto-shutdown after mainPWMDuration expires
```

### Manual Arming (Ground Test):
```
Command: DROGUE_ON
→ Drogue PWM activates with configured voltage
→ Auto-shuts off after drogue_time
→ Can be manually stopped early with DROGUE_OFF

Command: MAIN_ON  
→ Main PWM activates with configured voltage
→ Auto-shuts off after main_time
→ Can be manually stopped early with MAIN_OFF
```

## Additional Notes
- Configuration persists until reboot (not saved to flash)
- **Use before ARM command** to ensure correct voltages/durations during flight
- Can be changed mid-flight if needed (emergency adjustment)
- JSON payload limited to 180 characters (MAX_COMMAND_LENGTH - 20)
- **Duration starts immediately** upon activation (deployment or manual arming)
- Auto-shutdown is independent for each channel
- Configuration can be queried via `PWM_STATUS` command on base station
