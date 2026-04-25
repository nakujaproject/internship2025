/**
 * N4 Base Station - Arduino Mega (Main Controller)
 * Version: 2.0
 *
 * Role: I2C SLAVE (address 0x08).  ESP32 is the I2C MASTER.
 *   - Responds to ESP32 master I2C polls (requestFrom) with latest telemetry CSV
 *     received from ESP32 via the same I2C write path, OR with "CMD:xxx" when
 *     the Mega has a queued command to relay to the rocket via the ESP32.
 *   - Receives telemetry CSV written by ESP32 master over I2C (Wire.onReceive).
 *     POLLING-BASED receive — no ISR storage, checked in main loop.
 *   - Parses the 29-field CSV and displays key values on a 20×4 I2C LCD.
 *   - Mirrors every raw CSV to the Python server over USB Serial (Serial).
 *   - Sends heartbeat JSON to Python server over USB Serial when no data.
 *   - Sends telemetry JSON to Python server over USB Serial on each packet.
 *   - Accepts commands from: (a) Python server / laptop USB Serial, (b) 4×3 keypad.
 *   - Relays commands to ESP32 via I2C write (ESP32 forwards to rocket via XBee/ESP-NOW).
 *   - Drives 3 status LEDs:
 *       LED_ARMED  (22, RED)   — ON when operation_mode == 1  (armed, from CSV)
 *       LED_DROGUE (24, GREEN) — ON when drogue_pin_engaged == 1 (from CSV)
 *       LED_MAIN   (26, BLUE)  — ON when main_chute_pin_engaged == 1 (from CSV)
 *   - Reads ARM safety switch hardware interlock.
 *
 * I2C Bus:
 *   Mega SDA = pin 20,  SCL = pin 21  (hardware I2C)
 *   Mega I2C slave address : 0x08   ← THIS DEVICE
 *   LCD I2C address        : 0x27   (try 0x3F if blank)
 *   Pull-ups: 4.7 kΩ to 3.3 V on SDA and SCL (use 3.3 V — ESP32 logic level)
 *
 * NOTE: ESP32 is 3.3 V logic. Use a bi-directional level shifter on SDA/SCL.
 *
 * I2C Protocol (Mega slave ↔ ESP32 master):
 *   READ   ESP32 requestFrom → Mega sends: CSV telemetry string, or "CMD:xxx\0"
 *   WRITE  ESP32 beginTransmission → Mega receives: CSV telemetry string
 *
 * Keypad Mapping (4×3 matrix):
 *   1 = ARM      2 = DISARM    3 = RESET
 *   4 = MAIN_ON  5 = MAIN_OFF  6 = PAGE cycle
 *   7 = DROGUE_ON 8 = DROGUE_OFF 9 = STATUS
 *   * = XBEE     0 = HELP      # = CLEAR LCD
 *
 * Hardware Pins:
 *   Keypad ROWS : 52, 50, 48, 46
 *   Keypad COLS : 53, 51, 49
 *   LED_ARMED   : 22 (RED)   — armed flag from CSV
 *   LED_DROGUE  : 24 (GREEN) — drogue deployed from CSV
 *   LED_MAIN    : 26 (BLUE)  — main chute deployed from CSV
 *   ARM_SWITCH  : 28 (INPUT_PULLUP, active LOW = armed position)
 */

 //Switched to uart for testing

// Increase AVR hardware UART RX buffer BEFORE any includes.
// Default is 64 bytes — a single 29-field CSV is ~160 bytes, which overflows
// the default buffer causing silent byte drops and corrupted/concatenated lines.
#define SERIAL_RX_BUFFER_SIZE 256
#define SERIAL2_RX_BUFFER_SIZE 256

#include <Keypad.h>
#include <LiquidCrystal_I2C.h>



// ====== UART: Mega <-> ESP32 (Serial2) ======
#define UART_TX_BUF_SIZE      255   // max bytes per UART packet

// ====== Python Server / COM Port Detection ======
const char* DEVICE_ID = "MEGA:N4_BASE_MEGA_1";  // matches Python server detection

// ====== LCD (20×4) ======
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ====== Keypad (4×3) ======
const byte ROWS = 4, COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {52, 50, 48, 46};
byte colPins[COLS] = {53, 51, 49};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ====== GPIO: LEDs & Arm Switch ======
#define LED_ARMED      22   // RED   – rocket armed (operation_mode==1 from CSV)
#define LED_DROGUE     24   // GREEN – drogue deployed (drogue_pin_engaged==1 from CSV)
#define LED_MAIN       26   // BLUE  – main chute deployed (main_chute_pin_engaged==1 from CSV)
#define ARM_SWITCH_PIN 28   // Safety arm switch (INPUT_PULLUP, LOW = armed pos)

// ====== Telemetry Cache (29 fields) ======
struct TelemetryData {
  uint32_t record_number;
  uint8_t  operation_mode, state;
  float    ax, ay, az, pitch, roll;
  float    gx, gy, gz;
  float    latitude, longitude, gps_altitude;
  uint32_t gps_time;
  float    pressure, temperature;
  float    altitude_agl, velocity;
  uint8_t  drogue_pin_state, drogue_pin_engaged;
  uint8_t  main_chute_pin_state, main_chute_pin_engaged;
  float    battery_voltage, logic_rail_3v3_voltage;
  uint8_t  power_rail_low;
  int32_t  wifi_rssi;
  float    kalman_altitude, kalman_vertical_velocity;
};
TelemetryData telem;


// ====== UART Buffers ======
char     uartTxBuf[UART_TX_BUF_SIZE];
uint8_t  uartTxLen = 0;
char     uartRxBuf[UART_TX_BUF_SIZE];
uint8_t  uartRxLen = 0;


// ====== Queued command to relay to ESP32 via UART ======
char     pendingCmd[64];
bool     pendingCmdReady = false;

// ====== Deployment latch (stays true once fired) ======
bool drogueEverFired = false;
bool mainEverFired   = false;

// ====== Toggle states for ARM / MAIN / DROGUE buttons ======
bool toggleArmed  = false;   // key 1: toggles ARM ↔ DISARM
bool toggleMain   = false;   // key 4: toggles MAIN_ON ↔ MAIN_OFF
bool toggleDrogue = false;   // key 7: toggles DROGUE_ON ↔ DROGUE_OFF

// ====== PWM / Pyro Settings (editable on PAGE_PWM) ======
struct PyroSettings {
  float  vcc_v        = 14.8f;   // main power rail voltage
  float  drogue_v     = 9.0f;    // drogue firing voltage
  float  main_v       = 10.0f;   // main chute firing voltage
  uint16_t drogue_ms  = 3000;    // drogue fire duration (ms)
  uint16_t main_ms    = 5000;    // main fire duration (ms)
  uint8_t  field      = 0;       // currently selected field (0-4)
} pyroSet;

// ====== LCD pages ======
enum DisplayPage {
  PAGE_MAIN=0, PAGE_ALT, PAGE_GPS, PAGE_PYRO, PAGE_DEPLOY, PAGE_PWM, PAGE_COUNT
};
DisplayPage currentPage = PAGE_MAIN;

// ====== State ======
bool     dataReceived      = false;
bool     rocketArmed       = false;
bool     connected         = false;
uint32_t packetsReceived   = 0;
uint32_t lastPacketTime    = 0;
const uint32_t CONN_TIMEOUT= 15000;

// LCD refresh throttle
uint32_t lastLcdRefresh = 0;
const uint32_t LCD_REFRESH_MS = 300;

// ====== Forward Declarations ======
bool    parseCSV(const char* csv);
void    buildUARTTxBuffer();
void    processUARTReceive();
void    queueCommandForESP32(const char* cmd);
void    sendCommandToESP32(const char* cmd);
void    refreshLCD(bool forceAll);
void    handleKeypad();
void    handleSerialCommands();
void    updateLEDs();
void    printHelp();
void    logToLaptop(const char* prefix, const char* msg);
String  buildStatusString();
void    sendHeartbeat();
void    sendTelemetryJSON();

// ====== 29-Field CSV Parser ======
bool parseCSV(const char* csv) {
  if (!csv || csv[0] == '\0') return false;
  char buf[512];
  size_t len = strnlen(csv, sizeof(buf) - 1);
  memcpy(buf, csv, len);
  buf[len] = '\0';
  char* sp = nullptr;
  char* tok = strtok_r(buf, ",", &sp);
  int f = 0;
  while (tok && f < 29) {
    switch (f) {
      case  0: telem.record_number         = (uint32_t)strtoul(tok,nullptr,10); break;
      case  1: telem.operation_mode        = (uint8_t) strtoul(tok,nullptr,10); break;
      case  2: telem.state                 = (uint8_t) strtoul(tok,nullptr,10); break;
      case  3: telem.ax                    = atof(tok); break;
      case  4: telem.ay                    = atof(tok); break;
      case  5: telem.az                    = atof(tok); break;
      case  6: telem.pitch                 = atof(tok); break;
      case  7: telem.roll                  = atof(tok); break;
      case  8: telem.gx                    = atof(tok); break;
      case  9: telem.gy                    = atof(tok); break;
      case 10: telem.gz                    = atof(tok); break;
      case 11: telem.latitude              = atof(tok); break;
      case 12: telem.longitude             = atof(tok); break;
      case 13: telem.gps_altitude          = atof(tok); break;
      case 14: telem.gps_time              = (uint32_t)strtoul(tok,nullptr,10); break;
      case 15: telem.pressure              = atof(tok); break;
      case 16: telem.temperature           = atof(tok); break;
      case 17: telem.altitude_agl          = atof(tok); break;
      case 18: telem.velocity              = atof(tok); break;
      case 19: telem.drogue_pin_state      = (uint8_t)strtoul(tok,nullptr,10); break;
      case 20: telem.drogue_pin_engaged    = (uint8_t)strtoul(tok,nullptr,10); break;
      case 21: telem.main_chute_pin_state  = (uint8_t)strtoul(tok,nullptr,10); break;
      case 22: telem.main_chute_pin_engaged= (uint8_t)strtoul(tok,nullptr,10); break;
      case 23: telem.battery_voltage       = atof(tok); break;
      case 24: telem.logic_rail_3v3_voltage= atof(tok); break;
      case 25: telem.power_rail_low        = (uint8_t)strtoul(tok,nullptr,10); break;
      case 26: telem.wifi_rssi             = (int32_t)strtol(tok,nullptr,10); break;
      case 27: telem.kalman_altitude       = atof(tok); break;
      case 28: telem.kalman_vertical_velocity= atof(tok); break;
    }
    tok = strtok_r(nullptr, ",", &sp);
    f++;
  }
  return (f == 29);
}


// ====== UART Receive Handler (called from main loop) ======
// Reads Serial2 byte-by-byte. On each complete line (newline-terminated) it
// immediately processes and resets, then CONTINUES reading — so if multiple
// lines have stacked up in the buffer they are all handled in one loop() call
// rather than one-per-call (which caused keypad lag and RX overflow).
void processUARTReceive() {
  while (Serial2.available()) {
    char c = Serial2.read();

    if (c == '\n' || c == '\r') {
      if (uartRxLen == 0) continue;   // skip empty / bare CR lines

      uartRxBuf[uartRxLen] = '\0';

      // ── process the completed line ──────────────────────────────
      String payload = String(uartRxBuf);
      payload.trim();

      // Normalize: remove leading CSV: if present
      if (payload.startsWith("CSV:")) payload = payload.substring(4);

      // If multiple CSV entries were concatenated, split them defensively
      while (payload.length() > 0) {
        int nextIdx = payload.indexOf("CSV:");
        String part;
        if (nextIdx >= 0) {
          part = payload.substring(0, nextIdx);
          payload = payload.substring(nextIdx + 4);
        } else {
          part = payload;
          payload = "";
        }

        part.trim();
        if (part.length() == 0) continue;

        Serial.print("CSV:");
        Serial.println(part);

        if (parseCSV(part.c_str())) {
          packetsReceived++;
          lastPacketTime = millis();
          dataReceived   = true;
          rocketArmed    = (telem.operation_mode == 1);
          connected      = true;

          if (telem.drogue_pin_engaged)     drogueEverFired = true;
          if (telem.main_chute_pin_engaged) mainEverFired   = true;

          buildUARTTxBuffer();
          sendTelemetryJSON();
          logToLaptop("TELEM_OK", part.c_str());
        } else {
          // Only log PARSE_FAIL if it looks like it could have been a CSV
          if (part.length() > 0 && part.charAt(0) >= '0' && part.charAt(0) <= '9') {
            logToLaptop("PARSE_FAIL", part.c_str());
          }
        }
      }

      // ── reset buffer and keep reading ───────────────────────────
      uartRxLen = 0;

    } else if (uartRxLen < UART_TX_BUF_SIZE - 1) {
      uartRxBuf[uartRxLen++] = c;
    } else {
      // Line longer than buffer — discard and start fresh
      uartRxLen = 0;
    }
  }
}

// ====== Build UART TX Buffer ======
// Rebuilds the 29-field CSV string the ESP32 reads back on request.
void buildUARTTxBuffer() {
  int n = snprintf(uartTxBuf, sizeof(uartTxBuf),
    "%lu,%u,%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.8f,%.8f,%.2f,%lu,%.2f,%.2f,%.2f,%.2f,%u,%u,%u,%u,%.2f,%.2f,%u,%d,%.2f,%.2f\n",
    (unsigned long)telem.record_number,
    telem.operation_mode, telem.state,
    telem.ax, telem.ay, telem.az,
    telem.pitch, telem.roll,
    telem.gx, telem.gy, telem.gz,
    telem.latitude, telem.longitude, telem.gps_altitude,
    (unsigned long)telem.gps_time,
    telem.pressure, telem.temperature,
    telem.altitude_agl, telem.velocity,
    telem.drogue_pin_state, telem.drogue_pin_engaged,
    telem.main_chute_pin_state, telem.main_chute_pin_engaged,
    telem.battery_voltage, telem.logic_rail_3v3_voltage,
    telem.power_rail_low, (int)telem.wifi_rssi,
    telem.kalman_altitude, telem.kalman_vertical_velocity);
  uartTxLen = (uint8_t)constrain(n, 0, UART_TX_BUF_SIZE - 1);
}

// ====== Queue Command for ESP32 (UART) ======
// Mega can send command at any time via UART.
void queueCommandForESP32(const char* cmd) {
  snprintf(pendingCmd, sizeof(pendingCmd), "CMD:%s\n", cmd);
  pendingCmdReady = true;
  logToLaptop("CMD_QUEUED", cmd);
  lcd.setCursor(0, 3);
  char row[21];
  snprintf(row, sizeof(row), "%-20s", (String("CMD:") + cmd).c_str());
  lcd.print(row);
}

// ====== Send Command to ESP32 via UART ======
void sendCommandToESP32(const char* cmd) {
  queueCommandForESP32(cmd);
  if (pendingCmdReady) {
    Serial2.print(pendingCmd);
    pendingCmdReady = false;
  }
}

// ====== HEARTBEAT for Python Server COM Port Detection ======
// Transferred from ESP32 (TODO: Add to mega Code)
// Sends JSON every 500ms over USB Serial when no telemetry,
// so the Python server can auto-detect this COM port.
void sendHeartbeat() {
  static uint32_t lastHeartbeatSend = 0;
  if (dataReceived) return;
  if (millis() - lastHeartbeatSend < 500) return;

  // Build minimal JSON heartbeat matching the Python server's expected format
  char jsonBuf[200];
  snprintf(jsonBuf, sizeof(jsonBuf),
    "{\"type\":\"heartbeat\",\"uptime\":%lu,\"device_id\":\"%s\",\"waiting_for_data\":true}|%s",
    millis(), DEVICE_ID, DEVICE_ID);

  Serial.println(jsonBuf);
  lastHeartbeatSend = millis();
}

// ====== Send Telemetry JSON to Python Server ======
// Uses dtostrf() for every float because AVR (Mega 2560) snprintf does NOT
// support %f/%g by default — it silently outputs '?' for every float argument.
// dtostrf(value, minWidth, decimals, buffer) is the correct AVR approach.
void sendTelemetryJSON() {
  if (!dataReceived) return;

  // Scratch buffers for dtostrf conversions
  char fa[12], fb[12], fc[12], fd[12], fe[12], ff[12];

  static char jsonBuf[900];
  int pos = 0;

  // ── record / mode / state ─────────────────────────────────────────────
  dtostrf(telem.battery_voltage, 6, 2, fa);
  pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
    "{\"record_number\":%lu,\"operation_mode\":%u,\"state\":%u,"
    "\"battery_voltage\":%s,\"wifi_rssi\":%d,",
    (unsigned long)telem.record_number,
    telem.operation_mode, telem.state,
    fa, (int)telem.wifi_rssi);

  // ── accelerometer ────────────────────────────────────────────────────
  dtostrf(telem.ax,    7, 2, fa); dtostrf(telem.ay,   7, 2, fb);
  dtostrf(telem.az,    7, 2, fc); dtostrf(telem.pitch, 7, 2, fd);
  dtostrf(telem.roll,  7, 2, fe);
  pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
    "\"acc_data\":{\"ax\":%s,\"ay\":%s,\"az\":%s,\"pitch\":%s,\"roll\":%s},",
    fa, fb, fc, fd, fe);

  // ── gyro ─────────────────────────────────────────────────────────────
  dtostrf(telem.gx, 7, 2, fa); dtostrf(telem.gy, 7, 2, fb); dtostrf(telem.gz, 7, 2, fc);
  pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
    "\"gyro_data\":{\"gx\":%s,\"gy\":%s,\"gz\":%s},", fa, fb, fc);

  // ── GPS ──────────────────────────────────────────────────────────────
  dtostrf(telem.latitude,     11, 6, fa); dtostrf(telem.longitude,    11, 6, fb);
  dtostrf(telem.gps_altitude,  8, 2, fc);
  pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
    "\"gps_data\":{\"latitude\":%s,\"longitude\":%s,\"gps_altitude\":%s,\"time\":%lu},",
    fa, fb, fc, (unsigned long)telem.gps_time);

  // ── altitude / Kalman ─────────────────────────────────────────────────
  dtostrf(telem.pressure,                 9, 2, fa);
  dtostrf(telem.temperature,              7, 2, fb);
  dtostrf(telem.altitude_agl,             9, 2, fc);
  dtostrf(telem.velocity,                 7, 2, fd);
  dtostrf(telem.kalman_altitude,          9, 2, fe);
  dtostrf(telem.kalman_vertical_velocity, 8, 2, ff);
  pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
    "\"alt_data\":{\"pressure\":%s,\"temperature\":%s,\"AGL\":%s,"
    "\"velocity\":%s,\"kalman_altitude\":%s,\"kalman_vertical_velocity\":%s},",
    fa, fb, fc, fd, fe, ff);

  // ── power ─────────────────────────────────────────────────────────────
  dtostrf(telem.logic_rail_3v3_voltage, 6, 2, fa);
  pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
    "\"power\":{\"logic_rail_3v3_voltage\":%s,\"power_rail_low\":%u},",
    fa, telem.power_rail_low);

  // ── chute state ───────────────────────────────────────────────────────
  pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
    "\"chute_state\":{\"pyro1_state\":%u,\"pyro1_engaged\":%u,"
    "\"pyro2_state\":%u,\"pyro2_engaged\":%u},",
    telem.drogue_pin_state, telem.drogue_pin_engaged,
    telem.main_chute_pin_state, telem.main_chute_pin_engaged);

  // ── footer ────────────────────────────────────────────────────────────
  pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
    "\"timestamp\":%lu,\"packets_received\":%lu}|%s\n",
    millis(), (unsigned long)packetsReceived, DEVICE_ID);

  // Write in chunks so we never block the main loop
  int written = 0;
  while (written < pos) {
    int avail = Serial.availableForWrite();
    if (avail <= 0) { delay(1); continue; }
    int chunk = min(avail, pos - written);
    Serial.write((const uint8_t*)(jsonBuf + written), chunk);
    written += chunk;
  }
}

// ====== LED Update ======
void updateLEDs() {
  // RED: ON when operation_mode == 1 (armed) in received CSV
  digitalWrite(LED_ARMED,  (dataReceived && rocketArmed) ? HIGH : LOW);

  // GREEN: ON when drogue fired (latched — stays ON once triggered)
  digitalWrite(LED_DROGUE, (dataReceived && (telem.drogue_pin_engaged || drogueEverFired)) ? HIGH : LOW);

  // BLUE: ON when main chute fired (latched)
  digitalWrite(LED_MAIN,   (dataReceived && (telem.main_chute_pin_engaged || mainEverFired)) ? HIGH : LOW);
}

// ====== LCD Rendering ======

// Helper: print a 20-char wide padded string on a row
void lcdRow(uint8_t row, const char* str) {
  char padded[21];
  snprintf(padded, sizeof(padded), "%-20s", str);
  lcd.setCursor(0, row);
  lcd.print(padded);
}

void refreshLCD(bool forceAll) {
  uint32_t now = millis();
  if (!forceAll && (now - lastLcdRefresh < LCD_REFRESH_MS)) return;
  lastLcdRefresh = now;

  char line[21];

  switch (currentPage) {
    // ── PAGE 0: Main summary ──────────────────────────────────────
    case PAGE_MAIN: {
      const char* connStr  = connected   ? "CONN" : "----";
      const char* armedStr = rocketArmed ? "ARMED" : "SAFE ";
      snprintf(line, sizeof(line), "N4 %-4s %5s Pk%4lu",
               connStr, armedStr, (unsigned long)(packetsReceived % 10000));
      lcdRow(0, line);

      if (dataReceived) {
        char sagl[8], svel[7];
        dtostrf(telem.altitude_agl, 7, 1, sagl);
        dtostrf(telem.velocity,     5, 1, svel);
        snprintf(line, sizeof(line), "ALT%7sm V%5sm/s", sagl, svel);
      } else {
        snprintf(line, sizeof(line), "ALT  ------  ------");
      }
      lcdRow(1, line);

      if (dataReceived) {
        char sbat[5], srssi[5];
        dtostrf(telem.battery_voltage, 4, 1, sbat);
        snprintf(srssi, sizeof(srssi), "%4d", (int)telem.wifi_rssi);
        snprintf(line, sizeof(line), "BAT%sV  RSSI%sdBm", sbat, srssi);
      } else {
        snprintf(line, sizeof(line), "BAT  ----  RSSI ----");
      }
      lcdRow(2, line);
      break;
    }

    // ── PAGE 1: Altitude detail ───────────────────────────────────
    case PAGE_ALT: {
      lcdRow(0, "--- ALTITUDE PAGE --");
      if (dataReceived) {
        char sa[10], sk[10], sv[9];
        dtostrf(telem.altitude_agl,             9, 2, sa);
        dtostrf(telem.kalman_altitude,           9, 2, sk);
        dtostrf(telem.kalman_vertical_velocity,  7, 2, sv);
        snprintf(line, sizeof(line), "AGL %s m", sa);  lcdRow(1, line);
        snprintf(line, sizeof(line), "KLM %s m", sk);  lcdRow(2, line);
        snprintf(line, sizeof(line), "KVL %s m/s", sv); lcdRow(3, line);
      } else {
        lcdRow(1, "No data"); lcdRow(2, ""); lcdRow(3, "");
      }
      break;
    }

    // ── PAGE 2: GPS ───────────────────────────────────────────────
    case PAGE_GPS: {
      lcdRow(0, "---   GPS PAGE   ---");
      if (dataReceived) {
        char slat[12], slon[12], salt[8];
        dtostrf(telem.latitude,    11, 6, slat);
        dtostrf(telem.longitude,   11, 6, slon);
        dtostrf(telem.gps_altitude, 7, 1, salt);
        snprintf(line, sizeof(line), "LAT %s", slat);   lcdRow(1, line);
        snprintf(line, sizeof(line), "LON %s", slon);   lcdRow(2, line);
        snprintf(line, sizeof(line), "ALT %sm T%6lu", salt, (unsigned long)telem.gps_time);
        lcdRow(3, line);
      } else {
        lcdRow(1, "No GPS data"); lcdRow(2, ""); lcdRow(3, "");
      }
      break;
    }

    // ── PAGE 3: Pyro / chute status ───────────────────────────────
    case PAGE_PYRO: {
      lcdRow(0, "--- PYRO PAGE    ---");
      if (dataReceived) {
        char sv3[6];
        dtostrf(telem.logic_rail_3v3_voltage, 5, 2, sv3);
        snprintf(line, sizeof(line), "DRG St%d En%d",
                 telem.drogue_pin_state, telem.drogue_pin_engaged);
        lcdRow(1, line);
        snprintf(line, sizeof(line), "MAN St%d En%d",
                 telem.main_chute_pin_state, telem.main_chute_pin_engaged);
        lcdRow(2, line);
        snprintf(line, sizeof(line), "3V3 %sV Rail%s",
                 sv3, telem.power_rail_low ? "LOW" : " OK");
        lcdRow(3, line);
      } else {
        lcdRow(1, "No data"); lcdRow(2, ""); lcdRow(3, "");
      }
      break;
    }

    // ── PAGE 4: Deployment Status ─────────────────────────────────
    case PAGE_DEPLOY: {
      lcdRow(0, "-- DEPLOYMENT STATUS");

      // Row 1: Armed
      if (dataReceived) {
        lcdRow(1, rocketArmed ? "ARMED:  [  YES  ]   " : "ARMED:  [  NO   ]   ");
      } else {
        lcdRow(1, "ARMED:  [ ----  ]   ");
      }

      // Row 2: Drogue — latched, stays FIRED once triggered
      if (dataReceived) {
        bool df = telem.drogue_pin_engaged || drogueEverFired;
        lcdRow(2, df ? "DROGUE: [ FIRED ]   " : "DROGUE: [  ---  ]   ");
      } else {
        lcdRow(2, "DROGUE: [ ----  ]   ");
      }

      // Row 3: Main chute — latched
      if (dataReceived) {
        bool mf = telem.main_chute_pin_engaged || mainEverFired;
        lcdRow(3, mf ? "MAIN:   [ FIRED ]   " : "MAIN:   [  ---  ]   ");
      } else {
        lcdRow(3, "MAIN:   [ ----  ]   ");
      }
      break;
    }

    // ── PAGE 5: PWM / Pyro Settings ──────────────────────────────────
    case PAGE_PWM: {
      // Row 0: header + selected field name
      const char* fieldNames[] = {"VCC","DRG-V","MN-V","DRG-T","MN-T"};
      snprintf(line, sizeof(line), "-PWM SET [%s]-------", fieldNames[pyroSet.field]);
      lcdRow(0, line);

      // Row 1: voltages
      char svcc[7], sdrv[7], smnv[7];
      dtostrf(pyroSet.vcc_v,    5, 1, svcc);
      dtostrf(pyroSet.drogue_v, 5, 1, sdrv);
      dtostrf(pyroSet.main_v,   5, 1, smnv);
      snprintf(line, sizeof(line), "VCC%sV DRG%sV", svcc, sdrv);
      lcdRow(1, line);
      snprintf(line, sizeof(line), "MN %sV", smnv);
      lcdRow(2, line);

      // Row 3: durations
      snprintf(line, sizeof(line), "DRG%4dms MN%5dms", pyroSet.drogue_ms, pyroSet.main_ms);
      lcdRow(3, line);

      // Place hardware cursor on the active field position
      lcd.noCursor();
      switch (pyroSet.field) {
        case 0: lcd.setCursor(3,  1); break;  // VCC value
        case 1: lcd.setCursor(12, 1); break;  // DRG-V value
        case 2: lcd.setCursor(3,  2); break;  // MN-V value
        case 3: lcd.setCursor(3,  3); break;  // DRG-T value
        case 4: lcd.setCursor(13, 3); break;  // MN-T value
      }
      lcd.cursor();
      break;
    }

    default: break;
  }
}

// ====== Keypad Handler ======
void handleKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  Serial.print("[KEY] ");
  Serial.println(key);

  // ── PAGE_PWM: number keys adjust selected field ─────────────────────
  if (currentPage == PAGE_PWM) {
    switch (key) {
      case '2':  // previous field
        pyroSet.field = (pyroSet.field == 0) ? 4 : pyroSet.field - 1;
        refreshLCD(true); return;
      case '5':  // next field
        pyroSet.field = (pyroSet.field + 1) % 5;
        refreshLCD(true); return;
      case '4':  // decrement value
        switch (pyroSet.field) {
          case 0: pyroSet.vcc_v      = max(0.0f,  pyroSet.vcc_v    - 0.1f); break;
          case 1: pyroSet.drogue_v   = max(0.0f,  pyroSet.drogue_v - 0.1f); break;
          case 2: pyroSet.main_v     = max(0.0f,  pyroSet.main_v   - 0.1f); break;
          case 3: pyroSet.drogue_ms  = max(100,   (int)pyroSet.drogue_ms - 100); break;
          case 4: pyroSet.main_ms    = max(100,   (int)pyroSet.main_ms   - 100); break;
        }
        refreshLCD(true); return;
      case '7':  // increment value
        switch (pyroSet.field) {
          case 0: pyroSet.vcc_v      = min(25.0f, pyroSet.vcc_v    + 0.1f); break;
          case 1: pyroSet.drogue_v   = min(25.0f, pyroSet.drogue_v + 0.1f); break;
          case 2: pyroSet.main_v     = min(25.0f, pyroSet.main_v   + 0.1f); break;
          case 3: pyroSet.drogue_ms  = min(9900,  (int)pyroSet.drogue_ms + 100); break;
          case 4: pyroSet.main_ms    = min(9900,  (int)pyroSet.main_ms   + 100); break;
        }
        refreshLCD(true); return;
      case '9': {  // SEND settings to rocket
        char cmd[120];
        char svcc[7], sdrv[7], smnv[7];
        dtostrf(pyroSet.vcc_v,    5, 1, svcc);
        dtostrf(pyroSet.drogue_v, 5, 1, sdrv);
        dtostrf(pyroSet.main_v,   5, 1, smnv);
        snprintf(cmd, sizeof(cmd),
          "SET_PWM:{\"vcc\":%s,\"drogue_v\":%s,\"main_v\":%s,"
          "\"drogue_time\":%u,\"main_time\":%u}",
          svcc, sdrv, smnv, pyroSet.drogue_ms, pyroSet.main_ms);
        sendCommandToESP32(cmd);
        lcdRow(3, "SETTINGS SENT       ");
        return;
      }
      case '6':  // still cycle pages from PWM page
        break;
      default: return;  // ignore other keys when in PWM page
    }
  }

  switch (key) {
    // ── KEY 1: ARM toggle (ARM ↔ DISARM) ──────────────────────────────
    case '1':
      if (!toggleArmed) {
        // ARM
        if (digitalRead(ARM_SWITCH_PIN) == LOW) {
          sendCommandToESP32("ARM");
          toggleArmed = true;
          lcdRow(3, "ARMED               ");
        } else {
          logToLaptop("SAFETY", "ARM blocked — arm switch not engaged");
          lcdRow(3, "ARM SWITCH OFF!     ");
        }
      } else {
        // DISARM
        sendCommandToESP32("DISARM");
        toggleArmed = false;
        lcdRow(3, "DISARMED            ");
      }
      break;

    // ── KEY 2: free — send STATUS to laptop ───────────────────────────
    case '2':
      Serial.println(buildStatusString());
      lcdRow(3, "STATUS->USB         ");
      break;

    // ── KEY 3: RESET ──────────────────────────────────────────────────
    case '3':
      sendCommandToESP32("RESET");
      toggleArmed  = false;
      toggleMain   = false;
      toggleDrogue = false;
      break;

    // ── KEY 4: MAIN toggle (MAIN_ON ↔ MAIN_OFF) ───────────────────────
    //    In PAGE_PWM this key decrements — handled above.
    case '4':
      toggleMain = !toggleMain;
      sendCommandToESP32(toggleMain ? "MAIN_ON" : "MAIN_OFF");
      break;

    // ── KEY 5: free — XBee toggle ─────────────────────────────────────
    case '5': {
      static bool xbeeOnLocal = true;
      xbeeOnLocal = !xbeeOnLocal;
      sendCommandToESP32(xbeeOnLocal ? "XBEE_ON" : "XBEE_OFF");
      break;
    }

    // ── KEY 6: cycle display pages ────────────────────────────────────
    case '6':
      lcd.noCursor();  // turn off cursor if leaving PWM page
      currentPage = (DisplayPage)((currentPage + 1) % PAGE_COUNT);
      lcd.clear();
      refreshLCD(true);
      break;

    // ── KEY 7: DROGUE toggle (DROGUE_ON ↔ DROGUE_OFF) ─────────────────
    //    In PAGE_PWM this key increments — handled above.
    case '7':
      toggleDrogue = !toggleDrogue;
      sendCommandToESP32(toggleDrogue ? "DROGUE_ON" : "DROGUE_OFF");
      break;

    // ── KEY 8: free — HELP ────────────────────────────────────────────
    case '8':
      printHelp();
      break;

    // ── KEY 9: STATUS ─────────────────────────────────────────────────
    case '9':
      Serial.println(buildStatusString());
      lcdRow(3, "STATUS->USB         ");
      break;

    // ── KEY *: MODE cycle (XBEE→BEACON→MQTT) ─────────────────────────
    case '*': {
      static uint8_t modeIdx = 0;
      const char* modes[] = {"CMD_XBEE_MODE","CMD_BEACON_MODE","CMD_MQTT_MODE"};
      modeIdx = (modeIdx + 1) % 3;
      sendCommandToESP32(modes[modeIdx]);
      break;
    }

    // ── KEY 0: HELP ───────────────────────────────────────────────────
    case '0': printHelp(); break;

    // ── KEY #: clear LCD ──────────────────────────────────────────────
    case '#':
      lcd.clear();
      refreshLCD(true);
      break;
  }
}

// ====== Laptop Serial Command Handler ======
void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print("[SERIAL CMD] ");
  Serial.println(cmd);

  if (cmd.equalsIgnoreCase("HELP")) { printHelp(); return; }
  if (cmd.equalsIgnoreCase("STATUS")) { Serial.println(buildStatusString()); return; }
  if (cmd.equalsIgnoreCase("PAGE")) {
    lcd.noCursor();
    currentPage = (DisplayPage)((currentPage + 1) % PAGE_COUNT);
    lcd.clear(); refreshLCD(true); return;
  }
  // Pop-test commands are forwarded verbatim to the rocket
  // (flight computer handles the test-mode guard internally)
  sendCommandToESP32(cmd.c_str());
}

// ====== Log to Laptop ======
void logToLaptop(const char* prefix, const char* msg) {
  Serial.print("[");
  Serial.print(prefix);
  Serial.print("] ");
  Serial.println(msg);
}

// ====== Status String ======
String buildStatusString() {
  char buf[200];
  snprintf(buf, sizeof(buf),
           "STATUS pkts=%lu armed=%s conn=%s rec=%lu alt=%.1f vel=%.1f bat=%.2f rssi=%d",
           (unsigned long)packetsReceived,
           rocketArmed?"YES":"NO",
           connected?"YES":"NO",
           (unsigned long)telem.record_number,
           telem.altitude_agl, telem.velocity,
           telem.battery_voltage, (int)telem.wifi_rssi);
  return String(buf);
}

// ====== Print Help to Serial ======
void printHelp() {
  Serial.println("=== N4 Base Station v2.0 Commands ===");
  Serial.println("Keypad (normal pages):");
  Serial.println("  1 = ARM/DISARM toggle   2 = STATUS");
  Serial.println("  3 = RESET               4 = MAIN_ON/OFF toggle");
  Serial.println("  5 = XBEE toggle         6 = Next page");
  Serial.println("  7 = DROGUE_ON/OFF toggle 8 = HELP");
  Serial.println("  9 = STATUS->USB         * = Mode cycle (XBee/Beacon/MQTT)");
  Serial.println("  0 = HELP                # = Clear LCD");
  Serial.println("Keypad (PWM Settings page):");
  Serial.println("  2/5 = Prev/Next field   4/7 = Dec/Inc value");
  Serial.println("  9   = Send settings to rocket");
  Serial.println("Serial commands: ARM DISARM RESET MAIN_ON MAIN_OFF DROGUE_ON DROGUE_OFF");
  Serial.println("  CMD_XBEE_MODE CMD_BEACON_MODE CMD_MQTT_MODE CMD_AUTO_FALLBACK_ON/OFF");
  Serial.println("  SET_PWM:{\"vcc\":14.8,\"drogue_v\":9.0,\"main_v\":10.0,\"drogue_time\":3000,\"main_time\":5000}");
  Serial.println("  POP_TEST_DROGUE  POP_TEST_MAIN  (test mode pyro deploy, no preflight block)");
  Serial.println("  STATUS  HELP  PAGE");
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);    // USB Serial (debug/PC)
  Serial2.begin(115200);   // UART to ESP32 — must match ESP MegaSerial baud

  // ── GPIO ──
  pinMode(LED_ARMED,      OUTPUT);
  pinMode(LED_DROGUE,     OUTPUT);
  pinMode(LED_MAIN,       OUTPUT);
  pinMode(ARM_SWITCH_PIN, INPUT_PULLUP);
  digitalWrite(LED_ARMED,  LOW);
  digitalWrite(LED_DROGUE, LOW);
  digitalWrite(LED_MAIN,   LOW);

  // Startup LED flash to confirm hardware
  digitalWrite(LED_ARMED,  HIGH);
  digitalWrite(LED_DROGUE, HIGH);
  digitalWrite(LED_MAIN,   HIGH);
  delay(400);
  digitalWrite(LED_ARMED,  LOW);
  digitalWrite(LED_DROGUE, LOW);
  digitalWrite(LED_MAIN,   LOW);

  // ── LCD ──
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("N4 Base Station v2.0");
  lcd.setCursor(0, 1); lcd.print("UART BRIDGE 115200  ");
  lcd.setCursor(0, 2); lcd.print("22=ARM 24=DRG 26=MN");
  lcd.setCursor(0, 3); lcd.print("Initialising...");
  delay(1500);
  lcd.clear();

  Serial.println("=== N4 Base Station (Mega) v2.0 ===");
  Serial.println("UART BRIDGE @ 115200 baud (TX2=16, RX2=17)");
  Serial.print("Device ID: "); Serial.println(DEVICE_ID);
  Serial.println("LEDs: 22=ARMED(RED) 24=DROGUE(GRN) 26=MAIN(BLU)");
  Serial.println("Type HELP for command list.");

  // Send 3 rapid heartbeats for immediate Python server COM port detection
  for (int i = 0; i < 3; i++) {
    char jsonBuf[200];
    snprintf(jsonBuf, sizeof(jsonBuf),
      "{\"type\":\"heartbeat\",\"uptime\":%lu,\"device_id\":\"%s\",\"waiting_for_data\":true}|%s",
      millis(), DEVICE_ID, DEVICE_ID);
    Serial.println(jsonBuf);
    delay(100);
  }

  refreshLCD(true);
}

// ====== MAIN LOOP ======
void loop() {
  // Process any CSV telemetry written to us by ESP32 via UART
  processUARTReceive();

  // Heartbeat to Python server when no telemetry (COM port detection)
  sendHeartbeat();

  handleKeypad();
  handleSerialCommands();
  updateLEDs();
  refreshLCD(false);

  // Connection timeout notice on LCD row 3
  if (dataReceived && (millis() - lastPacketTime > CONN_TIMEOUT)) {
    static uint32_t lastTimeoutMsg = 0;
    if (millis() - lastTimeoutMsg > 5000) {
      lastTimeoutMsg = millis();
      connected = false;
      logToLaptop("WARN", "Telemetry timeout");
      lcd.setCursor(0, 3);
      lcd.print("!! NO SIGNAL !!     ");
    }
  }
}
