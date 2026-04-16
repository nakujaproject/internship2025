/**
 * N4 Base Station - Arduino Mega (Main Controller)
 * Version: 1.0
 *
 * Role: Central hub of the base station.
 *   - Polls ESP32 via I2C for incoming CSV telemetry
 *   - Parses the 29-field CSV and displays key values on a 20×4 I2C LCD
 *   - Mirrors every raw CSV to the laptop over USB Serial (Serial)
 *   - Accepts commands from: (a) laptop USB Serial, (b) 4×3 keypad
 *   - Relays commands to ESP32 via I2C (ESP32 sends them to rocket via XBee / ESP-NOW)
 *   - Drives status LEDs and reads an ARM safety switch
 *
 * I2C Bus:
 *   Mega SDA = pin 20,  SCL = pin 21  (hardware I2C)
 *   ESP32 slave address: 0x08
 *   LCD I2C address    : 0x27  (try 0x3F if blank)
 *   Pull-ups: 4.7 kΩ to 5V on SDA and SCL
 *
 * NOTE: ESP32 is 3.3V logic. Use a bi-directional level shifter on SDA/SCL,
 *       or a simple voltage divider (Mega→ESP32 direction only) if no shifter.
 *
 * I2C Protocol (Mega ↔ ESP32):
 *   WRITE  Mega→ESP32 : send ASCII command string (no newline needed)
 *   READ   Mega←ESP32 : first byte = payload length (0 = no new data),
 *                       remaining bytes = CSV telemetry string
 *
 * Keypad Mapping (4×3 matrix):
 *   1 = ARM      2 = DISARM    3 = RESET
 *   4 = MAIN_ON  5 = MAIN_OFF  6 = (spare / MODE cycle)
 *   7 = DROGUE_ON 8 = DROGUE_OFF 9 = STATUS
 *   * = XBEE     0 = HELP      # = CLEAR LCD
 *
 * Hardware Pins:
 *   Keypad ROWS : 52, 50, 48, 46
 *   Keypad COLS : 53, 51, 49
 *   LED_ARMED   : 22 (red)
 *   LED_CONN    : 24 (green)
 *   LED_DATA    : 26 (blue / yellow)
 *   ARM_SWITCH  : 28 (INPUT_PULLUP, active LOW = armed position)
 */

#include <Wire.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>

// ====== I2C: ESP32 Slave ======
#define ESP32_I2C_ADDR  0x08
#define I2C_READ_BYTES  255   // max CSV we request per poll

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
#define LED_ARMED      22   // RED   – rocket armed
#define LED_CONN       24   // GREEN – telemetry connection active
#define LED_DATA       26   // BLUE  – blinks on each new packet
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

// ====== State ======
bool     dataReceived      = false;
bool     rocketArmed       = false;
bool     connected         = false;
uint32_t packetsReceived   = 0;
uint32_t lastPacketTime    = 0;
const uint32_t CONN_TIMEOUT= 15000;

// LCD display pages (cycle with keypad '6')
enum DisplayPage { PAGE_MAIN=0, PAGE_ALT, PAGE_GPS, PAGE_PYRO, PAGE_COUNT };
DisplayPage currentPage = PAGE_MAIN;

// LCD refresh throttle
uint32_t lastLcdRefresh = 0;
const uint32_t LCD_REFRESH_MS = 300;

// Data LED blink
uint32_t dataLedOffTime = 0;
bool     dataLedOn      = false;

// ====== Forward Declarations ======
bool    parseCSV(const char* csv);
void    sendCommandToESP32(const char* cmd);
void    refreshLCD(bool forceAll);
void    handleKeypad();
void    handleSerialCommands();
void    pollESP32Telemetry();
void    updateLEDs();
void    printHelp();
void    logToLaptop(const char* prefix, const char* msg);
String  buildStatusString();

// ====== 29-Field CSV Parser ======
bool parseCSV(const char* csv) {
  if (!csv || csv[0] == '\0') return false;
  char buf[512];
  size_t len = strlen(csv);
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, csv, len);
  buf[len] = '\0';
  char* sp = NULL;
  char* tok = strtok_r(buf, ",", &sp);
  int f = 0;
  while (tok && f < 29) {
    switch (f) {
      case  0: telem.record_number         = (uint32_t)strtoul(tok,NULL,10); break;
      case  1: telem.operation_mode        = (uint8_t) strtoul(tok,NULL,10); break;
      case  2: telem.state                 = (uint8_t) strtoul(tok,NULL,10); break;
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
      case 14: telem.gps_time              = (uint32_t)strtoul(tok,NULL,10); break;
      case 15: telem.pressure              = atof(tok); break;
      case 16: telem.temperature           = atof(tok); break;
      case 17: telem.altitude_agl          = atof(tok); break;
      case 18: telem.velocity              = atof(tok); break;
      case 19: telem.drogue_pin_state      = (uint8_t)strtoul(tok,NULL,10); break;
      case 20: telem.drogue_pin_engaged    = (uint8_t)strtoul(tok,NULL,10); break;
      case 21: telem.main_chute_pin_state  = (uint8_t)strtoul(tok,NULL,10); break;
      case 22: telem.main_chute_pin_engaged= (uint8_t)strtoul(tok,NULL,10); break;
      case 23: telem.battery_voltage       = atof(tok); break;
      case 24: telem.logic_rail_3v3_voltage= atof(tok); break;
      case 25: telem.power_rail_low        = (uint8_t)strtoul(tok,NULL,10); break;
      case 26: telem.wifi_rssi             = (int32_t)strtol(tok,NULL,10); break;
      case 27: telem.kalman_altitude       = atof(tok); break;
      case 28: telem.kalman_vertical_velocity = atof(tok); break;
    }
    tok = strtok_r(NULL, ",", &sp);
    f++;
  }
  return (f == 29);
}

// ====== Send Command to ESP32 via I2C ======
void sendCommandToESP32(const char* cmd) {
  String full = String(cmd);
  const size_t payloadMax = 26;  // 2-byte frame header + <=26 payload fits AVR Wire TX buffer.
  uint8_t err = 0;

  auto sendFrame = [&](char frameType, const String& payload) -> uint8_t {
    String frame = String(frameType) + ":" + payload;
    Wire.beginTransmission(ESP32_I2C_ADDR);
    Wire.write((const uint8_t*)frame.c_str(), frame.length());
    uint8_t rc = Wire.endTransmission();
    delay(2);
    return rc;
  };

  if (full.length() <= payloadMax) {
    err = sendFrame('C', full);
  } else {
    size_t pos = 0;
    bool first = true;

    while (pos < (size_t)full.length()) {
      size_t remain = full.length() - pos;
      size_t chunkLen = remain > payloadMax ? payloadMax : remain;
      String chunk = full.substring(pos, pos + chunkLen);

      char frameType;
      if (first) {
        frameType = 'B';
        first = false;
      } else if (pos + chunkLen >= (size_t)full.length()) {
        frameType = 'E';
      } else {
        frameType = 'M';
      }

      err = sendFrame(frameType, chunk);
      if (err != 0) break;
      pos += chunkLen;
    }
  }

  if (err == 0) {
    logToLaptop("CMD_SENT", cmd);
    lcd.setCursor(0, 3);
    lcd.print("CMD:");
    lcd.print(cmd);
    // Pad/clear rest of row
    for (int i = 4 + strlen(cmd); i < 20; i++) lcd.print(' ');
  } else {
    char msg[50];
    snprintf(msg, sizeof(msg), "I2C err=%d for: %s", err, cmd);
    logToLaptop("CMD_FAIL", msg);
    lcd.setCursor(0, 3);
    lcd.print("I2C ERR             ");
  }
}

// ====== Poll ESP32 for Latest Telemetry ======
void pollESP32Telemetry() {
  // Request up to I2C_READ_BYTES bytes; first byte is payload length
  uint8_t requested = I2C_READ_BYTES;
  Wire.requestFrom((uint8_t)ESP32_I2C_ADDR, requested);
  if (!Wire.available()) return;

  uint8_t payloadLen = Wire.read();
  if (payloadLen == 0) {
    // Drain any leftover bytes
    while (Wire.available()) Wire.read();
    return;
  }

  char csvBuf[256];
  uint8_t idx = 0;
  while (Wire.available() && idx < payloadLen && idx < sizeof(csvBuf) - 1) {
    csvBuf[idx++] = Wire.read();
  }
  while (Wire.available()) Wire.read();  // discard overflow
  csvBuf[idx] = '\0';

  // Forward raw CSV to laptop
  Serial.print("CSV:");
  Serial.println(csvBuf);

  // Parse
  if (parseCSV(csvBuf)) {
    packetsReceived++;
    lastPacketTime = millis();
    dataReceived   = true;
    rocketArmed    = (telem.operation_mode == 1);
    connected      = true;

    // Blink data LED
    digitalWrite(LED_DATA, HIGH);
    dataLedOn     = true;
    dataLedOffTime= millis() + 100;

    logToLaptop("TELEM_OK", csvBuf);
  } else {
    logToLaptop("PARSE_FAIL", csvBuf);
  }
}

// ====== LED Update ======
void updateLEDs() {
  // Arm LED
  digitalWrite(LED_ARMED, rocketArmed ? HIGH : LOW);

  // Connection LED
  if (dataReceived && (millis() - lastPacketTime <= CONN_TIMEOUT)) {
    digitalWrite(LED_CONN, HIGH);
    connected = true;
  } else {
    digitalWrite(LED_CONN, LOW);
    connected = false;
  }

  // Data blink
  if (dataLedOn && millis() >= dataLedOffTime) {
    digitalWrite(LED_DATA, LOW);
    dataLedOn = false;
  }
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
      // Row 0: connection & arm state
      const char* connStr  = connected    ? "CONN" : "----";
      const char* armedStr = rocketArmed  ? "ARMED" : "SAFE ";
      snprintf(line, sizeof(line), "N4 %-4s %5s Pk%4lu", connStr, armedStr, (unsigned long)(packetsReceived % 10000));
      lcdRow(0, line);

      // Row 1: altitude & velocity
      if (dataReceived) {
        snprintf(line, sizeof(line), "ALT%7.1fm V%5.1fm/s", telem.altitude_agl, telem.velocity);
      } else {
        snprintf(line, sizeof(line), "ALT  ------  ------");
      }
      lcdRow(1, line);

      // Row 2: battery & RSSI
      if (dataReceived) {
        snprintf(line, sizeof(line), "BAT%4.1fV  RSSI%4ddBm", telem.battery_voltage, (int)telem.wifi_rssi);
      } else {
        snprintf(line, sizeof(line), "BAT  ----  RSSI ----");
      }
      lcdRow(2, line);

      // Row 3: last command hint (updated by sendCommandToESP32 directly)
      // Leave row 3 for command feedback — don't overwrite here.
      break;
    }

    // ── PAGE 1: Altitude detail ───────────────────────────────────
    case PAGE_ALT: {
      lcdRow(0, "--- ALTITUDE PAGE --");
      if (dataReceived) {
        snprintf(line, sizeof(line), "AGL %9.2f m", telem.altitude_agl);
        lcdRow(1, line);
        snprintf(line, sizeof(line), "KLM %9.2f m", telem.kalman_altitude);
        lcdRow(2, line);
        snprintf(line, sizeof(line), "KVL %7.2f m/s", telem.kalman_vertical_velocity);
        lcdRow(3, line);
      } else {
        lcdRow(1, "No data");
        lcdRow(2, "");
        lcdRow(3, "");
      }
      break;
    }

    // ── PAGE 2: GPS ───────────────────────────────────────────────
    case PAGE_GPS: {
      lcdRow(0, "---   GPS PAGE   ---");
      if (dataReceived) {
        snprintf(line, sizeof(line), "LAT %10.6f", telem.latitude);
        lcdRow(1, line);
        snprintf(line, sizeof(line), "LON %10.6f", telem.longitude);
        lcdRow(2, line);
        snprintf(line, sizeof(line), "ALT %7.1fm T%6lu", telem.gps_altitude, (unsigned long)telem.gps_time);
        lcdRow(3, line);
      } else {
        lcdRow(1, "No GPS data");
        lcdRow(2, "");
        lcdRow(3, "");
      }
      break;
    }

    // ── PAGE 3: Pyro / chute status ───────────────────────────────
    case PAGE_PYRO: {
      lcdRow(0, "--- PYRO PAGE    ---");
      if (dataReceived) {
        snprintf(line, sizeof(line), "DRG St%d En%d",
                 telem.drogue_pin_state, telem.drogue_pin_engaged);
        lcdRow(1, line);
        snprintf(line, sizeof(line), "MAN St%d En%d",
                 telem.main_chute_pin_state, telem.main_chute_pin_engaged);
        lcdRow(2, line);
        snprintf(line, sizeof(line), "3V3 %.2fV Rail%s",
                 telem.logic_rail_3v3_voltage, telem.power_rail_low ? "LOW" : " OK");
        lcdRow(3, line);
      } else {
        lcdRow(1, "No data");
        lcdRow(2, "");
        lcdRow(3, "");
      }
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

  switch (key) {
    // Safety: only allow ARM if physical arm switch is also in armed position
    case '1':
      if (digitalRead(ARM_SWITCH_PIN) == LOW) {
        sendCommandToESP32("ARM");
      } else {
        logToLaptop("SAFETY", "ARM blocked — arm switch not engaged");
        lcd.setCursor(0, 3);
        lcd.print("ARM SWITCH OFF!     ");
      }
      break;
    case '2': sendCommandToESP32("DISARM");    break;
    case '3': sendCommandToESP32("RESET");     break;
    case '4': sendCommandToESP32("MAIN_ON");   break;
    case '5': sendCommandToESP32("MAIN_OFF");  break;
    case '6':
      // Cycle display pages
      currentPage = (DisplayPage)((currentPage + 1) % PAGE_COUNT);
      lcd.clear();
      refreshLCD(true);
      break;
    case '7': sendCommandToESP32("DROGUE_ON"); break;
    case '8': sendCommandToESP32("DROGUE_OFF");break;
    case '9':
      // Print status to laptop
      Serial.println(buildStatusString());
      lcd.setCursor(0, 3);
      lcd.print("STATUS->USB         ");
      break;
    case '*':
      // Toggle XBee (send XBEE_ON or XBEE_OFF alternately based on local state)
      {
        static bool xbeeOnLocal = true;
        xbeeOnLocal = !xbeeOnLocal;
        sendCommandToESP32(xbeeOnLocal ? "XBEE_ON" : "XBEE_OFF");
      }
      break;
    case '0': printHelp(); break;
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

  if (cmd.equalsIgnoreCase("HELP")) {
    printHelp();
    return;
  }
  if (cmd.equalsIgnoreCase("STATUS")) {
    Serial.println(buildStatusString());
    return;
  }
  if (cmd.equalsIgnoreCase("PAGE")) {
    currentPage = (DisplayPage)((currentPage + 1) % PAGE_COUNT);
    lcd.clear();
    refreshLCD(true);
    return;
  }

  // Pass everything else straight to ESP32
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
  Serial.println("=== N4 Base Station Commands ===");
  Serial.println("ARM / DISARM / RESET");
  Serial.println("MAIN_ON / MAIN_OFF / DROGUE_ON / DROGUE_OFF");
  Serial.println("CMD_XBEE_MODE / CMD_BEACON_MODE / CMD_MQTT_MODE");
  Serial.println("CMD_AUTO_FALLBACK_ON / CMD_AUTO_FALLBACK_OFF");
  Serial.println("XBEE_ON / XBEE_OFF");
  Serial.println("XBEE_TEST / PWM_STATUS");
  Serial.println("SET_PWM:{\"vcc\":14.8,\"drogue_v\":9.0,\"main_v\":10.0,\"drogue_time\":3000,\"main_time\":5000}");
  Serial.println("STATUS / HELP / PAGE");
  Serial.println("Keypad: 1=ARM 2=DISARM 3=RESET 4=MAIN_ON 5=MAIN_OFF");
  Serial.println("        6=PageCycle 7=DRG_ON 8=DRG_OFF 9=Status *=XBee 0=Help #=Clear");
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  Wire.begin();  // Mega I2C master (SDA=20, SCL=21)

  // GPIO
  pinMode(LED_ARMED,      OUTPUT);
  pinMode(LED_CONN,       OUTPUT);
  pinMode(LED_DATA,       OUTPUT);
  pinMode(ARM_SWITCH_PIN, INPUT_PULLUP);
  digitalWrite(LED_ARMED, LOW);
  digitalWrite(LED_CONN,  LOW);
  digitalWrite(LED_DATA,  LOW);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("N4 Base Station v1.0");
  lcd.setCursor(0, 1); lcd.print("Mega + ESP32 + XBee");
  lcd.setCursor(0, 2); lcd.print("I2C slave @ 0x08");
  lcd.setCursor(0, 3); lcd.print("Initialising...");
  delay(1500);
  lcd.clear();

  Serial.println("=== N4 Base Station (Mega) v1.0 ===");
  Serial.println("I2C master ready. Polling ESP32 @ 0x08");
  Serial.println("Type HELP for command list.");

  // Initial display
  refreshLCD(true);
}

// ====== MAIN LOOP ======
void loop() {
  // Poll ESP32 for new telemetry every 200 ms
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll >= 200) {
    lastPoll = millis();
    pollESP32Telemetry();
  }

  handleKeypad();
  handleSerialCommands();
  updateLEDs();
  refreshLCD(false);

  // Connection timeout notice on LCD
  if (dataReceived && (millis() - lastPacketTime > CONN_TIMEOUT)) {
    static uint32_t lastTimeoutMsg = 0;
    if (millis() - lastTimeoutMsg > 5000) {
      lastTimeoutMsg = millis();
      logToLaptop("WARN", "Telemetry timeout");
      lcd.setCursor(0, 3);
      lcd.print("!! NO SIGNAL !!     ");
    }
  }
}

