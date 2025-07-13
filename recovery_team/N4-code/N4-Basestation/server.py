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

# === LOGGING ===
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("BaseStationServer")

# === FIND AND CONNECT TO SERIAL DEVICE ===
def find_arduino_port():
    ports = list_ports.comports()
    for port in ports:
        if "Arduino" in port.description or "CH340" in port.description or "USB" in port.description:
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
    if command in ["ARM", "DIS"] and serial_conn and serial_conn.is_open:
        serial_conn.write((command + "\n").encode())
        logger.info(f"Forwarded command '{command}' to serial")

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
                logger.debug(f"Raw data: {line}")
                try:
                    parsed = json.loads(line)
                    client.publish(MQTT_TELEMETRY_TOPIC, json.dumps(parsed))
                    logger.info(f"Published telemetry to {MQTT_TELEMETRY_TOPIC}")
                except json.JSONDecodeError:
                    logger.warning(f"Invalid JSON: {line}")
        except KeyboardInterrupt:
            logger.info("Interrupted. Exiting...")
            break
        except Exception as e:
            logger.error(f"Unexpected error: {e}")
        time.sleep(0.05)

if __name__ == '__main__':
    logger.info("Starting base station server...")
    main_loop()
