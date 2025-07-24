import serial
import json
import logging
import time
import paho.mqtt.client as mqtt
from serial.tools import list_ports
from threading import Lock

# === CONFIG ===
SERIAL_BAUD = 115200
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TELEMETRY_TOPIC = "n4/flight-computer-1"
MQTT_COMMAND_TOPIC = "n4/commands"
MQTT_LOG_TOPIC = "n4/logs"
MQTT_STATUS_TOPIC = "n4/base-station-status"

# Connection tracking
CONNECTION_TIMEOUT = 15000  # 15 seconds in milliseconds
HEARTBEAT_INTERVAL = 5      # Send status every 5 seconds

# === LOGGING ===
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger("BaseStationServer")

# === CONNECTION STATUS TRACKING ===
class ConnectionStatus:
    def __init__(self):
        self.lock = Lock()
        self.is_connected = False
        self.last_telemetry_time = 0
        self.packets_received = 0
        self.start_time = time.time()
        
    def update_telemetry_received(self):
        with self.lock:
            self.last_telemetry_time = time.time() * 1000  # Convert to milliseconds
            self.packets_received += 1
            self.is_connected = True
            
    def check_connection_status(self):
        with self.lock:
            current_time = time.time() * 1000
            if self.last_telemetry_time > 0:
                age = current_time - self.last_telemetry_time
                # Keep connected status even after timeout, but note the age
                return {
                    "connected": self.is_connected,
                    "last_packet_age_ms": int(age),
                    "packets_received": self.packets_received,
                    "timeout_exceeded": age > CONNECTION_TIMEOUT,
                    "uptime_seconds": int(time.time() - self.start_time)
                }
            else:
                return {
                    "connected": False,
                    "last_packet_age_ms": 0,
                    "packets_received": 0,
                    "timeout_exceeded": False,
                    "uptime_seconds": int(time.time() - self.start_time)
                }

connection_status = ConnectionStatus()

# === FIND AND CONNECT TO SERIAL DEVICE ===
def find_arduino_port():
    ports = list_ports.comports()
    for port in ports:
        if "Arduino" in port.description or "CH340" in port.description or "USB" in port.description or "Silicon Labs" in port.description:
            return port.device
    return None

def initialize_serial():
    try:
        port = find_arduino_port() or '/dev/ttyUSB0'
        ser = serial.Serial(port, SERIAL_BAUD, timeout=0.2)
        logger.info(f"✅ Connected to serial port: {port}")
        return ser
    except Exception as e:
        logger.error(f"❌ Failed to open serial port: {e}")
        return None

serial_conn = initialize_serial()

# === MQTT ===
client = mqtt.Client()
mqtt_connected = False

def on_mqtt_connect(client, userdata, flags, rc):
    global mqtt_connected
    if rc == 0:
        mqtt_connected = True
        logger.info("✅ MQTT connected successfully")
    else:
        mqtt_connected = False
        logger.error(f"❌ MQTT connection failed with code: {rc}")

def on_mqtt_disconnect(client, userdata, rc):
    global mqtt_connected
    mqtt_connected = False
    logger.warning("⚠️ MQTT disconnected")

# Command listener (MQTT -> Serial)
def on_mqtt_message(client, userdata, msg):
    command = msg.payload.decode().strip()
    logger.info(f"📨 Received command via MQTT: {command}")
    
    if command in ["ARM", "DISARM", "RESET"] and serial_conn and serial_conn.is_open:
        # Forward command directly to flight computer
        serial_conn.write((command + "\n").encode())
        logger.info(f"✅ Forwarded command '{command}' to flight computer")
        
        # Publish command confirmation
        if mqtt_connected:
            cmd_status = {
                "command": command,
                "sent_to_flight_computer": command,
                "timestamp": time.time(),
                "status": "forwarded"
            }
            client.publish(f"{MQTT_COMMAND_TOPIC}/status", json.dumps(cmd_status))
            
    else:
        logger.warning(f"⚠️ Unknown command or serial not available: {command}")

# Setup MQTT
client.on_connect = on_mqtt_connect
client.on_disconnect = on_mqtt_disconnect
client.on_message = on_mqtt_message

try:
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.subscribe(MQTT_COMMAND_TOPIC)
    client.loop_start()
    logger.info("🔌 MQTT setup initiated...")
except Exception as e:
    logger.error(f"❌ MQTT setup failed: {e}")

# === CSV TO JSON CONVERSION ===
def parse_csv_to_json(csv_line):
    """
    Parse 23-field CSV telemetry data to JSON format
    CSV Format (23 fields):
    0: record_number, 1: operation_mode, 2: state, 3-5: accel, 6-7: attitude,
    8-10: gyro, 11-12: gps coords, 13: gps_altitude, 14: gps_time,
    15: pressure, 16: temperature, 17: rel_altitude, 18: velocity,
    19: drogue_pin_state, 20: main_chute_pin_state, 21: battery_voltage, 22: wifi_rssi
    """
    try:
        values = csv_line.strip().split(',')
        
        # Ensure we have exactly 23 fields
        if len(values) != 23:
            logger.warning(f"⚠️ Expected 23 CSV fields, got {len(values)}: {csv_line}")
            return None
        
        # Convert to appropriate data types
        parsed_data = {
            "record_number": int(float(values[0])),
            "operation_mode": int(float(values[1])),
            "state": int(float(values[2])),
            "acc_data": {
                "ax": float(values[3]),
                "ay": float(values[4]),
                "az": float(values[5]),
                "pitch": float(values[6]),
                "roll": float(values[7])
            },
            "gyro_data": {
                "gx": float(values[8]),
                "gy": float(values[9]),
                "gz": float(values[10])
            },
            "gps_data": {
                "latitude": float(values[11]),
                "longitude": float(values[12]),
                "gps_altitude": float(values[13]),
                "time": int(float(values[14]))  # GPS time
            },
            "alt_data": {
                "pressure": float(values[15]),
                "temperature": float(values[16]),
                "AGL": float(values[17]),  # rel_altitude
                "velocity": float(values[18])
            },
            "chute_state": {
                "pyro1_state": int(float(values[19])),  # drogue_pin_state
                "pyro2_state": int(float(values[20]))   # main_chute_pin_state
            },
            "battery_voltage": float(values[21]),
            "wifi_rssi": int(float(values[22])),  # WiFi RSSI (or 0 for beacon mode)
            
            # Add metadata
            "timestamp": time.time(),
            "communication_mode": "MQTT" if int(float(values[22])) != 0 else "Beacon",
            "signal_strength": int(float(values[22])) if int(float(values[22])) != 0 else None
        }
        
        return parsed_data
        
    except (ValueError, IndexError) as e:
        logger.error(f"❌ CSV parsing error: {e} - Line: {csv_line}")
        return None

# === STATUS PUBLISHING ===
def publish_connection_status():
    """Publish current connection status to MQTT"""
    if not mqtt_connected:
        return
        
    status = connection_status.check_connection_status()
    status_msg = {
        "type": "base_station_status",
        "timestamp": time.time(),
        "serial_connected": serial_conn is not None and serial_conn.is_open,
        "mqtt_connected": mqtt_connected,
        "rocket_connection": status,
        "monitoring_active": True
    }
    
    try:
        client.publish(MQTT_STATUS_TOPIC, json.dumps(status_msg))
        
        # Log status periodically
        if status["connected"]:
            if status["timeout_exceeded"]:
                logger.info(f"📡 MONITORING: Connection established but stale "
                          f"(age: {status['last_packet_age_ms']/1000:.1f}s, "
                          f"packets: {status['packets_received']})")
            else:
                logger.debug(f"📡 CONNECTED: Age: {status['last_packet_age_ms']/1000:.1f}s, "
                           f"Packets: {status['packets_received']}")
        else:
            logger.debug(f"📡 WAITING: No telemetry received yet (uptime: {status['uptime_seconds']}s)")
            
    except Exception as e:
        logger.error(f"❌ Failed to publish status: {e}")

# === MAIN LOOP: SERIAL -> MQTT ===
def main_loop():
    if not serial_conn:
        logger.error("❌ Serial not connected.")
        return

    last_status_time = 0
    
    logger.info("🔄 Starting continuous monitoring loop...")
    logger.info("📡 Supporting JSON telemetry from ESP32 base station")
    logger.info("🔍 DEBUGGING MODE: Printing all received serial data")

    while True:
        try:
            # Publish status periodically
            current_time = time.time()
            if current_time - last_status_time >= HEARTBEAT_INTERVAL:
                publish_connection_status()
                last_status_time = current_time
            
            # Process serial data
            if serial_conn.in_waiting:
                line = serial_conn.readline().decode().strip()
                
                # 🔍 DEBUG: Print ALL received data
                if line.strip():  # Only print non-empty lines
                    print(f"🔍 RAW RECEIVED: {line}")
                
                # Handle different message types
                if line.startswith("[BEACON TX]") or line.startswith("[MQTT TX]") or line.startswith("[BEACON DEBUG]") or line.startswith("[MQTT DEBUG]"):
                    print(f"🎯 TELEMETRY DETECTED: {line}")
                    
                    # Extract labeled telemetry data
                    if "]" in line:
                        label = line.split("]")[0] + "]"
                        csv_data = line.split("]", 1)[1].strip()
                        
                        print(f"📊 LABEL: {label}")
                        print(f"📊 CSV DATA: {csv_data}")
                        
                        # Parse CSV to JSON
                        parsed_data = parse_csv_to_json(csv_data)
                        if parsed_data:
                            print(f"✅ PARSED SUCCESSFULLY")
                            print(f"📈 RECORD: {parsed_data['record_number']}")
                            print(f"🔋 BATTERY: {parsed_data['battery_voltage']}V")
                            print(f"📡 RSSI: {parsed_data['wifi_rssi']}")
                            print(f"🚀 ALT: {parsed_data['alt_data']['AGL']}m")
                            print(f"⚡ VEL: {parsed_data['alt_data']['velocity']}m/s")
                            
                            # Update connection status
                            connection_status.update_telemetry_received()
                            
                            # Add connection metadata
                            status = connection_status.check_connection_status()
                            parsed_data["connection_status"] = {
                                "connected": True,
                                "packet_age_ms": 0,  # Fresh packet
                                "total_packets": status["packets_received"],
                                "source_label": label
                            }
                            
                            # Determine if this was actual transmission or debug
                            is_transmission = "TX" in label
                            parsed_data["is_transmission"] = is_transmission
                            
                            if mqtt_connected:
                                client.publish(MQTT_TELEMETRY_TOPIC, json.dumps(parsed_data))
                                print(f"✅ SENT TO MQTT: {MQTT_TELEMETRY_TOPIC}")
                            else:
                                print(f"❌ MQTT NOT CONNECTED - Data not sent")
                            
                            # Enhanced logging with mode detection
                            mode = "MQTT" if parsed_data["wifi_rssi"] != 0 else "Beacon"
                            tx_status = "TRANSMITTED" if is_transmission else "DEBUG"
                            rssi_info = f"RSSI: {parsed_data['wifi_rssi']}dBm" if parsed_data["wifi_rssi"] != 0 else "No WiFi RSSI"
                            
                            logger.info(f"📡 {label} #{status['packets_received']} - "
                                      f"Record #{parsed_data['record_number']} - "
                                      f"{mode} Mode - {tx_status} - {rssi_info}")
                        else:
                            print(f"❌ FAILED TO PARSE CSV")
                            logger.warning(f"⚠️ Failed to parse CSV from: {label}")
                
                elif line.startswith("LOG:"):
                    print(f"📝 LOG MESSAGE: {line}")
                    # Log message
                    log_data = line[4:]  # Remove "LOG:" prefix
                    try:
                        log_json = json.loads(log_data)
                        if mqtt_connected:
                            client.publish(MQTT_LOG_TOPIC, json.dumps(log_json))
                        logger.info(f"📝 Log: {log_json.get('message', 'Unknown')}")
                    except json.JSONDecodeError:
                        logger.warning(f"⚠️ Invalid log JSON: {log_data}")
                        
                elif line.startswith("STATUS:"):
                    print(f"📊 STATUS MESSAGE: {line}")
                    # Status message from flight computer
                    status_data = line[7:]  # Remove "STATUS:" prefix
                    try:
                        status_json = json.loads(status_data)
                        if mqtt_connected:
                            # Publish flight computer internal status
                            enhanced_status = {
                                "type": "flight_computer_internal",
                                "timestamp": time.time(),
                                **status_json
                            }
                            client.publish(f"{MQTT_STATUS_TOPIC}/internal", json.dumps(enhanced_status))
                        
                        logger.debug(f"📊 Flight computer: Armed={status_json.get('armed', 'Unknown')}")
                    except json.JSONDecodeError:
                        logger.warning(f"⚠️ Invalid status JSON: {status_data}")
                
                elif "," in line and len(line.split(',')) == 23:
                    print(f"🎯 UNLABELED 23-FIELD CSV: {line}")
                    
                    # Direct 23-field CSV telemetry (unlabeled)
                    parsed_data = parse_csv_to_json(line)
                    if parsed_data:
                        print(f"✅ UNLABELED CSV PARSED SUCCESSFULLY")
                        print(f"📈 RECORD: {parsed_data['record_number']}")
                        print(f"🔋 BATTERY: {parsed_data['battery_voltage']}V")
                        print(f"📡 RSSI: {parsed_data['wifi_rssi']}")
                        
                        # Update connection status
                        connection_status.update_telemetry_received()
                        
                        # Add connection metadata
                        status = connection_status.check_connection_status()
                        parsed_data["connection_status"] = {
                            "connected": True,
                            "packet_age_ms": 0,  # Fresh packet
                            "total_packets": status["packets_received"],
                            "source_label": "[UNLABELED]"
                        }
                        
                        if mqtt_connected:
                            client.publish(MQTT_TELEMETRY_TOPIC, json.dumps(parsed_data))
                            print(f"✅ UNLABELED DATA SENT TO MQTT")
                        
                        mode = "MQTT" if parsed_data["wifi_rssi"] != 0 else "Beacon"
                        rssi_info = f"RSSI: {parsed_data['wifi_rssi']}dBm" if parsed_data["wifi_rssi"] != 0 else "No WiFi RSSI"
                        
                        logger.info(f"📡 [UNLABELED] #{status['packets_received']} - "
                                  f"Record #{parsed_data['record_number']} - "
                                  f"{mode} Mode - {rssi_info}")
                    else:
                        print(f"❌ FAILED TO PARSE UNLABELED CSV")
                
                # 🎯 NEW: Handle JSON telemetry data from ESP32 base station
                elif line.startswith('{"record_number"'):
                    print(f"🎯 JSON TELEMETRY DETECTED: {line[:100]}...")
                    
                    try:
                        # Parse the JSON directly
                        parsed_data = json.loads(line)
                        
                        print(f"✅ JSON PARSED SUCCESSFULLY")
                        print(f"📈 RECORD: {parsed_data.get('record_number', 'N/A')}")
                        print(f"🔋 BATTERY: {parsed_data.get('battery_voltage', 0)}V")
                        print(f"📡 RSSI: {parsed_data.get('wifi_rssi', 0)}")
                        print(f"🚀 ALT: {parsed_data.get('alt_data', {}).get('AGL', 0)}m")
                        print(f"⚡ VEL: {parsed_data.get('alt_data', {}).get('velocity', 0)}m/s")
                        print(f"🎛️ STATE: {parsed_data.get('state', 0)}")
                        print(f"🔥 MODE: {parsed_data.get('operation_mode', 0)}")
                        
                        # Update connection status
                        connection_status.update_telemetry_received()
                        
                        # Add connection metadata
                        status = connection_status.check_connection_status()
                        parsed_data["connection_status"] = {
                            "connected": True,
                            "packet_age_ms": 0,  # Fresh packet
                            "total_packets": status["packets_received"],
                            "source_label": "[ESP32_JSON]"
                        }
                        
                        # Ensure all required fields are present for React app
                        if "gps_data" not in parsed_data:
                            parsed_data["gps_data"] = {"latitude": 0, "longitude": 0, "gps_altitude": 0, "time": 0}
                        if "alt_data" not in parsed_data:
                            parsed_data["alt_data"] = {"pressure": 0, "temperature": 0, "AGL": 0, "velocity": 0}
                        if "acc_data" not in parsed_data:
                            parsed_data["acc_data"] = {"ax": 0, "ay": 0, "az": 0, "pitch": 0, "roll": 0}
                        if "gyro_data" not in parsed_data:
                            parsed_data["gyro_data"] = {"gx": 0, "gy": 0, "gz": 0}
                        if "chute_state" not in parsed_data:
                            parsed_data["chute_state"] = {"pyro1_state": 0, "pyro2_state": 0}
                        
                        if mqtt_connected:
                            client.publish(MQTT_TELEMETRY_TOPIC, json.dumps(parsed_data))
                            print(f"✅ JSON DATA SENT TO MQTT: {MQTT_TELEMETRY_TOPIC}")
                        else:
                            print(f"❌ MQTT NOT CONNECTED - Data not sent")
                        
                        mode = "MQTT" if parsed_data.get("wifi_rssi", 0) != 0 else "Beacon"
                        rssi_info = f"RSSI: {parsed_data.get('wifi_rssi', 0)}dBm" if parsed_data.get("wifi_rssi", 0) != 0 else "No WiFi RSSI"
                        
                        logger.info(f"📡 [ESP32_JSON] #{status['packets_received']} - "
                                  f"Record #{parsed_data.get('record_number', 'N/A')} - "
                                  f"{mode} Mode - {rssi_info}")
                        
                    except json.JSONDecodeError as e:
                        print(f"❌ FAILED TO PARSE JSON: {e}")
                        logger.warning(f"⚠️ Failed to parse JSON: {e}")
                
                else:
                    # Other debug output from flight computer
                    if line.strip():  # Only log non-empty lines
                        print(f"🖥️ OTHER: {line}")
                        logger.debug(f"🖥️ Other: {line}")
                        
        except KeyboardInterrupt:
            logger.info("👋 Interrupted. Exiting...")
            break
        except Exception as e:
            logger.error(f"❌ Unexpected error: {e}")
            time.sleep(1)  # Brief pause on error
        
        time.sleep(0.01)  # Reduced delay for faster response

if __name__ == '__main__':
    logger.info("🚀 Starting base station server for N4 Flight Computer...")
    logger.info("📡 Supporting JSON telemetry from ESP32 base station")
    logger.info("🔧 RSSI detection: >0 = MQTT mode, 0 = Beacon mode")
    logger.info("🔍 DEBUGGING ENABLED: All serial data will be printed")
    main_loop()