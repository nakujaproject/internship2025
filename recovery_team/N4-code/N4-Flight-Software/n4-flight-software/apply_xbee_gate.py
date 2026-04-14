#!/usr/bin/env python3
"""
Apply telemetry gating for XBee transmission
"""

# Read the file with UTF-8 encoding
with open('src/main.cpp', 'r', encoding='utf-8', errors='ignore') as f:
    content = f.read()

# Replace XBee telemetry transmission block
old_xbee = '''        if (comm_manager.isXBeeActive()) {
            // Always send telemetry data in both TEST and FLIGHT modes.
            XBeeSerial.println(telemetry_packet_buffer);
            Serial.println("[XBEE TX] ✓ Sent: Rec#" + String(telemetry_received_packet.record_number));
            xbee_success = true;
        } else {
            Serial.println("[XBEE DISABLED] XBee mode not active");
        }'''

new_xbee = '''        if (comm_manager.isXBeeActive()) {
            // In FLIGHT mode gate: skip if sensors not healthy
            bool xbee_sensor_ok = g_spiffs_ready && g_bmp_ready && g_imu_ready && g_gps_ready && g_ads_ready;
            bool xbee_battery_ok = (battery_voltage >= BAT_CUTOFF && battery_voltage <= BAT_MAX_VALID);
            
            if (g_test_mode || (xbee_sensor_ok && xbee_battery_ok)) {
                XBeeSerial.println(telemetry_packet_buffer);
                Serial.println("[XBEE TX] ✓ Sent: Rec#" + String(telemetry_received_packet.record_number));
                xbee_success = true;
            } else {
                xbee_success = false;
            }
        } else {
            Serial.println("[XBEE DISABLED] XBee mode not active");
            xbee_success = false;
        }'''

if old_xbee in content:
    content = content.replace(old_xbee, new_xbee)
    xbee_updated = True
    print("✓ XBee transmission section updated")
else:
    xbee_updated = False
    print("✗ XBee section not found - trying manual verification...")
    if 'XBeeSerial.println' in content:
        print("  Note: XBeeSerial.println found but block structure doesn't match")

# Write the file back with UTF-8 encoding
with open('src/main.cpp', 'w', encoding='utf-8') as f:
    f.write(content)

print("\n" + "=" * 70)
print("XBEE TELEMETRY GATING")
print("=" * 70)
print(f"XBee updated: {'✓ YES' if xbee_updated else '✗ NO'}")
print("\nBehavior:")
print("- TEST mode:  Send XBee telemetry regardless of sensor state")
print("- FLIGHT mode: Send ONLY if all sensors ready + battery healthy")
print("=" * 70)
