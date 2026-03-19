/**
 * @file    ADS1115_pyro_deployment_test.ino
 * @brief   N4 Flight Computer — Pyro Deployment + Battery Health Unit Test
 *
 * =============================================================================
 * PURPOSE
 * =============================================================================
 *  This test combines two subsystems:
 *   1. ADS1115 battery-voltage monitoring (via voltage divider on A0)
 *   2. PWM-controlled pyro channel fire sequences (DROGUE and MAIN)
 *
 *  The test runs through a 3-configuration matrix on each channel:
 *    Config A : 500 Hz, low  voltage (4 V target),  2 000 ms
 *    Config B : 500 Hz, nominal voltage (6 V target), 3 000 ms  ← prod default
 *    Config C : 500 Hz, high voltage (9 V target),  5 000 ms
 *
 *  Battery voltage is sampled before and after each fire.
 *  A structured report is printed to Serial at the end.
 *
 * =============================================================================
 * !! SAFETY WARNING !!
 * =============================================================================
 *  DO NOT connect real pyrotechnic charges to the pyro pins during this test.
 *  Use a dummy load instead — e.g. an LED + 100 Ω resistor, or a continuity
 *  tester bridged across the channel terminals.  The firmware has NO knowledge
 *  of whether a real charge is present.
 * =============================================================================
 *
 * =============================================================================
 * HARDWARE SETUP
 * =============================================================================
 *
 *  --- Battery voltage divider (ADS1115 channel A0) ---
 *
 *   Battery (+) ──┬── R1 (4.7 kΩ) ──┬── R2 (1.1 kΩ) ── GND
 *                 │                  │
 *                N/C             ADS1115 A0
 *
 *   V_bat = V_A0 × (R1 + R2) / R2 = V_A0 × 5.2727
 *   Max readable: 3.3 V × 5.2727 ≈ 17.4 V  → supports 4S LiPo (16.8 V max)
 *
 *  --- ADS1115 wiring ---
 *   VDD  → 3.3 V
 *   GND  → GND
 *   SDA  → ESP32 GPIO 21
 *   SCL  → ESP32 GPIO 22
 *   ADDR → GND  (address 0x48)
 *   A0   → voltage-divider mid-point
 *
 *  --- Pyro pins (dummy-load only!) ---
 *   DROGUE pin : GPIO 25  (DROGUE_PWM_CHANNEL  = LEDC ch 3)
 *   MAIN   pin : GPIO 12  (MAIN_PWM_CHANNEL    = LEDC ch 4)
 *
 *   Supply voltage assumed: 17.8 V (matches flight computer Vcc default)
 *   PWM resolution        : 8-bit (0–255)
 *
 * =============================================================================
 * REQUIRED LIBRARY
 * =============================================================================
 *  "Adafruit ADS1X15"  by Adafruit  (install via Arduino Library Manager)
 *  ESP32 LEDC is built into the ESP32 Arduino core — no extra library needed.
 * =============================================================================
 */

#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ═════════════════════════════════════════════════════════════════════════════
// CONFIGURATION  — mirror of include/defs.h (production values)
// ═════════════════════════════════════════════════════════════════════════════

// --- Pyro GPIO pins ----------------------------------------------------------
#define DROGUE_PIN           25
#define MAIN_CHUTE_EJECT_PIN 12

// --- LEDC channel assignments ------------------------------------------------
#define DROGUE_PWM_CHANNEL    3
#define MAIN_PWM_CHANNEL      4

// --- PWM hardware settings ---------------------------------------------------
#define PYRO_PWM_FREQ       500       // Hz
#define PYRO_PWM_RES_BITS     8       // 0–255

// --- Pyro supply voltage (actual main battery at flight computer) -------------
#define PYRO_SUPPLY_VOLTAGE  17.8f    // volts (Vcc in main.cpp)

// ─── Voltage Divider (battery voltage sense) ─────────────────────────────────
const float R1_OHMS       = 4700.0f;
const float R2_OHMS       = 1100.0f;
const float DIVIDER_RATIO = (R1_OHMS + R2_OHMS) / R2_OHMS;  // 5.2727

// ─── ADS1115 ─────────────────────────────────────────────────────────────────
const uint8_t ADS_ADDR       = 0x48;
const uint8_t ADC_CH         = 2;   // A0 — battery voltage divider
const uint8_t ADC_CH_DROGUE  = 1;   // A1 — drogue pin voltage divider
const uint8_t ADC_CH_MAIN    = 0;   // A2 — main pin voltage divider

// Threshold: ADS-measured pyro line voltage (after divider) above this
// confirms the pin is actively being driven (PWM average > 0).
// 17.8 V supply × lowest duty (Config-A ~22%) / divider ratio ≈ 0.74 V.
// Set threshold conservatively at 0.4 V to catch all three configs.
const float PYRO_DETECT_THRESHOLD_V = 0.4f;

// ─── I2C pins ─────────────────────────────────────────────────────────────────
const int SDA_PIN = 21;
const int SCL_PIN = 22;

// ─── Battery thresholds (4S LiPo defaults) ───────────────────────────────────
const uint8_t CELL_COUNT    = 4;
const float   CELL_MAX_V    = 4.20f;
const float   CELL_NOM_V    = 3.70f;
const float   CELL_LOW_V    = 3.50f;
const float   CELL_CRIT_V   = 3.30f;
const float   CELL_CUTOFF_V = 3.00f;

const float BAT_FULL   = CELL_MAX_V    * CELL_COUNT;  // 16.80 V
const float BAT_NOM    = CELL_NOM_V    * CELL_COUNT;  // 14.80 V
const float BAT_LOW    = CELL_LOW_V    * CELL_COUNT;  // 14.00 V
const float BAT_CRIT   = CELL_CRIT_V   * CELL_COUNT;  // 13.20 V
const float BAT_CUTOFF = CELL_CUTOFF_V * CELL_COUNT;  // 12.00 V

// ─── Pyro test matrix ────────────────────────────────────────────────────────
// Each entry: { label, supply_V, target_V, freq_Hz, duration_ms }
struct PyroConfig {
    const char*   label;
    float         supply_V;
    float         target_V;
    uint32_t      freq_Hz;
    uint32_t      duration_ms;
};

// 3 configurations applied to EACH channel
const PyroConfig TEST_MATRIX[] = {
    { "Config-A  (LOW  4V / 2s)",  PYRO_SUPPLY_VOLTAGE, 4.0f, 500, 2000 },
    { "Config-B  (NOM  6V / 3s)",  PYRO_SUPPLY_VOLTAGE, 6.0f, 500, 3000 },
    { "Config-C  (HIGH 9V / 5s)",  PYRO_SUPPLY_VOLTAGE, 9.0f, 500, 5000 },
};
const uint8_t NUM_CONFIGS = sizeof(TEST_MATRIX) / sizeof(TEST_MATRIX[0]);

// ─── Sampling ─────────────────────────────────────────────────────────────────
const uint8_t  AVG_SAMPLES        = 8;
const uint32_t SETTLE_MS          = 300;   // wait after pin change before reading

// ═════════════════════════════════════════════════════════════════════════════
// RESULT STORE
// ═════════════════════════════════════════════════════════════════════════════

struct ChannelResult {
    const char*  channelName;
    uint8_t      gpioPin;
    uint8_t      ledcChannel;
    uint8_t      adsChannel;        // ADS1115 channel used to sense this pin
    // per-config results
    float        batV_before[3];
    float        batV_after[3];
    uint8_t      duty[3];
    uint32_t     freq_Hz[3];
    uint32_t     duration_ms[3];
    float        target_V[3];
    float        pyroV_max[3];      // peak ADS-measured pyro line voltage during fire
    bool         pinActive[3];      // pyro line voltage exceeded detection threshold
    bool         ran[3];
};

ChannelResult results[2];   // [0] = DROGUE, [1] = MAIN

// ═════════════════════════════════════════════════════════════════════════════
// GLOBALS
// ═════════════════════════════════════════════════════════════════════════════

Adafruit_ADS1115 ads;
float voltageBuffer[8] = {0};
uint8_t bufIdx   = 0;
bool    bufFull  = false;

// ═════════════════════════════════════════════════════════════════════════════
// UTILITIES
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Compute LEDC duty from desired output voltage.
 *   duty = (targetV / supplyV) × (2^bits − 1)
 *   clamped to [0, 255] for 8-bit
 */
int computeDuty(float supplyV, float targetV) {
    if (supplyV <= 0.0f) return 0;
    float ratio = targetV / supplyV;
    ratio = constrain(ratio, 0.0f, 1.0f);
    return (int)(ratio * 255.0f);
}

/**
 * Circular-buffer rolling average over AVG_SAMPLES readings.
 */
float rollingAverage(float newVal) {
    voltageBuffer[bufIdx] = newVal;
    bufIdx = (bufIdx + 1) % AVG_SAMPLES;
    if (bufIdx == 0) bufFull = true;

    uint8_t count = bufFull ? AVG_SAMPLES : bufIdx;
    float   sum   = 0.0f;
    for (uint8_t i = 0; i < count; i++) sum += voltageBuffer[i];
    return sum / (float)count;
}

/**
 * Read the voltage on a pyro pin via ADS1115 (single sample, through divider).
 * Used during fire to check the line is actually being driven.
 */
float readPyroPinV(uint8_t adsChannel) {
    float sum = 0.0f;
    const uint8_t N = 4;
    for (uint8_t i = 0; i < N; i++) {
        int16_t raw  = ads.readADC_SingleEnded(adsChannel);
        float   pinV = ads.computeVolts(raw);
        if (pinV < 0.0f || pinV > 3.6f) pinV = 0.0f;
        sum += pinV * DIVIDER_RATIO;
        delay(8);
    }
    return sum / (float)N;
}

/**
 * Read a stable, averaged battery voltage from ADS1115.
 * Takes AVG_SAMPLES quick readings and returns the mean.
 */
float readBatteryV() {
    float sum = 0.0f;
    for (uint8_t i = 0; i < AVG_SAMPLES; i++) {
        int16_t raw  = ads.readADC_SingleEnded(ADC_CH);
        float   pinV = ads.computeVolts(raw);
        if (pinV < 0.0f || pinV > 3.6f) pinV = 0.0f;  // reject bad reads
        sum += pinV * DIVIDER_RATIO;
        delay(10);
    }
    return sum / AVG_SAMPLES;
}

/**
 * Return status label for a given pack voltage.
 */
const char* batteryStatus(float v) {
    if (v >= BAT_FULL)   return "FULL    ";
    if (v >= BAT_NOM)    return "NOMINAL ";
    if (v >= BAT_LOW)    return "LOW  !  ";
    if (v >= BAT_CRIT)   return "CRITICAL";
    if (v >= BAT_CUTOFF) return "CUTOFF!!";
    return                      "NO BATT ";
}

/**
 * Linear battery percentage.
 */
float batteryPercent(float v) {
    if (v >= BAT_FULL)   return 100.0f;
    if (v <= BAT_CUTOFF) return   0.0f;
    return 100.0f * (v - BAT_CUTOFF) / (BAT_FULL - BAT_CUTOFF);
}

void printSeparator(char c = '-', uint8_t len = 72) {
    for (uint8_t i = 0; i < len; i++) Serial.print(c);
    Serial.println();
}

// ═════════════════════════════════════════════════════════════════════════════
// ADS1115 SELF-TEST
// ═════════════════════════════════════════════════════════════════════════════

bool adsSelfTest() {
    Serial.println(F("\n  -- ADS1115 self-test ----------------------------------"));

    int16_t raw  = ads.readADC_SingleEnded(ADC_CH);
    float   pinV = ads.computeVolts(raw);
    float   batV = pinV * DIVIDER_RATIO;

    Serial.print(F("  Raw ADC   : ")); Serial.println(raw);
    Serial.print(F("  Pin V     : ")); Serial.print(pinV, 4); Serial.println(F(" V"));
    Serial.print(F("  Battery V : ")); Serial.print(batV, 4); Serial.println(F(" V"));
    Serial.print(F("  Status    : ")); Serial.println(batteryStatus(batV));

    bool pinOK = (pinV >= 0.0f && pinV <= 3.6f);
    Serial.print(F("  Pin range : ")); Serial.println(pinOK ? F("PASS") : F("FAIL -- check wiring"));

    bool batOK = (batV >= BAT_CUTOFF);
    Serial.print(F("  Bat range : "));
    if (batOK) Serial.println(F("PASS"));
    else       Serial.println(F("WARN -- below cutoff or no battery"));

    float expLSB  = 4.096f / 32768.0f;
    float measLSB = (raw != 0) ? (pinV / (float)raw) : 0.0f;
    bool  lsbOK   = (fabsf(measLSB - expLSB) < expLSB * 0.05f);
    Serial.print(F("  LSB check : exp="));  Serial.print(expLSB,  6);
    Serial.print(F(" V  meas="));           Serial.print(measLSB, 6);
    Serial.println(lsbOK ? F(" V  PASS") : F(" V  WARN"));

    Serial.println(F("  -------------------------------------------------------"));
    return pinOK;
}

// ═════════════════════════════════════════════════════════════════════════════
// PYRO CHANNEL TEST
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Run all NUM_CONFIGS PWM tests on one pyro channel.
 *
 * @param res        pointer to result struct to fill
 * @param name       human-readable channel name ("DROGUE" / "MAIN")
 * @param pin        GPIO pin number
 * @param ledcCh     LEDC channel (0–15); stored in result for reference only
 * @param adsChannel ADS1115 channel (A1 or A2) wired to this pyro line
 */
void testChannel(ChannelResult* res, const char* name, uint8_t pin, uint8_t ledcCh, uint8_t adsChannel) {

    // --- Header ---
    Serial.println();
    printSeparator('=');
    Serial.print(F("  CHANNEL : "));
    Serial.print(name);
    Serial.print(F("  (GPIO "));
    Serial.print(pin);
    Serial.print(F("  LEDC ch "));
    Serial.print(ledcCh);
    Serial.println(F(")"));
    printSeparator('=');

    res->channelName  = name;
    res->gpioPin      = pin;
    res->ledcChannel  = ledcCh;
    res->adsChannel   = adsChannel;

    // Safety: ensure pin starts LOW before attaching LEDC
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delay(100);

    for (uint8_t c = 0; c < NUM_CONFIGS; c++) {
        const PyroConfig& cfg = TEST_MATRIX[c];

        uint8_t  duty = (uint8_t)computeDuty(cfg.supply_V, cfg.target_V);
        res->duty[c]        = duty;
        res->freq_Hz[c]     = cfg.freq_Hz;
        res->duration_ms[c] = cfg.duration_ms;
        res->target_V[c]    = cfg.target_V;
        res->pyroV_max[c]   = 0.0f;
        res->pinActive[c]   = false;
        res->ran[c]         = false;

        Serial.println();
        Serial.print(F("  >>> "));
        Serial.println(cfg.label);
        Serial.print(F("      Supply="));  Serial.print(cfg.supply_V, 1);
        Serial.print(F("V  Target="));    Serial.print(cfg.target_V,  1);
        Serial.print(F("V  Freq="));      Serial.print(cfg.freq_Hz);
        Serial.print(F("Hz  Duty="));     Serial.print(duty);
        Serial.print(F("/255 ("));        Serial.print((duty * 100) / 255);
        Serial.print(F("%)  Duration=")); Serial.print(cfg.duration_ms);
        Serial.println(F("ms"));

        // --- Battery before ---
        delay(SETTLE_MS);
        float bV_before = readBatteryV();
        res->batV_before[c] = bV_before;
        Serial.print(F("      Battery BEFORE : "));
        Serial.print(bV_before, 3);
        Serial.print(F(" V  ["));
        Serial.print(batteryStatus(bV_before));
        Serial.println(F("]"));

        // --- Configure LEDC and fire ---
        // ESP32 Arduino core 3.x: ledcAttach(pin, freq, resolution) replaces
        // the old ledcSetup + ledcAttachPin combo.  Channel is auto-assigned.
        ledcAttach(pin, cfg.freq_Hz, PYRO_PWM_RES_BITS);
        delay(10);
        ledcWrite(pin, duty);

        Serial.println(F("      Sampling pyro line voltage via ADS1115 during fire..."));
        // ESP32 LEDC owns the pin — digitalRead is unreliable on a PWM pin.
        // Instead, read the ADS channel wired to this pyro line.
        // Average of multiple samples gives a proxy for the PWM average voltage.
        float    pyroVMax  = 0.0f;
        uint32_t fireStart = millis();
        while ((millis() - fireStart) < cfg.duration_ms) {
            float v = readPyroPinV(adsChannel);
            if (v > pyroVMax) pyroVMax = v;
            delay(100);
        }
        bool pinActive = (pyroVMax >= PYRO_DETECT_THRESHOLD_V);
        res->pyroV_max[c]  = pyroVMax;
        res->pinActive[c]  = pinActive;
        Serial.print(F("      Pyro line peak V (ADS A"));
        Serial.print(adsChannel);
        Serial.print(F(") : "));
        Serial.print(pyroVMax, 3);
        Serial.print(F(" V  → "));
        Serial.println(pinActive ? F("ACTIVE (OK)") : F("INACTIVE  (WARN — check wiring/divider on A1/A2)"));

        // --- Stop PWM ---
        ledcWrite(pin, 0);
        ledcDetach(pin);
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        delay(SETTLE_MS);

        // --- Battery after ---
        float bV_after = readBatteryV();
        res->batV_after[c] = bV_after;
        Serial.print(F("      Battery AFTER  : "));
        Serial.print(bV_after, 3);
        Serial.print(F(" V  ["));
        Serial.print(batteryStatus(bV_after));
        Serial.print(F("]   Delta="));
        Serial.print(bV_after - bV_before, 3);
        Serial.println(F(" V"));

        res->ran[c] = true;

        // Cool-down between configs
        Serial.println(F("      [cool-down 2 s]"));
        delay(2000);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// FINAL REPORT
// ═════════════════════════════════════════════════════════════════════════════

void printReport(float initBatV) {
    Serial.println();
    printSeparator('*');
    Serial.println(F("  PYRO DEPLOYMENT TEST -- FINAL REPORT"));
    printSeparator('*');
    Serial.print(F("  Initial battery voltage : "));
    Serial.print(initBatV, 3);
    Serial.print(F(" V  ["));
    Serial.print(batteryStatus(initBatV));
    Serial.println(F("]"));
    Serial.print(F("  Battery at start       : "));
    Serial.print(batteryPercent(initBatV), 1);
    Serial.println(F(" %"));
    Serial.println();

    for (uint8_t ch = 0; ch < 2; ch++) {
        ChannelResult& r = results[ch];
        Serial.print(F("  ── CHANNEL : "));
        Serial.print(r.channelName);
        Serial.print(F("  GPIO "));
        Serial.print(r.gpioPin);
        Serial.print(F("  LEDC ch "));
        Serial.print(r.ledcChannel);
        Serial.print(F("  ADS A"));
        Serial.println(r.adsChannel);
        printSeparator('-', 72);

        // Column header
        Serial.println(F("  Config       | SupplyV | TargetV | Duty | Freq | Duration | BatBefore | BatAfter  | Delta  | PyroV   | Active | Result"));
        Serial.println(F("  -------------|---------|---------|------|------|----------|-----------|-----------|--------|---------|--------|-------"));

        for (uint8_t c = 0; c < NUM_CONFIGS; c++) {
            if (!r.ran[c]) {
                Serial.print(F("  Config-"));
                Serial.print((char)('A' + c));
                Serial.println(F("  | SKIPPED"));
                continue;
            }

            float delta = r.batV_after[c] - r.batV_before[c];
            bool  pass  = r.ran[c] && r.pinActive[c];

            char buf[140];
            snprintf(buf, sizeof(buf),
                "  %-12s | %5.1f V  | %5.1f V  | %3u  | %3lu Hz | %5lu ms  | %7.3f V  | %7.3f V  | %+6.3f | %5.2f V | %-6s | %s",
                TEST_MATRIX[c].label,
                (double)PYRO_SUPPLY_VOLTAGE,
                (double)r.target_V[c],
                r.duty[c],
                (unsigned long)r.freq_Hz[c],
                (unsigned long)r.duration_ms[c],
                (double)r.batV_before[c],
                (double)r.batV_after[c],
                (double)delta,
                (double)r.pyroV_max[c],
                r.pinActive[c] ? "YES" : "NO",
                pass ? "PASS" : "FAIL"
            );
            Serial.println(buf);
        }
        Serial.println();
    }

    // ── Overall summary ────────────────────────────────────────────────────
    printSeparator('=');
    Serial.println(F("  SUMMARY"));
    printSeparator('=');

    bool allPass = true;
    for (uint8_t ch = 0; ch < 2; ch++) {
        ChannelResult& r = results[ch];
        for (uint8_t c = 0; c < NUM_CONFIGS; c++) {
            if (r.ran[c]) {
                Serial.print(F("  "));
                Serial.print(r.channelName);
                Serial.print(F("  "));
                Serial.print(TEST_MATRIX[c].label);
                Serial.print(F("  → duty="));
                Serial.print(r.duty[c]);
                Serial.print(F("/255  pyroV_max="));
                Serial.print(r.pyroV_max[c], 2);
                Serial.print(F("V  active="));
                Serial.print(r.pinActive[c] ? "YES" : "NO ");
                if (!r.pinActive[c]) {
                    Serial.print(F("  ← WARN: pyro line below threshold (check voltage divider on A1/A2)"));
                    allPass = false;
                }
                Serial.println();
            }
        }
    }

    // Final battery reading
    float finalBatV = readBatteryV();
    float batDrop   = initBatV - finalBatV;
    Serial.println();
    Serial.print(F("  Final battery voltage  : ")); Serial.print(finalBatV, 3); Serial.println(F(" V"));
    Serial.print(F("  Total voltage drop     : ")); Serial.print(batDrop,   3); Serial.println(F(" V"));
    Serial.print(F("  Battery status         : ")); Serial.println(batteryStatus(finalBatV));

    if (batDrop > 1.0f)
        Serial.println(F("  [WARN] Significant voltage drop detected — check pyro supply capacity"));

    Serial.println();
    if (allPass)
        Serial.println(F("  ✓  ALL CHANNELS PASSED"));
    else
        Serial.println(F("  ✗  ONE OR MORE CHANNELS HAVE WARNINGS — review output above"));

    printSeparator('*');
    Serial.println(F("  Test complete.  Safe to power down."));
    printSeparator('*');
}

// ═════════════════════════════════════════════════════════════════════════════
// SETUP
// ═════════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    Serial.println(F("\n"));
    printSeparator('=');
    Serial.println(F("  N4 FLIGHT COMPUTER -- PYRO DEPLOYMENT + BATTERY TEST"));
    printSeparator('=');
    Serial.println(F("  !! SAFETY: USE DUMMY LOADS ONLY (LED+resistor or continuity meter)"));
    Serial.println(F("  !! DO NOT connect real pyro charges during this test."));
    printSeparator();
    Serial.print(F("  Voltage divider : R1="));  Serial.print(R1_OHMS, 0);
    Serial.print(F(" Ohm  R2="));                Serial.print(R2_OHMS, 0);
    Serial.print(F(" Ohm  ratio="));             Serial.println(DIVIDER_RATIO, 4);
    Serial.print(F("  Battery pack    : "));      Serial.print(CELL_COUNT);
    Serial.print(F("S LiPo  Full="));            Serial.print(BAT_FULL, 2);
    Serial.print(F("V  Cutoff="));               Serial.print(BAT_CUTOFF, 2);
    Serial.println(F("V"));
    Serial.print(F("  Pyro pins       : DROGUE=GPIO "));
    Serial.print(DROGUE_PIN);
    Serial.print(F("  MAIN=GPIO "));
    Serial.println(MAIN_CHUTE_EJECT_PIN);
    Serial.print(F("  PWM             : "));
    Serial.print(PYRO_PWM_FREQ);
    Serial.print(F(" Hz  8-bit  supply="));
    Serial.print(PYRO_SUPPLY_VOLTAGE, 1);
    Serial.println(F(" V"));
    printSeparator();

    // --- Init I2C + ADS1115 ---
    Wire.begin(SDA_PIN, SCL_PIN);

    if (!ads.begin(ADS_ADDR)) {
        Serial.println(F("[FATAL] ADS1115 not found!  Check SDA/SCL/VDD/GND/ADDR."));
        while (1) delay(1000);
    }

    ads.setGain(GAIN_ONE);                  // ±4.096 V, 0.125 mV/LSB
    ads.setDataRate(RATE_ADS1115_128SPS);

    Serial.print(F("  [ADS1115] OK  addr=0x"));
    Serial.print(ADS_ADDR, HEX);
    Serial.println(F("  gain=GAIN_ONE  128 SPS"));

    bool adsOK = adsSelfTest();
    if (!adsOK) {
        Serial.println(F("[FATAL] ADS1115 self-test failed.  Halting."));
        while (1) delay(1000);
    }

    // --- Initial battery check (abort if critically low) ---
    float initBatV = readBatteryV();
    Serial.println();
    Serial.print(F("  Initial battery : "));
    Serial.print(initBatV, 3);
    Serial.print(F(" V  ["));
    Serial.print(batteryStatus(initBatV));
    Serial.print(F("]  "));
    Serial.print(batteryPercent(initBatV), 1);
    Serial.println(F(" %"));

    if (initBatV < BAT_CRIT && initBatV > 1.0f) {
        Serial.println(F("[WARN] Battery is at CRITICAL level.  Pyro test will still run but"));
        Serial.println(F("       voltage readings may be unreliable.  Charge battery first."));
    }

    if (initBatV <= BAT_CUTOFF && initBatV > 1.0f) {
        Serial.println(F("[ABORT] Battery below cutoff threshold.  Halting for safety."));
        while (1) delay(1000);
    }

    // --- Safety: drive pyro pins LOW before tests begin ---
    pinMode(DROGUE_PIN,           OUTPUT); digitalWrite(DROGUE_PIN,           LOW);
    pinMode(MAIN_CHUTE_EJECT_PIN, OUTPUT); digitalWrite(MAIN_CHUTE_EJECT_PIN, LOW);
    delay(200);

    Serial.println();
    Serial.println(F("  [OK] ADS1115 ready.  Starting pyro channel tests..."));
    Serial.println(F("  (Each config fires for its duration then cools down 2 s)\n"));
    delay(1000);

    // ═══════════════════════════════════════════════════════════════════════
    // RUN TESTS
    // ═══════════════════════════════════════════════════════════════════════

    testChannel(&results[0], "DROGUE", DROGUE_PIN,           DROGUE_PWM_CHANNEL, ADC_CH_DROGUE);
    testChannel(&results[1], "MAIN",   MAIN_CHUTE_EJECT_PIN, MAIN_PWM_CHANNEL,   ADC_CH_MAIN);

    // ═══════════════════════════════════════════════════════════════════════
    // FINAL REPORT
    // ═══════════════════════════════════════════════════════════════════════
    printReport(initBatV);
}

// ─────────────────────────────────────────────────────────────────────────────
// LOOP  — nothing to do after the test
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    // Continuous battery monitoring after the deployment test completes
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 2000) {
        lastPrint = millis();
        float v = readBatteryV();
        Serial.print(F("  [MONITOR] Battery: "));
        Serial.print(v, 3);
        Serial.print(F(" V ["));
        Serial.print(batteryStatus(v));
        Serial.print(F("]  "));
        Serial.print(batteryPercent(v), 1);
        Serial.println(F(" %"));
    }
}
