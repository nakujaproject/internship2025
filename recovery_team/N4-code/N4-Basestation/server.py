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
MQTT_BROKER = "localhost"
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
                write_timeout=1
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
client = mqtt.Client()
mqtt_connected = False

def on_mqtt_connect(*args):
    global mqtt_connected
    mqtt_connected = True
    client.subscribe(MQTT_COMMAND_TOPIC)
    client.subscribe(MQTT_TELEMETRY_TOPIC)
    logger.info("MQTT connected")

def on_mqtt_disconnect(*args):
    global mqtt_connected
    mqtt_connected = False
    logger.warning("MQTT disconnected")

def on_mqtt_message(client, userdata, msg):
    try:
        if msg.topic == MQTT_COMMAND_TOPIC:
            cmd = msg.payload.decode().strip()
            logger.info(f"MQTT command: {cmd}")
            send_command(cmd, "MQTT")
        elif msg.topic == MQTT_TELEMETRY_TOPIC:
            # Forward and normalize telemetry from flight computer
            try:
                payload = msg.payload.decode(errors='replace')
                # Try to parse as JSON
                if payload.strip().startswith("{"):
                    data = json.loads(payload)
                    data["communication_mode"] = "MQTT"
                # Try to parse as CSV only if it looks like CSV (no braces or colons)
                elif ":" not in payload and "{" not in payload and "," in payload:
                    parts = payload.split(',')
                    if len(parts) >= 23:
                        # Forward all fields as a dict, do not slice or drop any
                        data = {f"field_{i+1}": parts[i] for i in range(len(parts))}
                        data["fields"] = parts  # full raw CSV fields
                        # Optionally, map known fields for clarity (if present)
                        try:
                            data["record_number"] = int(parts[0])
                            data["state"] = int(parts[2])
                            data["acc_data"] = {
                                "ax": float(parts[3]),
                                "ay": float(parts[4]),
                                "az": float(parts[5])
                            }
                            data["rssi"] = parts[22]
                        except Exception:
                            pass
                        data["timestamp"] = time.time()
                        data["communication_mode"] = "MQTT"
                    else:
                        logger.debug(f"[MQTT] Ignored non-telemetry line: {payload}")
                        return
                else:
                    logger.debug(f"[MQTT] Ignored unknown line: {payload}")
                    return
                client.publish(APP_TELEMETRY_TOPIC, json.dumps(data))
                connection_status.update_mqtt()
                logger.debug(f"Forwarded MQTT telemetry: {data}")
            except Exception as e:
                logger.error(f"[MQTT] Telemetry parse error: {e}")
    except Exception as e:
        logger.error(f"MQTT error: {e}")

client.on_connect = on_mqtt_connect
client.on_disconnect = on_mqtt_disconnect
client.on_message = on_mqtt_message

try:
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_start()
except Exception as e:
    logger.error(f"MQTT setup failed: {e}")

# === DATA HANDLING ===
def process_serial_data(line):
    """Process incoming serial data and forward to app"""
    try:
        # Skip log/status lines
        if line.startswith("LOG:") or line.startswith("STATUS:"):
            logger.debug(f"[SERIAL] Skipped non-telemetry line: {line}")
            return
        # Try to parse as JSON
        if line.strip().startswith("{"):
            try:
                data = json.loads(line)
                data["communication_mode"] = "BEACON"
            except Exception as e:
                logger.debug(f"[SERIAL] Ignored invalid/corrupt JSON: {line}")
                return
        # Try to parse as CSV only if it looks like CSV (no braces or colons)
        elif ":" not in line and "{" not in line and "," in line:
            parts = line.split(',')
            if len(parts) >= 23:
                try:
                    # Forward all fields as a dict, do not slice or drop any
                    data = {f"field_{i+1}": parts[i] for i in range(len(parts))}
                    data["fields"] = parts  # full raw CSV fields
                    # Optionally, map known fields for clarity (if present)
                    try:
                        data["record_number"] = int(parts[0])
                        data["state"] = int(parts[2])
                        data["acc_data"] = {
                            "ax": float(parts[3]),
                            "ay": float(parts[4]),
                            "az": float(parts[5])
                        }
                        data["rssi"] = parts[22]
                    except Exception:
                        pass
                    data["communication_mode"] = "BEACON"
                    data["timestamp"] = time.time()
                except Exception as e:
                    logger.debug(f"[SERIAL] Ignored corrupt CSV: {line}")
                    return
            else:
                logger.debug(f"[SERIAL] Ignored non-telemetry line: {line}")
                return
        else:
            logger.debug(f"[SERIAL] Ignored unknown line: {line}")
            return
        # Forward to app
        client.publish(APP_TELEMETRY_TOPIC, json.dumps(data))
        connection_status.update_serial()
        logger.debug(f"Forwarded beacon data: {data}")
    except Exception as e:
        logger.error(f"Serial processing error: {e}")


# --- Command retry logic ---
import threading
command_retry_threads = {}

def send_command(cmd, source):
    """Send command to flight computer, with retry for mode commands until data is received."""
    mode_cmds = ["CMD_MQTT_MODE", "CMD_BEACON_MODE", "CMD_DUAL_MODE"]
    cmd_upper = cmd.strip().upper()
    if cmd_upper in mode_cmds:
        # Start a retry thread for this mode command
        if cmd_upper in command_retry_threads and command_retry_threads[cmd_upper].is_alive():
            logger.info(f"Retry thread for {cmd_upper} already running.")
        else:
            t = threading.Thread(target=retry_mode_command, args=(cmd_upper, source), daemon=True)
            command_retry_threads[cmd_upper] = t
            t.start()
        return True
    # For non-mode commands, just send once
    return _send_command_once(cmd, source)

def _send_command_once(cmd, source):
    if not serial_conn or not serial_conn.is_open:
        if not open_serial():
            return False
    try:
        serial_conn.write((cmd + "\n").encode())
        logger.info(f"Sent command: {cmd} from {source}")
        # If switching to MQTT mode, ensure MQTT client is connected and subscribed
        if cmd.strip().upper() == "CMD_MQTT_MODE":
            try:
                if not mqtt_connected:
                    logger.info("[MQTT] Reconnecting MQTT client for CMD_MQTT_MODE...")
                    client.reconnect()
                client.subscribe(MQTT_COMMAND_TOPIC)
                client.subscribe(MQTT_TELEMETRY_TOPIC)
                logger.info("[MQTT] Subscribed to command and telemetry topics after CMD_MQTT_MODE")
            except Exception as e:
                logger.error(f"[MQTT] Error ensuring MQTT setup: {e}")
        return True
    except Exception as e:
        logger.error(f"Command failed: {e}")
        close_serial()
        return False

def retry_mode_command(cmd_upper, source):
    """Keep sending the mode command every 2s until data is received via the expected channel."""
    logger.info(f"Starting retry thread for {cmd_upper}")
    last_data_time = 0
    def got_data():
        mode = cmd_upper.replace("CMD_", "").replace("_MODE", "")
        now = time.time()
        if mode == "MQTT":
            # Data received via MQTT
            return (now - connection_status.last_mqtt) < 3
        elif mode == "BEACON":
            # Data received via serial
            return (now - connection_status.last_serial) < 3
        elif mode == "DUAL":
            # Data received via both
            return ((now - connection_status.last_serial) < 3) and ((now - connection_status.last_mqtt) < 3)
        return False
    while not got_data():
        _send_command_once(cmd_upper, source)
        time.sleep(2)
    logger.info(f"Data received for {cmd_upper}, stopping retry thread.")

# === MAIN LOOP ===
def main_loop():
    """Main processing loop"""
    last_status = 0
    
    while True:
        try:
            # Check serial for data
            if serial_conn and serial_conn.is_open:
                if serial_conn.in_waiting:
                    line = serial_conn.readline().decode(errors='replace').strip()
                    if line:
                        process_serial_data(line)
            
            # Send status periodically
            if time.time() - last_status > HEARTBEAT_INTERVAL:
                status = {
                    "mode": connection_status.get_mode(),
                    "timestamp": time.time(),
                    "serial_connected": serial_conn is not None and serial_conn.is_open,
                    "mqtt_connected": mqtt_connected
                }
                client.publish(MQTT_STATUS_TOPIC, json.dumps(status))
                last_status = time.time()
            
            time.sleep(0.1)
            
        except KeyboardInterrupt:
            break
        except Exception as e:
            logger.error(f"Main loop error: {e}")
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
    logger.info("Starting N4 Base Station")
    
    # Start main loop in background
    loop_thread = threading.Thread(target=main_loop, daemon=True)
    loop_thread.start()
    
    # Run command interface
    try:
        command_interface()
    finally:
        close_serial()
        client.disconnect()
        logger.info("Shutdown complete")