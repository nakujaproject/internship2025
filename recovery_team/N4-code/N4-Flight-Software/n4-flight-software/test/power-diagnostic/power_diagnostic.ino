#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// --- Pins ---
#define DROGUE_PIN 25
#define MAIN_PIN   12
#define ARM_PIN    27

// --- ADS1115 Channels ---
#define CH_BATTERY 2 // A0
#define CH_DROGUE  1 // A1
#define CH_MAIN    0 // A2
#define CH_3V3_REF 3 // A3 (Direct 3.3V Monitoring)

Adafruit_ADS1115 ads;

// Standard Divider Ratio for 4.7k / 1.1k
const float HW_RATIO = 5.2727f;

float liveBatV = 16.8f;
bool powerRailLow = false;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (!ads.begin()) {
    Serial.println("!!! ADS1115 NOT FOUND !!!");
    while (1) {
      delay(1000);
    }
  }

  ads.setGain(GAIN_ONE); // +/- 4.096V range

  pinMode(DROGUE_PIN, OUTPUT);
  pinMode(MAIN_PIN, OUTPUT);
  pinMode(ARM_PIN, OUTPUT);

  digitalWrite(DROGUE_PIN, LOW);
  digitalWrite(MAIN_PIN, LOW);
  digitalWrite(ARM_PIN, LOW);

  Serial.println("\n========================================");
  Serial.println("  N4 BMS & POWER DIAGNOSTIC SYSTEM");
  Serial.println("========================================");
  Serial.println("Type 'help' for serial commands.");
}

void printAllTelemetry() {
  // Read Raw Pin Voltages
  float rawA0 = ads.computeVolts(ads.readADC_SingleEnded(CH_BATTERY));
  float rawA1 = ads.computeVolts(ads.readADC_SingleEnded(CH_DROGUE));
  float rawA2 = ads.computeVolts(ads.readADC_SingleEnded(CH_MAIN));
  float rawA3 = ads.computeVolts(ads.readADC_SingleEnded(CH_3V3_REF));

  // Use the 3.3V rail as a sanity check / light calibration factor.
  float railFactor = rawA3 > 0.1f ? (rawA3 / 3.3f) : 1.0f;
  if (railFactor < 0.85f) railFactor = 0.85f;
  if (railFactor > 1.15f) railFactor = 1.15f;

  // Scaled Voltages via Dividers
  float vBat    = rawA0 * HW_RATIO * railFactor;
  float vDrogue = rawA1 * HW_RATIO * railFactor;
  float vMain   = rawA2 * HW_RATIO * railFactor;

  liveBatV = vBat;
  powerRailLow = (rawA3 < 3.00f);

  bool drogueEngaged = (vDrogue > 0.75f);
  bool mainEngaged   = (vMain > 0.75f);

  Serial.print("[LOG] ");
  Serial.print("3.3V RAIL: "); Serial.print(rawA3, 3); Serial.print("V | ");
  Serial.print("BAT: ");      Serial.print(vBat, 2);    Serial.print("V | ");
  Serial.print("DRG PIN: ");  Serial.print(vDrogue, 2);  Serial.print("V | ");
  Serial.print("MAI PIN: ");  Serial.print(vMain, 2);    Serial.print("V | ");
  Serial.print("DRG ENG: ");  Serial.print(drogueEngaged ? "YES" : "NO"); Serial.print(" | ");
  Serial.print("MAI ENG: ");  Serial.print(mainEngaged ? "YES" : "NO");
  Serial.println();

  if (powerRailLow) {
    Serial.println("  >> ERROR: 3.3V rail below 3.0V - data transmission may be unstable <<");
  }
}

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  status   - print battery, rail, and chute status");
  Serial.println("  arm      - drive ARM pin high");
  Serial.println("  disarm   - drive ARM pin low");
  Serial.println("  drogue   - fire drogue output");
  Serial.println("  main     - fire main output");
  Serial.println("  help     - show this help");
}

bool handleSerialCommand(const String& cmd) {
  if (cmd == "status") {
    printAllTelemetry();
    return true;
  }
  if (cmd == "arm") {
    digitalWrite(ARM_PIN, HIGH);
    Serial.println("\n[SYSTEM ARMED]");
    printAllTelemetry();
    return true;
  }
  if (cmd == "disarm") {
    digitalWrite(ARM_PIN, LOW);
    Serial.println("\n[SYSTEM DISARMED]");
    printAllTelemetry();
    return true;
  }
  if (cmd == "drogue") {
    fire(DROGUE_PIN, 6.0f, "DROGUE");
    return true;
  }
  if (cmd == "main") {
    fire(MAIN_PIN, 6.0f, "MAIN");
    return true;
  }
  if (cmd == "help") {
    printHelp();
    return true;
  }
  return false;
}

void fire(int pin, float targetV, const char* label) {
  int duty = (int)((targetV / liveBatV) * 255.0f);
  duty = constrain(duty, 0, 255);

  Serial.print("\n>>> FIRING: "); Serial.println(label);
  ledcAttach(pin, 500, 8);
  ledcWrite(pin, duty);

  for (int i = 0; i < 6; i++) {
    delay(500);
    if (Serial.available()) {
      String pending = Serial.readStringUntil('\n');
      pending.trim();
      if (pending.length() > 0) {
        if (pending == "status" || pending == "help" || pending == "disarm" || pending == "arm") {
          handleSerialCommand(pending);
        }
      }
    }
    printAllTelemetry();
  }

  ledcWrite(pin, 0);
  ledcDetach(pin);
  digitalWrite(pin, LOW);
  Serial.println(">>> FIRE COMPLETE.\n");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (!handleSerialCommand(cmd)) {
      Serial.print("Unknown command: ");
      Serial.println(cmd);
      printHelp();
    }
  }
}
