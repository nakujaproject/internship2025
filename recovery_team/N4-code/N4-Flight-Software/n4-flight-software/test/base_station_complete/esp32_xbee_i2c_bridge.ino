/**
 * N4 Base Station - ESP32 (XBee + Radio Bridge)
 * Version: 9.0 - Mega I2C Architecture
 *
 * Role: Radio bridge only. Receives telemetry from rocket via XBee (UART1),
 *       ESP-NOW beacon (WiFi promiscuous), and forwards everything to the
 *       Arduino Mega over I2C (ESP32 = I2C slave, address 0x08).
 *       Receives command strings from Mega via I2C and sends them to the
 *       rocket via XBee UART / ESP-NOW.
 *
 * Communication:
 *   UART0  (Serial)      : USB debug output only
 *   UART1  (XBeeSerial)  : XBee Pro 900HP @ 115200 baud
 *   I2C    (Wire slave)  : SDA=21, SCL=22, address=0x08
 *                          → receives commands from Mega (I2C master)
 *                          → Mega requests CSV telemetry via I2C read
 *
 * I2C Protocol (Mega ↔ ESP32):
 *   WRITE (Mega→ESP32) : ASCII command string, e.g. "ARM\n"
 *   READ  (Mega→ESP32) : ESP32 responds with latest CSV line (up to 255 bytes)
 *                        First byte = length of payload that follows.
 *                        If no new data: first byte = 0x00.
 *
 * Hardware:
 *   ESP32 DevKit
 *   XBee Pro 900HP  : RX=34, TX=32, RSSI_PWM=35
 *   I2C to Mega     : SDA=21, SCL=22 (with 4.7kΩ pull-ups to 3.3V)
 *
 * Note: ESP-NOW / beacon path kept intact from v8 for rocket-side radio.
 *       The ESP-01 UART bridge is removed; Mega replaces that role.
 */

#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <string.h>

// ====== I2C Slave Configuration ======
#define I2C_SLAVE_ADDR  0x08
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22

// ====== XBee Serial Configuration (UART1) ======
HardwareSerial XBeeSerial(1);
#define XBEE_RX       34
#define XBEE_TX       32
#define XBEE_RSSI_PIN 35
#define XBEE_BAUD     115200

// ====== Device / Rocket Identity ======
const char* DEVICE_ID   = "ESP32:N4_BASE_I2C_1";
uint8_t rocket_mac[]    = {0x08, 0xD1, 0xF9, 0x15, 0x9C, 0x04};
uint8_t my_mac[]        = {0x14, 0x08, 0x08, 0xAC, 0x82, 0xF8};

// ====== Communication Mode ======
enum CommunicationMode { MODE_MQTT=0, MODE_BEACON=1, MODE_XBEE=2, MODE_AUTO=3 };
CommunicationMode currentMode = MODE_AUTO;
bool xbeeEnabled = true;
uint32_t lastXBeePacketTime = 0;

// ====== Connection Tracking ======
const uint32_t CONNECTION_TIMEOUT = 15000;
bool hasEverConnected   = false;
bool currentlyConnected = false;
bool rocketArmed        = false;

// ====== 29-Field Telemetry Structure ======
struct TelemetryData {
  uint32_t record_number;
  uint8_t  operation_mode;
  uint8_t  state;
  float    ax, ay, az;
  float    pitch, roll;
  float    gx, gy, gz;
  float    latitude, longitude;
  float    gps_altitude;
  uint32_t gps_time;
  float    pressure;
  float    temperature;
  float    altitude_agl;
  float    velocity;
  uint8_t  drogue_pin_state;
  uint8_t  drogue_pin_engaged;
  uint8_t  main_chute_pin_state;
  uint8_t  main_chute_pin_engaged;
  float    battery_voltage;
  float    logic_rail_3v3_voltage;
  uint8_t  power_rail_low;
  int32_t  wifi_rssi;
  float    kalman_altitude;
  float    kalman_vertical_velocity;
};

TelemetryData telemetry;
int8_t  beacon_rssi     = -100;
int8_t  xbee_rssi       = -100;
uint32_t packetsReceived = 0;
uint32_t lastPacketTime  = 0;
bool     dataReceived    = false;

// ====== Command Handling ======
String   lastCommand     = "";
bool     commandPending  = false;
uint32_t commandSentTime = 0;
const uint32_t COMMAND_TIMEOUT = 5000;

// ====== PWM Config Status ======
struct PWMConfigStatus {
  float    vcc = 0, drogue_v = 0, main_v = 0;
  unsigned long drogue_time_ms = 0, main_time_ms = 0;
  bool     config_received    = false;
  uint32_t last_update_time   = 0;
} pwm_status;

// ====== I2C Telemetry Buffer ======
// Holds the latest CSV line ready to be read by Mega.
// Protected by a simple flag (single-core safe for this use-case).
#define I2C_BUF_SIZE 300
volatile char  i2cTxBuf[I2C_BUF_SIZE];
volatile uint8_t i2cTxLen = 0;
volatile bool  i2cDataReady = false;

// Incoming command from Mega (received in I2C write ISR)
#define CMD_BUF_SIZE 220
volatile char  i2cCmdBuf[CMD_BUF_SIZE];
volatile uint8_t i2cCmdLen = 0;
volatile bool  i2cCmdReady = false;

// Command fragment assembler (supports long commands over 32-byte Mega I2C writes)
volatile char i2cCmdAssembleBuf[CMD_BUF_SIZE];
volatile uint8_t i2cCmdAssembleLen = 0;
volatile bool i2cCmdAssembleActive = false;

// ====== I2C Callbacks ======

bool copyCommandToReadyBuffer(const char* src, size_t len) {
  if (!src || len == 0) return false;
  if (len >= CMD_BUF_SIZE) len = CMD_BUF_SIZE - 1;
  memcpy((char*)i2cCmdBuf, src, len);
  i2cCmdBuf[len] = '\0';
  i2cCmdLen = (uint8_t)len;
  i2cCmdReady = true;
  return true;
}

void resetAssembleBuffer() {
  i2cCmdAssembleLen = 0;
  i2cCmdAssembleBuf[0] = '\0';
  i2cCmdAssembleActive = false;
}

bool appendAssembleFragment(const char* frag, size_t len) {
  if (!frag || len == 0) return false;
  size_t freeSpace = (CMD_BUF_SIZE - 1) - i2cCmdAssembleLen;
  if (len > freeSpace) {
    len = freeSpace;
  }
  memcpy((char*)i2cCmdAssembleBuf + i2cCmdAssembleLen, frag, len);
  i2cCmdAssembleLen += (uint8_t)len;
  i2cCmdAssembleBuf[i2cCmdAssembleLen] = '\0';
  return len > 0;
}

// Mega writes a command to ESP32
void onI2CReceive(int numBytes) {
  char rxBuf[64];
  uint8_t idx = 0;
  while (Wire.available() && idx < sizeof(rxBuf) - 1) {
    rxBuf[idx++] = Wire.read();
  }
  while (Wire.available()) Wire.read();  // discard overflow

  rxBuf[idx] = '\0';
  if (idx == 0) return;

  // Chunked command framing from Mega:
  // C:<full_command>
  // B:<first_chunk>, M:<middle_chunk>, E:<last_chunk>
  if (idx >= 2 && rxBuf[1] == ':') {
    const char frameType = rxBuf[0];
    const char* payload = rxBuf + 2;
    size_t payloadLen = strlen(payload);

    if (frameType == 'C') {
      copyCommandToReadyBuffer(payload, payloadLen);
      resetAssembleBuffer();
      return;
    }

    if (frameType == 'B') {
      resetAssembleBuffer();
      i2cCmdAssembleActive = true;
      appendAssembleFragment(payload, payloadLen);
      return;
    }

    if (frameType == 'M') {
      if (i2cCmdAssembleActive) {
        appendAssembleFragment(payload, payloadLen);
      }
      return;
    }

    if (frameType == 'E') {
      if (i2cCmdAssembleActive) {
        appendAssembleFragment(payload, payloadLen);
        copyCommandToReadyBuffer((const char*)i2cCmdAssembleBuf, i2cCmdAssembleLen);
      }
      resetAssembleBuffer();
      return;
    }
  }

  // Backward compatibility: treat payload as whole command.
  copyCommandToReadyBuffer(rxBuf, strlen(rxBuf));
}

// Mega requests latest telemetry CSV
void onI2CRequest() {
  if (i2cDataReady && i2cTxLen > 0) {
    Wire.write((uint8_t)i2cTxLen);                      // length byte first
    Wire.write((const uint8_t*)i2cTxBuf, i2cTxLen);    // payload
    i2cDataReady = false;                               // consumed
  } else {
    Wire.write((uint8_t)0x00);  // no new data
  }
}

// ====== Helper: Build CSV into I2C TX buffer ======
void updateI2CBuffer() {
  char buf[I2C_BUF_SIZE];
  int32_t linkRssi = (currentMode == MODE_XBEE) ? xbee_rssi : beacon_rssi;
  int n = snprintf(buf, sizeof(buf),
    "%lu,%u,%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
    "%.8f,%.8f,%.2f,%lu,%.2f,%.2f,%.2f,%.2f,"
    "%u,%u,%u,%u,%.2f,%.2f,%u,%d,%.2f,%.2f",
    telemetry.record_number,    telemetry.operation_mode, telemetry.state,
    telemetry.ax,               telemetry.ay,             telemetry.az,
    telemetry.pitch,            telemetry.roll,
    telemetry.gx,               telemetry.gy,             telemetry.gz,
    telemetry.latitude,         telemetry.longitude,
    telemetry.gps_altitude,     telemetry.gps_time,
    telemetry.pressure,         telemetry.temperature,
    telemetry.altitude_agl,     telemetry.velocity,
    telemetry.drogue_pin_state, telemetry.drogue_pin_engaged,
    telemetry.main_chute_pin_state, telemetry.main_chute_pin_engaged,
    telemetry.battery_voltage,  telemetry.logic_rail_3v3_voltage,
    telemetry.power_rail_low,   linkRssi,
    telemetry.kalman_altitude,  telemetry.kalman_vertical_velocity);

  if (n > 0 && n < I2C_BUF_SIZE) {
    memcpy((char*)i2cTxBuf, buf, n);
    i2cTxLen    = (uint8_t)n;
    i2cDataReady = true;
  }
}

void processCommandString(const String& commandRaw) {
  if (commandRaw.length() == 0) return;

  // Preserve original case for JSON payload processing.
  String raw = commandRaw;
  raw.trim();
  if (raw.length() == 0) return;

  if (raw.startsWith("SET_PWM:") || raw.startsWith("set_pwm:")) {
    String jsonPayload = raw.substring(8);
    jsonPayload.trim();

    StaticJsonDocument<256> testDoc;
    DeserializationError error = deserializeJson(testDoc, jsonPayload);
    if (error) {
      logUSB("ERROR", ("Invalid SET_PWM JSON: " + String(error.c_str())).c_str());
      return;
    }

    lastCommand = "CMD_SET_PWM_CONFIG:" + jsonPayload;
    commandPending = true;
    commandSentTime = millis();
    logUSB("INFO", "Queued CMD_SET_PWM_CONFIG via I2C/USB command path");
    return;
  }

  String command = raw;
  command.toUpperCase();
  logUSB("INFO", ("CMD: " + command).c_str());

  if (command == "ARM" || command == "DISARM" || command == "RESET" ||
      command == "MAIN_ON" || command == "MAIN_OFF" ||
      command == "DROGUE_ON" || command == "DROGUE_OFF") {
    lastCommand = command;
    commandPending = true;
    commandSentTime = millis();
  }
  else if (command == "CMD_MQTT_MODE" || command == "MQTT_MODE" || command == "MQTT") {
    lastCommand = "CMD_MQTT_MODE";
    commandPending = true;
    commandSentTime = millis();
  }
  else if (command == "CMD_BEACON_MODE" || command == "BEACON_MODE" || command == "BEACON") {
    lastCommand = "CMD_BEACON_MODE";
    commandPending = true;
    commandSentTime = millis();
  }
  else if (command == "CMD_XBEE_MODE" || command == "XBEE_MODE" || command == "XBEE") {
    lastCommand = "CMD_XBEE_MODE";
    commandPending = true;
    commandSentTime = millis();
  }
  else if (command == "CMD_AUTO_FALLBACK_ON" || command == "AUTO_FALLBACK_ON" || command == "AUTO_ON") {
    lastCommand = "CMD_AUTO_FALLBACK_ON";
    commandPending = true;
    commandSentTime = millis();
  }
  else if (command == "CMD_AUTO_FALLBACK_OFF" || command == "AUTO_FALLBACK_OFF" || command == "AUTO_OFF") {
    lastCommand = "CMD_AUTO_FALLBACK_OFF";
    commandPending = true;
    commandSentTime = millis();
  }
  else if (command == "XBEE_ON") {
    xbeeEnabled = true;
    logUSB("INFO", "XBee enabled");
  }
  else if (command == "XBEE_OFF") {
    xbeeEnabled = false;
    logUSB("INFO", "XBee disabled");
  }
  else if (command == "PWM_STATUS") {
    if (pwm_status.config_received) {
      char msg[200];
      snprintf(msg, sizeof(msg), "PWM: Vcc=%.1fV, Drogue=%.1fV(%lums), Main=%.1fV(%lums)",
               pwm_status.vcc, pwm_status.drogue_v, pwm_status.drogue_time_ms,
               pwm_status.main_v, pwm_status.main_time_ms);
      logUSB("INFO", msg);
    } else {
      logUSB("INFO", "No PWM config received yet");
    }
  }
  else if (command == "HELP") {
    logUSB("INFO", "Commands: ARM, DISARM, RESET, MAIN_ON, MAIN_OFF, DROGUE_ON, DROGUE_OFF");
    logUSB("INFO", "Modes: MQTT, BEACON, XBEE, AUTO_ON, AUTO_OFF");
    logUSB("INFO", "XBee: XBEE_ON, XBEE_OFF, XBEE_TEST");
    logUSB("INFO", "PWM: SET_PWM:{json}, PWM_STATUS");
    logUSB("INFO", "Status: STATUS, HELP");
  }
  else if (command == "XBEE_TEST") {
    if (XBeeSerial) {
      XBeeSerial.println("TEST_FROM_BASE_STATION");
      XBeeSerial.flush();
      int rawAnalog = analogRead(XBEE_RSSI_PIN);
      int8_t testRssi = readXBeeRSSI();
      char msg[96];
      snprintf(msg, sizeof(msg), "XBEE_TEST raw=%d rssi=%d", rawAnalog, testRssi);
      logUSB("INFO", msg);
    } else {
      logUSB("ERROR", "XBee UART not initialized");
    }
  }
  else if (command == "STATUS") {
    const char* modeStr = currentMode == MODE_XBEE ? "XBee" :
                          currentMode == MODE_BEACON ? "Beacon" :
                          currentMode == MODE_MQTT ? "MQTT" : "Auto";
    char msg[140];
    snprintf(msg, sizeof(msg), "Mode=%s XBee=%s Pkts=%lu Conn=%s",
             modeStr, xbeeEnabled ? "ON" : "OFF", (unsigned long)packetsReceived,
             currentlyConnected ? "YES" : "NO");
    logUSB("INFO", msg);
  }
}

// ====== Read XBee RSSI from PWM pin ======
int8_t readXBeeRSSI() {
  int   analogValue = analogRead(XBEE_RSSI_PIN);
  float voltage     = (analogValue / 4095.0f) * 3.3f;
  if (voltage >= 3.0f) return -40;
  else if (voltage >= 2.5f) return (int8_t)(-40  - (int8_t)((3.0f - voltage) * 40));
  else if (voltage >= 2.0f) return (int8_t)(-60  - (int8_t)((2.5f - voltage) * 40));
  else if (voltage >= 1.5f) return (int8_t)(-80  - (int8_t)((2.0f - voltage) * 20));
  else if (voltage >= 1.0f) return (int8_t)(-90  - (int8_t)((1.5f - voltage) * 20));
  else                      return -100;
}

// ====== USB-only log helper ======
void logUSB(const char* level, const char* msg) {
  Serial.print("[");
  Serial.print(level);
  Serial.print("] ");
  Serial.println(msg);
}

// ====== 29-Field CSV Parser ======
bool parseCSV(const char* csv, TelemetryData& data) {
  if (!csv || csv[0] == '\0') return false;
  char buffer[512];
  size_t len = strnlen(csv, sizeof(buffer) - 1);
  memcpy(buffer, csv, len);
  buffer[len] = '\0';
  char* sp = nullptr;
  char* token = strtok_r(buffer, ",", &sp);
  int field = 0;
  while (token && field < 29) {
    switch (field) {
      case  0: data.record_number         = (uint32_t)strtoul(token, nullptr, 10); break;
      case  1: data.operation_mode        = (uint8_t) strtoul(token, nullptr, 10); break;
      case  2: data.state                 = (uint8_t) strtoul(token, nullptr, 10); break;
      case  3: data.ax                    = strtof(token, nullptr); break;
      case  4: data.ay                    = strtof(token, nullptr); break;
      case  5: data.az                    = strtof(token, nullptr); break;
      case  6: data.pitch                 = strtof(token, nullptr); break;
      case  7: data.roll                  = strtof(token, nullptr); break;
      case  8: data.gx                    = strtof(token, nullptr); break;
      case  9: data.gy                    = strtof(token, nullptr); break;
      case 10: data.gz                    = strtof(token, nullptr); break;
      case 11: data.latitude              = strtof(token, nullptr); break;
      case 12: data.longitude             = strtof(token, nullptr); break;
      case 13: data.gps_altitude          = strtof(token, nullptr); break;
      case 14: data.gps_time              = (uint32_t)strtoul(token, nullptr, 10); break;
      case 15: data.pressure              = strtof(token, nullptr); break;
      case 16: data.temperature           = strtof(token, nullptr); break;
      case 17: data.altitude_agl          = strtof(token, nullptr); break;
      case 18: data.velocity              = strtof(token, nullptr); break;
      case 19: data.drogue_pin_state      = (uint8_t)strtoul(token, nullptr, 10); break;
      case 20: data.drogue_pin_engaged    = (uint8_t)strtoul(token, nullptr, 10); break;
      case 21: data.main_chute_pin_state  = (uint8_t)strtoul(token, nullptr, 10); break;
      case 22: data.main_chute_pin_engaged= (uint8_t)strtoul(token, nullptr, 10); break;
      case 23: data.battery_voltage       = strtof(token, nullptr); break;
      case 24: data.logic_rail_3v3_voltage= strtof(token, nullptr); break;
      case 25: data.power_rail_low        = (uint8_t)strtoul(token, nullptr, 10); break;
      case 26: data.wifi_rssi             = (int32_t)strtol (token, nullptr, 10); break;
      case 27: data.kalman_altitude       = strtof(token, nullptr); break;
      case 28: data.kalman_vertical_velocity = strtof(token, nullptr); break;
    }
    token = strtok_r(nullptr, ",", &sp);
    field++;
  }
  return (field == 29);
}

// ====== Connection Status ======
void updateConnectionStatus() {
  if (dataReceived) {
    hasEverConnected   = true;
    currentlyConnected = ((millis() - lastPacketTime) <= CONNECTION_TIMEOUT);
  } else {
    currentlyConnected = false;
  }
}

// ====== XBee Telemetry Handler ======
void handleXBeeTelemetry() {
  if (!xbeeEnabled || !XBeeSerial.available()) return;
  static String xbeeBuffer = "";
  while (XBeeSerial.available()) {
    char c = XBeeSerial.read();
    if (c == '\n' || c == '\r') {
      if (xbeeBuffer.length() > 0) {
        if (parseCSV(xbeeBuffer.c_str(), telemetry)) {
          packetsReceived++;
          lastXBeePacketTime = millis();
          lastPacketTime     = millis();
          dataReceived       = true;
          xbee_rssi          = readXBeeRSSI();
          if (currentMode == MODE_AUTO) currentMode = MODE_XBEE;
          rocketArmed = (telemetry.operation_mode == 1);
          updateConnectionStatus();
          updateI2CBuffer();   // ← make available to Mega
          Serial.printf("[XBEE] Pkt#%lu Rec#%lu Alt=%.1fm Vel=%.1fm/s\n",
                        (unsigned long)packetsReceived,
                        (unsigned long)telemetry.record_number,
                        telemetry.altitude_agl, telemetry.velocity);
        } else {
          logUSB("WARN", ("XBee parse failed: " + xbeeBuffer).c_str());
        }
        xbeeBuffer = "";
      }
    } else if (c >= 32 && c <= 126) {
      xbeeBuffer += c;
      if (xbeeBuffer.length() > 300) xbeeBuffer = "";
    }
  }
}

// ====== Beacon (WiFi Promiscuous) Handler ======
void handleBeacon(const wifi_promiscuous_pkt_t* pkt) {
  const uint8_t* payload = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  int8_t raw_rssi = pkt->rx_ctrl.rssi;
  beacon_rssi = (raw_rssi > -20) ? raw_rssi : (raw_rssi < -100 ? -100 : raw_rssi);

  if (len < 60 || payload[0] != 0x80) return;
  if (memcmp(&payload[10], rocket_mac, 6) != 0) return;

  for (int i = 36; i < len - 2; i++) {
    if (payload[i] == 0xDD) {
      uint8_t data_len = payload[i + 1];
      char csv_data[512];
      memcpy(csv_data, &payload[i + 2], data_len);
      csv_data[data_len] = '\0';
      if (parseCSV(csv_data, telemetry)) {
        packetsReceived++;
        lastPacketTime = millis();
        dataReceived   = true;
        rocketArmed    = (telemetry.operation_mode == 1);
        if (currentMode == MODE_AUTO) currentMode = MODE_BEACON;
        updateConnectionStatus();
        updateI2CBuffer();   // ← make available to Mega
        Serial.printf("[BCNR] Pkt#%lu Rec#%lu Alt=%.1fm Vel=%.1fm/s RSSI=%d\n",
                      (unsigned long)packetsReceived,
                      (unsigned long)telemetry.record_number,
                      telemetry.altitude_agl, telemetry.velocity, beacon_rssi);
      }
      break;
    }
  }
}

void promiscuousRx(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type == WIFI_PKT_MGMT) handleBeacon((wifi_promiscuous_pkt_t*)buf);
}

// ====== ESP-NOW Receive Callback ======
void onESPNowDataReceived(const esp_now_recv_info_t* recv_info,
                          const uint8_t* incomingData, int len) {
  String response = "";
  for (int i = 0; i < len; i++) response += (char)incomingData[i];
  response.trim();
  logUSB("INFO", ("ESP-NOW resp: " + response).c_str());

  if (response.startsWith("PWM_CONFIG_OK:")) {
    int vccStart    = response.indexOf("Vcc=")    + 4;
    int drogueStart = response.indexOf("Drogue=") + 7;
    int mainStart   = response.indexOf("Main=")   + 5;
    if (vccStart > 3 && drogueStart > 6 && mainStart > 4) {
      pwm_status.vcc          = response.substring(vccStart, response.indexOf(',', vccStart)).toFloat();
      String dStr = response.substring(drogueStart, response.indexOf(',', drogueStart));
      pwm_status.drogue_v      = dStr.substring(0, dStr.indexOf('V')).toFloat();
      pwm_status.drogue_time_ms= dStr.substring(dStr.indexOf('(') + 1, dStr.indexOf("ms)")).toInt();
      String mStr = response.substring(mainStart);
      pwm_status.main_v        = mStr.substring(0, mStr.indexOf('V')).toFloat();
      pwm_status.main_time_ms  = mStr.substring(mStr.indexOf('(') + 1, mStr.indexOf("ms)")).toInt();
      pwm_status.config_received   = true;
      pwm_status.last_update_time  = millis();
    }
  }
}

// ====== Process I2C Command from Mega ======
void processI2CCommand(const char* cmd) {
  processCommandString(String(cmd));
}

// ====== Send Command to Rocket (ESP-NOW) ======
void sendCommandToRocket() {
  if (!commandPending) return;
  if (millis() - commandSentTime > COMMAND_TIMEOUT) {
    logUSB("WARN", ("CMD timeout: " + lastCommand).c_str());
    commandPending = false;
    return;
  }
  esp_err_t result = ESP_FAIL;
  if      (lastCommand.startsWith("CMD_SET_PWM_CONFIG:"))
    result = esp_now_send(rocket_mac, (uint8_t*)lastCommand.c_str(), lastCommand.length());
  else if (lastCommand == "ARM")                  result = esp_now_send(rocket_mac, (uint8_t*)"ARM",                   3);
  else if (lastCommand == "DISARM")               result = esp_now_send(rocket_mac, (uint8_t*)"DISARM",               6);
  else if (lastCommand == "RESET")                result = esp_now_send(rocket_mac, (uint8_t*)"RESET",                5);
  else if (lastCommand == "MAIN_ON")              result = esp_now_send(rocket_mac, (uint8_t*)"MAIN_ON",              7);
  else if (lastCommand == "MAIN_OFF")             result = esp_now_send(rocket_mac, (uint8_t*)"MAIN_OFF",             8);
  else if (lastCommand == "DROGUE_ON")            result = esp_now_send(rocket_mac, (uint8_t*)"DROGUE_ON",            9);
  else if (lastCommand == "DROGUE_OFF")           result = esp_now_send(rocket_mac, (uint8_t*)"DROGUE_OFF",          10);
  else if (lastCommand == "CMD_MQTT_MODE")        result = esp_now_send(rocket_mac, (uint8_t*)"CMD_MQTT_MODE",       13);
  else if (lastCommand == "CMD_BEACON_MODE")      result = esp_now_send(rocket_mac, (uint8_t*)"CMD_BEACON_MODE",     15);
  else if (lastCommand == "CMD_XBEE_MODE")        result = esp_now_send(rocket_mac, (uint8_t*)"CMD_XBEE_MODE",       13);
  else if (lastCommand == "CMD_AUTO_FALLBACK_ON") result = esp_now_send(rocket_mac, (uint8_t*)"CMD_AUTO_FALLBACK_ON",20);
  else if (lastCommand == "CMD_AUTO_FALLBACK_OFF")result = esp_now_send(rocket_mac, (uint8_t*)"CMD_AUTO_FALLBACK_OFF",21);

  String sendMsg = (result == ESP_OK ? "ESP-NOW sent: " : "ESP-NOW fail: ") + lastCommand;
  logUSB(result == ESP_OK ? "INFO" : "ERROR", sendMsg.c_str());
  commandPending = false;
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  delay(500);

  logUSB("INFO", "========================================");
  logUSB("INFO", "N4 ESP32 Base Station v9.0 - I2C Slave");
  logUSB("INFO", "========================================");

  // I2C slave
  Wire.begin(I2C_SLAVE_ADDR, I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  Wire.onReceive(onI2CReceive);
  Wire.onRequest(onI2CRequest);
  logUSB("INFO", "I2C slave ready @ 0x08");

  // XBee UART
  pinMode(XBEE_RSSI_PIN, INPUT);
  analogReadResolution(12);
  XBeeSerial.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX, XBEE_TX);
  delay(200);
  logUSB("INFO", "XBee UART1 ready (RX=34, TX=32, RSSI=35)");
  XBeeSerial.println("XBEE_BASE_STATION_READY");

  // WiFi / ESP-NOW
  WiFi.mode(WIFI_STA);
  esp_wifi_set_mac(WIFI_IF_STA, my_mac);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    logUSB("ERROR", "ESP-NOW init failed — restarting");
    ESP.restart();
  }
  esp_now_register_recv_cb(onESPNowDataReceived);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, rocket_mac, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) logUSB("ERROR", "Failed to add ESP-NOW peer");
  else                                        logUSB("INFO",  "ESP-NOW peer registered");

  // Beacon promiscuous
  wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(promiscuousRx);
  logUSB("INFO", "Beacon listening enabled");
  logUSB("INFO", "Ready — waiting for telemetry");
}

// ====== MAIN LOOP ======
void loop() {
  // Handle incoming I2C command (set in ISR)
  if (i2cCmdReady) {
    i2cCmdReady = false;
    processI2CCommand((const char*)i2cCmdBuf);
  }

  // Optional direct USB command path for bench testing without Mega.
  if (Serial.available()) {
    String usbCmd = Serial.readStringUntil('\n');
    usbCmd.trim();
    if (usbCmd.length() > 0) {
      processCommandString(usbCmd);
    }
  }

  handleXBeeTelemetry();
  sendCommandToRocket();
  updateConnectionStatus();

  // Periodic USB status log every 10s
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 10000) {
    lastStatus = millis();
    const char* modeStr = currentMode == MODE_XBEE   ? "XBee"  :
                          currentMode == MODE_BEACON  ? "Beacon":
                          currentMode == MODE_MQTT    ? "MQTT"  : "Auto";
    char msg[120];
    snprintf(msg, sizeof(msg),
             "Status: mode=%s xbee=%s pkts=%lu armed=%s connected=%s",
             modeStr, xbeeEnabled?"ON":"OFF",
             (unsigned long)packetsReceived,
             rocketArmed?"YES":"NO",
             currentlyConnected?"YES":"NO");
    logUSB("INFO", msg);
  }

  // Connection timeout warning
  if (dataReceived) {
    static bool warnGiven = false;
    if (millis() - lastPacketTime > CONNECTION_TIMEOUT) {
      if (!warnGiven) { logUSB("WARN", "Telemetry timeout"); warnGiven = true; }
    } else { warnGiven = false; }
  }

  delay(5);
}

