# PWM Duration Configuration Update Summary

## Overview
Extended PWM configuration system to support **configurable on-time durations** for drogue and main pyro channels, in addition to voltage control.

## Changes Made

### Flight Computer (`src/main.cpp`)

#### 1. New Global Variables
```cpp
unsigned long droguePWMDuration = 5000;  // Default 5 seconds
unsigned long mainPWMDuration = 5000;    // Default 5 seconds
```

#### 2. Updated PWMConfig Structure
```cpp
struct PWMConfig {
    float vcc;
    float drogue_voltage;
    float main_voltage;
    unsigned long drogue_duration_ms;  // NEW: Duration in milliseconds
    unsigned long main_duration_ms;    // NEW: Duration in milliseconds
};
```

#### 3. Enhanced parsePWMConfig()
- Parses `drogue_time` and `main_time` from JSON
- Validates duration range: **100ms - 60000ms** (0.1s - 60s)
- Preserves existing values if fields not specified
- Returns error for out-of-range durations

#### 4. Enhanced applyPWMConfig()
- Applies duration configuration to global variables
- Logs durations in SPIFFS event log
- Serial debug output includes durations

#### 5. Updated Deployment Functions
- `drogueChuteDeploy()` - Logs configured duration
- `mainChuteDeploy()` - Logs configured duration
- `armDroguePyro()` - Logs configured duration with 🔥 emoji
- `armMainPyro()` - Logs configured duration with 🔥 emoji

#### 6. Enhanced checkAutoDisarm()
- Uses `droguePWMDuration` instead of fixed `SHORT_PYRO_TIMEOUT`
- Uses `mainPWMDuration` instead of fixed `SHORT_PYRO_TIMEOUT`
- Logs actual duration in auto-shutdown messages
- Independent timing for each channel

#### 7. Enhanced ESP-NOW Response
```cpp
snprintf(response, sizeof(response),
         "PWM_CONFIG_OK:Vcc=%.1f,Drogue=%.1fV(%lums),Main=%.1fV(%lums)",
         Vcc, desiredDrogueV, droguePWMDuration,
         desiredMainV, mainPWMDuration);
```

### Documentation (`PWM_CONFIG_COMMANDS.md`)

#### Updated Sections:
1. **Command Format** - Added duration examples
2. **JSON Schema** - Added `drogue_time` and `main_time` parameters
3. **Parameter Table** - Added duration ranges and defaults
4. **Base Station Integration** - Updated all 3 methods for durations
5. **Response Handling** - Updated response format with durations
6. **Validation Rules** - Added duration constraints
7. **Usage Scenarios** - Added 5 new scenarios with duration examples
8. **Testing Procedure** - Updated to test durations with oscilloscope
9. **Troubleshooting** - Added duration-specific issues
10. **Recommended Settings** - Added 4 presets by e-match type
11. **Deployment Sequence** - Added behavior documentation
12. **Integration Checklist** - Updated with duration verification

## Command Examples

### Full Configuration (Voltages + Durations)
```bash
SET_PWM:{"vcc":14.8,"drogue_v":9.0,"main_v":10.0,"drogue_time":3000,"main_time":5000}
```

### Voltage Only (Uses Default 5000ms)
```bash
SET_PWM:{"vcc":14.8,"drogue_v":9.0,"main_v":10.0}
```

### Duration Only (Preserves Existing Voltages)
```bash
SET_PWM:{"drogue_time":2000,"main_time":8000}
```

### Single Channel Duration Update
```bash
SET_PWM:{"main_time":10000}
```

## Response Format

### Success (with durations)
```
PWM_CONFIG_OK:Vcc=14.8,Drogue=9.0V(3000ms),Main=10.0V(5000ms)
```

### Errors
```
PWM_CONFIG_ERROR:Invalid_drogue_duration
PWM_CONFIG_ERROR:Invalid_main_duration
```

## Validation

### Duration Constraints
- **Minimum**: 100ms (prevents accidental instant cutoff)
- **Maximum**: 60000ms (60 seconds safety limit)
- **Typical Range**: 1000-5000ms for e-matches
- **Extended Range**: 5000-10000ms for backup deployments

### Safety Features
1. ✅ Independent auto-shutdown per channel
2. ✅ Duration validation before applying config
3. ✅ Configurable on-the-fly (even mid-flight)
4. ✅ Logged to SPIFFS with timestamps
5. ✅ Manual override still works (DROGUE_OFF/MAIN_OFF)

## Testing Recommendations

### Bench Test
```bash
# Safe low-voltage, short duration test
SET_PWM:{"vcc":3.3,"drogue_v":3.0,"drogue_time":500,"main_v":3.0,"main_time":500}
DROGUE_ON  # Should auto-shutdown after 500ms
```

### E-Match Test
```bash
# Standard e-match configuration
SET_PWM:{"vcc":14.8,"drogue_v":9.0,"drogue_time":2000,"main_v":10.0,"main_time":3000}
# Connect oscilloscope to verify timing
```

### Flight Configuration
```bash
# Production settings
SET_PWM:{"vcc":17.8,"drogue_v":10.0,"drogue_time":3000,"main_v":12.0,"main_time":5000}
# Verify with PWM_STATUS command
```

## Migration Notes

### For Existing Deployments
- Default durations remain 5000ms (backward compatible)
- No firmware changes required if using defaults
- Old commands without durations still work

### For New Deployments
- Add duration fields to pre-flight configuration
- Test durations with oscilloscope before flight
- Document configured durations in flight log

## Key Benefits

1. **Flexibility** - Adjust burn time for different e-match types
2. **Safety** - Configurable limits prevent excessive pyro activation
3. **Testing** - Short durations for safe bench tests
4. **Backup** - Extended durations for redundant deployment
5. **Per-Channel Control** - Independent settings for drogue and main

## Files Modified
- ✅ `src/main.cpp` - Core implementation
- ✅ `PWM_CONFIG_COMMANDS.md` - Complete documentation update
- ✅ `PWM_DURATION_UPDATE_SUMMARY.md` - This summary

## Next Steps
1. Flash updated firmware to flight computer
2. Update base station with new command format
3. Test duration accuracy with oscilloscope
4. Document final configuration in flight checklist
5. Perform e-match continuity and firing tests with new durations
