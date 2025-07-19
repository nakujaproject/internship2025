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
    
    if command in ["ARM", "DISARM", "DIS"] and serial_conn and serial_conn.is_open:
        # Convert DISARM to DIS for base station compatibility
        if command == "DISARM":
            command_to_send = "DIS"
        else:
            command_to_send = command
            
        serial_conn.write((command_to_send + "\n").encode())
        logger.info(f"✅ Forwarded command '{command}' as '{command_to_send}' to base station")
        
        # Publish command confirmation
        if mqtt_connected:
            cmd_status = {
                "command": command,
                "sent_to_base": command_to_send,
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
    logger.info("📡 Will keep monitoring even after 15s timeout - connection persists")

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
                
                # Handle different message types from base station
                if line.startswith("LOG:"):
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
                    # Status message from base station
                    status_data = line[7:]  # Remove "STATUS:" prefix
                    try:
                        status_json = json.loads(status_data)
                        if mqtt_connected:
                            # Publish base station internal status
                            enhanced_status = {
                                "type": "base_station_internal",
                                "timestamp": time.time(),
                                **status_json
                            }
                            client.publish(f"{MQTT_STATUS_TOPIC}/internal", json.dumps(enhanced_status))
                        
                        logger.debug(f"📊 Base station: Armed={status_json.get('armed', 'Unknown')}, "
                                   f"Packets={status_json.get('packets_received', 0)}")
                    except json.JSONDecodeError:
                        logger.warning(f"⚠️ Invalid status JSON: {status_data}")
                        
                else:
                    # Regular telemetry data - this indicates active connection
                    try:
                        parsed = json.loads(line)
                        
                        # Update connection status
                        connection_status.update_telemetry_received()
                        
                        # Add connection metadata
                        status = connection_status.check_connection_status()
                        parsed["connection_status"] = {
                            "connected": True,
                            "packet_age_ms": 0,  # Fresh packet
                            "total_packets": status["packets_received"]
                        }
                        
                        if mqtt_connected:
                            client.publish(MQTT_TELEMETRY_TOPIC, json.dumps(parsed))
                        
                        logger.info(f"📡 Telemetry #{status['packets_received']} - "
                                  f"Record #{parsed.get('record_number', 'N/A')} "
                                  f"[CONNECTED]")
                        
                    except json.JSONDecodeError:
                        logger.warning(f"⚠️ Invalid telemetry JSON: {line}")
                        
        except KeyboardInterrupt:
            logger.info("👋 Interrupted. Exiting...")
            break
        except Exception as e:
            logger.error(f"❌ Unexpected error: {e}")
            time.sleep(1)  # Brief pause on error
        
        time.sleep(0.01)  # Reduced delay for faster response

if __name__ == '__main__':
    logger.info("🚀 Starting base station server with continuous monitoring...")
    logger.info("📡 Connection will persist even after 15s timeout")
    main_loop()