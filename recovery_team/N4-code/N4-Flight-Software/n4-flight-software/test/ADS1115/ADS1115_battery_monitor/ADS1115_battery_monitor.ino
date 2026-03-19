/**
 * @file    ADS1115_battery_monitor.ino
 * @brief   ADS1115 Battery Monitor — Unit Test (Voltage Reading)
 *
 * =============================================================================
 * HARDWARE SETUP
 * =============================================================================
 *
 *  Battery (+) ──┬── R1 (4.7 kΩ) ──┬── R2 (1.1 kΩ) ── GND
 *                │                  │
 *               N/C             ADS1115 A0
 *
 *  Voltage divider ratio:  V_A0 = V_bat × R2 / (R1 + R2)
 *                               = V_bat × 1100 / 5800
 *                               = V_bat × 0.18966
 *
 *  Inverse (what we compute):
 *            V_bat = V_A0 × (R1 + R2) / R2
 *                  = V_A0 × 5800 / 1100
 *                  = V_A0 × 5.2727
 *
 *  Max safe V_bat with this divider (ADS1115 input ≤ 3.3V supply):
 *            V_bat_max ≈ 3.3V × 5.2727 ≈ 17.4V  → handles 4S LiPo (16.8V max)
 *
 * =============================================================================
 * ADS1115 WIRING
 * =============================================================================
 *
 *  VDD  → 3.3V  (from flight computer)
 *  GND  → GND
 *  SDA  → ESP32 GPIO 21
 *  SCL  → ESP32 GPIO 22
 *  ADDR → GND   (I2C address 0x48)
 *
 *  Channel : A0 (single-ended)
 *  PGA/Gain: GAIN_ONE  → ±4.096 V full-scale  (LSB = 0.125 mV)
 *
 *  Why GAIN_ONE:
 *    V_A0 max ≤ 3.3V (VDD).  GAIN_TWO (±2.048V) would clip at ~10.8V battery
 *    — too low for a 3S or 4S pack.  GAIN_ONE lets us read up to ~17.4V.
 *
 * =============================================================================
 * BATTERY THRESHOLDS  (4S LiPo defaults — change CELL_COUNT below)
 * =============================================================================
 *
 *  FULL     : 16.80 V  (4.20 V/cell × 4)
 *  NOMINAL  : 14.80 V  (3.70 V/cell × 4)
 *  LOW      : 14.00 V  (3.50 V/cell × 4)
 *  CRITICAL : 13.20 V  (3.30 V/cell × 4)
 *  CUTOFF   : 12.00 V  (3.00 V/cell × 4)  ← never discharge below this
 *
 * =============================================================================
 * REQUIRED LIBRARY  (install via Arduino Library Manager)
 * =============================================================================
 *  "Adafruit ADS1X15"  by Adafruit
 * =============================================================================
 */

#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ─── Voltage Divider ─────────────────────────────────────────────────────────
const float R1_OHMS       = 4700.0;                         // upper resistor (Ω)
const float R2_OHMS       = 1100.0;                         // lower resistor — ADS side (Ω)
const float DIVIDER_RATIO = (R1_OHMS + R2_OHMS) / R2_OHMS; // 5.2727

// ─── ADS1115 ─────────────────────────────────────────────────────────────────
const uint8_t ADS_ADDR   = 0x48;    // ADDR pin tied to GND
const uint8_t ADC_CH     = 0;       // A0 channel

// ─── I2C pins (ESP32 hardware defaults) ──────────────────────────────────────
const int SDA_PIN = 21;
const int SCL_PIN = 22;

// ─── Battery config ───────────────────────────────────────────────────────────
const uint8_t CELL_COUNT    = 4;      // change to 3 for 3S, 2 for 2S, etc.
const float   CELL_MAX_V    = 4.20;   // LiPo full charge per cell
const float   CELL_NOM_V    = 3.70;   // nominal per cell
const float   CELL_LOW_V    = 3.50;   // low-warning per cell
const float   CELL_CRIT_V   = 3.30;   // critical per cell
const float   CELL_CUTOFF_V = 3.00;   // absolute minimum per cell

const float BAT_FULL   = CELL_MAX_V    * CELL_COUNT;  // 16.80 V
const float BAT_NOM    = CELL_NOM_V    * CELL_COUNT;  // 14.80 V
const float BAT_LOW    = CELL_LOW_V    * CELL_COUNT;  // 14.00 V
const float BAT_CRIT   = CELL_CRIT_V   * CELL_COUNT;  // 13.20 V
const float BAT_CUTOFF = CELL_CUTOFF_V * CELL_COUNT;  // 12.00 V

// ─── Sampling ────────────────────────────────────────────────────────────────
const uint32_t SAMPLE_INTERVAL_MS = 500;  // how often to read (ms)
const uint8_t  AVG_SAMPLES        = 8;    // rolling-average window depth

// ─── Globals ─────────────────────────────────────────────────────────────────
Adafruit_ADS1115 ads;
float   voltageBuffer[8] = {0};
uint8_t bufIdx    = 0;
bool    bufFull   = false;

// ─────────────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("\n===================================================="));
  Serial.println(F("  ADS1115 Battery Monitor -- Unit Test"));
  Serial.println(F("===================================================="));
  Serial.print(F("  Divider : R1="));  Serial.print(R1_OHMS, 0);
  Serial.print(F("  R2="));            Serial.print(R2_OHMS, 0);
  Serial.print(F("  ratio="));         Serial.println(DIVIDER_RATIO, 4);
  Serial.print(F("  Pack    : "));     Serial.print(CELL_COUNT);
  Serial.print(F("S LiPo  Full="));    Serial.print(BAT_FULL, 2);
  Serial.print(F("V  Cutoff="));       Serial.print(BAT_CUTOFF, 2);
  Serial.println(F("V\n"));

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!ads.begin(ADS_ADDR)) {
    Serial.println(F("[FATAL] ADS1115 not found on I2C!"));
    Serial.println(F("        Check: VDD=3.3V, SDA=21, SCL=22, ADDR=GND"));
    while (1) { delay(1000); }
  }

  ads.setGain(GAIN_ONE);              // ±4.096 V, 0.125 mV/LSB
  ads.setDataRate(RATE_ADS1115_128SPS); // 128 samples/sec

  Serial.print(F("  [ADS1115] OK  addr=0x"));
  Serial.print(ADS_ADDR, HEX);
  Serial.println(F("  gain=GAIN_ONE  rate=128SPS"));

  selfTest();

  Serial.println(F("\n  [OK] Continuous readings starting...\n"));
  Serial.println(F("  RawADC  | PinV (V) | BatV (V) | AvgV (V) |  %   | Status"));
  Serial.println(F("  --------|----------|----------|----------|------|--------"));
}

// ─────────────────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  static uint32_t lastSample = 0;

  if (millis() - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = millis();

    int16_t raw  = ads.readADC_SingleEnded(ADC_CH);
    float   pinV = ads.computeVolts(raw);

    // Guard: reject out-of-range pin voltage
    if (pinV < 0.0 || pinV > 3.6) {
      Serial.print(F("  [WARN] Pin V out of range: "));
      Serial.print(pinV, 4);
      Serial.println(F(" V -- skipping"));
      return;
    }

    float batV = pinV * DIVIDER_RATIO;   // restore actual battery voltage
    float avgV = rollingAverage(batV);   // smoothed reading
    float pct  = batteryPercent(avgV);

    printRow(raw, pinV, batV, avgV, pct);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// FUNCTIONS
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Circular-buffer rolling average.
 * Uses however many samples are available until the buffer is full.
 */
float rollingAverage(float newVal) {
  voltageBuffer[bufIdx] = newVal;
  bufIdx = (bufIdx + 1) % AVG_SAMPLES;
  if (bufIdx == 0) bufFull = true;

  uint8_t count = bufFull ? AVG_SAMPLES : bufIdx;
  float   sum   = 0.0;
  for (uint8_t i = 0; i < count; i++) sum += voltageBuffer[i];
  return sum / count;
}

/**
 * Linear map: CUTOFF → 0%,  FULL → 100%.
 */
float batteryPercent(float v) {
  if (v >= BAT_FULL)   return 100.0;
  if (v <= BAT_CUTOFF) return 0.0;
  return 100.0 * (v - BAT_CUTOFF) / (BAT_FULL - BAT_CUTOFF);
}

/**
 * Return status string for a given pack voltage.
 */
String batteryStatus(float v) {
  if (v >= BAT_FULL)  return "FULL    ";
  if (v >= BAT_NOM)   return "NOMINAL ";
  if (v >= BAT_LOW)   return "LOW  !  ";
  if (v >= BAT_CRIT)  return "CRITICAL";
  return                     "CUTOFF!!";
}

/**
 * Print one reading as a formatted table row, plus threshold warnings.
 */
void printRow(int16_t raw, float pinV, float batV, float avgV, float pct) {
  Serial.print(F("  "));
  Serial.print(raw);
  Serial.print(F(" | "));
  Serial.print(pinV, 4);
  Serial.print(F(" | "));
  Serial.print(batV, 4);
  Serial.print(F(" | "));
  Serial.print(avgV, 4);
  Serial.print(F(" | "));
  Serial.print(pct, 1);
  Serial.print(F("% | "));
  Serial.println(batteryStatus(avgV));

  if (avgV <= BAT_CUTOFF)
    Serial.println(F("  !! CUTOFF REACHED -- power down immediately !!"));
  else if (avgV <= BAT_CRIT)
    Serial.println(F("  !! Battery critically low"));
  else if (avgV <= BAT_LOW)
    Serial.println(F("  !  Battery low -- consider landing soon"));
}

/**
 * One-shot startup self-test.
 * Takes a single reading and checks physical plausibility.
 */
void selfTest() {
  Serial.println(F("\n  -- Self-test ------------------------------------------"));

  int16_t raw  = ads.readADC_SingleEnded(ADC_CH);
  float   pinV = ads.computeVolts(raw);
  float   batV = pinV * DIVIDER_RATIO;

  Serial.print(F("  Raw ADC   : ")); Serial.println(raw);
  Serial.print(F("  Pin V     : ")); Serial.print(pinV, 4); Serial.println(F(" V"));
  Serial.print(F("  Battery V : ")); Serial.print(batV, 4); Serial.println(F(" V"));

  // Pin voltage must sit within 0–VDD
  bool pinOK = (pinV >= 0.0 && pinV <= 3.6);
  Serial.print(F("  Pin range : ")); Serial.println(pinOK ? F("PASS") : F("FAIL (check wiring)"));

  // Battery should be above cutoff if battery is connected
  bool batOK = (batV >= BAT_CUTOFF);
  Serial.print(F("  Bat range : "));
  if (batOK) Serial.println(F("PASS"));
  else       Serial.println(F("WARN -- below cutoff or no battery connected"));

  // LSB sanity: GAIN_ONE → 4.096 / 32768 = 0.000125 V
  float expLSB  = 4.096 / 32768.0;
  float measLSB = (raw != 0) ? (pinV / (float)raw) : 0.0;
  bool  lsbOK   = (fabs(measLSB - expLSB) < expLSB * 0.05);
  Serial.print(F("  LSB check : expected=")); Serial.print(expLSB, 6);
  Serial.print(F("V  measured="));             Serial.print(measLSB, 6);
  Serial.println(lsbOK ? F("V  PASS") : F("V  WARN"));

  Serial.println(F("  -------------------------------------------------------"));
}
