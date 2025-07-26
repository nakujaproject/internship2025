import serial
import json
import logging
import time
import threading
import paho.mqtt.client as mqtt
from serial.tools import list_ports
from threading import Lock
from datetime import datetime

# === CONFIG ===
SERIAL_BAUD = 115200
MQTT_BROKER = "localhost"  # Update with your broker IP
MQTT_PORT = 1883
MQTT_TELEMETRY_TOPIC = "n4/flight-computer-1"
MQTT_COMMAND_TOPIC = "n4/commands"
MQTT_LOG_TOPIC = "n4/logs"
MQTT_STATUS_TOPIC = "n4/base-station-status"
APP_TELEMETRY_TOPIC = "n4/app/flight-computer-1"  # Unified app channel
APP_LOG_TOPIC = "n4/app/logs"
CONNECTION_TIMEOUT = 15000  # 15 seconds in milliseconds
HEARTBEAT_INTERVAL = 5      # Send status every 5 seconds
COMMAND_TIMEOUT = 3         # Wait 3 seconds for command response
AUTO_DETECT_INTERVAL = 5    # Check for active connection every 5 seconds

# === SERIAL CONNECTION ===
serial_conn = None
serial_lock = threading.Lock()

def find_esp32_port():
    """Find ESP32 port with enhanced detection"""
    ports = list_ports.comports()
    esp32_ids = ['ESP32', 'CP210', 'CH340', 'CH341', 'Silicon Labs']
    
    for port in ports:
        desc = port.description or ""
        hwid = port.hwid or ""
        if any(id in desc for id in esp32_ids) or '10C4:EA60' in hwid:
            logger.info(f"Found ESP32 at {port.device}")
            return port.device
    return None

def open_serial():
    global serial_conn
    with serial_lock:
        if serial_conn and serial_conn.is_open:
            serial_conn.close()
        
        port = find_esp32_port()
        if not port:
            logger.error("No ESP32 port found")
            return False
            
        try:
            serial_conn = serial.Serial(
                port=port,
                baudrate=SERIAL_BAUD,
                timeout=1,
                write_timeout=1,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            time.sleep(2)  # Wait for connection
            serial_conn.reset_input_buffer()
            logger.info(f"Serial connected to {port}")
            return True
        except Exception as e:
            logger.error(f"Serial connection failed: {e}")
            return False

def close_serial():
    global serial_conn
    with serial_lock:
        if serial_conn and serial_conn.is_open:
            serial_conn.close()
            logger.info("Serial port closed")

# === LOGGING ===
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger("BaseStationServer")

# === CONNECTION STATUS ===
class ConnectionStatus:
    def __init__(self):
        self.lock = Lock()
        self.last_serial = 0
        self.last_mqtt = 0
        self.current_mode = "UNKNOWN"
        
    def update_serial(self):
        with self.lock:
            self.last_serial = time.time()
            
    def update_mqtt(self):
        with self.lock:
            self.last_mqtt = time.time()
            
    def get_mode(self):
        with self.lock:
            now = time.time()
            serial_active = (now - self.last_serial) < AUTO_DETECT_INTERVAL*2
            mqtt_active = (now - self.last_mqtt) < AUTO_DETECT_INTERVAL*2
            
            if serial_active and mqtt_active:
                return "DUAL"
            elif mqtt_active:
                return "MQTT"
            elif serial_active:
                return "BEACON"
            return "UNKNOWN"

connection_status = ConnectionStatus()

# === MQTT CLIENT ===
client = mqtt.Client(protocol=mqtt.MQTTv311)  # Using protocol version 3.1.1
mqtt_connected = False

def on_mqtt_connect(client, userdata, flags, rc):
    global mqtt_connected
    if rc == 0:
        mqtt_connected = True
        client.subscribe(MQTT_COMMAND_TOPIC)
        client.subscribe(MQTT_TELEMETRY_TOPIC)
        logger.info("✅ MQTT connected")
    else:
        mqtt_connected = False
        logger.error(f"❌ MQTT connection failed with code {rc}")

def on_mqtt_disconnect(client, userdata, rc):
    global mqtt_connected
    mqtt_connected = False
    logger.warning("⚠️ MQTT disconnected")

def on_mqtt_message(client, userdata, msg):
    try:
        if msg.topic == MQTT_COMMAND_TOPIC:
            cmd = msg.payload.decode().strip()
            logger.info(f"📨 MQTT command: {cmd}")
            send_command(cmd, "MQTT")
            
        elif msg.topic == MQTT_TELEMETRY_TOPIC:
            payload = msg.payload.decode(errors='replace').strip()
            if not payload:
                return
                
            # Forward to app with consistent format
            try:
                # Check if it's CSV format (like your example)
                if "," in payload and not payload.startswith("{"):
                    fields = payload.split(",")
                    if len(fields) >= 23:  # Match your CSV format
                        data = {
                            "record_number": int(fields[0]),
                            "operation_mode": int(fields[1]),
                            "state": int(fields[2]),
                            "acc_data": {
                                "ax": float(fields[3]),
                                "ay": float(fields[4]),
                                "az": float(fields[5]),
                                "pitch": float(fields[6]),
                                "roll": float(fields[7])
                            },
                            "gyro_data": {
                                "gx": float(fields[8]),
                                "gy": float(fields[9]),
                                "gz": float(fields[10])
                            },
                            "gps_data": {
                                "latitude": float(fields[11]),
                                "longitude": float(fields[12]),
                                "altitude": float(fields[13]),
                                "time": int(fields[14])
                            },
                            "alt_data": {
                                "pressure": float(fields[15]),
                                "temperature": float(fields[16]),
                                "AGL": float(fields[17]),
                                "velocity": float(fields[18])
                            },
                            "chute_state": {
                                "drogue": int(fields[19]),
                                "main": int(fields[20])
                            },
                            "battery_voltage": float(fields[21]),
                            "wifi_rssi": int(fields[22]),
                            "communication_mode": "MQTT",
                            "timestamp": time.time(),
                            "raw": payload  # Include raw data for reference
                        }
                        client.publish(APP_TELEMETRY_TOPIC, json.dumps(data))
                        connection_status.update_mqtt()
                        logger.debug(f"📡 Forwarded MQTT telemetry (CSV format)")
                else:
                    # Try to parse as JSON if not CSV
                    try:
                        data = json.loads(payload)
                        data["communication_mode"] = "MQTT"
                        client.publish(APP_TELEMETRY_TOPIC, json.dumps(data))
                        connection_status.update_mqtt()
                        logger.debug(f"📡 Forwarded MQTT telemetry (JSON format)")
                    except json.JSONDecodeError:
                        logger.warning(f"⚠️ Unrecognized MQTT payload format: {payload}")
            except Exception as e:
                logger.error(f"❌ MQTT telemetry processing error: {e}")
                
    except Exception as e:
        logger.error(f"❌ MQTT message handling error: {e}")

client.on_connect = on_mqtt_connect
client.on_disconnect = on_mqtt_disconnect
client.on_message = on_mqtt_message

try:
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_start()
    logger.info("🔌 MQTT setup initiated...")
except Exception as e:
    logger.error(f"❌ MQTT setup failed: {e}")

# === DATA HANDLING ===
def process_serial_data(line):
    """Process incoming serial data and forward to app with consistent format"""
    try:
        line = line.strip()
        if not line:
            return
            
        # Skip log/status messages
        if line.startswith(("LOG:", "STATUS:")):
            logger.debug(f"📝 [SERIAL] {line}")
            return
            
        # Check if it's CSV format (like your MQTT data)
        if "," in line and not line.startswith("{"):
            fields = line.split(",")
            if len(fields) >= 23:  # Match your CSV format
                data = {
                    "record_number": int(fields[0]),
                    "operation_mode": int(fields[1]),
                    "state": int(fields[2]),
                    "acc_data": {
                        "ax": float(fields[3]),
                        "ay": float(fields[4]),
                        "az": float(fields[5]),
                        "pitch": float(fields[6]),
                        "roll": float(fields[7])
                    },
                    "gyro_data": {
                        "gx": float(fields[8]),
                        "gy": float(fields[9]),
                        "gz": float(fields[10])
                    },
                    "gps_data": {
                        "latitude": float(fields[11]),
                        "longitude": float(fields[12]),
                        "altitude": float(fields[13]),
                        "time": int(fields[14])
                    },
                    "alt_data": {
                        "pressure": float(fields[15]),
                        "temperature": float(fields[16]),
                        "AGL": float(fields[17]),
                        "velocity": float(fields[18])
                    },
                    "chute_state": {
                        "drogue": int(fields[19]),
                        "main": int(fields[20])
                    },
                    "battery_voltage": float(fields[21]),
                    "wifi_rssi": int(fields[22]),
                    "communication_mode": "BEACON",
                    "timestamp": time.time(),
                    "raw": line  # Include raw data for reference
                }
                client.publish(APP_TELEMETRY_TOPIC, json.dumps(data))
                connection_status.update_serial()
                logger.debug(f"📡 Forwarded beacon telemetry")
            else:
                logger.warning(f"⚠️ Invalid CSV format: {line}")
        else:
            # Try to parse as JSON if not CSV
            try:
                data = json.loads(line)
                data["communication_mode"] = "BEACON"
                client.publish(APP_TELEMETRY_TOPIC, json.dumps(data))
                connection_status.update_serial()
                logger.debug(f"📡 Forwarded beacon telemetry (JSON format)")
            except json.JSONDecodeError:
                logger.warning(f"⚠️ Unrecognized serial payload format: {line}")
                
    except Exception as e:
        logger.error(f"❌ Serial data processing error: {e}")

def send_command(cmd, source):
    """Send command to flight computer"""
    if not serial_conn or not serial_conn.is_open:
        if not open_serial():
            return False
    
    try:
        with serial_lock:
            serial_conn.reset_input_buffer()
            serial_conn.reset_output_buffer()
            serial_conn.write((cmd + "\n").encode())
            serial_conn.flush()
            
            logger.info(f"📤 Sent command: {cmd} (source: {source})")
            
            # Wait for response
            start_time = time.time()
            while time.time() - start_time < COMMAND_TIMEOUT:
                if serial_conn.in_waiting:
                    response = serial_conn.readline().decode(errors='replace').strip()
                    if response:
                        logger.info(f"📩 Response: {response}")
                        return True
                time.sleep(0.1)
                
            logger.warning("⌛ No response received")
            return False
            
    except Exception as e:
        logger.error(f"❌ Command failed: {e}")
        close_serial()
        return False

# === MAIN LOOP ===
def main_loop():
    """Main processing loop"""
    last_status = 0
    
    while True:
        try:
            # Check serial for data
            if serial_conn and serial_conn.is_open:
                while serial_conn.in_waiting:
                    line = serial_conn.readline().decode(errors='replace').strip()
                    if line:
                        process_serial_data(line)
            
            # Send status periodically
            if time.time() - last_status > HEARTBEAT_INTERVAL:
                status = {
                    "mode": connection_status.get_mode(),
                    "timestamp": time.time(),
                    "serial_connected": serial_conn is not None and serial_conn.is_open,
                    "mqtt_connected": mqtt_connected,
                    "last_serial": connection_status.last_serial,
                    "last_mqtt": connection_status.last_mqtt
                }
                client.publish(MQTT_STATUS_TOPIC, json.dumps(status))
                last_status = time.time()
            
            time.sleep(0.1)
            
        except KeyboardInterrupt:
            break
        except Exception as e:
            logger.error(f"❌ Main loop error: {e}")
            time.sleep(1)

def command_interface():
    """Interactive command interface"""
    print("\nN4 Command Interface (type 'help' for commands)")
    while True:
        try:
            cmd = input("n4> ").strip().lower()
            if cmd == "help":
                print("Commands: status, mqtt, beacon, dual, arm, disarm, reset, quit")
            elif cmd == "quit":
                break
            elif cmd in ["status", "mqtt", "beacon", "dual", "arm", "disarm", "reset"]:
                send_command(cmd.upper(), "CONSOLE")
            else:
                print("Unknown command")
        except (EOFError, KeyboardInterrupt):
            break
        except Exception as e:
            print(f"Error: {e}")

if __name__ == '__main__':
    logger.info("🚀 Starting N4 Base Station Server")
    
    # Start main loop in background
    loop_thread = threading.Thread(target=main_loop, daemon=True)
    loop_thread.start()
    
    # Run command interface
    try:
        command_interface()
    finally:
        close_serial()
        client.disconnect()
        logger.info("👋 Shutdown complete")