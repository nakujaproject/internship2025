import serial
import json
import logging
import time
import paho.mqtt.client as mqtt
from serial.tools import list_ports

# === CONFIG ===
SERIAL_BAUD = 115200
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TELEMETRY_TOPIC = "n4/flight-computer-1"
MQTT_COMMAND_TOPIC = "n4/commands"
MQTT_LOG_TOPIC = "n4/logs"

# === LOGGING ===
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("BaseStationServer")

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
        logger.info(f"Connected to serial port: {port}")
        return ser
    except Exception as e:
        logger.error(f"Failed to open serial port: {e}")
        return None

serial_conn = initialize_serial()

# === MQTT ===
client = mqtt.Client()

# Command listener (MQTT -> Serial)
def on_mqtt_message(client, userdata, msg):
    command = msg.payload.decode().strip()
    logger.info(f"Received command via MQTT: {command}")
    
    # FIX: Accept both ARM, DISARM, and DIS
    if command in ["ARM", "DISARM", "DIS"] and serial_conn and serial_conn.is_open:
        # Convert DISARM to DIS for base station compatibility
        if command == "DISARM":
            command_to_send = "DIS"
        else:
            command_to_send = command
            
        serial_conn.write((command_to_send + "\n").encode())
        logger.info(f"Forwarded command '{command}' as '{command_to_send}' to base station")
    else:
        logger.warning(f"Unknown command or serial not available: {command}")

# MQTT connect
client.on_message = on_mqtt_message
client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.subscribe(MQTT_COMMAND_TOPIC)
client.loop_start()

# === MAIN LOOP: SERIAL -> MQTT ===
def main_loop():
    if not serial_conn:
        logger.error("Serial not connected.")
        return

    while True:
        try:
            if serial_conn.in_waiting:
                line = serial_conn.readline().decode().strip()
                
                # Handle different message types from base station
                if line.startswith("LOG:"):
                    # Log message
                    log_data = line[4:]  # Remove "LOG:" prefix
                    try:
                        log_json = json.loads(log_data)
                        client.publish(MQTT_LOG_TOPIC, json.dumps(log_json))
                        logger.info(f"Published log: {log_json.get('message', 'Unknown')}")
                    except json.JSONDecodeError:
                        logger.warning(f"Invalid log JSON: {log_data}")
                        
                elif line.startswith("STATUS:"):
                    # Status message - just log it, don't publish as telemetry
                    status_data = line[7:]  # Remove "STATUS:" prefix
                    try:
                        status_json = json.loads(status_data)
                        logger.debug(f"Base station status: Armed={status_json.get('armed', 'Unknown')}, Packets={status_json.get('packets_received', 0)}")
                    except json.JSONDecodeError:
                        logger.warning(f"Invalid status JSON: {status_data}")
                        
                else:
                    # Regular telemetry data
                    try:
                        parsed = json.loads(line)
                        client.publish(MQTT_TELEMETRY_TOPIC, json.dumps(parsed))
                        logger.info(f"Published telemetry - Record #{parsed.get('record_number', 'N/A')}")
                    except json.JSONDecodeError:
                        logger.warning(f"Invalid telemetry JSON: {line}")
                        
        except KeyboardInterrupt:
            logger.info("Interrupted. Exiting...")
            break
        except Exception as e:
            logger.error(f"Unexpected error: {e}")
        time.sleep(0.01)  # Reduced delay for faster command response

if __name__ == '__main__':
    logger.info("Starting base station server...")
    main_loop()