#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// --- Pins (Nakuja N4 Layout) ---
#define DROGUE_PIN 25
#define MAIN_PIN   12
#define ARM_PIN    27  

// --- ADS1115 Channels ---
#define CH_BATTERY 2 // A0
#define CH_DROGUE  1 // A1
#define CH_MAIN    0 // A2
#define CH_XBEE    3 // A3 (RSSI Pin 6 with 10k resistor + 47uF Cap)

Adafruit_ADS1115 ads;

// --- INDIVIDUAL CALIBRATION RATIOS ---
const float RATIO_BAT    = 5.2727 * 1.064; 
const float RATIO_DROGUE = 5.2727 * 0.758; 
const float RATIO_MAIN   = 5.2727 * 1.366; 

float liveBatV = 12.8; 

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  
  if (!ads.begin()) {
    Serial.println("!!! ADS1115 NOT FOUND !!!");
    while (1);
  }
  
  // GAIN_ONE: +/- 4.096V (Perfect for XBee 3.3V RSSI)
  ads.setGain(GAIN_ONE); 

  pinMode(DROGUE_PIN, OUTPUT);
  pinMode(MAIN_PIN, OUTPUT);
  pinMode(ARM_PIN, OUTPUT);
  
  digitalWrite(DROGUE_PIN, LOW);
  digitalWrite(MAIN_PIN, LOW);
  digitalWrite(ARM_PIN, LOW);

  Serial.println("\n========================================");
  Serial.println("  N4 FULL SYSTEM MONITOR (INC. XBEE)   ");
  Serial.println("========================================");
  
  // --- STARTUP DELAY FOR 47uF CAPACITOR ---
  Serial.print("Initializing XBee RSSI Filter (47uF charge-up)...");
  for(int i = 0; i < 5; i++) {
    delay(400); // 2 second total delay
    Serial.print(".");
  }
  Serial.println(" READY.");
  
  Serial.println("Commands: 'arm', 'disarm', 'drogue', 'main', 'status'");
}

void printAllTelemetry() {
  // 1. Read Raw ADC values
  float vBat    = ads.computeVolts(ads.readADC_SingleEnded(CH_BATTERY)) * RATIO_BAT;
  float vDrogue = ads.computeVolts(ads.readADC_SingleEnded(CH_DROGUE)) * RATIO_DROGUE;
  float vMain   = ads.computeVolts(ads.readADC_SingleEnded(CH_MAIN))   * RATIO_MAIN;
  
  // 2. Read XBee RSSI (Direct 3.3V logic, no divider)
  float vRSSI   = ads.computeVolts(ads.readADC_SingleEnded(CH_XBEE));
  float rssiPct = (vRSSI / 3.3) * 100.0;
  if(rssiPct > 100.0) rssiPct = 100.0; // Cap at 100%

  liveBatV = vBat; // Update for PWM safety

  Serial.print("[TELEMETRY] ");
  Serial.print("BAT: ");    Serial.print(vBat, 2);    Serial.print("V | ");
  Serial.print("DRG: ");    Serial.print(vDrogue, 2);  Serial.print("V | ");
  Serial.print("MAI: ");    Serial.print(vMain, 2);    Serial.print("V | ");
  Serial.print("RSSI: ");   Serial.print(rssiPct, 1);  Serial.println("%");
}

void fire(int pin, float targetV, const char* label) {
  int duty = (targetV / liveBatV) * 255;
  duty = constrain(duty, 0, 255);

  Serial.print("\n>>> FIRE START: "); Serial.println(label);
  Serial.print("Duty: "); Serial.println(duty);

  ledcAttach(pin, 500, 8);
  ledcWrite(pin, duty);
  
  // Telemetry during fire
  for(int i = 0; i < 6; i++) {
    delay(500);
    printAllTelemetry();
  }
  
  ledcWrite(pin, 0);
  ledcDetach(pin);
  digitalWrite(pin, LOW);
  Serial.println(">>> FIRE ENDED.\n");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "status") {
      printAllTelemetry();
    } 
    else if (cmd == "arm") {
      digitalWrite(ARM_PIN, HIGH);
      Serial.println("SYSTEM ARMED");
      printAllTelemetry();
    }
    else if (cmd == "disarm") {
      digitalWrite(ARM_PIN, LOW);
      Serial.println("SYSTEM DISARMED");
      printAllTelemetry();
    }
    else if (cmd == "drogue") {
      fire(DROGUE_PIN, 6.0, "DROGUE");
    } 
    else if (cmd == "main") {
      fire(MAIN_PIN, 6.0, "MAIN");
    }
  }
}