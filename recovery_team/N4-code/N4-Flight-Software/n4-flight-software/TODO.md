# N4 XBee Debug & MAC Update - TODO Steps

## Status: Active (Approved by user)

### Step 1: [IN PROGRESS] Update Base Station ESP-NOW MAC addresses to `{10:06:1c:a6:11:f0}`
- `base_station_xbee_fixed.cpp`
- `base_station_enhanced_rssi.cpp` 
- `base_station_with_beacon_rssi.cpp`

### Step 2: Add XBee Debug Prints
- Rocket `src/main.cpp`: Print mode switches, XBee TX confirms.
- Base stations: Log parsed packets, RSSI.

### Step 3: Test Sequence (Post-Flash)
```
1. Base Serial: XBEE_TEST → Verify UART/RSSI ✓
2. Base: STATUS → Current modes
3. Base: CMD_XBEE_MODE → Rocket switches to XBee
4. Rocket Serial: Watch [XBEE TX] ✓ Sent
5. XCTU on BASE XBee DIN → CSV data arrives
```

### Step 4: Verify Full System
- Rocket CSV → Base parse → JSON/BT output.

**Progress: 0/4**  
**Next: Edit base_station_xbee_fixed.cpp**

