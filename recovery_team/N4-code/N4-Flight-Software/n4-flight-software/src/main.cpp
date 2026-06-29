#include "communication_manager.h"
/**
 * @file main.cpp
 * @author Edwin Mwiti
 * @version N4
 * @date July 15 2024
 * 
 * @brief This contains the main driver code for the flight computer
 * 
 * 0x5765206D6179206D616B65206F757220706C616E73202C
 * 0x62757420476F642068617320746865206C61737420776F7264
 * 
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <WiFi.h>
#include <ESP32PWM.h>
#include <PubSubClient.h> // TODO: ADD A MQTT SWITCH - TO USE MQTT OR NOT
#include <TinyGPSPlus.h>  // handle GPS
#include <SFE_BMP180.h>     // For BMP180
#include <FS.h>             // File system functions
#include <SD.h>             // SD card logging function
#include <SPIFFS.h>         // SPIFFS file system function
#include <esp_task_wdt.h>   // Watchdog timer functions
#include <ArduinoJson.h>    // JSON parsing for PWM configuration
//#include "freertos/FreeRTOS.h"
//#include "freertos/semphr.h"
#include "defs.h"           // misc defines
#include "mpu.h"            // for reading MPU6050
#include "CustomSerialFlash.h"    // Handling external SPI flash memory
#include "logger.h"         // system logging
#include "data_types.h"     // definitions of data types used
#include "states.h"         // state machine states
#include "system_logger.h"  // system logging functions
#include "system_log_levels.h"  // system logging log levels
#include "wifi-config.h"    // handle wifi connection
#include "kalman_filter.h"  // handle kalman filter functions
#include "ring_buffer.h"    // for apogee detection
#include "espnow_beacon_transmitter.h"
#include "communication_manager.h"  // smart communication management
#include "sd_logger.h" // SD- Card logging

/**
 * flight states
 * these states are to be used for flight
**/
// OPERATION_MODE enum now defined in defs.h

/* state machine variables*/
uint8_t operation_mode = 0;                                         /*!< Tells whether software is in safe or flight mode - FLIGHT_MODE=1, SAFE_MODE=0 */
bool is_system_armed = false;                                       /*!< Global armed state for all communication modes */

// 🔥 ISOLATED COMMUNICATION SYSTEM - Global variables for independent mode control
bool use_mqtt_mode = false;                                         /*!< Enable MQTT transmission - disabled by default for beacon runs */
bool use_beacon_mode = true;                                        /*!< Enable beacon transmission - default mode */
bool use_xbee_mode = false;                                         /*!< Enable XBee transmission - controlled by XBEE flag in defs.h */
bool auto_fallback_enabled = true;                                  /*!< Enable automatic fallback to beacon when MQTT fails */
bool communication_mode_locked = false;                             /*!< Lock mode changes during critical flight phases */
communication_status_t comm_status = {0};                           /*!< Communication status tracking */

// XBee Hardware Serial (UART1 - GPS uses UART2)
HardwareSerial XBeeSerial(1);  // Use UART1 for XBee (GPS is on UART2)

// // I2C bus mutex to prevent concurrent transactions across tasks.
// SemaphoreHandle_t i2c_mutex = NULL;

// static inline bool i2cLock(TickType_t timeout) {
//     if (i2c_mutex == NULL) {
//         return true;
//     }
//     return xSemaphoreTake(i2c_mutex, timeout) == pdTRUE;
// }

// static inline void i2cUnlock() {
//     if (i2c_mutex != NULL) {
//         xSemaphoreGive(i2c_mutex);
//     }
// }

/* non-task function prototypes definition */
void initDynamicWIFI();
void drogueChuteDeploy();
void mainChuteDeploy();
float kalmanFilter(float z);
void checkRunTestToggle();
void non_blocking_buzz(uint16_t interval);
void blocking_buzz(uint16_t interval);
void preflightHealthTask(void* pvParameters);
double altimeter_get_pressure();
void mqtt_command_processor(const char*, const char*);
void arm_pyros();
void disarm_pyros();
void chutesInit();
// void armMainChute();
// void disarmMainChute();
// void armDrogueChute();
// void disarmDrogueChute();
void checkChuteStatus();

void espnowCommandTask(void* pvParameters);
void xbeeCommandTask(void* pvParameters);
SDLogger sdLogger(SD_CS_PIN);

// Forward declaration for globals referenced in early function definitions.
extern volatile bool g_pyro_pwm_ready;

// 🔥 GLOBAL COMMUNICATION MANAGER - External declaration (defined in communication_manager.cpp)
extern CommunicationManager comm_manager;

// Global telemetry buffer for seamless mode switching
static char global_telemetry_buffer[320];
static bool telemetry_data_ready = false;
static uint32_t last_telemetry_time = 0;

// Function to update global telemetry buffer for seamless switching
void updateGlobalTelemetryBuffer(const char* buffer) {
    strncpy(global_telemetry_buffer, buffer, sizeof(global_telemetry_buffer) - 1);
    global_telemetry_buffer[sizeof(global_telemetry_buffer) - 1] = '\0';
    telemetry_data_ready = true;
    last_telemetry_time = millis();
}

// Function to get the latest telemetry for immediate transmission during mode switch
bool getLatestTelemetryBuffer(char* buffer, size_t buffer_size) {
    if (telemetry_data_ready && (millis() - last_telemetry_time < 1000)) { // Data fresh within 1 second
        strncpy(buffer, global_telemetry_buffer, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return true;
    }
    return false;
}

// Create transmitter instance
ESPNowBeaconTransmitter transmitter(ROCKET_MAC, BASE_MAC);

unsigned long mainPyroArmTime = 0;
unsigned long droguePyroArmTime = 0;
const unsigned long PYRO_ARM_DURATION = 5 * 60 * 1000; // 5 minutes
bool mainPyroArmed = false;
bool droguePyroArmed = false;

// --- PWM objects and voltage-based configuration ---
ESP32PWM droguePWM;
ESP32PWM mainPWM;

// User-specified voltages and durations (configurable via ESP-NOW JSON commands)
const float DEFAULT_PWM_VCC_FALLBACK = 16.8f;
float Vcc = DEFAULT_PWM_VCC_FALLBACK;  // input battery voltage
float desiredDrogueV = 3.0f;   // desired output voltage at drogue pin
float desiredMainV   = 10.0f;  // desired output voltage at main pin

// Configurable PWM on-durations (milliseconds)
unsigned long droguePWMDuration = 5000;  // Default 5 seconds
unsigned long mainPWMDuration = 5000;    // Default 5 seconds

// PWM Configuration structure for JSON commands with durations
struct PWMConfig {
    float vcc;
    float drogue_voltage;
    float main_voltage;
    unsigned long drogue_duration_ms;  // Duration in milliseconds
    unsigned long main_duration_ms;    // Duration in milliseconds
};

// Timing-based auto-shutdown variables
unsigned long drogueStartTime = 0;
unsigned long mainStartTime = 0;
bool drogueActive = false;
bool mainActive = false;

// Parse PWM configuration from JSON string with durations
bool parsePWMConfig(const char* jsonStr, PWMConfig& config) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        debug("❌ JSON parse error: ");
        debugln(error.c_str());
        return false;
    }
    
    // Extract values with defaults (preserve existing values if not specified)
    config.vcc = doc["vcc"] | Vcc;
    config.drogue_voltage = doc["drogue_v"] | desiredDrogueV;
    config.main_voltage = doc["main_v"] | desiredMainV;
    config.drogue_duration_ms = doc["drogue_time"] | droguePWMDuration;
    config.main_duration_ms = doc["main_time"] | mainPWMDuration;
    
    // Validate voltage ranges
    if (config.vcc < 0 || config.vcc > 20.0f) {
        debugln("❌ Invalid Vcc range (0-20V)");
        return false;
    }
    if (config.drogue_voltage < 0 || config.drogue_voltage > config.vcc) {
        debugln("❌ Invalid drogue voltage");
        return false;
    }
    if (config.main_voltage < 0 || config.main_voltage > config.vcc) {
        debugln("❌ Invalid main voltage");
        return false;
    }
    
    // Validate duration ranges (100ms to 60 seconds)
    if (config.drogue_duration_ms < 100 || config.drogue_duration_ms > 60000) {
        debugln("❌ Invalid drogue duration (100-60000ms)");
        return false;
    }
    if (config.main_duration_ms < 100 || config.main_duration_ms > 60000) {
        debugln("❌ Invalid main duration (100-60000ms)");
        return false;
    }
    
    return true;
}

// Apply PWM configuration (thread-safe) with durations
void applyPWMConfig(const PWMConfig& config) {
    Vcc = config.vcc;
    desiredDrogueV = config.drogue_voltage;
    desiredMainV = config.main_voltage;
    droguePWMDuration = config.drogue_duration_ms;
    mainPWMDuration = config.main_duration_ms;
    
    debug("✅ PWM Config Updated: Vcc=");
    debug(Vcc);
    debug("V, Drogue=");
    debug(desiredDrogueV);
    debug("V (");
    debug(droguePWMDuration);
    debug("ms), Main=");
    debug(desiredMainV);
    debug("V (");
    debug(mainPWMDuration);
    debugln("ms)");
}

// Compute PWM duty from desired voltage (0..255)
int computeDuty(float Vcc, float desiredV) {
    if (Vcc <= 0) return 0;
    float duty = desiredV / Vcc;
    duty = constrain(duty, 0.0f, 1.0f);
    return (int)(duty * 255);
}

// Helper queries
bool isDrogueOn() { return drogueActive; }
bool isMainOn()   { return mainActive; }

void arm_pyros() {
    digitalWrite(REMOTE_SWITCH, HIGH);
    // Small delay to ensure pin state is stable
    vTaskDelay(pdMS_TO_TICKS(10));
}

void drogueChuteDeploy() {
    // Start PWM at voltage-derived duty
    int duty = computeDuty(Vcc, desiredDrogueV);
    drogueActive = false;	//added for defensive programming
    droguePWM.write(duty);
    drogueStartTime = millis();
    drogueActive = true;
    DROGUE_DEPLOY_FLAG = 1;
    debugln(String("📦 DROGUE DEPLOYED (V=") + String(desiredDrogueV) + 
            String("V, PWM=") + String(duty) + String("/255, Duration=") + 
            String(droguePWMDuration) + String("ms)"));
}

void mainChuteDeploy() {
    // Start PWM at voltage-derived duty
    int duty = computeDuty(Vcc, desiredMainV);
    mainActive = false;	//added for defensive programming
    mainPWM.write(duty);
    mainStartTime = millis();
    mainActive = true;
    MAIN_CHUTE_EJECT_FLAG = 1;
    debugln(String("📦 MAIN CHUTE DEPLOYED (V=") + String(desiredMainV) + 
            String("V, PWM=") + String(duty) + String("/255, Duration=") + 
            String(mainPWMDuration) + String("ms)"));
}

void chutesInit() {
    pinMode(DROGUE_PIN, OUTPUT);
    pinMode(MAIN_CHUTE_EJECT_PIN, OUTPUT);
    pinMode(REMOTE_SWITCH, OUTPUT); // remote switch to arm pyros
    // Initialize PWM timers and attach pins: 1kHz, 8-bit
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    droguePWM.attachPin(DROGUE_PIN, 1000, 8); // 1kHz, 8-bit
    mainPWM.attachPin(MAIN_CHUTE_EJECT_PIN, 1000, 8);
    droguePWM.write(0);
    mainPWM.write(0);
    g_pyro_pwm_ready = true;
}

/**
 *
 */
void disarm_pyros() {
    digitalWrite(REMOTE_SWITCH, LOW);
    // Small delay to ensure pin state is stable
    vTaskDelay(pdMS_TO_TICKS(10));
}


void armMainPyro() {
    mainPyroArmTime = millis();
    int duty = computeDuty(Vcc, desiredMainV);
    mainPWM.write(duty);
    mainStartTime = millis();
    mainActive = true;
    mainPyroArmed = true;
    MAIN_CHUTE_EJECT_FLAG = 1;
    debugln(String("🔥 MAIN PYRO ARMED (V=") + String(desiredMainV) + 
            String("V, PWM=") + String(duty) + String("/255, Duration=") + 
            String(mainPWMDuration) + String("ms)"));
}

void disarmMainPyro() {
    mainPWM.write(0);
    mainActive = false;
    mainPyroArmed = false;
    MAIN_CHUTE_EJECT_FLAG = 0;
    debugln("MAIN PYRO DISARMED (PWM=0)");
}

void armDroguePyro() {
    int duty = computeDuty(Vcc, desiredDrogueV);
    droguePWM.write(duty);
    drogueStartTime = millis();
    drogueActive = true;
    droguePyroArmTime = millis();
    droguePyroArmed = true;
    DROGUE_DEPLOY_FLAG = 1;
    debugln(String("🔥 DROGUE PYRO ARMED (V=") + String(desiredDrogueV) + 
            String("V, PWM=") + String(duty) + String("/255, Duration=") + 
            String(droguePWMDuration) + String("ms)"));
}

void disarmDroguePyro() {
    droguePWM.write(0);
    drogueActive = false;
    droguePyroArmed = false;
    DROGUE_DEPLOY_FLAG = 0;
    debugln("DROGUE PYRO DISARMED (PWM=0)");
}

void checkAutoDisarm() {
    if (mainPyroArmed && (millis() - mainPyroArmTime >= PYRO_ARM_DURATION)) {
        disarmMainPyro();
        debugln("⏰ MAIN PYRO auto-disarmed after timer");
    }
    if (droguePyroArmed && (millis() - droguePyroArmTime >= PYRO_ARM_DURATION)) {
        disarmDroguePyro();
        debugln("⏰ DROGUE PYRO auto-disarmed after timer");
    }
    // Use configurable durations instead of fixed timeout
    if (mainActive && (millis() - mainStartTime >= mainPWMDuration)) {
        mainPWM.write(0);
        mainActive = false;
        debug("⏰ MAIN PYRO auto-shutdown after ");
        debug(mainPWMDuration);
        debugln("ms (PWM=0)");
    }
    if (drogueActive && (millis() - drogueStartTime >= droguePWMDuration)) {
        droguePWM.write(0);
        drogueActive = false;
        debug("⏰ DROGUE PYRO auto-shutdown after ");
        debug(droguePWMDuration);
        debugln("ms (PWM=0)");
    }
}




/* state machine variables*/
ARMED_FLIGHT_STATE current_state = ARMED_FLIGHT_STATE::PRE_FLIGHT_GROUND;	    /*!< The starting state - we start at PRE_FLIGHT_GROUND state */

uint8_t STATE_BIT_MASK = 0;

/* GPS object */
HardwareSerial gpsSerial(2); // PIN 16 AND 17 
TinyGPSPlus gps;
char gps_buffer[20];
gps_type_t gps_packet;

/* system logger */
SystemLogger SYSTEM_LOGGER;
const char* system_log_file = "/event_log.txt";
LOG_LEVEL level = INFO;
const char* rocket_ID = "FC1";             /*!< Unique ID of the rocket. Change to the needed rocket name before uploading */

/* set initial mode as safe mode
 * flag to indicate if we are in test or flight mode - This will
 * be changed by a command from the base station
 * */
uint8_t is_safe_mode = OPERATION_MODE::SAFE_MODE;

/* Intervals for buzzer state indication - see docs */
enum BUZZ_INTERVALS {
  SETUP_INIT = 200,
  ARMING_PROCEDURE = 500
};

enum BUZZER_PATTERN : uint8_t {
        BUZZER_PATTERN_NONE = 0,
        BUZZER_PATTERN_SHORT_ACK,
        BUZZER_PATTERN_STARTUP_TEST,
        BUZZER_PATTERN_STARTUP_FLIGHT
};

/* LED blink intervals */
enum BLINK_INTERVALS {
    SAFE_BLINK = 400,
    ARMED_BLINK = 100
};

unsigned long current_non_block_time = 0;
unsigned long last_non_block_time = 0;
bool buzz_state = 0;

volatile uint8_t g_buzzer_pattern_request = BUZZER_PATTERN_NONE;
volatile bool g_test_mode = (TEST == 1);
volatile bool g_preflight_block_flight = false;
volatile bool g_preflight_alarm_active = false;
volatile bool g_preflight_checks_complete = false;
volatile uint32_t g_boot_time_ms = 0;

volatile bool g_spiffs_ready = false;
volatile bool g_bmp_ready = false;
volatile bool g_imu_ready = false;
volatile bool g_gps_ready = false;
volatile bool g_ads_ready = false;
volatile bool g_altimeter_sample_valid = false;
volatile bool g_pyro_pwm_ready = false;
volatile uint8_t drogue_pin_engaged = 0;
volatile uint8_t main_chute_pin_engaged = 0;
volatile float logic_rail_3v3_voltage = 3.3f;
volatile uint8_t g_power_rail_low = 0;

TaskHandle_t preflightHealthTaskHandle = NULL;

static inline void requestBuzzerPattern(uint8_t pattern) {
    g_buzzer_pattern_request = pattern;
}

#if USE_SIMULATION

bool load_state = false;
struct sim_record {
	float time_s;
	float altitude_m;
	float velocity_mps;
	float acceleration_mps2;
};

#define MAX_SIM_RECORDS 1243

sim_record sim_data[MAX_SIM_RECORDS];

uint16_t sim_count = 0;
uint16_t sim_index = 0;

bool load_sim_file(const char *filename)
{
	File file = SPIFFS.open(filename);
	if (!file) {
		debugln("Failed to open simulation file!");
		return false;
	}

	sim_count = 0;

	while (file.available()) {
		String line = file.readStringUntil('\n');
		line.trim();

		if (line.length() == 0) continue; //skip blank lines
		if (line[0] == '#') continue; //skip comments
		if (!isdigit(line[0])) continue; //skip title

		float t, alt, vel, accel;

		if (sscanf(line.c_str(), 
					"%f,%f,%f,%f",
					&t, 
					&alt,
					&vel,
					&accel) == 4) {
			if (sim_count < MAX_SIM_RECORDS) {
				sim_data[sim_count].time_s = t;
				sim_data[sim_count].altitude_m = alt;
				sim_data[sim_count].velocity_mps = vel;
				sim_data[sim_count].acceleration_mps2 = accel;
			
				sim_count++;
			}
		}
	}

	file.close();
	return true;
}
#endif


static void buildTelemetryCsv(const telemetry_type_t& telemetry_received_packet, char* buffer, size_t buffer_len) {
    snprintf(buffer, buffer_len,
             "%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.8f,%.8f,%.2f,%u,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%.2f,%.2f,%d,%d,%.2f,%.2f\n",
             telemetry_received_packet.record_number,
             telemetry_received_packet.operation_mode,
             telemetry_received_packet.state,
             telemetry_received_packet.acc_data.ax,
             telemetry_received_packet.acc_data.ay,
             telemetry_received_packet.acc_data.az,
             telemetry_received_packet.acc_data.pitch,
             telemetry_received_packet.acc_data.roll,
             telemetry_received_packet.gyro_data.gx,
             telemetry_received_packet.gyro_data.gy,
             telemetry_received_packet.gyro_data.gz,
             gps_packet.latitude,
             gps_packet.longitude,
             gps_packet.gps_altitude,
             gps_packet.time,
             telemetry_received_packet.alt_data.pressure,
             telemetry_received_packet.alt_data.temperature,
             telemetry_received_packet.alt_data.rel_altitude,
             telemetry_received_packet.alt_data.velocity,
             telemetry_received_packet.drogue_pin_state,
             telemetry_received_packet.drogue_pin_engaged,
             telemetry_received_packet.main_chute_pin_state,
             telemetry_received_packet.main_chute_pin_engaged,
             telemetry_received_packet.battery_voltage,
             telemetry_received_packet.logic_rail_3v3_voltage,
             telemetry_received_packet.power_rail_low,
             telemetry_received_packet.wifi_rssi,
             telemetry_received_packet.alt_data.kalman_altitude,
             telemetry_received_packet.alt_data.kalman_vertical_velocity);
}

typedef struct {
    bool sensor_issue;
    bool battery_issue;
    bool power_rail_issue;
    bool chute_line_issue;
    uint8_t drogue_line_state;
    uint8_t main_line_state;
    float measured_battery_voltage;
    float measured_3v3_voltage;
    bool has_issue;
} preflight_check_result_t;

// 🔥 GLOBAL KALMAN FILTER OUTPUTS - Accessible by all tasks
float AltitudeKalman = 0.0, VelocityVerticalKalman = 0.0;

uint8_t mqtt_connect_flag;

/* hardware init check - to pinpoint any hardware failure during setup */
#define BMP_CHECK_BIT           0
#define IMU_CHECK_BIT           1   
#define FLASH_CHECK_BIT         2
#define GPS_CHECK_BIT           3
#define SD_CHECK_BIT            4
#define SPIFFS_CHECK_BIT        5
#define TEST_HARDWARE_CHECK_BIT 6
uint8_t SUBSYSTEM_INIT_MASK = 0b00000000;

/**
 * MQTT helper instances, if using MQTT to transmit telemetry
 */

WiFiClient wifi_client;
PubSubClient client(wifi_client);
uint8_t MQTTInit(const char* broker_IP, uint16_t broker_port);

/* WIFI configuration class object */
WIFIConfig wifi_config;

uint8_t drogue_pyro = 25;
uint8_t main_pyro = 12;
uint8_t flash_cs_pin = 5;                   /*!< External flash memory chip select pin */
uint8_t remote_switch = 27;

//chute deployment variables
volatile uint8_t drogue_pin_state = 0; 
volatile uint8_t main_chute_pin_state = 0;

// Battery monitoring variable
volatile float battery_voltage = 0.0;

// WiFi RSSI monitoring variable (for MQTT mode)
volatile int32_t wifi_rssi = 0;

// ═══════════════════════════════════════════════════════════════════════════════════════
// ADS1115 Battery & Pyro Line Voltage Monitor
// ═══════════════════════════════════════════════════════════════════════════════════════
Adafruit_ADS1115 ads;

// ADS1115 channel assignments (corrected per user's configuration)
const uint8_t ADS_ADDR       = 0x48;
const uint8_t ADC_CH_BATTERY = 2;   // A2 — battery voltage divider
const uint8_t ADC_CH_DROGUE  = 1;   // A1 — drogue pin voltage divider
const uint8_t ADC_CH_MAIN    = 0;   // A0 — main pin voltage divider
const uint8_t ADC_CH_3V3     = 3;   // A3 — 3.3V rail monitor

// Runtime channel mapping (for auto-detection at boot if needed)
volatile uint8_t g_adc_ch_battery = ADC_CH_BATTERY;
volatile uint8_t g_adc_ch_drogue  = ADC_CH_DROGUE;
volatile uint8_t g_adc_ch_main    = ADC_CH_MAIN;
volatile float g_ads_rail_factor  = 1.0f;

// I2C pins (ESP32 hardware defaults)
const int I2C_SDA_PIN = 21;
const int I2C_SCL_PIN = 22;

// Voltage divider constants (same 4.7kΩ / 1.1kΩ on all three ADS channels)
const float DIVIDER_R1       = 4700.0f;  // upper resistor (Ω)
const float DIVIDER_R2       = 1100.0f;  // lower resistor — ADS side (Ω)
const float DIVIDER_RATIO    = (DIVIDER_R1 + DIVIDER_R2) / DIVIDER_R2;  // 5.2727

// Pyro line detection threshold (after divider)
// For 17.8V supply × 22% duty (Config-A) → ~3.9V avg → 0.74V at ADS input.
// Set threshold at 0.4V to reliably detect all PWM configs.
const float PYRO_DETECT_THRESHOLD_V = 0.4f;

// Battery thresholds (4S LiPo defaults)
const uint8_t CELL_COUNT    = 4;
const float   CELL_CRIT_V   = 3.30f;
const float   CELL_CUTOFF_V = 3.00f;
const float   BAT_CRIT      = CELL_CRIT_V   * CELL_COUNT;  // 13.20 V
const float   BAT_CUTOFF    = CELL_CUTOFF_V * CELL_COUNT;  // 12.00 V
const float   BAT_MAX_VALID = 18.5f;                       // upper sanity limit for 4S + margin
const float   BAT_MIN_PLAUSIBLE_4S = 10.5f;                // reject impossible half-voltage glitches

// ADS availability flag - prevents repeated I2C errors when sensor is absent
volatile bool ads_monitor_ready = false;

// Task handle for battery monitoring
TaskHandle_t batteryMonitorTaskHandle = NULL;

/* Flight data logging */
uint8_t flash_led_pin = 32;                  /*!< LED pin connected to indicate flash memory formatting  */
char filename[] = "data_01.txt";         /*!< data log filename - Filename must be less than 20 chars, including the file extension */
uint32_t FILE_SIZE_512K = 524288L;          /*!< 512KB */
uint32_t FILE_SIZE_1M  = 1048576L;          /*!< 1MB */
uint32_t FILE_SIZE_4M  = 4194304L;          /*!< 4MB */
SerialFlashFile file;                       /*!< object representing file object for flash memory */
unsigned long long previous_log_time = 0;   /*!< The last time we logged data to memory */
unsigned long long current_log_time = 0;    /*!< What is the processor time right now? */
uint16_t log_sample_interval = 5;          /*!< After how long should we sample and log data to flash memory? */
/*/For data logging to disable both 
SD-CARD and flash memory before 
selecting the one data has to be logged to */
void disableAllDevices() {
  digitalWrite(flash_cs_pin, HIGH);
  digitalWrite(SD_CS_PIN, HIGH);
}
/* create flash memory log object */
DataLogger data_logger(flash_cs_pin, RED_LED_PIN, filename, file, FILE_SIZE_512K);

/* position integration variables */
long long current_time = 0;
long long previous_time = 0;

/* To store the main telemetry packet being sent over MQTT */
char telemetry_packet_buffer[320];
ring_buffer altitude_ring_buffer;
double baseline = 0.0; // to store baseline pressure from the altimeter
float curr_val;
float oldest_val;
uint8_t apogee_flag =0; // to signal that we have detected apogee
static int apogee_val = 0; // apogee altitude aproximmation
uint8_t main_eject_flag = 0;
volatile uint8_t DROGUE_DEPLOY_FLAG = 0;
volatile uint8_t MAIN_CHUTE_EJECT_FLAG = 0;

// Global current telemetry data for ARM altitude check
telemetry_type_t g_current_telemetry = {0};
unsigned long g_last_telemetry_update = 0;
static unsigned long apogee_detected_time = 0; // Timestamp when apogee was detected
/**
* @brief create dynamic WIFI
*/
void initDynamicWIFI() {
    // 🔥 STRICT BEACON MODE - Only initialize WiFi if MQTT flag is enabled
    if (MQTT) {
        uint8_t wifi_result = wifi_config.WifiConnect(false, ROCKET_MAC); // MQTT mode: use_beacon_mode = false
        if(wifi_result) {
            debugln("Wifi config OK!");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "Wifi config OK!\r\n");
        } else {
            debugln("Wifi config failed");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "Wifi config failed\r\n");
        }
    } else {
        debugln("MQTT disabled - WiFi not initialized in initDynamicWIFI (will be handled separately for beacon mode)");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "MQTT disabled - WiFi skipped in initDynamicWIFI\r\n");
    }
}

/**
* Check the toggle pin for TESTING or RUN mode 
 */
void checkRunTestToggle() {
    pinMode(SET_TEST_MODE_PIN, INPUT_PULLUP);
    pinMode(SET_RUN_MODE_PIN, INPUT_PULLUP);

    bool test_pin_grounded = (digitalRead(SET_TEST_MODE_PIN) == LOW);
    bool run_pin_grounded = (digitalRead(SET_RUN_MODE_PIN) == LOW);

    // Default mode comes from defs.h TEST flag, hardware pins can override at boot.
    g_test_mode = (TEST == 1);

    if (test_pin_grounded && !run_pin_grounded) {
        g_test_mode = true;
        debugln("MODE:TEST (override pin grounded)");
    } else if (run_pin_grounded && !test_pin_grounded) {
        g_test_mode = false;
        debugln("MODE:FLIGHT (override pin grounded)");
    } else if (run_pin_grounded && test_pin_grounded) {
        debugln("MODE:PIN CONFLICT (both grounded) -> using defs.h TEST flag");
        debugln(g_test_mode ? "MODE:TEST" : "MODE:FLIGHT");
    } else {
        debugln(g_test_mode ? "MODE:TEST (defs.h)" : "MODE:FLIGHT (defs.h)");
    }
}
/*!
 * @brief process commands sent from the base station
 * @param command
 * ARM
 * DISARM
 * RESET
 * DROGUE ON
 * DROGUE OFF
 * MAIN ON
 * MAIN OFF
 */
void espnowCommandTask(void* pvParameters) {
    ESPNowBeaconTransmitter::CommandPacket cmd;
    char cmdBuffer[MAX_COMMAND_LENGTH]; // Use MAX_COMMAND_LENGTH to match queue buffer

    while (1) {
        if (transmitter.getNextCommand(&cmd)) {
            size_t len = (cmd.length < (MAX_COMMAND_LENGTH - 1)) ? cmd.length : (MAX_COMMAND_LENGTH - 1);
            memcpy(cmdBuffer, cmd.command, len);
            cmdBuffer[len] = '\0';
            
            while (len > 0 && (cmdBuffer[len-1] == ' ' || cmdBuffer[len-1] == '\n' || cmdBuffer[len-1] == '\r')) {
                cmdBuffer[--len] = '\0';
            }

            // Handle communication mode commands
            if (strncmp(cmdBuffer, "CMD_MQTT_MODE", 13) == 0 ||
                strncmp(cmdBuffer, "CMD_BEACON_MODE", 15) == 0 ||
                strncmp(cmdBuffer, "CMD_XBEE_MODE", 13) == 0 ||
                strncmp(cmdBuffer, "CMD_AUTO_FALLBACK", 17) == 0 ||
                strncmp(cmdBuffer, "CMD_GET_MODE", 12) == 0) {
                comm_manager.handleModeCommand(String(cmdBuffer), "ESP_NOW");
            }
            // Handle PWM configuration command with durations
            else if (strncmp(cmdBuffer, "CMD_SET_PWM_CONFIG:", 19) == 0) {
                // Extract JSON payload after "CMD_SET_PWM_CONFIG:"
                const char* jsonPayload = cmdBuffer + 19;
                PWMConfig newConfig;
                
                if (parsePWMConfig(jsonPayload, newConfig)) {
                    applyPWMConfig(newConfig);
                    
                    // Send confirmation with durations back via ESP-NOW
                    char response[150];
                    snprintf(response, sizeof(response),
                             "PWM_CONFIG_OK:Vcc=%.1f,Drogue=%.1fV(%lums),Main=%.1fV(%lums)",
                             Vcc, desiredDrogueV, droguePWMDuration,
                             desiredMainV, mainPWMDuration);
                    esp_now_send(transmitter.getBaseMAC(), (uint8_t*)response, strlen(response));
                } else {
                    debugln("❌ Invalid PWM config JSON");
                    const char* error_msg = "PWM_CONFIG_ERROR:Invalid_JSON";
                    esp_now_send(transmitter.getBaseMAC(), (uint8_t*)error_msg, strlen(error_msg));
                }
            }
            else if (strcmp(cmdBuffer, "ARM") == 0) {
                #if USE_KALMAN_FOR_STATE_DETECTION
                float current_altitude = g_current_telemetry.alt_data.kalman_altitude;
                #else
                float current_altitude = g_current_telemetry.alt_data.rel_altitude;
                #endif
                
                arm_pyros();
                chutesInit();
                if (use_beacon_mode) transmitter.setArmed(true);
                is_system_armed = true;
                operation_mode = OPERATION_MODE::ARMED_MODE;
                requestBuzzerPattern(BUZZER_PATTERN_SHORT_ACK);
                
                if ((millis() - g_last_telemetry_update) > 2000) {
                    debugln("⚠️ ARMED via ESP-NOW (Warning: Stale telemetry data)");
                    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING,
                                           system_log_file, "ARMED via ESP-NOW - Stale telemetry data\r\n");
                } else if (current_altitude < ARM_ALTITUDE_THRESHOLD) {
                    debug("⚠️ ARMED via ESP-NOW (Warning: Low altitude ");
                    debug(current_altitude);
                    debug("m < ");
                    debug(ARM_ALTITUDE_THRESHOLD);
                    debugln("m)");
                    char log_msg[100];
                    snprintf(log_msg, sizeof(log_msg), "ARMED via ESP-NOW - Low altitude %.1fm\r\n", current_altitude);
                    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING,
                                           system_log_file, log_msg);
                } else {
                    debug("🚀 ARMED via ESP-NOW (Alt: ");
                    debug(current_altitude);
                    debugln("m ✓)");
                    char log_msg[100];
                    snprintf(log_msg, sizeof(log_msg), "ARMED via ESP-NOW at %.1fm altitude\r\n", current_altitude);
                    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                           system_log_file, log_msg);
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            } 
            else if (strcmp(cmdBuffer, "DISARM") == 0) {
                disarm_pyros();
                if (use_beacon_mode) transmitter.setArmed(false);
                is_system_armed = false;
                operation_mode = OPERATION_MODE::SAFE_MODE;
                requestBuzzerPattern(BUZZER_PATTERN_SHORT_ACK);
                debugln("🛑 DISARMED via ESP-NOW");
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                       system_log_file, "DISARMED via ESP-NOW\r\n");
                vTaskDelay(pdMS_TO_TICKS(50));
            } 
            else if (strcmp(cmdBuffer, "RESET") == 0) {
                debugln("🔄 RESET via ESP-NOW");
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                       system_log_file, "RESET via ESP-NOW\r\n");
                vTaskDelay(pdMS_TO_TICKS(100));
                ESP.restart();
            }
            else if (strcmp(cmdBuffer, "DROGUE_ON") == 0) {
                armDroguePyro();
                debugln("🪂 DROGUE CHUTE ARMED via ESP-NOW");
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                       system_log_file, "DROGUE CHUTE ARMED via ESP-NOW\r\n");
            }
            else if (strcmp(cmdBuffer, "DROGUE_OFF") == 0) {
                disarmDroguePyro();
                debugln("🪂 DROGUE CHUTE DISARMED via ESP-NOW");
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                       system_log_file, "DROGUE CHUTE DISARMED via ESP-NOW\r\n");
            }
            else if (strcmp(cmdBuffer, "MAIN_ON") == 0) {
                armMainPyro();
                debugln("🪂 MAIN CHUTE ARMED via ESP-NOW");
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                       system_log_file, "MAIN CHUTE ARMED via ESP-NOW\r\n");
            }
            else if (strcmp(cmdBuffer, "MAIN_OFF") == 0) {
                disarmMainPyro();
                debugln("🪂 MAIN CHUTE DISARMED via ESP-NOW");
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                       system_log_file, "MAIN CHUTE DISARMED via ESP-NOW\r\n");
            }
            // ── POP TEST commands (TEST MODE ONLY) ───────────────────────────
            // Fires a pyro directly without requiring ARM or passing preflight
            // chute-line checks. The preflightHealthTask suppresses its chute-line
            // anomaly report in g_test_mode so the test completes cleanly.
            else if (strcmp(cmdBuffer, "POP_TEST_DROGUE") == 0) {
                if (g_test_mode) {
                    debugln("🧪 POP TEST: Firing DROGUE (test mode, no preflight gate)");
                    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                           system_log_file, "POP TEST: DROGUE fired\r\n");
                    armDroguePyro();
                    // Auto-disarm after configured duration
                    vTaskDelay(pdMS_TO_TICKS(droguePWMDuration > 0 ? droguePWMDuration : 3000));
                    disarmDroguePyro();
                    debugln("🧪 POP TEST: DROGUE disarmed");
                } else {
                    debugln("⛔ POP_TEST_DROGUE rejected: only allowed in TEST mode");
                }
            }
            else if (strcmp(cmdBuffer, "POP_TEST_MAIN") == 0) {
                if (g_test_mode) {
                    debugln("🧪 POP TEST: Firing MAIN (test mode, no preflight gate)");
                    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                           system_log_file, "POP TEST: MAIN fired\r\n");
                    armMainPyro();
                    vTaskDelay(pdMS_TO_TICKS(mainPWMDuration > 0 ? mainPWMDuration : 5000));
                    disarmMainPyro();
                    debugln("🧪 POP TEST: MAIN disarmed");
                } else {
                    debugln("⛔ POP_TEST_MAIN rejected: only allowed in TEST mode");
                }
            }
            else {
                debugln("Unknown ESP-NOW cmd: " + String(cmdBuffer));
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING,
                                       system_log_file, ("Unknown ESP-NOW cmd: " + String(cmdBuffer) + "\r\n").c_str());
            }
        }
    	checkAutoDisarm();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}



void mqtt_command_processor(const char* topic, const char* payload) {
    if(strcmp(topic, "n4/commands") == 0) {
        String command = String(payload);
        
        if(command.startsWith("CMD_")) {
            comm_manager.handleModeCommand(command, "MQTT");
            return;
        }
        
        if(strcmp(payload, "ARM") == 0) {
            arm_pyros();
            chutesInit();
            if (use_beacon_mode) transmitter.setArmed(true);
            is_system_armed = true;
            operation_mode = OPERATION_MODE::ARMED_MODE;
            requestBuzzerPattern(BUZZER_PATTERN_SHORT_ACK);
            #if USE_KALMAN_FOR_STATE_DETECTION
            float current_altitude = g_current_telemetry.alt_data.kalman_altitude;
            #else
            float current_altitude = g_current_telemetry.alt_data.rel_altitude;
            #endif
            
            if ((millis() - g_last_telemetry_update) > 2000) {
                debugln("⚠️ ARMED via MQTT (Warning: Stale telemetry data)");
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, 
                                       system_log_file, "ARMED via MQTT - Stale telemetry data\r\n");
            } else if (current_altitude < ARM_ALTITUDE_THRESHOLD) {
                debug("⚠️ ARMED via MQTT (Warning: Low altitude ");
                debug(current_altitude);
                debugln("m)");
                char log_msg[100];
                snprintf(log_msg, sizeof(log_msg), "ARMED via MQTT - Low altitude %.1fm\r\n", current_altitude);
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING,
                                       system_log_file, log_msg);
            } else {
                debug("🚀 ARMED via MQTT (Alt: ");
                debug(current_altitude);
                debugln("m ✓)");
                char log_msg[100];
                snprintf(log_msg, sizeof(log_msg), "ARMED via MQTT at %.1fm altitude\r\n", current_altitude);
                SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, 
                                       system_log_file, log_msg);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        } 
        else if(strcmp(payload, "DISARM") == 0) {
            disarm_pyros();
            if (use_beacon_mode) transmitter.setArmed(false);
            is_system_armed = false;
            operation_mode = OPERATION_MODE::SAFE_MODE;
            requestBuzzerPattern(BUZZER_PATTERN_SHORT_ACK);
            debugln("🛑 DISARMED via MQTT");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                   system_log_file, "DISARMED via MQTT\r\n");
            vTaskDelay(pdMS_TO_TICKS(50));
        } 
        else if(strcmp(payload, "RESET") == 0) {
            debugln("🔄 RESET via MQTT");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                   system_log_file, "RESET via MQTT\r\n");
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP.restart();
        }
        else if(strcmp(payload, "DROGUE_ON") == 0) {
            armDroguePyro();
            debugln("🪂 DROGUE CHUTE ARMED via MQTT");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                   system_log_file, "DROGUE CHUTE ARMED via MQTT\r\n");
        }
        else if(strcmp(payload, "DROGUE_OFF") == 0) {
            disarmDroguePyro();
            debugln("🪂 DROGUE CHUTE DISARMED via MQTT");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                   system_log_file, "DROGUE CHUTE DISARMED via MQTT\r\n");
        }
        else if(strcmp(payload, "MAIN_ON") == 0) {
            armMainPyro();
            debugln("🪂 MAIN CHUTE ARMED via MQTT");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                   system_log_file, "MAIN CHUTE ARMED via MQTT\r\n");
        }
        else if(strcmp(payload, "MAIN_OFF") == 0) {
            disarmMainPyro();
            debugln("🪂 MAIN CHUTE DISARMED via MQTT");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                   system_log_file, "MAIN CHUTE DISARMED via MQTT\r\n");
        }
        // ── POP TEST (TEST MODE ONLY) ─────────────────────────────────────
        else if(strcmp(payload, "POP_TEST_DROGUE") == 0) {
            if (g_test_mode) {
                debugln("🧪 POP TEST: Firing DROGUE via MQTT");
                armDroguePyro();
                vTaskDelay(pdMS_TO_TICKS(droguePWMDuration > 0 ? droguePWMDuration : 3000));
                disarmDroguePyro();
                debugln("🧪 POP TEST: DROGUE complete");
            } else {
                debugln("⛔ POP_TEST_DROGUE rejected: TEST mode only");
            }
        }
        else if(strcmp(payload, "POP_TEST_MAIN") == 0) {
            if (g_test_mode) {
                debugln("🧪 POP TEST: Firing MAIN via MQTT");
                armMainPyro();
                vTaskDelay(pdMS_TO_TICKS(mainPWMDuration > 0 ? mainPWMDuration : 5000));
                disarmMainPyro();
                debugln("🧪 POP TEST: MAIN complete");
            } else {
                debugln("⛔ POP_TEST_MAIN rejected: TEST mode only");
            }
        }
        else {
            debugln("🔍 Unknown MQTT command: " + String(payload));
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING,
                                   system_log_file, ("Unknown MQTT command: " + String(payload) + "\r\n").c_str());
        }
    }
}



void xbeeCommandTask(void* pvParameters) {
    String line;

    while (1) {
        while (XBeeSerial.available() > 0) {
            char c = (char)XBeeSerial.read();
            if (c == '\n' || c == '\r') {
                if (line.length() > 0) {
                    String command = line;
                    command.trim();
                    String command_upper = command;
                    command_upper.toUpperCase();

                    if (command.length() > 0) {
                        // Mode commands are always accepted so links can be recovered.
                        if (command_upper.startsWith("CMD_MQTT_MODE") ||
                            command_upper.startsWith("CMD_BEACON_MODE") ||
                            command_upper.startsWith("CMD_XBEE_MODE") ||
                            command_upper.startsWith("CMD_AUTO_FALLBACK") ||
                            command_upper.startsWith("CMD_GET_MODE")) {
                            comm_manager.handleModeCommand(command_upper, "XBEE");
                        }
                        // Ignore likely telemetry/CSV lines on command channel.
                        else if (command.indexOf(',') >= 0) {
                            // no-op
                        }
                        // In XBee mode, process non-mode commands over XBee.
                        else if (comm_manager.isXBeeActive()) {
                            if (command_upper == "ARM" || command_upper == "CMD_ARM") {
                                if (!g_test_mode && (!g_preflight_checks_complete || g_preflight_block_flight)) {
                                    debugln("⛔ ARM rejected via XBee: preflight checks not satisfied (FLIGHT mode)");
                                } else {
                                    arm_pyros();
                                    chutesInit();
                                    if (use_beacon_mode) transmitter.setArmed(true);
                                    is_system_armed = true;
                                    operation_mode = OPERATION_MODE::ARMED_MODE;
                                    requestBuzzerPattern(BUZZER_PATTERN_SHORT_ACK);
                                    debugln("🚀 ARMED via XBee");
                                }
                            } else if (command_upper == "DISARM" || command_upper == "CMD_DISARM") {
                                disarm_pyros();
                                if (use_beacon_mode) transmitter.setArmed(false);
                                is_system_armed = false;
                                operation_mode = OPERATION_MODE::SAFE_MODE;
                                requestBuzzerPattern(BUZZER_PATTERN_SHORT_ACK);
                                debugln("🛑 DISARMED via XBee");
                            } else if (command_upper == "RESET" || command_upper == "CMD_RESET") {
                                debugln("🔄 RESET via XBee");
                                vTaskDelay(pdMS_TO_TICKS(100));
                                ESP.restart();
                            } else if (command_upper == "DROGUE_ON") {
                                armDroguePyro();
                                debugln("🪂 DROGUE CHUTE ARMED via XBee");
                            } else if (command_upper == "DROGUE_OFF") {
                                disarmDroguePyro();
                                debugln("🪂 DROGUE CHUTE DISARMED via XBee");
                            } else if (command_upper == "MAIN_ON") {
                                armMainPyro();
                                debugln("🪂 MAIN CHUTE ARMED via XBee");
                            } else if (command_upper == "MAIN_OFF") {
                                disarmMainPyro();
                                debugln("🪂 MAIN CHUTE DISARMED via XBee");
                            // ── POP TEST (TEST MODE ONLY) ─────────────────────
                            // Fires a pyro directly — no ARM, no preflight gate.
                            // preflightHealthTask suppresses chute-line anomaly
                            // in g_test_mode so the test completes cleanly.
                            } else if (command_upper == "POP_TEST_DROGUE") {
                                if (g_test_mode) {
                                    debugln("🧪 POP TEST: Firing DROGUE via XBee");
                                    armDroguePyro();
                                    vTaskDelay(pdMS_TO_TICKS(droguePWMDuration > 0 ? droguePWMDuration : 3000));
                                    disarmDroguePyro();
                                    debugln("🧪 POP TEST: DROGUE complete");
                                } else {
                                    debugln("⛔ POP_TEST_DROGUE rejected: TEST mode only");
                                }
                            } else if (command_upper == "POP_TEST_MAIN") {
                                if (g_test_mode) {
                                    debugln("🧪 POP TEST: Firing MAIN via XBee");
                                    armMainPyro();
                                    vTaskDelay(pdMS_TO_TICKS(mainPWMDuration > 0 ? mainPWMDuration : 5000));
                                    disarmMainPyro();
                                    debugln("🧪 POP TEST: MAIN complete");
                                } else {
                                    debugln("⛔ POP_TEST_MAIN rejected: TEST mode only");
                                }
                            } else if (command_upper.startsWith("CMD_SET_PWM_CONFIG:")) {
                                int payloadIndex = command.indexOf(':');
                                const char* jsonPayload = (payloadIndex >= 0) ? command.substring(payloadIndex + 1).c_str() : "";
                                PWMConfig newConfig;
                                if (parsePWMConfig(jsonPayload, newConfig)) {
                                    applyPWMConfig(newConfig);
                                } else {
                                    debugln("❌ Invalid PWM config JSON via XBee");
                                }
                            }
                        }
                    }
                }
                line = "";
            } else {
                line += c;
                if (line.length() > 200) {
                    line = "";
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}







/**
 * Task creation handles
 */
 TaskHandle_t readAccelerationTaskHandle;
 TaskHandle_t readAltimeterTaskHandle;
 TaskHandle_t readGPSTaskHandle;
 TaskHandle_t clearTelemetryQueueTaskHandle;
 TaskHandle_t checkFlightStateTaskHandle;
 TaskHandle_t flightStateCallbackTaskHandle;
 TaskHandle_t MQTT_TransmitTelemetryTaskHandle;
 TaskHandle_t XBee_TransmitTelemetryTaskHandle;
 TaskHandle_t kalmanFilterTaskHandle;
 TaskHandle_t dmpFIFOPollingTaskHandle;
 
 TaskHandle_t debugToTerminalTaskHandle;
 TaskHandle_t logToMemoryTaskHandle;
 TaskHandle_t opModeIndicateTaskHandle;
 TaskHandle_t espnowCommandTaskHandle;

/**
 * ///////////////////////// DATA TYPES /////////////////////////
*/
accel_type_t acc_data;
gyro_type_t gyro_data;
gps_type_t gps_data;
altimeter_type_t altimeter_data;
telemetry_type_t telemetry_packet;

/**
 * ///////////////////////// END OF DATA VARIABLES /////////////////////////
*/

//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////// PERIPHERALS INIT                              /////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

/**
 * create an MPU6050 object
 * 0x68 is the address of the MPU
 * set gyro to max deg to 1000 deg/sec
 * set accel fs reading to 16g
*/
MPU6050 imu(MPU_ADDRESS, MPU_ACCEL_RANGE, GYRO_RANGE); 

/* create BMP object */
SFE_BMP180 altimeter;
double altimeter_temperature = 0.0;
altimeter_type_t altimeter_packet;

/**
* @brief initialize Buzzer
*/
void buzzerInit() {
    pinMode(BUZZER_PIN, OUTPUT);
}

void LED_init() {
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
}



/**
* @brief initialize SPIFFS for event logging during flight
*/
uint8_t InitSPIFFS() {
    if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
        debugln("SPIFFS mount failed"); // TODO: Set a flag for test GUI
        return 0;
    } else {
        debugln("SPIFFS init success");
        return 1;
    }
}

/**
* @brief initilize SD card 
 */
uint8_t initSD() {
    if (!SD.begin(SD_CS_PIN)) {
        debugln(F("[-]SD Card mounting failed"));
        return 0;
    } else {
        debugln(F("[+]SD card Init OK!"));

        /* check for card type */
        uint8_t cardType = SD.cardType();
        if (cardType == CARD_NONE) {
            debugln("[-]No SD card attached");
        } else {
            debugln("[+]Valid card found");
        }

        // initialize test data file
        File file = SD.open("data.txt", FILE_WRITE); // TODO: change file name to const char*
        if (!file) {
            debugln("[File does not exist. Creating file]");
            debugln("Test data file created");
        } else {
            debugln("[*]Data file already exists");
        }
        file.close();

        // initialize test state file 
        File state_file = SD.open("/state.txt", FILE_WRITE);
        if(!state_file) {
            debugln("State file does not exit. Creating file...");

            debugln("state file created."); // TODO: move to system logger
        }

        state_file.close();

        return 1;
    }
}

/*!****************************************************************************
 * @brief Initialize BMP180 barometric sensor
 * @return TODO: 1 if init OK, 0 otherwise
 * 
 *******************************************************************************/
uint8_t BMPInit() {
    if(altimeter.begin()) {
        debugln("[+]BMP init OK.");
        return 1;
    } else {
        debugln("[+]BMP init failed");
        return 0;
    }
}

/*!****************************************************************************
 * @brief Initialize the GPS connected on Serial2
 * @return 1 if init OK, 0 otherwise
 * 
 *******************************************************************************/
uint8_t GPSInit() {
    gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX, GPS_TX);

    debugln("[+]GPS init OK!"); 

    /**
     * FIXME: Proper GPS init check!
     * Look into if the GPS has acquired a LOCK on satelites 
     * Only if it has a lock then can we return a 1
     * */ 

    return 1;
}

/**
* @brief - non-blocking buzz 
 */
void non_blocking_buzz(uint16_t interval) {
        /* non-blocking buzz */
    current_non_block_time = millis();
    if((current_non_block_time - last_non_block_time) > interval) {
        buzz_state = !buzz_state;
        last_non_block_time = current_non_block_time;
        digitalWrite(BUZZER_PIN, buzz_state);
    }
}

/**
 * @brief blocking buzz 
 */
void blocking_buzz(uint16_t interval) {
    (void)interval;
    requestBuzzerPattern(BUZZER_PATTERN_SHORT_ACK);
}

/**
 * ///////////////////////// END OF PERIPHERALS INIT /////////////////////////
 */
QueueHandle_t telemetry_data_queue_handle;
QueueHandle_t log_to_mem_queue_handle;
QueueHandle_t check_state_queue_handle;
QueueHandle_t debug_to_term_queue_handle;
QueueHandle_t kalman_filter_queue_handle;
QueueHandle_t kalman2d_input_queue_handle;



//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////// ACCELERATION AND ROCKET ATTITUDE DETERMINATION /////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

// /*!****************************************************************************
//  * @brief DMP FIFO Polling Task - Reads from MPU6050 FIFO buffer for quaternion-based angles
//  * This task polls the DMP FIFO buffer to get stable yaw/pitch/roll estimates
//  * that minimize drift compared to direct accelerometer angle calculations
//  * @param pvParameters - Task parameters (unused)
//  * @return Continuously updates imu.dmp_data with latest quaternion-derived angles
//  * 
//  *******************************************************************************/
// void dmpFIFOPollingTask(void* pvParameter) {
//     MPU6050::DMPPacket dmp_packet;
//     uint32_t last_dmp_debug_ms = 0;
    
//     while(1) {
//         // Poll FIFO buffer for latest DMP packet
//         if (imu.pollFIFO(dmp_packet)) {
//             // DMP data successfully read and decoded
//             // dmp_packet now contains:
//             //   - yaw, pitch, roll (degrees, quaternion-derived)
//             //   - gx, gy, gz (deg/s, gyro rates)
            
//             // Store in imu object for use by readAccelerationTask
//             imu.dmp_data = dmp_packet;
            
//             // Keep DMP debug lightweight to avoid serial backpressure on high-priority task.
//             if (millis() - last_dmp_debug_ms > 2000) {
//                 debug("[DMP] Pitch=");
//                 debug(dmp_packet.pitch);
//                 debug("° Roll=");
//                 debug(dmp_packet.roll);
//                 debug("° Yaw=");
//                 debug(dmp_packet.yaw);
//                 debug("° GyrX=");
//                 debug(dmp_packet.gx);
//                 debugln("°/s");
//                 last_dmp_debug_ms = millis();
//             }
//         }
        
//         // Keep polling near DMP production rate and yield bus time to other tasks.
//         vTaskDelay(pdMS_TO_TICKS(20));
//     }
// }

//////////////////////////////////////////////////////////////////////////////////////////////

#if USE_SIMULATION
TaskHandle_t simulationTaskHandle = NULL;

void simulation_task(void *pvParameters) 
{
	telemetry_type_t telemetry;
	uint32_t start = millis();

	while (sim_index < sim_count) {
		float elapsed = (millis() - start) / 1000.0f;

		if (elapsed >= sim_data[sim_index].time_s) {
			telemetry.alt_data.rel_altitude = sim_data[sim_index].altitude_m;
			telemetry.alt_data.kalman_vertical_velocity = sim_data[sim_index].velocity_mps;
			xQueueSend(check_state_queue_handle, &telemetry, 0);
			sim_index++;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	debug("Simulation complete!");
	vTaskDelete(NULL);
}
#endif
/*!****************************************************************************
 * @brief Read acceleration data from the accelerometer
 * @param pvParameters - A value that is passed as the paramater to the created task.
 * If pvParameters is set to the address of a variable then the variable must still exist when the created task executes - 
 * so it is not valid to pass the address of a stack variable.
 * @return Updates accelerometer data struct on the telemetry queue
 * 
 *******************************************************************************/
void readAccelerationTask(void* pvParameter) {
    telemetry_type_t acc_data_lcl;
    static uint32_t record_counter = 0; // Reset to 0 on each reboot

    while(1) {
        acc_data_lcl.operation_mode = operation_mode;
        acc_data_lcl.record_number = ++record_counter; // Use pre-increment for proper counting
        acc_data_lcl.state = current_state;
        acc_data_lcl.alt_data.rel_altitude = altimeter_packet.rel_altitude;
        acc_data_lcl.drogue_pin_state = drogue_pin_state;
        acc_data_lcl.drogue_pin_engaged = drogue_pin_engaged;
        acc_data_lcl.main_chute_pin_state = main_chute_pin_state;
        acc_data_lcl.main_chute_pin_engaged = main_chute_pin_engaged;
        
        // Use the global battery voltage read by batteryMonitorTask
        acc_data_lcl.battery_voltage = battery_voltage;
        acc_data_lcl.logic_rail_3v3_voltage = logic_rail_3v3_voltage;
        acc_data_lcl.power_rail_low = g_power_rail_low;
        
        // Use the global wifi_rssi read by monitorChutePinsTask
        // acc_data_lcl.wifi_rssi = wifi_rssi;

        // // 🔥 DMP MODE: Use quaternion-derived angles and FIFO gyro to minimize drift
        // if (imu.isDMPReady() && imu.hasFreshDMPPacket(200)) {
        //     // Use DMP-derived pitch/roll/gyro from FIFO buffer (more stable, less drift)
        //     acc_data_lcl.acc_data.pitch = imu.dmp_data.pitch;
        //     acc_data_lcl.acc_data.roll = imu.dmp_data.roll;
            
        //     acc_data_lcl.gyro_data.gx = imu.dmp_data.gx;
        //     acc_data_lcl.gyro_data.gy = imu.dmp_data.gy;
        //     acc_data_lcl.gyro_data.gz = imu.dmp_data.gz;

        //     // Use cached accel values updated by the FIFO polling task to avoid
        //     // extra MPU I2C reads from this task.
        //     acc_data_lcl.acc_data.ax = imu.acc_x_real;
        //     acc_data_lcl.acc_data.ay = imu.acc_y_real;
        //     acc_data_lcl.acc_data.az = imu.acc_z_real;
        // } else {
        //     // FALLBACK: Legacy mode - direct angle calculations (atan2/asin)
        //     // read acceleration
            acc_data_lcl.acc_data.ax = imu.readXAcceleration();
            acc_data_lcl.acc_data.ay = imu.readYAcceleration();
            acc_data_lcl.acc_data.az = imu.readZAcceleration();

            // read angular velocities
            acc_data_lcl.gyro_data.gx = imu.readXAngularVelocity();
            acc_data_lcl.gyro_data.gy = imu.readYAngularVelocity();
            acc_data_lcl.gyro_data.gz = imu.readZAngularVelocity();

            // get pitch and roll from accelerometer
            acc_data_lcl.acc_data.pitch = imu.getPitch();
            acc_data_lcl.acc_data.roll = imu.getRoll();
        // END of legacy IMU read block

        // 🔥 SYNCHRONIZED KALMAN DATA - Include latest Kalman filter results in all telemetry packets
        acc_data_lcl.alt_data = altimeter_packet; // Copy entire altimeter data including Kalman results
        
        // 🛡️ UPDATE GLOBAL TELEMETRY - For ARM altitude safety checks
        g_current_telemetry = acc_data_lcl;
        g_last_telemetry_update = millis();
        
        // Send to queues for other tasks
        if (check_state_queue_handle != NULL) xQueueSend(check_state_queue_handle, &acc_data_lcl, pdMS_TO_TICKS(10));

        // Provide latest-only accel+telemetry to Kalman task (overwrite queue of length 1)
        if (kalman_filter_queue_handle != NULL) {
            xQueueOverwrite(kalman_filter_queue_handle, &acc_data_lcl);
        }

        // Task delay -> align with IMU sampling (20 ms)
        vTaskDelay(pdMS_TO_TICKS(20));
    } // end while(1)
} // end readAccelerationTask



//////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////// ALTITUDE AND VELOCITY DETERMINATION ////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////

/*!****************************************************************************
 * @brief Read the raw pressure from the altimeter
 *******************************************************************************/
double altimeter_get_pressure()
{
    char status;
    double T, P, p0, a;
    status = altimeter.startTemperature();
    if(status != 0)
    {
        vTaskDelay(pdMS_TO_TICKS(status)); // Non-blocking delay for FreeRTOS tasks
        status = altimeter.getTemperature(T);
        altimeter_temperature = T;
        if(status != 0)
        {
            status = altimeter.startPressure(3);
            if(status != 0)
            {
                vTaskDelay(pdMS_TO_TICKS(status)); // Non-blocking delay for FreeRTOS tasks
                status = altimeter.getPressure(P, T);
                if(status != 0)
                {
                    return P;
                } else debugln("Error getting pressure");
            } else debugln("error starting pressure");
        } else debugln("error getting temperature");
    } else debugln("error starting pressure measurement");
 return P;  
}

// /*!****************************************************************************
//  * @brief Read atm pressure data from the barometric sensor onboard
//  *******************************************************************************/


float estimatedAltitude = 0;

#if USE_SIMULATION
// Simulated 120m flight profile (as previously used)
void readAltimeterTask(void* pvParameters) {
    static double a = 0; // altitude
    static double P = 101325;
    static int phase = 0; // 0 ascent, 1 apogee hold, 2 descent
    static int count = 0;
    while(1) {
        if (phase == 0) {
            // Ascent: 3600m in 15s, so 240m/s, update every 100ms (0.1s)
            a += 24.0; // 24m per 0.1s = 240m/s
            if(++count >= 150) { // 15s ascent
                phase = 1;
                count = 0;
            }
        }
        else if (phase == 1) {
            // Hold at apogee for 0.5s
            if(++count >= 5) { // 0.5s hold
                phase = 2;
                count = 0;
            }
        }
        else {
            // Descent: 3600m to 0m in 14.5s, so ~248m/s
            a -= 24.8; // 24.8m per 0.1s = 248m/s
            if (a <= 0) a = 0;
        }
        P = 101325 * exp(-a / 8434.5);
        estimatedAltitude = a;
        float filtered_alt = kalmanFilter(a);
        altimeter_packet.filtered_altitude_1d = filtered_alt;
    // FIX: latest-only altitude input for Kalman -> overwrite (queue length = 1)
    if (kalman2d_input_queue_handle != NULL) xQueueOverwrite(kalman2d_input_queue_handle, &filtered_alt); // FIX
        altimeter_packet.temperature = 25.0;
        altimeter_packet.pressure = P;
        altimeter_packet.rel_altitude = a;
        altimeter_packet.kalman_altitude = AltitudeKalman;
        altimeter_packet.kalman_vertical_velocity = VelocityVerticalKalman;
        vTaskDelay(pdMS_TO_TICKS(100)); // 0.1s per loop - FIX: use pdMS_TO_TICKS
    }
}
#else
// Real altimeter task (BMP sensor)
void readAltimeterTask(void* pvParameters) {
    telemetry_type_t alt_data_lcl;
    while(1) {
        double P = altimeter_get_pressure();
        double a = altimeter.altitude(P, baseline);
        estimatedAltitude = a;
        float filtered_alt = kalmanFilter(a);
        altimeter_packet.filtered_altitude_1d = filtered_alt;
    // FIX: latest-only altitude input for Kalman -> overwrite (queue length = 1)
    if (kalman2d_input_queue_handle != NULL) xQueueOverwrite(kalman2d_input_queue_handle, &filtered_alt); // FIX
        altimeter_packet.temperature = altimeter_temperature;
        altimeter_packet.pressure = P;
        altimeter_packet.rel_altitude = a;
        altimeter_packet.kalman_altitude = AltitudeKalman;
        altimeter_packet.kalman_vertical_velocity = VelocityVerticalKalman;
        vTaskDelay(pdMS_TO_TICKS(50)); // FIX: use pdMS_TO_TICKS
    }
}
#endif


// /*!****************************************************************************
//  * @brief Read the GPS location data and altitude and append to telemetry packet for transmission
//  * @param pvParameters - A value that is passed as the paramater to the created task.
//  * If pvParameters is set to the address of a variable then the variable must still exist when the created task executes - 
//  * so it is not valid to pass the address of a stack variable.
//  * 
//  *******************************************************************************/
#if USE_SIMULATION
void readGPSTask(void* pvParameters){
    static float sim_gps_altitude = 0; static int phase=0; static int count=0;
    while(1){
        if(phase==0){sim_gps_altitude+=2.0; if(++count>=60){phase=1;count=0;}}
        else if(phase==1){ if(++count>=10){phase=2;count=0;} }
        else if(phase==2){ sim_gps_altitude-=1.0; if(sim_gps_altitude<0){sim_gps_altitude=0; phase=0; count=0; vTaskDelay(3000/portTICK_PERIOD_MS);} }
        gps_packet.latitude = -1.2833;
        gps_packet.longitude = 36.8167;
        gps_packet.gps_altitude = sim_gps_altitude;
        gps_packet.time = millis();
        vTaskDelay(100/portTICK_PERIOD_MS);
    }
}
#else
void readGPSTask(void* pvParameters){
    float latitude = 0, longitude = 0, g_altitude = 0;
    while(1){
        while(gpsSerial.available()>0){ gps.encode(gpsSerial.read()); }
        if(gps.location.isValid()){ latitude = gps.location.lat(); longitude = gps.location.lng(); }
        if(gps.altitude.isValid()){ g_altitude = gps.altitude.meters(); }
        gps_packet.latitude = latitude;
        gps_packet.longitude = longitude;
        gps_packet.gps_altitude = g_altitude;
        gps_packet.time = millis();
        vTaskDelay(200/portTICK_PERIOD_MS);
    }
}
#endif

/**
 * @brief Kalman filter estimated value calculation
 * 
 */
// float kalmanFilter(float z) {
//     float estimated_altitude_pred = estimated_altitude;
//     float error_covariance_pred = error_covariance_bmp + process_variance_bmp;
//     kalman_gain_bmp = error_covariance_pred / (error_covariance_pred + measurement_variance_bmp);
//     estimated_altitude = estimated_altitude_pred + kalman_gain_bmp * (z - estimated_altitude_pred);
//     error_covariance_bmp = (1 - kalman_gain_bmp) * error_covariance_pred;

//     return estimated_altitude;
// }
BLA::Matrix<2,2> F, P, Q, I;
BLA::Matrix<2,1> G, S, K;
BLA::Matrix<1,2> H;
BLA::Matrix<1,1> R, L, inv_L, Acc, M;

// 3-state Kalman (altitude, vertical velocity, vertical acceleration) for robust fusion.
BLA::Matrix<3,3> KF3_A, KF3_P, KF3_Q, KF3_I;
BLA::Matrix<2,3> KF3_H;
BLA::Matrix<2,2> KF3_R;
BLA::Matrix<3,1> KF3_X;

extern float estimatedAltitude;
float timeStep = 0.003; // 3ms time step for 2D Kalman filter (matches task delay)
const int ledPin= 25;
unsigned long timer = 0;

float errorCovariance_bmp = 1.0;
float processVariance_bmp = 0.001;
float measurementVariance_bmp = 0.1;
float kalmanGain_bmp;

// ============================================================================
// 🔧 ZERO VELOCITY UPDATE (ZUPT) - Velocity Drift Correction
// ============================================================================
// Stationary detection parameters
#define ZUPT_ACC_THRESHOLD_M_S2    0.3f    // Accel threshold for stationary (m/s²)
#define ZUPT_ALT_CHANGE_THRESHOLD  0.05f   // Max altitude change (m) over detection window
#define ZUPT_TIME_WINDOW_MS        1500    // Time window for stationary confirmation (ms)
#define ZUPT_VELOCITY_VARIANCE     0.01f   // Process noise for velocity measurement update

// ZUPT state tracking (static to persist across task invocations)
static struct {
    bool is_stationary;                    // Current stationary state
    unsigned long stationary_start_ms;     // When stationary motion began
    float altitude_at_window_start;        // Reference altitude for change detection
    unsigned long altitude_sample_time_ms; // Timestamp of altitude reference
    uint8_t consecutive_frames;            // Frames meeting stationary criteria
} zupt_state = {false, 0, 0.0f, 0, 0};

// ============================================================================
// ZUPT Helper: Detect stationary condition
// Returns true if rocket appears stationary based on multi-criteria check
// ============================================================================
bool isStationaryCondition(float abs_acc, float current_alt, float recent_alt_change) {
    // Criteria 1: Acceleration must be low (< threshold)
    if (abs_acc > ZUPT_ACC_THRESHOLD_M_S2) {
        zupt_state.consecutive_frames = 0;
        return false;
    }
    
    // Criteria 2: Altitude must be stable (change < threshold)
    if (fabs(recent_alt_change) > ZUPT_ALT_CHANGE_THRESHOLD) {
        zupt_state.consecutive_frames = 0;
        return false;
    }
    
    // Criteria 3: Time confirmation - must be stationary for confirmation window
    zupt_state.consecutive_frames++;
    unsigned long now = millis();
    
    if (!zupt_state.is_stationary) {
        // Starting stationary detection
        if (zupt_state.consecutive_frames == 1) {
            zupt_state.stationary_start_ms = now;
            zupt_state.altitude_at_window_start = current_alt;
            zupt_state.altitude_sample_time_ms = now;
        }
        
        // Check if window expired without transition to stationary
        if ((now - zupt_state.stationary_start_ms) > ZUPT_TIME_WINDOW_MS) {
            return true;  // Confirmed stationary
        }
        return false;
    }
    
    return true;  // Already confirmed stationary
}

// ============================================================================
// ZUPT Helper: Perform velocity-only measurement update
// Updates S(1,0) (velocity) using Kalman equations with v_measurement = 0
// ============================================================================
void applyVelocityMeasurementUpdate(BLA::Matrix<1,1>& R_vel) {
    // Measurement model for velocity only: H_v = [0 1]
    // Measurement: z_v = 0 (velocity should be zero when stationary)
    
    BLA::Matrix<1,2> H_v = {0, 1};  // Extract velocity from state
    BLA::Matrix<1,1> z_v = {0.0};   // Measurement: zero velocity
    BLA::Matrix<1,1> y_v;           // Innovation (measurement residual)
    BLA::Matrix<1,1> S_v;           // Innovation covariance
    BLA::Matrix<2,1> K_v;           // Kalman gain (2x1)
    BLA::Matrix<1,1> S_v_inv;       // Inverse of innovation covariance
    
    // Innovation: y = z - H*x
    y_v = z_v - H_v * S;
    
    // Innovation covariance: S = H*P*H^T + R
    S_v = H_v * P * ~H_v + R_vel;
    
    // Check for singularity
    if (fabs(S_v(0, 0)) < 1e-8) {
        // Skip this update if covariance is too small
        return;
    }
    
    // Kalman gain: K = P*H^T / S
    S_v_inv = {1.0f / S_v(0, 0)};
    K_v = P * ~H_v * S_v_inv;
    
    // State update: x = x + K*y
    S = S + K_v * y_v;
    
    // Covariance update: P = (I - K*H)*P
    P = (I - K_v * H_v) * P;
    
    // Extract updated velocity
    VelocityVerticalKalman = S(1, 0);
}

// ============================================================================
// End ZUPT Definitions
// ============================================================================


  void init_kalman_matrices() {
  F = {1, 0.0034, 0, 1};
  G = {0.5 * 0.003 * 0.003, 0.003};
  H = {1, 0};
  I = {1, 0, 0, 1};
  Q = G * ~G * 4.0f * 4.0f;
  R = {0.3 * 0.3};
  P = {0, 0, 0, 0};
  S = {0, 0};

  // 3-state model based on altitude + acceleration measurement.
  float dt = timeStep;
  KF3_A = {1.0f, dt, 0.5f * dt * dt,
      0.0f, 1.0f, dt,
      0.0f, 0.0f, 1.0f};
  KF3_H = {1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f};
  KF3_P = {1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f};
  KF3_R = {0.25f, 0.0f,
      0.0f, 0.75f};
  const float q3 = 0.0001f;
  KF3_Q = {q3, q3, q3,
      q3, q3, q3,
      q3, q3, q3};
  KF3_I = {1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f};
  KF3_X = {0.0f, 0.0f, 0.0f};
  }
// Apply Kalman filter to new altitude measurements
float kalmanFilter(float z) {
  float estimatedAltitude_pred = estimatedAltitude;
  float errorCovariance_pred = errorCovariance_bmp + processVariance_bmp;
  kalmanGain_bmp = errorCovariance_pred / (errorCovariance_pred + measurementVariance_bmp);
  estimatedAltitude = estimatedAltitude_pred + kalmanGain_bmp * (z - estimatedAltitude_pred);
  errorCovariance_bmp = (1 - kalmanGain_bmp) * errorCovariance_pred;

  return estimatedAltitude;
}
// void taskKalman2D(void *pvParameters) {
//     Serial.println("📡 Kalman2D Task Started");
//     telemetry_type_t input_data;
//     telemetry_type_t telemetry_data;
//     telemetry_type_t acc_data_lcl;
//     while (true) {
//         //Serial.println("📡 Kalman2D Task Started in loop");
     

//     timer = millis();

//     // Read raw acceleration and gyroscope data
    


//     // Calculate tilt-adjusted AccZInertial
//     // AngleRoll = atan2(a.acceleration.y, sqrt(a.acceleration.x * a.acceleration.x + (a.acceleration.z+1.0) * a.acceleration.z)) * 180 / PI;
//     // AnglePitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + (a.acceleration.z+1.0)* a.acceleration.z)) * 180 / PI;
//     // //Gravitational acceleration is in the downward direction and is taken as positive ,since we are moving upwards, we need to invert the sign so that g is negative
//     // AccZInertial = -((-sin(AnglePitch * (PI / 180)) * a.acceleration.x +
//     //         cos(AnglePitch * (PI / 180)) * sin(AngleRoll * (PI / 180)) * a.acceleration.y +
//     //         cos(AnglePitch * (PI / 180)) * cos(AngleRoll * (PI / 180)) * a.acceleration.z) - 8.93);//should be -9.81 , taken as 9 to account for error
//     // AccZInertial_g = AccZInertial / 9.81;
//     float AccYInertial = acc_data_lcl.acc_data.az-1.03;
//     float AccYInertial_g = AccYInertial / 9.81;
//     // Apply 2D Kalman filter
//     Acc = {AccYInertial};
//     //Acc= 0 ;
//     S = F * S + G * Acc;
//     P = F * P * ~F + Q;
//     L = H * P * ~H + R;
//     inv_L = Inverse(L);
//     K = P * ~H * inv_L;
//     float input_alt;
//     if (xQueueReceive(kalman2d_input_queue_handle, &input_alt, 0) == pdTRUE) {
//     M = {input_alt};  
//     }

    
//     S = S + K * (M - H * S);
//     AltitudeKalman = S(0, 0);
//     VelocityVerticalKalman = S(1, 0);

//     altimeter_packet.kalman_altitude = AltitudeKalman;
//     altimeter_packet.kalman_vertical_velocity = VelocityVerticalKalman;
//     P = (I - K * H) * P;
   
//     telemetry_data.alt_data.kalman_altitude = altimeter_packet.kalman_altitude;
//     telemetry_data.alt_data.kalman_vertical_velocity = altimeter_packet.kalman_vertical_velocity;

//      // After updating altimeter_packet.kalman_altitude and altimeter_packet.kalman_vertical_velocity

//    telemetry_type_t telemetry_data;
//    telemetry_data.alt_data = altimeter_packet; // Copy all altimeter data, including filtered values
// // Fill other fields as needed (accel, gyro, gps, etc.)

//    xQueueSend(debug_to_term_queue_handle, &telemetry_data, 0); // Or your actual output queue


    
//     // Serial output
//     digitalWrite(ledPin,HIGH);
//     //Serial.println("📡 Kalman2D Task Running");

//     // Serial.print(" Raw acceleration:");Serial.print(az);Serial.print("\n");
//     // Serial.print("accleration in z direction:");Serial.print(AccZInertial);Serial.print("\n");
//     // Serial.print("Raw Altitude:");Serial.print(estimatedAltitude);Serial.print("\n");
//     //Serial.print("Filtered Altitude:"); Serial.print(AltitudeKalman); Serial.print("\n");
//     // Serial.print("VerticalVelocity:"); Serial.print(VelocityVerticalKalman); Serial.print("\n");
//     //Serial.print("AccZInertial (m/s²):"); Serial.print(AccYInertial); Serial.print("\n");
//     //Serial.print("AccZInertial (g):"); Serial.print(AccYInertial_g); Serial.print("\n");
//      vTaskDelay((timeStep * 1000) / portTICK_PERIOD_MS);
//     }
   
//     }




void taskKalman2D(void *pvParameters) {
    float input_altitude;
    float previous_altitude = 0.0f;
    telemetry_type_t acc_data_lcl;
    static unsigned long last_altitude_update_ms = 0;
    static bool accel_bias_ready = false;
    static float accel_bias_sum_g = 0.0f;
    static uint16_t accel_bias_samples = 0;
    static unsigned long accel_bias_start_ms = 0;
    static float accel_z_bias_g = 0.0f;
    static unsigned long last_kalman_update_ms = 0;  // For real-dt measurement
    
    while (true) {
        // Wait for filtered altitude data from the readAltimeterTask
        if (xQueueReceive(kalman2d_input_queue_handle, &input_altitude, portMAX_DELAY) == pdTRUE) {

            // ── Measure real dt since last Kalman update ──────────────────────
            unsigned long now_ms = millis();
            float real_dt = (last_kalman_update_ms == 0) ? 0.05f :
                            constrain((float)(now_ms - last_kalman_update_ms) / 1000.0f, 0.005f, 0.5f);
            last_kalman_update_ms = now_ms;

            // Rebuild F and G with the actual measured timestep each cycle so the
            // prediction model stays accurate regardless of altimeter task rate.
            F = {1.0f, real_dt,
                 0.0f, 1.0f};
            G = {0.5f * real_dt * real_dt,
                 real_dt};
            // Q stays proportional to G*G^T with the same process noise level
            Q = G * ~G * 4.0f * 4.0f;

            // Also rebuild 3-state A matrix with real dt
            KF3_A = {1.0f, real_dt, 0.5f * real_dt * real_dt,
                     0.0f, 1.0f,    real_dt,
                     0.0f, 0.0f,    1.0f};

            // ── Get acceleration data ─────────────────────────────────────────
            bool imu_data_valid = false;
            if (kalman_filter_queue_handle != NULL &&
                xQueuePeek(kalman_filter_queue_handle, &acc_data_lcl, 0) == pdTRUE) {

                // Preflight accel bias calibration to remove static gravity offset.
                if (!accel_bias_ready && current_state == ARMED_FLIGHT_STATE::PRE_FLIGHT_GROUND) {
                    if (accel_bias_start_ms == 0) accel_bias_start_ms = millis();
                    accel_bias_sum_g += acc_data_lcl.acc_data.az;
                    accel_bias_samples++;
                    if ((millis() - accel_bias_start_ms) >= 2000 && accel_bias_samples >= 20) {
                        accel_z_bias_g = accel_bias_sum_g / accel_bias_samples;
                        accel_bias_ready = true;
                        Serial.printf("[KALMAN] Accel bias calibrated: az_bias=%.4f g\n", accel_z_bias_g);
                    }
                }

                // Check if IMU data looks valid (not all-zero from bus contention)
                float az = acc_data_lcl.acc_data.az;
                float ax = acc_data_lcl.acc_data.ax;
                float ay = acc_data_lcl.acc_data.ay;
                // A completely stationary IMU on a bench will read ~1g on one axis.
                // If all axes are exactly 0.0 the I2C read failed.
                imu_data_valid = (fabsf(az) > 0.01f || fabsf(ax) > 0.01f || fabsf(ay) > 0.01f);

                if (imu_data_valid) {
                    float AccYInertial = 0.0f;
                    if (accel_bias_ready) {
                        AccYInertial = (az - accel_z_bias_g) * 9.80665f;
                    } else {
                        AccYInertial = (az * 9.8f) - 9.425f;
                    }

                    // Suppress tiny bias drift when stationary on the bench
                    bool likely_stationary = (!is_system_armed) && (fabsf(input_altitude) < 1.0f);
                    if (likely_stationary && fabsf(AccYInertial) < 0.8f) AccYInertial = 0.0f;

                    Acc = {AccYInertial};
                } else {
                    // IMU bus read failed — use zero acceleration to avoid divergence
                    Acc = {0.0f};
                }
            } else {
                Acc = {0.0f};
            }

            // ── 2-STATE KALMAN PREDICTION ────────────────────────────────────
            S = F * S + G * Acc;
            P = F * P * ~F + Q;

            // ── 2-STATE KALMAN UPDATE (altitude measurement) ─────────────────
            L = H * P * ~H + R;
            if (fabs(L(0, 0)) < 1e-6 || isnan(L(0,0))) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            inv_L = Inverse(L);
            K = P * ~H * inv_L;
            M = {input_altitude};
            S = S + K * (M - H * S);
            P = (I - K * H) * P;

            // ── 3-STATE KALMAN (altitude + acceleration fusion) ──────────────
            // Only run 3-state when IMU data is valid — avoids divergence from zero readings
            if (imu_data_valid) {
                BLA::Matrix<2,1> Z3 = {input_altitude, (float)Acc(0, 0)};
                BLA::Matrix<3,1> X3_minus = KF3_A * KF3_X;
                BLA::Matrix<3,3> P3_minus = KF3_A * KF3_P * (~KF3_A) + KF3_Q;
                BLA::Matrix<2,2> S3_cov = KF3_H * P3_minus * (~KF3_H) + KF3_R;

                if (fabs(S3_cov(0, 0)) > 1e-6f && fabs(S3_cov(1, 1)) > 1e-6f) {
                    BLA::Matrix<3,2> K3 = P3_minus * (~KF3_H) * Inverse(S3_cov);
                    KF3_X = X3_minus + K3 * (Z3 - KF3_H * X3_minus);
                    KF3_P = (KF3_I - K3 * KF3_H) * P3_minus;
                } else {
                    KF3_X = X3_minus;
                    KF3_P = P3_minus;
                }
                // Fuse 2-state and 3-state estimates
                AltitudeKalman         = (0.60f * S(0, 0)) + (0.40f * KF3_X(0, 0));
                VelocityVerticalKalman = (0.60f * S(1, 0)) + (0.40f * KF3_X(1, 0));
            } else {
                // IMU invalid — rely entirely on 2-state barometric filter
                AltitudeKalman         = S(0, 0);
                VelocityVerticalKalman = S(1, 0);
            }
            
            // ============================================================================
            // 🔧 ZUPT APPLICATION - Velocity drift correction when stationary
            // ============================================================================
            // Check if we should apply ZUPT (only during pre-flight and post-flight ground states)
            bool apply_zupt = (current_state == ARMED_FLIGHT_STATE::PRE_FLIGHT_GROUND ||
                               current_state == ARMED_FLIGHT_STATE::POST_FLIGHT_GROUND);
            
            if (apply_zupt) {
                // Calculate altitude change since last sample
                unsigned long now_ms = millis();
                unsigned long time_delta_ms = now_ms - last_altitude_update_ms;
                float altitude_change = fabs(input_altitude - previous_altitude);
                
                // Get absolute acceleration magnitude
                float abs_acc = fabs(Acc(0, 0));
                
                // Check stationary condition
                if (isStationaryCondition(abs_acc, input_altitude, altitude_change)) {
                    // Stationary confirmed - apply velocity-only ZUPT update
                    if (!zupt_state.is_stationary) {
                        zupt_state.is_stationary = true;
                        debugln("🔧 ZUPT ACTIVE: Stationary detected");
                    }
                    
                    // Perform velocity measurement update with tighter variance
                    BLA::Matrix<1,1> R_vel = {ZUPT_VELOCITY_VARIANCE};
                    applyVelocityMeasurementUpdate(R_vel);
                    
                    // Optional: Debug output (comment out for production)
                    // Serial.printf("  V(zupt): %.4f | P[1,1]: %.6f\n", VelocityVerticalKalman, P(1, 1));
                } else {
                    // Motion detected - disable ZUPT
                    if (zupt_state.is_stationary) {
                        zupt_state.is_stationary = false;
                        debugln("🔧 ZUPT INACTIVE: Motion detected");
                    }
                }
                
                // Update altitude tracking
                previous_altitude = input_altitude;
                last_altitude_update_ms = now_ms;
            } else {
                // Not in ground state - ensure ZUPT is off
                if (zupt_state.is_stationary) {
                    zupt_state.is_stationary = false;
                }
                // Reset altitude tracking when transitioning away from ground
                previous_altitude = input_altitude;
                last_altitude_update_ms = millis();
            }
            // ============================================================================
            // End ZUPT Application
            // ============================================================================
            
            // 🔥 SYNCHRONIZED KALMAN OUTPUT - Update global altimeter packet for consistent data across all tasks
            altimeter_packet.kalman_altitude = AltitudeKalman;
            altimeter_packet.kalman_vertical_velocity = VelocityVerticalKalman;

            // Build a single telemetry packet per Kalman update and send to downstream queues
            telemetry_type_t telemetry_out;
            telemetry_out.alt_data = altimeter_packet; // includes filtered values
            telemetry_out.acc_data = acc_data_lcl.acc_data;
            telemetry_out.gyro_data = acc_data_lcl.gyro_data;
            telemetry_out.gps_data = gps_packet;
            telemetry_out.operation_mode = operation_mode;
            telemetry_out.state = current_state;
            telemetry_out.drogue_pin_state = drogue_pin_state;
            telemetry_out.drogue_pin_engaged = drogue_pin_engaged;
            telemetry_out.main_chute_pin_state = main_chute_pin_state;
            telemetry_out.main_chute_pin_engaged = main_chute_pin_engaged;
            telemetry_out.battery_voltage = battery_voltage;
            telemetry_out.logic_rail_3v3_voltage = logic_rail_3v3_voltage;
            telemetry_out.power_rail_low = g_power_rail_low;
            telemetry_out.wifi_rssi = wifi_rssi;
            static uint32_t kalman_record_counter = 0;
            telemetry_out.record_number = ++kalman_record_counter;

            // Send the telemetry exactly once per Kalman update to consumers
            if (debug_to_term_queue_handle != NULL) xQueueSend(debug_to_term_queue_handle, &telemetry_out, pdMS_TO_TICKS(10)); // FIX
            if (log_to_mem_queue_handle != NULL) xQueueSend(log_to_mem_queue_handle, &telemetry_out, 0); // FIX: non-blocking enqueue to unified log queue
            if (telemetry_data_queue_handle != NULL) xQueueSend(telemetry_data_queue_handle, &telemetry_out, pdMS_TO_TICKS(10)); // FIX

        }

        vTaskDelay(pdMS_TO_TICKS((uint32_t)(timeStep * 1000))); // FIX: use pdMS_TO_TICKS
    }
}


/*!***************************************************************************
 * @brief Filter data using the Kalman Filter 
 * 
 */
void kalmanFilterTask(void* pvParameters) {
    
    while (1) {
        vTaskDelay(CONSUME_TASK_DELAY/portTICK_PERIOD_MS);
    }
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  Serial.print("🔥 Stack overflow in task: ");
  Serial.println(pcTaskName);
}

// /*!****************************************************************************
//  * @brief check various condition from flight data to change the flight state
//  * - -see states.h for more info --
//  *
//  *******************************************************************************/
// void checkFlightState(void* pvParameters) {
//     // get the flight state from the telemetry task
//     telemetry_type_t flight_data; 
    
//     while (1) {
//         xQueueReceive(check_state_queue_handle, &flight_data, portMAX_DELAY);

//         if(apogee_flag != 1) {
//             // states before apogee
//             // debug("altitude value:"); debugln(flight_data.alt_data.altitude);
//             if(flight_data.alt_data.rel_altitude < LAUNCH_DETECTION_THRESHOLD) {
//                 current_state = ARMED_FLIGHT_STATE::PRE_FLIGHT_GROUND;
//                 //debugln("PREFLIGHT");
//                 delay(STATE_CHANGE_DELAY);
//             } else if(LAUNCH_DETECTION_THRESHOLD < flight_data.alt_data.rel_altitude < (LAUNCH_DETECTION_THRESHOLD+LAUNCH_DETECTION_ALTITUDE_WINDOW) ) {
//                 current_state = ARMED_FLIGHT_STATE::POWERED_FLIGHT;
//                 //debugln("POWERED");
//                 delay(STATE_CHANGE_DELAY);
//             } 

//             // COASTING

//             // APOGEE and APOGEE DETECTION
//             ring_buffer_put(&altitude_ring_buffer, flight_data.alt_data.rel_altitude);
//             if(ring_buffer_full(&altitude_ring_buffer) == 1) {
//                 oldest_val = ring_buffer_get(&altitude_ring_buffer);
//             }

//             //debug("Curr val:");debug(flight_data.alt_data.altitude); debug("    "); debugln(oldest_val);
//             if((oldest_val - flight_data.alt_data.rel_altitude) >= APOGEE_DETECTION_THRESHOLD) {
//                 if(apogee_flag == 0) {
//                     apogee_val = ( (oldest_val - flight_data.alt_data.rel_altitude) / 2 ) + oldest_val;

//                     current_state = ARMED_FLIGHT_STATE::APOGEE;
//                     delay(STATE_CHANGE_DELAY);
//                     //debugln("APOGEE");
//                     delay(STATE_CHANGE_DELAY);
//                     current_state = ARMED_FLIGHT_STATE::DROGUE_DEPLOY;
//                     //debugln("DROGUE");
//                     delay(STATE_CHANGE_DELAY);
//                     current_state =  ARMED_FLIGHT_STATE::DROGUE_DESCENT;
//                     //debugln("DROGUE_DESCENT");
//                     delay(STATE_CHANGE_DELAY);
//                     apogee_flag = 1;
//                 }
//             }

//         } else if(apogee_flag == 1) {
//             if(LAUNCH_DETECTION_THRESHOLD <= flight_data.alt_data.rel_altitude <= apogee_val) {
//                 if(main_eject_flag == 0) {
//                     current_state = ARMED_FLIGHT_STATE::MAIN_DEPLOY;
//                     //debugln("MAIN");
//                     delay(STATE_CHANGE_DELAY);
//                     main_eject_flag = 1;
//                 } else if (main_eject_flag == 1) { // todo: confirm check_done_flag
//                     current_state = ARMED_FLIGHT_STATE::MAIN_DESCENT;
//                     //debugln("MAIN_DESC");
//                     delay(STATE_CHANGE_DELAY);
//                 }
//             }

//             if(flight_data.alt_data.rel_altitude < LAUNCH_DETECTION_THRESHOLD) {
//                 current_state = ARMED_FLIGHT_STATE::POST_FLIGHT_GROUND;
//                 //debugln("POST_FLIGHT");
//             }
//         }

//         flight_data.state = current_state;

//     }
// }

// state machine
void checkFlightState(void* pvParameters) {
    telemetry_type_t flight_data;
    static uint8_t last_state = 0xFF;
    static float max_altitude = 0.0f;
    static uint8_t descent_count = 0;

    while (1) {
        xQueueReceive(check_state_queue_handle, &flight_data, portMAX_DELAY);

        // 🎯 Use Kalman filtered altitude for enhanced accuracy
        #if USE_KALMAN_FOR_STATE_DETECTION
        float alt = flight_data.alt_data.kalman_altitude;
        #else
        float alt = flight_data.alt_data.rel_altitude;
        #endif

        // Update global telemetry for ARM altitude check
        g_current_telemetry = flight_data;
        g_last_telemetry_update = millis();

        // Do not run flight state transitions while disarmed or with invalid baro samples.
        if (!is_system_armed || !g_altimeter_sample_valid) {
            current_state = ARMED_FLIGHT_STATE::PRE_FLIGHT_GROUND;
            apogee_flag = 0;
            apogee_val = 0;
            max_altitude = 0.0f;
            descent_count = 0;
            vTaskDelay(pdMS_TO_TICKS(STATE_CHANGE_DELAY));
            continue;
        }

        // // 🚀 AUTOMATIC ARMING DISABLED - Only manual ARM commands will arm the system
        // if (!is_system_armed && alt >= ARM_ALTITUDE_THRESHOLD) {
        //     arm_pyros();
        //     chutesInit();
        //     if (use_beacon_mode) {
        //         transmitter.setArmed(true);
        //     }
        //     is_system_armed = true;
        //     operation_mode = OPERATION_MODE::ARMED_MODE;
        //     blocking_buzz(BUZZ_INTERVALS::ARMING_PROCEDURE);
            
        //     debug("🚀 AUTO-ARMED at ");
        //     debug(alt);
        //     debug("m altitude (threshold: ");
        //     debug(ARM_ALTITUDE_THRESHOLD);
        //     debugln("m) ✓");
            
        //     char log_msg[100];
        //     snprintf(log_msg, sizeof(log_msg), "AUTO-ARMED at %.1fm altitude\r\n", alt);
        //     SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
        //                            system_log_file, log_msg);
        // }

        // --- Pre-apogee states ---
        if (!apogee_flag) {
            if (alt < LAUNCH_DETECTION_THRESHOLD) {
                current_state = ARMED_FLIGHT_STATE::PRE_FLIGHT_GROUND;
            } else if (alt >= LAUNCH_DETECTION_THRESHOLD &&
                       alt < (LAUNCH_DETECTION_THRESHOLD + LAUNCH_DETECTION_ALTITUDE_WINDOW)) {
                current_state = ARMED_FLIGHT_STATE::POWERED_FLIGHT;
            } else if (alt >= (LAUNCH_DETECTION_THRESHOLD + LAUNCH_DETECTION_ALTITUDE_WINDOW)) {
                current_state = ARMED_FLIGHT_STATE::COASTING;
            }

            // 🎯 Enhanced apogee detection using Kalman filtered altitude
            ring_buffer_put(&altitude_ring_buffer, alt);
            
            // Only check for apogee after we've clearly launched
            if (alt > (LAUNCH_DETECTION_THRESHOLD + LAUNCH_DETECTION_ALTITUDE_WINDOW)) {
#if USE_SIMULATION
                // In simulation we expect relatively small peak (~120m) so logic identical; kept for future tuning.
#endif
                static float max_altitude = 0;
                static uint8_t descent_count = 0;
                
                // Track maximum altitude reached
                if (alt > max_altitude) {
                    max_altitude = alt;
                    descent_count = 0; // Reset descent counter when still climbing
                    debug("🚀 New max altitude: ");
                    debug(max_altitude);
                    debugln("m");
                }
                // Check if we're consistently descending from maximum
                else if (alt < (max_altitude - APOGEE_DETECTION_THRESHOLD)) { // descent exceeding threshold
                    descent_count++;
                    debug("⬇️ Descent detected (");
                    debug(descent_count);
                    debug("/3): Alt=");
                    debug(alt);
                    debug("m, Max=");
                    debug(max_altitude);
                    debugln("m");
                    
                    // Confirm apogee after 3 consecutive readings showing descent
                    if (descent_count >= 3 && apogee_flag == 0) {
                        apogee_val = max_altitude;
                        current_state = ARMED_FLIGHT_STATE::APOGEE;
                        apogee_flag = 1;
                        apogee_detected_time = millis(); // Record apogee detection time
                        debug("🎯 APOGEE DETECTED at max altitude: ");
                        debug(max_altitude);
                        debug("m, current: ");
                        debug(alt);
                        debugln("m (Kalman filtered)!");
                        
                        char log_msg[150];
                        snprintf(log_msg, sizeof(log_msg), "APOGEE DETECTED - Max: %.1fm, Current: %.1fm\r\n", max_altitude, alt);
                        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, log_msg);
                    }
                }
            }
        }
        // --- Post-apogee states ---
        else {
            switch (current_state) {
                case ARMED_FLIGHT_STATE::APOGEE:
                    // 📦 Deploy drogue after delay (not immediately at apogee)
                    if ((millis() - apogee_detected_time) >= DROGUE_DEPLOY_DELAY_MS) {
                        current_state = ARMED_FLIGHT_STATE::DROGUE_DEPLOY;
                        debug("⏰ Drogue deployment delay (");
                        debug(DROGUE_DEPLOY_DELAY_MS);
                        debugln("ms) completed");
                    }
                    break;
                case ARMED_FLIGHT_STATE::DROGUE_DEPLOY:
                    // Deploy drogue immediately when entering this state (if armed and in flight)
                    if(operation_mode == OPERATION_MODE::ARMED_MODE && is_system_armed && DROGUE_DEPLOY_FLAG == 0) {
                        // Additional safety check: Only deploy if we've actually been in flight (above launch threshold)
                        #if USE_KALMAN_FOR_STATE_DETECTION
                        float current_alt = g_current_telemetry.alt_data.kalman_altitude;
                        #else
                        float current_alt = g_current_telemetry.alt_data.rel_altitude;
                        #endif
                        
                        if (current_alt > LAUNCH_DETECTION_THRESHOLD) {
                            drogueChuteDeploy();
                            current_state = ARMED_FLIGHT_STATE::DROGUE_DESCENT; // Move to next state immediately
                            debug("📦 DROGUE DEPLOYED at ");
                            debug(current_alt);
                            debugln("m altitude");
                            
                            char log_msg[100];
                            snprintf(log_msg, sizeof(log_msg), "DROGUE DEPLOYED at %.1fm altitude\r\n", current_alt);
                            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, log_msg);
                        } else {
                            debug("⚠️ DROGUE deployment blocked - altitude too low: ");
                            debug(current_alt);
                            debugln("m");
                        }
                    } else if (DROGUE_DEPLOY_FLAG == 1) {
                        // Drogue already deployed, move to descent state
                        current_state = ARMED_FLIGHT_STATE::DROGUE_DESCENT;
                        debugln("✅ DROGUE already deployed - moving to descent");
                    }
                    break;
                case ARMED_FLIGHT_STATE::DROGUE_DESCENT:
                    // 📦 Wait for main deploy altitude (using Kalman filtered data)
                    if (alt <= MAIN_EJECTION_HEIGHT && main_eject_flag == 0) {
                        current_state = ARMED_FLIGHT_STATE::MAIN_DEPLOY;
                        main_eject_flag = 1;
                        debug("📦 Main chute altitude reached: ");
                        debug(alt);
                        debug("m <= ");
                        debug(MAIN_EJECTION_HEIGHT);
                        debugln("m");
                    }
                    break;
                case ARMED_FLIGHT_STATE::MAIN_DEPLOY:
                    // Deploy main chute immediately when entering this state (if armed and in flight)
                    if(operation_mode == OPERATION_MODE::ARMED_MODE && is_system_armed && MAIN_CHUTE_EJECT_FLAG == 0) {
                        // Additional safety check: Only deploy if we're descending from a reasonable altitude
                        #if USE_KALMAN_FOR_STATE_DETECTION
                        float current_alt = g_current_telemetry.alt_data.kalman_altitude;
                        #else
                        float current_alt = g_current_telemetry.alt_data.rel_altitude;
                        #endif
                        
                        if (current_alt > LAUNCH_DETECTION_THRESHOLD) {
                            mainChuteDeploy();
                            current_state = ARMED_FLIGHT_STATE::MAIN_DESCENT; // Move to next state immediately
                            debug("📦 MAIN CHUTE DEPLOYED at ");
                            debug(current_alt);
                            debugln("m altitude");
                            
                            char log_msg[100];
                            snprintf(log_msg, sizeof(log_msg), "MAIN CHUTE DEPLOYED at %.1fm altitude\r\n", current_alt);
                            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, log_msg);
                        } else {
                            debug("MAIN CHUTE deployment blocked - altitude too low: ");
                            debug(current_alt);
                            debugln("m");
                        }
                    } else if (MAIN_CHUTE_EJECT_FLAG == 1) {
                        // Main chute already deployed, move to descent state
                        current_state = ARMED_FLIGHT_STATE::MAIN_DESCENT;
                        debugln(" MAIN CHUTE already deployed - moving to descent");
                    }
                    break;
                case ARMED_FLIGHT_STATE::MAIN_DESCENT:
                    // Wait for landing
                    if (alt < LAUNCH_DETECTION_THRESHOLD) {
                        current_state = ARMED_FLIGHT_STATE::POST_FLIGHT_GROUND;
                    }
                    break;
                default:
                    // If landed, stay in POST_FLIGHT_GROUND
                    if (alt < LAUNCH_DETECTION_THRESHOLD) {
                        current_state = ARMED_FLIGHT_STATE::POST_FLIGHT_GROUND;
                    }
                    break;
            }
        }

        // Debug state change
        if (current_state != last_state) {
            debug("🚀 State changed to: ");
            debug(current_state);
            debug(" (Alt: ");
            debug(alt);
            debugln("m)");
            last_state = current_state;
        }

        flight_data.state = current_state;
        vTaskDelay(pdMS_TO_TICKS(STATE_CHANGE_DELAY)); // FIX: use pdMS_TO_TICKS
    }
}

// ═══════════════════════════════════════════════════════════════════════════════════════
// ADS1115 Helper Functions
// ═══════════════════════════════════════════════════════════════════════════════════════

/**
 * @brief Read voltage from an ADS1115 channel through the voltage divider.
 * @param adsChannel ADS1115 channel (0-3)
 * @param numSamples Number of samples to average (default 4)
 * @return Actual battery/pyro voltage (after divider scaling)
 */
float readADSVoltage(uint8_t adsChannel, uint8_t numSamples = 4) {
    if (!ads_monitor_ready) {
        return 0.0f;
    }
    // Note: the original code had `if (pdMS_TO_TICKS(50)) { return 0.0f; }` here,
    // which always evaluated to true (pdMS_TO_TICKS(50) = 50 on ESP32), making
    // every ADS read return 0. That guard has been removed.

    auto railCalibrationFactor = [&](uint8_t channel) -> float {
        if (channel == ADC_CH_3V3) {
            return 1.0f;
        }
        float factor = logic_rail_3v3_voltage / 3.3f;
        if (factor < 0.85f) factor = 0.85f;
        if (factor > 1.15f) factor = 1.15f;
        return factor;
    };

    float sum = 0.0f;
    for (uint8_t i = 0; i < numSamples; i++) {
        int16_t raw  = ads.readADC_SingleEnded(adsChannel);
        float   pinV = ads.computeVolts(raw);
        // Guard: reject out-of-range readings
        if (pinV < 0.0f || pinV > 3.6f) pinV = 0.0f;
        sum += pinV * DIVIDER_RATIO * railCalibrationFactor(adsChannel);
        taskYIELD();
    }
    return sum / (float)numSamples;
}

/**
 * @brief Check if a pyro line is actively being driven (PWM on).
 * @param adsChannel ADS1115 channel connected to the pyro line
 * @return 1 if active (voltage > threshold), 0 otherwise
 */
uint8_t readPyroLineState(uint8_t adsChannel) {
    if (!ads_monitor_ready) {
        return 0;
    }

    // Debounced, hysteretic voltage detection to avoid floating-line false positives.
    // A pyro line is considered ACTIVE only after repeated high-voltage confirmations.
    static uint8_t stableState[4] = {0, 0, 0, 0};
    static uint8_t highCount[4]   = {0, 0, 0, 0};
    static uint8_t lowCount[4]    = {0, 0, 0, 0};

    uint8_t ch = (adsChannel <= 3) ? adsChannel : 0;
    float lineV = readADSVoltage(ch, 3);

    // Use measured battery as dynamic reference. When battery is invalid, use fallback.
    float supplyRef = ((battery_voltage >= BAT_CUTOFF) && (battery_voltage <= BAT_MAX_VALID))
                      ? battery_voltage
                      : DEFAULT_PWM_VCC_FALLBACK;

    // ON threshold: 15% of supply or absolute floor, whichever is higher.
    // Example at 15.0V => 2.25V threshold, which is far above noise/floating levels.
    float onThreshold = supplyRef * 0.15f;
    if (onThreshold < PYRO_DETECT_THRESHOLD_V) {
        onThreshold = PYRO_DETECT_THRESHOLD_V;
    }
    float offThreshold = onThreshold * 0.40f; // Schmitt-style hysteresis

    if (stableState[ch] == 0) {
        if (lineV >= onThreshold) {
            highCount[ch]++;
            if (highCount[ch] >= 3) {
                stableState[ch] = 1;
                highCount[ch] = 0;
                lowCount[ch] = 0;
            }
        } else {
            highCount[ch] = 0;
        }
    } else {
        if (lineV <= offThreshold) {
            lowCount[ch]++;
            if (lowCount[ch] >= 3) {
                stableState[ch] = 0;
                highCount[ch] = 0;
                lowCount[ch] = 0;
            }
        } else {
            lowCount[ch] = 0;
        }
    }

    return stableState[ch];
}

/*!****************************************************************************
 * @brief Initialize ADS1115 battery + chute monitoring (sensor-style init)
 * @return 1 if init OK, 0 otherwise
 *******************************************************************************/
uint8_t ADSInit() {
    // NOTE: Wire.begin() was already called in setup() — do NOT call it again here.
    // Calling Wire.begin() from inside a task or after tasks have started resets
    // the I2C peripheral and causes all other I2C devices to return 0.
    Wire.setTimeOut(50);

    if (!ads.begin(ADS_ADDR)) {
        ads_monitor_ready = false;
        battery_voltage = DEFAULT_PWM_VCC_FALLBACK;
        Vcc = DEFAULT_PWM_VCC_FALLBACK;
        debugln("[-] ADS1115 init failed - using fallback battery voltage 16.8V");
        return 0;
    }

    ads.setGain(GAIN_ONE);                  // +/-4.096 V, 0.125 mV/LSB
    ads.setDataRate(RATE_ADS1115_128SPS);   // 128 samples/sec
    ads_monitor_ready = true;

    float startup_battery_v = readADSVoltage(ADC_CH_BATTERY, 4);
    if (startup_battery_v >= 6.0f && startup_battery_v <= BAT_MAX_VALID) {
        battery_voltage = startup_battery_v;
        Vcc = startup_battery_v;
    } else {
        battery_voltage = DEFAULT_PWM_VCC_FALLBACK;
        Vcc = DEFAULT_PWM_VCC_FALLBACK;
    }

    debug("[+] ADS1115 init OK. Battery=");
    debug(String(battery_voltage, 2));
    debugln("V");
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════════════════════
// Battery Monitoring Task
// ═══════════════════════════════════════════════════════════════════════════════════════

/*!****************************************************************************
 * @brief Continuously monitor battery voltage via ADS1115 channel A2.
 * Updates the global battery_voltage variable.
 * Logs critical/cutoff warnings to system log.
 *******************************************************************************/
void batteryMonitorTask(void* pvParameters) {
    const uint32_t BATTERY_CHECK_INTERVAL_MS = 500;  // 2 Hz
    static float stable_battery_v = DEFAULT_PWM_VCC_FALLBACK;
    static bool stable_initialized = false;
    static bool stable_seeded_from_init = false;
    static uint8_t jump_count = 0;
    static uint8_t low_cutoff_confirm_count = 0;
    static uint8_t low_crit_confirm_count = 0;
    const uint8_t CUTOFF_CONFIRM_SAMPLES = 6; // 3s at 500ms cadence
    const uint8_t CRIT_CONFIRM_SAMPLES = 6;   // 3s at 500ms cadence
    const uint8_t LARGE_JUMP_CONFIRM_SAMPLES = 8; // 4s for large steps
    const float LARGE_JUMP_THRESHOLD_V = 2.5f;
    
    while (1) {
        #if USE_SIMULATION
        // In simulation mode, battery telemetry should be deterministic.
        battery_voltage = DEFAULT_PWM_VCC_FALLBACK;
        Vcc = DEFAULT_PWM_VCC_FALLBACK;
        vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
        continue;
        #endif

        if (!ads_monitor_ready) {
            // ADS1115 not present: keep fallback and avoid I2C traffic.
            // Keep last stable estimate to ride through transient I2C/ADS faults.
            battery_voltage = stable_initialized ? stable_battery_v : DEFAULT_PWM_VCC_FALLBACK;
            Vcc = battery_voltage;
            logic_rail_3v3_voltage = 3.3f;
            g_ads_rail_factor = 1.0f;
            g_power_rail_low = 0;
            vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
            continue;
        }

        float measured_3v3_v = readADSVoltage(ADC_CH_3V3, 4);
        if (measured_3v3_v >= 0.5f && measured_3v3_v <= 4.0f) {
            logic_rail_3v3_voltage = measured_3v3_v;
        }
        g_ads_rail_factor = logic_rail_3v3_voltage / 3.3f;
        if (g_ads_rail_factor < 0.85f) g_ads_rail_factor = 0.85f;
        if (g_ads_rail_factor > 1.15f) g_ads_rail_factor = 1.15f;
        g_power_rail_low = (logic_rail_3v3_voltage < 3.0f) ? 1 : 0;

        // Read battery voltage and use it for PWM supply math.
        float measured_battery_v = readADSVoltage(ADC_CH_BATTERY, 8);

        if (g_power_rail_low) {
            battery_voltage = stable_battery_v;
            Vcc = stable_battery_v;
            vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
            continue;
        }

        // Accept physically sane values; low battery is valid and must not be replaced by fallback.
        bool plausible = (measured_battery_v >= BAT_MIN_PLAUSIBLE_4S && measured_battery_v <= BAT_MAX_VALID);

        // Seed stable value from ADSInit result to avoid locking to a noisy first sample.
        if (!stable_seeded_from_init) {
            if (battery_voltage >= BAT_MIN_PLAUSIBLE_4S && battery_voltage <= BAT_MAX_VALID) {
                stable_battery_v = battery_voltage;
                stable_initialized = true;
            }
            stable_seeded_from_init = true;
        }

        // Initialize tracking from first valid sample to avoid locking to fallback 16.8V.
        if (plausible && !stable_initialized) {
            stable_battery_v = measured_battery_v;
            battery_voltage = measured_battery_v;
            Vcc = measured_battery_v;
            stable_initialized = true;
            jump_count = 0;
            vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
            continue;
        }

        float delta = fabsf(measured_battery_v - stable_battery_v);

        if (plausible && delta <= 1.0f) {
            jump_count = 0;
            stable_battery_v = (0.80f * stable_battery_v) + (0.20f * measured_battery_v);
            battery_voltage = stable_battery_v;
            Vcc = stable_battery_v;
        } else if (plausible && delta <= 2.0f) {
            // Require persistence for larger jumps before accepting.
            jump_count++;
            if (jump_count >= 3) {
                stable_battery_v = measured_battery_v;
                battery_voltage = stable_battery_v;
                Vcc = stable_battery_v;
                jump_count = 0;
            } else {
                battery_voltage = stable_battery_v;
                Vcc = stable_battery_v;
            }
        } else if (plausible && delta > LARGE_JUMP_THRESHOLD_V) {
            // Large steps need longer confirmation (bad first sample or wiring glitch).
            jump_count++;
            if (jump_count >= LARGE_JUMP_CONFIRM_SAMPLES) {
                stable_battery_v = measured_battery_v;
                battery_voltage = stable_battery_v;
                Vcc = stable_battery_v;
                jump_count = 0;
            } else {
                battery_voltage = stable_battery_v;
                Vcc = stable_battery_v;
            }
        } else {
            // Keep last stable value instead of propagating bad ADS samples.
            jump_count = 0;
            battery_voltage = stable_battery_v;
            Vcc = stable_battery_v;
        }
        
        // Safety: low-battery warnings with persistence filtering to ignore short glitches.
        static uint32_t lastWarningTime = 0;
        if (battery_voltage <= BAT_CUTOFF && battery_voltage > 1.0f) {
            if (low_cutoff_confirm_count < CUTOFF_CONFIRM_SAMPLES) {
                low_cutoff_confirm_count++;
            }
            low_crit_confirm_count = 0;

            if (low_cutoff_confirm_count >= CUTOFF_CONFIRM_SAMPLES && (millis() - lastWarningTime > 10000)) {  // warn every 10s
                debugln("[BATTERY] CUTOFF voltage reached - land immediately!");
                lastWarningTime = millis();
            }
        } else if (battery_voltage <= BAT_CRIT && battery_voltage > BAT_CUTOFF) {
            if (low_crit_confirm_count < CRIT_CONFIRM_SAMPLES) {
                low_crit_confirm_count++;
            }
            low_cutoff_confirm_count = 0;

            if (low_crit_confirm_count >= CRIT_CONFIRM_SAMPLES && (millis() - lastWarningTime > 30000)) {  // warn every 30s
                debugln("[BATTERY] Critical voltage - charge soon");
                lastWarningTime = millis();
            }
        } else {
            low_cutoff_confirm_count = 0;
            low_crit_confirm_count = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
    }
}

/*!****************************************************************************
 * @brief FLIGHT mode preflight gate with 1-minute grace period.
 * In FLIGHT mode, ARM is blocked until checks pass.
 * If issues persist beyond grace period, buzzer/LED alarm is enabled.
 *******************************************************************************/
void preflightHealthTask(void* pvParameters) {
    const uint32_t PREFLIGHT_GRACE_MS = 60000;
    uint32_t last_report_ms = 0;
    bool last_block_state = false;
    uint8_t preflight_battery_low_confirm_count = 0;
    const uint8_t PREFLIGHT_BATTERY_CONFIRM_SAMPLES = 5; // 1s at 200ms loop cadence

    auto forcePyroOutputsSafe = []() {
        // Force all pyro-related outputs low without blocking delays.
        digitalWrite(REMOTE_SWITCH, LOW);
        if (g_pyro_pwm_ready) {
            droguePWM.write(0);
            mainPWM.write(0);
        }
        digitalWrite(DROGUE_PIN, LOW);
        digitalWrite(MAIN_CHUTE_EJECT_PIN, LOW);

        drogueActive = false;
        mainActive = false;
        droguePyroArmed = false;
        mainPyroArmed = false;
        DROGUE_DEPLOY_FLAG = 0;
        MAIN_CHUTE_EJECT_FLAG = 0;
        drogue_pin_state = 0;
        main_chute_pin_state = 0;
        drogue_pin_engaged = 0;
        main_chute_pin_engaged = 0;
    };

    auto runStandardPreflightChecks = [&forcePyroOutputsSafe, &preflight_battery_low_confirm_count]() -> preflight_check_result_t {
        preflight_check_result_t r = {0};

        r.sensor_issue = (!g_spiffs_ready || !g_bmp_ready || !g_imu_ready || !g_gps_ready || !g_ads_ready);
        r.power_rail_issue = (g_power_rail_low != 0);

        // Explicit battery measurement for preflight validation.
        // Keep direct reads, but only raise issue after repeated low confirmations.
        if (g_ads_ready && ads_monitor_ready) {
            float measured = readADSVoltage(ADC_CH_BATTERY, 8);

            if (measured >= 1.0f && measured <= 20.0f) {
                r.measured_battery_voltage = measured;
            } else {
                r.measured_battery_voltage = battery_voltage;
            }

            bool raw_battery_issue = (r.measured_battery_voltage < BAT_CUTOFF) || (r.measured_battery_voltage > BAT_MAX_VALID);
            if (raw_battery_issue) {
                if (preflight_battery_low_confirm_count < 255) {
                    preflight_battery_low_confirm_count++;
                }
            } else {
                preflight_battery_low_confirm_count = 0;
            }
            r.battery_issue = (preflight_battery_low_confirm_count >= PREFLIGHT_BATTERY_CONFIRM_SAMPLES);
        } else {
            r.measured_battery_voltage = battery_voltage;
            r.battery_issue = false;
            preflight_battery_low_confirm_count = 0;
        }

        r.measured_3v3_voltage = logic_rail_3v3_voltage;

        // Explicit chute line measurement for preflight validation.
        if (g_ads_ready && ads_monitor_ready) {
            r.drogue_line_state = readPyroLineState(ADC_CH_DROGUE);
            r.main_line_state = readPyroLineState(ADC_CH_MAIN);
        } else {
            r.drogue_line_state = isDrogueOn() ? 1 : 0;
            r.main_line_state = isMainOn() ? 1 : 0;
        }

        bool switch_high = (digitalRead(REMOTE_SWITCH) == HIGH);
        r.chute_line_issue = (r.drogue_line_state != 0) ||
                             (r.main_line_state != 0) ||
                             switch_high ||
                             drogueActive || mainActive ||
                             droguePyroArmed || mainPyroArmed;

        // If any chute output looks active during preflight, force everything safe.
        if (!g_test_mode && r.chute_line_issue) {
            forcePyroOutputsSafe();
        }

        r.has_issue = r.sensor_issue || r.battery_issue || r.power_rail_issue || r.chute_line_issue;
        return r;
    };

    while (1) {
        preflight_check_result_t checks = runStandardPreflightChecks();
        uint32_t elapsed_ms = millis() - g_boot_time_ms;

        if (g_test_mode) {
            g_preflight_checks_complete = true;
            g_preflight_block_flight = false;
            g_preflight_alarm_active = false;

            if (checks.has_issue && (millis() - last_report_ms) > 5000) {
                debugln("[TEST MODE] Preflight issue detected (data transmission continues):");
                if (checks.battery_issue) {
                    debug("  - Battery out of range: ");
                    debug(checks.measured_battery_voltage);
                    debugln("V");
                }
                if (checks.power_rail_issue) {
                    debug("  - 3.3V rail low: ");
                    debug(checks.measured_3v3_voltage);
                    debugln("V");
                }
                if (checks.chute_line_issue) {
                    debug("  - Chute outputs were non-zero (drogue=");
                    debug(checks.drogue_line_state);
                    debug(", main=");
                    debug(checks.main_line_state);
                    debugln(") -> forced OFF");
                }
                if (!g_spiffs_ready) debugln("  - SPIFFS init failed");
                if (!g_bmp_ready) debugln("  - BMP180 init failed");
                if (!g_imu_ready) debugln("  - IMU init failed");
                if (!g_gps_ready) debugln("  - GPS init failed");
                if (!g_ads_ready) debugln("  - ADS1115 init failed");
                last_report_ms = millis();
            }
        } else {
            if (elapsed_ms < PREFLIGHT_GRACE_MS) {
                g_preflight_checks_complete = false;
                g_preflight_block_flight = true;
                g_preflight_alarm_active = false;

                if (checks.has_issue && (millis() - last_report_ms) > 5000) {
                    debug("[FLIGHT MODE] Preflight grace period active, ");
                    debug((PREFLIGHT_GRACE_MS - elapsed_ms) / 1000);
                    debugln("s remaining");
                    if (checks.battery_issue) {
                        debug("  - Battery out of range: ");
                        debug(checks.measured_battery_voltage);
                        debugln("V");
                    }
                    if (checks.power_rail_issue) {
                        debug("  - 3.3V rail low: ");
                        debug(checks.measured_3v3_voltage);
                        debugln("V");
                    }
                    if (checks.chute_line_issue) {
                        debug("  - Chute outputs were non-zero (drogue=");
                        debug(checks.drogue_line_state);
                        debug(", main=");
                        debug(checks.main_line_state);
                        debugln(") -> forced OFF");
                    }
                    if (!g_spiffs_ready) debugln("  - SPIFFS init failed");
                    if (!g_bmp_ready) debugln("  - BMP180 init failed");
                    if (!g_imu_ready) debugln("  - IMU init failed");
                    if (!g_gps_ready) debugln("  - GPS init failed");
                    if (!g_ads_ready) debugln("  - ADS1115 init failed");
                    last_report_ms = millis();
                }
            } else {
                g_preflight_checks_complete = true;
                g_preflight_block_flight = checks.has_issue;
                g_preflight_alarm_active = checks.has_issue;

                if (g_preflight_block_flight && is_system_armed) {
                    disarm_pyros();
                    if (use_beacon_mode) {
                        transmitter.setArmed(false);
                    }
                    is_system_armed = false;
                    operation_mode = OPERATION_MODE::SAFE_MODE;
                    debugln("[FLIGHT MODE] System forced to SAFE due to failed preflight checks");
                }

                if ((millis() - last_report_ms) > 2000) {
                    if (checks.has_issue) {
                        debugln("[FLIGHT MODE] Preflight FAILED - ARM blocked");
                        if (checks.battery_issue) {
                            debug("  - Battery out of range: ");
                            debug(checks.measured_battery_voltage);
                            debugln("V");
                        }
                        if (checks.power_rail_issue) {
                            debug("  - 3.3V rail low: ");
                            debug(checks.measured_3v3_voltage);
                            debugln("V");
                        }
                        if (checks.chute_line_issue) {
                            debug("  - Chute outputs were non-zero (drogue=");
                            debug(checks.drogue_line_state);
                            debug(", main=");
                            debug(checks.main_line_state);
                            debugln(") -> forced OFF");
                        }
                        if (!g_spiffs_ready) debugln("  - SPIFFS init failed");
                        if (!g_bmp_ready) debugln("  - BMP180 init failed");
                        if (!g_imu_ready) debugln("  - IMU init failed");
                        if (!g_gps_ready) debugln("  - GPS init failed");
                        if (!g_ads_ready) debugln("  - ADS1115 init failed");
                    } else if (last_block_state) {
                        debugln("[FLIGHT MODE] Preflight PASSED - ARM enabled");
                    }
                    last_report_ms = millis();
                }
            }
        }

        last_block_state = g_preflight_block_flight;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/*!****************************************************************************
 * @brief monitor the chute pins to see if they are deployed
 * @param pvParameters - A value that is passed as the paramater to the created task
 * If pvParameters is set to the address of a variable then the variable must still exist when the created task executes - 
 * so it is not valid to pass the address of a stack variable. 
 * This task reads the state of the drogue and main chute pins every 50ms
 * If the pin is HIGH, it means the chute has been deployed
 *******************************************************************************/



void monitorChutePinsTask(void* pvParameters) {
    while (1) {
        if (!is_system_armed) {
            // While disarmed, report both pyros as OFF to avoid false-positive telemetry.
            drogue_pin_state = 0;
            main_chute_pin_state = 0;
            drogue_pin_engaged = 0;
            main_chute_pin_engaged = 0;
        } else 
        // Read pyro line states via ADS1115 (detects PWM activity, not GPIO level)
        // Returns 1 if voltage > threshold (PWM actively driving), 0 otherwise.
        // If ADS is unavailable, fall back to software activation state.
        if (ads_monitor_ready) {
            drogue_pin_state = readPyroLineState(ADC_CH_DROGUE);
            main_chute_pin_state = readPyroLineState(ADC_CH_MAIN);
            drogue_pin_engaged = drogue_pin_state;
            main_chute_pin_engaged = main_chute_pin_state;
        } else {
            drogue_pin_state = isDrogueOn() ? 1 : 0;
            main_chute_pin_state = isMainOn() ? 1 : 0;
            drogue_pin_engaged = drogue_pin_state;
            main_chute_pin_engaged = main_chute_pin_state;
        }
        
        // Note: battery_voltage is now updated by batteryMonitorTask (500ms interval)
        // No need to read it here anymore - it's handled by the dedicated task
        
        // 🔥 ISOLATED RSSI HANDLING - Separate WiFi and Beacon RSSI logic
        if (comm_manager.isMQTTActive() && WiFi.isConnected()) {
            // MQTT mode: Use actual WiFi RSSI from flight computer
            wifi_rssi = WiFi.RSSI();
        } else {
            // Beacon mode: Send 0 - ESP32 base station will measure and override with actual beacon RSSI
            wifi_rssi = 0; // Flight computer sends 0, base station captures real beacon signal strength
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // FIX: use pdMS_TO_TICKS
    }
}



/*!****************************************************************************
 * @brief performs flight actions based on the current flight state

 * If the flight state requires an action, we perform it here
 * For example if the flight state is apogee, we perform MAIN_CHUTE ejection
 * 
 *******************************************************************************/
void flightStateCallback(void* pvParameters) {
    static ARMED_FLIGHT_STATE last_state = ARMED_FLIGHT_STATE::PRE_FLIGHT_GROUND;

    while(1) {
    // Read drogue and main chute pin states using digitalRead
        if (current_state != last_state) {
            last_state = current_state;
        }
        
        switch (current_state) {
            // PRE_FLIGHT_GROUND
            case ARMED_FLIGHT_STATE::PRE_FLIGHT_GROUND:
                //debugln("PRE-FLIGHT STATE");
                break;

            // POWERED_FLIGHT
            case ARMED_FLIGHT_STATE::POWERED_FLIGHT:
                //debugln("POWERED FLIGHT STATE");
                break;

            // COASTING
            case ARMED_FLIGHT_STATE::COASTING:
            //    debugln("COASTING");
                break;

            // APOGEE
            case ARMED_FLIGHT_STATE::APOGEE:
                //debugln("APOGEE");
                break;

            // DROGUE_DEPLOY
            case ARMED_FLIGHT_STATE::DROGUE_DEPLOY:
                // Backup deployment logic in case checkFlightState didn't trigger it
                if(operation_mode == OPERATION_MODE::ARMED_MODE && is_system_armed && DROGUE_DEPLOY_FLAG == 0) {
                    #if USE_KALMAN_FOR_STATE_DETECTION
                    float current_alt = g_current_telemetry.alt_data.kalman_altitude;
                    #else
                    float current_alt = g_current_telemetry.alt_data.rel_altitude;
                    #endif
                    
                    if (current_alt > LAUNCH_DETECTION_THRESHOLD) {
                        drogueChuteDeploy();
                        debug("BACKUP DROGUE DEPLOYMENT at ");
                        debug(current_alt);
                        debugln("m altitude");
                    }
                }
                break;

            // DROGUE_DESCENT
            case ARMED_FLIGHT_STATE::DROGUE_DESCENT:
                break;

            // MAIN_DEPLOY
            case ARMED_FLIGHT_STATE::MAIN_DEPLOY:
                // Backup deployment logic in case checkFlightState didn't trigger it
                if(operation_mode == OPERATION_MODE::ARMED_MODE && is_system_armed && MAIN_CHUTE_EJECT_FLAG == 0) {
                    #if USE_KALMAN_FOR_STATE_DETECTION
                    float current_alt = g_current_telemetry.alt_data.kalman_altitude;
                    #else
                    float current_alt = g_current_telemetry.alt_data.rel_altitude;
                    #endif
                    
                    if (current_alt > LAUNCH_DETECTION_THRESHOLD) {
                        mainChuteDeploy();
                        debug("🔄 BACKUP MAIN CHUTE DEPLOYMENT at ");
                        debug(current_alt);
                        debugln("m altitude");
                    }
                }
                break;

            // MAIN_DESCENT
            case ARMED_FLIGHT_STATE::MAIN_DESCENT:
            //    debugln("MAIN CHUTE DESCENT");
                break;

            // POST_FLIGHT_GROUND
            case ARMED_FLIGHT_STATE::POST_FLIGHT_GROUND:
            //    debugln("POST FLIGHT GROUND");
                break;
            
            // MAINTAIN AT PRE_FLIGHT_GROUND IF NO STATE IS SPECIFIED - NOT GONNA HAPPEN BUT BETTER SAFE THAN SORRY
            default:
                debugln(current_state);
                break;

        }
        
        vTaskDelay(pdMS_TO_TICKS(CONSUME_TASK_DELAY)); // FIX: use pdMS_TO_TICKS
    }
}

/*!****************************************************************************
 * @brief debug flight/test data to terminal, this task is called if the DEBUG_TO_TERMINAL is set to 1 (see defs.h)
 * @param pvParameter - A value that is passed as the parameter to the created task.
 * If pvParameter is set to the address of a variable then the variable must still exist when the created task executes - 
 * so it is not valid to pass the address of a stack variable.
 * 
 *******************************************************************************/
void debugToTerminalTask(void* pvParameters){
    telemetry_type_t telemetry_received_packet; // acceleration received from acceleration_queue
    static uint32_t last_timeout_log_ms = 0;

    while(true){
        // get telemetry data - block until a telemetry packet is available to avoid stale/duplicate prints
        if (debug_to_term_queue_handle != NULL) {
            if (xQueueReceive(debug_to_term_queue_handle, &telemetry_received_packet, pdMS_TO_TICKS(1000)) != pdTRUE) {
                if (millis() - last_timeout_log_ms > 5000) {
                    Serial.println("[DEBUG] Telemetry timeout - no new packets");
                    last_timeout_log_ms = millis();
                }
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        } else {
            // If queue not configured, yield briefly
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        buildTelemetryCsv(telemetry_received_packet, telemetry_packet_buffer, sizeof(telemetry_packet_buffer));
        
        // 🔥 UPDATE GLOBAL TELEMETRY BUFFER for seamless mode switching
        updateGlobalTelemetryBuffer(telemetry_packet_buffer);

        // Keep local CSV debug stream visible on serial monitor.
        Serial.print(telemetry_packet_buffer);

        // Logging is centralized in `logToMemory` task. This task only updates
        // the global telemetry buffer and handles terminal/beacon output.
        
        // Only transmit via beacon if beacon mode is active and MQTT mode is NOT
        bool beacon_success = false;
        bool beaconActive = comm_manager.isBeaconActive();
        bool mqttActive = comm_manager.isMQTTActive();
        
        if (beaconActive && !mqttActive) {
            // FLIGHT mode: gate telemetry if sensors not healthy
            bool sensor_health_ok = (g_spiffs_ready && g_bmp_ready && g_imu_ready && g_gps_ready && g_ads_ready);
            bool battery_ok = (battery_voltage >= BAT_CUTOFF && battery_voltage <= BAT_MAX_VALID);
            bool power_ok = (g_power_rail_low == 0) && (logic_rail_3v3_voltage >= 3.0f);
            
            if (g_test_mode || (sensor_health_ok && battery_ok && power_ok)) {
                beacon_success = transmitter.sendBeacon(telemetry_packet_buffer, strlen(telemetry_packet_buffer));
            } else {
                // In FLIGHT mode with sensor issues - suppress telemetry
                beacon_success = false;
                if ((millis() / 5000) % 2 == 0) {  // Log once every 10 seconds
                    debugln("[BEACON GATE] FLIGHT mode: Telemetry blocked - sensors not ready");
                }
            }
            if (beacon_success) {
                debugln("[BEACON TX] ✓ Sent: Rec#" + String(telemetry_received_packet.record_number));
            } else {
                debugln("[BEACON TX] ✗ Failed to send beacon");
            }
        }
        
        // Update communication manager with beacon transmission status
        comm_manager.updateTransmissionStatus(false, beacon_success, false);
        
        vTaskDelay(pdMS_TO_TICKS(CONSUME_TASK_DELAY)); // FIX: use pdMS_TO_TICKS
    }
}


/*!****************************************************************************
 * @brief log the data to the external flash memory
 * @param pvParameter - A value that is passed as the paramater to the created task.
 * If pvParameter is set to the address of a variable then the variable must still exist when the created task executes - 
 * so it is not valid to pass the address of a stack variable.
 * 
 *******************************************************************************/
// void logToMemory(void* pvParameter) {
//     telemetry_type_t received_packet;

//     while(1) {
//         xQueueReceive(log_to_mem_queue_handle, &received_packet, portMAX_DELAY);

//         // received_packet.record_number++; 

//         // is it time to record?
//         current_log_time = millis();

//         if(current_log_time - previous_log_time > log_sample_interval) {
//             previous_log_time = current_log_time;
//             data_logger.loggerWrite(received_packet);
//         }
        
//     }

// }
void logToMemory(void* pvParameter) {
    telemetry_type_t received_packet;

    while (1) {
        // Block until a telemetry packet is available. This task is the
        // single writer to both Flash and SD to avoid concurrent SPI access.
        if (xQueueReceive(log_to_mem_queue_handle, &received_packet, portMAX_DELAY) == pdTRUE) {
            // Log every received telemetry packet (one-per-reading cycle from Kalman)
            current_log_time = millis();
            previous_log_time = current_log_time;

            #if ENABLE_FLASH_LOGGING
                // Write to Flash (compile-time controlled)
                disableAllDevices();
                digitalWrite(flash_cs_pin, LOW);
                data_logger.loggerWrite(received_packet);
                digitalWrite(flash_cs_pin, HIGH);
            #endif

            #if ENABLE_SD_LOGGING
                // Write to SD if available
                if (sdLogger.initialized()) {
                    disableAllDevices();
                    digitalWrite(SD_CS_PIN, LOW);
                    sdLogger.log(received_packet, gps_packet);
                    digitalWrite(SD_CS_PIN, HIGH);
                }
            #endif
        }

        // Small yield to keep system responsive
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/*!****************************************************************************
 * @brief send flight data to ground
 * @param pvParameter - A value that is passed as a the parameter to the created task.
 * If pvParameter is set to the address of a variable then the variable must still exist when the created task executes -
 * so it is not valid to pass the address of a stack variable.
 *
 *******************************************************************************/
void MQTT_TransmitTelemetry(void* pvParameters) {
    telemetry_type_t telemetry_received_packet;

    while(1) {
        xQueueReceive(telemetry_data_queue_handle, &telemetry_received_packet, portMAX_DELAY);

        buildTelemetryCsv(telemetry_received_packet, telemetry_packet_buffer, sizeof(telemetry_packet_buffer));
        // 🔥 UPDATE GLOBAL TELEMETRY BUFFER for seamless mode switching
        updateGlobalTelemetryBuffer(telemetry_packet_buffer);
        // Persisting to SD/Flash is handled by the centralized `logToMemory` task.

        // 🔥 ISOLATED MQTT TRANSMISSION - Only transmit via MQTT when MQTT mode is active
        bool mqtt_success = false;
        if (comm_manager.isMQTTActive()) {
            #if BEACON_MODE_SAFETY_CHECKS
            // Additional safety check - ensure MQTT flag is enabled
            if (MQTT == 0) {
                debugln("[MQTT TX] MQTT disabled by flag - skipping transmission");
                mqtt_success = false;
            } else 
            #endif
            // Check WiFi connection status for MQTT mode
            if (!WiFi.isConnected()) {
                debugln("[MQTT TX] WiFi not connected - transmission failed");
                mqtt_success = false;
            }
            // In FLIGHT mode gate: skip telemetry if sensors are unhealthy.
            else {
                bool sensor_ok = g_spiffs_ready && g_bmp_ready && g_imu_ready && g_gps_ready && g_ads_ready;
                bool battery_ok = (battery_voltage >= BAT_CUTOFF && battery_voltage <= BAT_MAX_VALID);
                bool power_ok = (g_power_rail_low == 0) && (logic_rail_3v3_voltage >= 3.0f);

                if (g_test_mode || (sensor_ok && battery_ok && power_ok)) {
                    if (client.publish(MQTT_TELEMETRY_TOPIC, telemetry_packet_buffer)) {
                        debugln("[MQTT TX] " + String(telemetry_packet_buffer));
                        mqtt_success = true;
                    } else {
                        debugln("[MQTT TX] Failed to publish data");
                        mqtt_success = false;
                    }
                } else {
                    mqtt_success = false;
                    if ((millis() / 5000) % 2 == 0) {
                        debugln("[MQTT GATE] FLIGHT mode: Telemetry blocked - sensors not ready");
                    }
                }
            }
        }
        // Update communication manager with MQTT transmission status
        comm_manager.updateTransmissionStatus(mqtt_success, false, false);

        vTaskDelay(CONSUME_TASK_DELAY/ portTICK_PERIOD_MS);
    }
}

/*!****************************************************************************
 * @brief XBee Telemetry Transmission Task (CSV Format, Transparent Mode)
 * 
 * This task sends telemetry via XBee using transparent UART mode.
 * Data format: CSV string with newline termination (same as MQTT)
 * Transmission rate: Same as MQTT (controlled by queue)
 *******************************************************************************/
void XBee_TransmitTelemetry(void* pvParameters) {
    telemetry_type_t telemetry_received_packet;

    while(1) {
        xQueueReceive(telemetry_data_queue_handle, &telemetry_received_packet, portMAX_DELAY);

        buildTelemetryCsv(telemetry_received_packet, telemetry_packet_buffer, sizeof(telemetry_packet_buffer));
        
        // 🔥 UPDATE GLOBAL TELEMETRY BUFFER for seamless mode switching
        updateGlobalTelemetryBuffer(telemetry_packet_buffer);

        // 🔥 ISOLATED XBEE TRANSMISSION - Only transmit via XBee when XBee mode is active
        bool xbee_success = false;
        if (comm_manager.isXBeeActive()) {
            // In FLIGHT mode gate: skip telemetry if sensors are unhealthy.
            bool sensor_ok = g_spiffs_ready && g_bmp_ready && g_imu_ready && g_gps_ready && g_ads_ready;
            bool battery_ok = (battery_voltage >= BAT_CUTOFF && battery_voltage <= BAT_MAX_VALID);
            bool power_ok = (g_power_rail_low == 0) && (logic_rail_3v3_voltage >= 3.0f);

            if (g_test_mode || (sensor_ok && battery_ok && power_ok)) {
                XBeeSerial.println(telemetry_packet_buffer);
                Serial.println("[XBEE TX] ✓ Sent: Rec#" + String(telemetry_received_packet.record_number));
                xbee_success = true;
            } else {
                xbee_success = false;
                if ((millis() / 5000) % 2 == 0) {
                    Serial.println("[XBEE GATE] FLIGHT mode: Telemetry blocked - sensors not ready");
                }
            }
        }
        
        // Update communication manager with XBee transmission status
        comm_manager.updateTransmissionStatus(false, false, xbee_success);

        vTaskDelay(CONSUME_TASK_DELAY/ portTICK_PERIOD_MS);
    }
}

/*!
 * @brief Try reconnecting to MQTT if connection is lost
 *
 */
void MQTT_Reconnect() {
    // while(1) {
        if(!client.connected()) {
            debugln("[..]Attempting MQTT connection..."); // TODO: SYS LOGGER
            if (client.connect("FC")) {
                debugln("[+]MQTT reconnected");
                client.subscribe("n4/commands"); // TODO: USE DEFINE here
                mqtt_connect_flag = 1;
            } else {
                mqtt_connect_flag = 0;
                debug("failed, rc=");
                debugln(client.state());
                vTaskDelay(1000/portTICK_PERIOD_MS);
            }
        }
    // }
}

// This function is called whenever an MQTT message is received
void mqtt_Callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    const char* command = message.c_str();
    mqtt_command_processor(topic, command);
}

/*!****************************************************************************
 * @brief Initialize MQTT
 *
 *******************************************************************************/
void MQTTInit(const char* broker_IP, int broker_port) {
    // client.setBufferSize(MQTT_BUFFER_SIZE);
    debugln("[+]Initializing MQTT\n");
    client.setServer(broker_IP, broker_port);
    client.setCallback(mqtt_Callback);
    debugln("MQTT callback hooked!");
    debugln("[+]MQTT init OK");
}

/*!****************************************************************************
 * @brief blinks green LED for safe mode and red LED for armed mode
 *******************************************************************************/
void xOperationModeIndicateTask(void* pvParameters) {
    static bool startup_pattern_queued = false;
    static uint8_t active_pattern = BUZZER_PATTERN_NONE;
    static uint32_t pattern_start_ms = 0;
    static bool led_state = false;
    static uint32_t last_led_toggle_ms = 0;

    if (!startup_pattern_queued) {
        requestBuzzerPattern(g_test_mode ? BUZZER_PATTERN_STARTUP_TEST : BUZZER_PATTERN_STARTUP_FLIGHT);
        startup_pattern_queued = true;
    }

    while(1) {
        uint32_t now = millis();

        if (g_preflight_alarm_active) {
            // 3 beeps every 2 seconds, LED in sync.
            uint32_t phase = now % 2000;
            bool alarm_on = (phase < 150) || (phase >= 300 && phase < 450) || (phase >= 600 && phase < 750);

            digitalWrite(BUZZER_PIN, alarm_on ? HIGH : LOW);
            digitalWrite(RED_LED_PIN, alarm_on ? HIGH : LOW);
            digitalWrite(GREEN_LED_PIN, LOW);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (active_pattern == BUZZER_PATTERN_NONE && g_buzzer_pattern_request != BUZZER_PATTERN_NONE) {
            active_pattern = g_buzzer_pattern_request;
            g_buzzer_pattern_request = BUZZER_PATTERN_NONE;
            pattern_start_ms = now;
        }

        bool buzzer_on = false;
        if (active_pattern != BUZZER_PATTERN_NONE) {
            uint32_t pattern_elapsed = now - pattern_start_ms;
            switch (active_pattern) {
                case BUZZER_PATTERN_SHORT_ACK:
                    buzzer_on = (pattern_elapsed < 120);
                    if (pattern_elapsed >= 160) active_pattern = BUZZER_PATTERN_NONE;
                    break;
                case BUZZER_PATTERN_STARTUP_TEST:
                    buzzer_on = (pattern_elapsed < 100) || (pattern_elapsed >= 220 && pattern_elapsed < 320);
                    if (pattern_elapsed >= 380) active_pattern = BUZZER_PATTERN_NONE;
                    break;
                case BUZZER_PATTERN_STARTUP_FLIGHT:
                    buzzer_on = (pattern_elapsed < 320);
                    if (pattern_elapsed >= 380) active_pattern = BUZZER_PATTERN_NONE;
                    break;
                default:
                    active_pattern = BUZZER_PATTERN_NONE;
                    break;
            }
        }
        digitalWrite(BUZZER_PIN, buzzer_on ? HIGH : LOW);

        uint32_t blink_interval = operation_mode ? BLINK_INTERVALS::ARMED_BLINK : BLINK_INTERVALS::SAFE_BLINK;
        if ((now - last_led_toggle_ms) >= blink_interval) {
            led_state = !led_state;
            last_led_toggle_ms = now;
        }

        if (operation_mode) {
            digitalWrite(RED_LED_PIN, led_state ? HIGH : LOW);
            digitalWrite(GREEN_LED_PIN, LOW);
        } else {
            digitalWrite(GREEN_LED_PIN, led_state ? HIGH : LOW);
            digitalWrite(RED_LED_PIN, LOW);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/*!****************************************************************************
 * @brief fires the pyro-charge to deploy the drogue chute
 * Turn on the drogue chute ejection circuit by running the GPIO 
 * HIGH for a preset No. of seconds.  
 * Default no. of seconds to remain HIGH is 5 
 * 
 *******************************************************************************/
//void drogueChuteDeploy() {

    // // check for drogue chute deploy conditions 

    // //if the drogue deploy pin is HIGH, there is an error
    // if(digitalRead(DROGUE_PIN)) {
    //     // error
    // } else {
    //     // pulse the drogue pin for a number ofseceonds - determined from pop tests
    //     digitalWrite(DROGUE_PIN, HIGH);
    //     delay(PYRO_CHARGE_TIME); // TODO- Make this delay non-blocking

    //     // update the drogue deployed telemetry variable
    //     DROGUE_DEPLOY_FLAG = 1;
    //     debugln("DROGUE CHUTE DEPLOYED");
    // }

//}


void xCreateAllTasks() {
    debugln("Creating all tasks with enhanced error protection");
    
    // 🛡️ MEMORY CHECK: Verify available heap before task creation
    size_t free_heap_before = esp_get_free_heap_size();
    Serial.printf("[TASK CREATION] Available heap before tasks: %d bytes\n", free_heap_before);
    
    if (free_heap_before < 50000) { // Less than 50KB free
        Serial.println("[ERROR] Insufficient memory for task creation - halting");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "CRITICAL: Insufficient memory for task creation\r\n");
        return;
    }
    
    uint8_t tasks_created = 0;
    uint8_t tasks_failed = 0;

    // /* 🛡️ DMP FIFO POLLING TASK - with memory protection */
    // if (imu.isDMPReady()) {
    //     BaseType_t dmp_task = xTaskCreatePinnedToCore(dmpFIFOPollingTask, "dmpFIFOPoller", STACK_SIZE*2, NULL, 1, &dmpFIFOPollingTaskHandle, 1);
    //     if(dmp_task == pdPASS) {
    //         tasks_created++;
    //         debugln("[+]DMP FIFO polling task created OK.");
    //         SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]DMP FIFO polling task created OK.\r\n");
    //     } else {
    //         tasks_failed++;
    //         debugln("[-]DMP FIFO polling task creation failed - falling back to legacy mode");
    //         SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]DMP FIFO task creation failed\r\n");
    //     }
    // }
	#if USE_SIMULATION
	if (sim_count > 0) {
	BaseType_t sim = xTaskCreatePinnedToCore(simulation_task, "simulation", STACK_SIZE * 2, NULL, 2, &simulationTaskHandle, 1);

	if (sim == pdPASS) {
    	tasks_created++;
    	debugln("[+]Simulation task created OK.");
    	SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]Simulation task created OK.\r\n");
	}
	else {
    	tasks_failed++;
    	debugln("[-]Simulation task creation failed");
    	SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "[-]Simulation task creation failed\r\n");
	}
	} else {
		debugln("[-]No simulation data loaded.");	
	}

	#else
    /* 🛡️ READ ACCELERATION DATA - with memory protection */
    BaseType_t gr = xTaskCreatePinnedToCore(readAccelerationTask, "readAccelerometer", STACK_SIZE*4, NULL, 2, &readAccelerationTaskHandle, 1);
    if(gr == pdPASS) {
        tasks_created++;
        debugln("[+]Read acceleration task created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]Read acceleration task created OK.\r\n");
    } else {
        tasks_failed++;
        debugln("[-]Read acceleration task creation failed - CRITICAL");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "[CRITICAL]Read acceleration task creation failed\r\n");
    }

    /* 🛡️ TASK 3: READ GPS DATA - with error protection */
    BaseType_t rg = xTaskCreatePinnedToCore(readGPSTask, "readGPS", STACK_SIZE*2, NULL, 2, &readGPSTaskHandle, 1);
    if(rg == pdPASS) {
        tasks_created++;
        debugln("[+]Read GPS task created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]Read GPS task created OK.\r\n");
    } else {
        tasks_failed++;
        debugln("[-]Failed to create GPS task");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]Failed to create GPS task\r\n");
    }
	#endif

    /* 🛡️ CHECK FLIGHT STATE TASK - essential for flight safety */
    BaseType_t cf = xTaskCreatePinnedToCore(checkFlightState,"checkFlightState",STACK_SIZE*2,NULL, 2, &checkFlightStateTaskHandle, 1);
    if(cf == pdPASS) {
        tasks_created++;
        debugln("[+]checkFlightState task created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]checkFlightState task created OK.\r\n");
    } else {
        tasks_failed++;
        debugln("[-]Failed to create checkFlightState task - CRITICAL");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "[CRITICAL]Failed to create checkFlightState task\r\n");
    }

    /* 🛡️ FLIGHT STATE CALLBACK TASK - essential for pyro control */
    BaseType_t fs = xTaskCreatePinnedToCore(flightStateCallback, "flightStateCallback", STACK_SIZE*2, NULL, 2, &flightStateCallbackTaskHandle, 1);
    if(fs == pdPASS) {
        tasks_created++;
        debugln("[+]flightStateCallback task created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]flightStateCallback task created OK.\r\n");
    } else {
        tasks_failed++;
        debugln("[-]Failed to create flightStateCallback task - CRITICAL");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "[CRITICAL]Failed to create flightStateCallback task\r\n");
    }
    
    /* 🛡️ MONITOR CHUTE PINS TASK - essential for status monitoring
     * Pinned to Core 0: uses I2C (ADS1115) — keep off Core 1 to avoid MPU contention */
    BaseType_t mp = xTaskCreatePinnedToCore(monitorChutePinsTask, "monitorChutePins", STACK_SIZE, NULL, 2, NULL, 0);
    if(mp == pdPASS) {
        tasks_created++;
        debugln("[+]monitorChutePinsTask created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]monitorChutePinsTask created OK.\r\n");
    } else {
        tasks_failed++;
        debugln("[-]Failed to create monitorChutePinsTask");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]Failed to create monitorChutePinsTask\r\n");
    }
    
    /* 🛡️ BATTERY MONITOR TASK - essential for battery health monitoring via ADS1115
     * Pinned to Core 0: heaviest I2C user — 8 samples every 500ms */
    if (ads_monitor_ready) {
        BaseType_t bm = xTaskCreatePinnedToCore(batteryMonitorTask, "batteryMonitor", STACK_SIZE, NULL, 2, &batteryMonitorTaskHandle, 0);
        if(bm == pdPASS) {
            tasks_created++;
            debugln("[+]batteryMonitorTask created OK.");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]batteryMonitorTask created OK.\r\n");
        } else {
            tasks_failed++;
            debugln("[-]Failed to create batteryMonitorTask");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]Failed to create batteryMonitorTask\r\n");
        }
    } else {
        debugln("[!] batteryMonitorTask skipped (ADS1115 unavailable)");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "batteryMonitorTask skipped - ADS1115 unavailable\r\n");
    }

    /* 🛡️ PREFLIGHT HEALTH TASK - monitors sensors/battery and enforces FLIGHT mode gate
     * Pinned to Core 0: calls readADSVoltage (I2C) every 200ms */
    BaseType_t pf = xTaskCreatePinnedToCore(preflightHealthTask, "preflightHealth", STACK_SIZE*2, NULL, 2, &preflightHealthTaskHandle, 0);
    if (pf == pdPASS) {
        tasks_created++;
        debugln("[+]preflightHealth task created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]preflightHealth task created OK.\r\n");
    } else {
        tasks_failed++;
        debugln("[-]Failed to create preflightHealth task - CRITICAL");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "[CRITICAL]Failed to create preflightHealth task\r\n");
    }

    /* 🛡️ TRANSMIT TELEMETRY DATA - communication essential (only if MQTT enabled) */
    #if BEACON_MODE_SAFETY_CHECKS
    if (MQTT == 1) {
    #endif
        BaseType_t th = xTaskCreatePinnedToCore(MQTT_TransmitTelemetry, "transmit_telemetry", STACK_SIZE*4, NULL, 2, &MQTT_TransmitTelemetryTaskHandle, 1);
        if(th == pdPASS){
            tasks_created++;
            debugln("[+]MQTT transmit task created OK");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]MQTT transmit task created OK\r\n");
        } else {
            tasks_failed++;
            debugln("[-]MQTT transmit task failed to create");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]MQTT transmit task failed to create\r\n");
        }
    #if BEACON_MODE_SAFETY_CHECKS
    } else {
        debugln("[+] MQTT transmit task skipped - strict beacon mode");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "MQTT transmit task skipped - strict beacon mode\r\n");
    }
    #endif

    /* 🛡️ XBEE TASKS - always created, runtime behavior controlled by comm_manager */
    Serial.println("[XBEE TASK] Attempting to create XBee telemetry task...");
    BaseType_t xb = xTaskCreatePinnedToCore(XBee_TransmitTelemetry, "xbee_telemetry", STACK_SIZE*4, NULL, 2, &XBee_TransmitTelemetryTaskHandle, 1);
    if(xb == pdPASS){
        tasks_created++;
        debugln("[+]XBee transmit task created OK");
        Serial.printf("[XBEE TASK] Telemetry task created. use_xbee_mode=%d\n", use_xbee_mode);
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]XBee transmit task created OK\r\n");
    } else {
        tasks_failed++;
        debugln("[-]XBee transmit task failed to create");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]XBee transmit task failed to create\r\n");
    }

    Serial.println("[XBEE CMD] Attempting to create XBee command task...");
    BaseType_t xbcmd = xTaskCreatePinnedToCore(xbeeCommandTask, "xbee_command", STACK_SIZE*3, NULL, 3, NULL, 1);
    if(xbcmd == pdPASS) {
        tasks_created++;
        debugln("[+]XBee command task created OK");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]XBee command task created OK\r\n");
    } else {
        tasks_failed++;
        debugln("[-]XBee command task failed to create");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]XBee command task failed to create\r\n");
    }

    /* 🛡️ KALMAN FILTER 2D TASK - essential for altitude estimation */
    BaseType_t kf2d = xTaskCreatePinnedToCore(taskKalman2D, "Kalman2D", STACK_SIZE*4, NULL, 2, NULL, 1);
    if(kf2d == pdPASS) {
        tasks_created++;
        debugln("[+]Kalman2D task created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]Kalman2D task created OK.\r\n");
    } else {
        tasks_failed++;
        debugln("[-]Kalman2D task creation failed - CRITICAL");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "[CRITICAL]Kalman2D task creation failed\r\n");
    }

    #if DEBUG_TO_TERMINAL   // set DEBUG_TO_TERMINAL to 0 to prevent serial debug data to serial monitor
        /* 🛡️ TASK 7: DISPLAY DATA ON SERIAL MONITOR - FOR DEBUGGING */
        BaseType_t dt = xTaskCreatePinnedToCore(debugToTerminalTask,"debugToTerminalTask",STACK_SIZE*4, NULL,2,&debugToTerminalTaskHandle, 1);
        if(dt == pdPASS) {
            tasks_created++;
            debugln("[+]debugToTerminal task created OK");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]debugToTerminal task created OK\r\n");
        } else {
            tasks_failed++;
            debugln("[-]debugToTerminal task not created");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]debugToTerminal task not created\r\n");
        }
    #endif // DEBUG_TO_TERMINAL_TASK

    #if LOG_TO_MEMORY   // set LOG_TO_MEMORY to 1 to allow logging to memory 
        /* 🛡️ TASK 9: LOG DATA TO MEMORY */
        if(xTaskCreatePinnedToCore(logToMemory,"logToMemory",STACK_SIZE*4,NULL,2,&logToMemoryTaskHandle,1) != pdPASS){
            tasks_failed++;
            debugln("[-]logToMemory task failed to create");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]logToMemory task failed to create\r\n");
        }else{
            tasks_created++;
            debugln("[+]logToMemory task created OK.");
            SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]logToMemory task created OK.\r\n");
        }
    #endif // LOG_TO_MEMORY

    /* 🛡️ OPERATION MODE INDICATION TASK - essential for user feedback */
    if(xTaskCreatePinnedToCore(xOperationModeIndicateTask,"xOperationModeIndicateTask",STACK_SIZE*2,NULL,2,&opModeIndicateTaskHandle,1) != pdPASS){
        tasks_failed++;
        debugln("[-]xOperationModeIndicateTask task failed to create");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "[-]xOperationModeIndicateTask task failed to create\r\n");
    }else{
        tasks_created++;
        debugln("[+]xOperationModeIndicateTask task created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]xOperationModeIndicateTask task created OK.\r\n");
    }

    /* 🛡️ READ ALTIMETER DATA - essential for flight state detection */
    BaseType_t ra = xTaskCreatePinnedToCore(readAltimeterTask,"readAltimeter",STACK_SIZE*3,NULL,2, &readAltimeterTaskHandle, 1);
    if(ra == pdPASS) {
        tasks_created++;
        debugln("[+]readAltimeterTask created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]readAltimeterTask created OK.\r\n");
    } else {
        tasks_failed++;
        debugln("[-]Failed to create readAltimeterTask - CRITICAL");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "[CRITICAL]Failed to create readAltimeterTask\r\n");
    }

    /* 🛡️ Create ESP-NOW command task - will be managed by communication manager */
    BaseType_t ec = xTaskCreatePinnedToCore(
        espnowCommandTask,
        "ESPNowCmd",
        STACK_SIZE * 6,  // Increased from 2 to 6 to prevent stack overflow
        NULL,
        2,
        &espnowCommandTaskHandle,
        1
    );

    if (ec == pdPASS) {
        tasks_created++;
        debugln("[+]ESPNowCmd task created OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]ESPNowCmd task created OK.\r\n");
    } else {
        tasks_failed++;
        debugln("[-]Failed to create ESPNowCmd task - CRITICAL");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "[CRITICAL]Failed to create ESPNowCmd task\r\n");
    }

    // 🛡️ MEMORY CHECK AFTER TASK CREATION
    size_t free_heap_after = esp_get_free_heap_size();
    size_t heap_used = free_heap_before - free_heap_after;
    
    Serial.printf("[TASK CREATION SUMMARY] Tasks created: %d, Failed: %d\n", tasks_created, tasks_failed);
    Serial.printf("[MEMORY USAGE] Heap before: %d, After: %d, Used: %d bytes\n", 
                  free_heap_before, free_heap_after, heap_used);
    
    if (tasks_failed > 0) {
        Serial.printf("[WARNING] %d tasks failed to create - system may be unstable\n", tasks_failed);
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, 
                               ("Task creation failures: " + String(tasks_failed) + "\r\n").c_str());
    }
    
    if (free_heap_after < 20000) { // Less than 20KB remaining
        Serial.println("[WARNING] Low memory after task creation - monitoring required");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "Low memory after task creation\r\n");
    }

    debugln();
    debugln(F("=============================================="));
    debugln(F("========== FINISHED CREATING TASKS ==========="));
    debugln(F("==============================================\n"));
    
    // 🛡️ WATCHDOG PROTECTION: Reset watchdog after task creation
    esp_task_wdt_reset();
}

/*!****************************************************************************
 * @brief Setup - perform initialization of all hardware subsystems, create queues, create queue handles
 * initialize system check table
 * 
 *******************************************************************************/
void setup() {
    /* initialize serial */
    Serial.begin(BAUDRATE);

    // Initialize I2C bus ONCE here in setup(), before any peripheral init or task creation.
    // NEVER call Wire.begin() inside a task or peripheral init function — it resets the
    // I2C peripheral and causes all concurrent I2C devices to return 0x0000.
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);  // 400 kHz — BMP180, MPU6050, ADS1115 all support this
    Wire.setTimeOut(50);    // 50ms timeout prevents bus lockup from NACK
    Serial.println("[I2C] Bus initialized on SDA=21, SCL=22 @ 400kHz");
    // g_boot_time_ms = millis();

    // // Initialize I2C mutex before any bus activity.
    // i2c_mutex = xSemaphoreCreateMutex();
    // if (i2c_mutex == NULL) {
    //     Serial.println("[I2C] Failed to create I2C mutex - continuing without bus locking");
    // }
    
    // Initialize XBee UART (always initialize, controlled by comm_manager)
    XBeeSerial.begin(XBEE_BAUD_RATE, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
    debugln("[+] XBee UART initialized on Serial1/UART1 (RX=34, TX=32, 115200 baud)");
    Serial.printf("[XBEE INIT] UART1 - Pins: RX=%d, TX=%d, Baud=%d\n", XBEE_RX_PIN, XBEE_TX_PIN, XBEE_BAUD_RATE);
    
    // Verify UART is functional
    if (XBeeSerial) {
        Serial.println("[XBEE CHECK] UART1 is available ✓");
        
        // Send test message to verify XBee connection
        XBeeSerial.println("XBEE_TEST_STARTUP");
        Serial.println("[XBEE TEST] Sent startup test message");
        
        // Flush TX buffer to ensure data is sent
        XBeeSerial.flush();
        Serial.println("[XBEE TEST] TX buffer flushed - data transmitted");
        
        // Check if any data received (XBee in transparent mode won't respond, but this checks RX)
        if (XBeeSerial.available()) {
            Serial.print("[XBEE RX] Unexpected data received: ");
            while (XBeeSerial.available()) {
                Serial.write(XBeeSerial.read());
            }
            Serial.println();
        } else {
            Serial.println("[XBEE RX] No echo (expected in transparent mode)");
        }
        
        Serial.println("[XBEE STATUS] ✓ Initialization complete - check base station for received data");
    } else {
        Serial.println("[XBEE ERROR] ❌ UART1 initialization FAILED!");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::ERROR, system_log_file, "XBee UART1 init failed\r\n");
    }

    debugln("=========INITIALIZING FLIGHT COMPUTER============");
    LED_init();
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    buzzerInit();

    // Standard preflight baseline: ensure all pyro outputs start in safe OFF state.
    pinMode(DROGUE_PIN, OUTPUT);
    pinMode(MAIN_CHUTE_EJECT_PIN, OUTPUT);
    pinMode(REMOTE_SWITCH, OUTPUT);
    digitalWrite(DROGUE_PIN, LOW);
    digitalWrite(MAIN_CHUTE_EJECT_PIN, LOW);
    digitalWrite(REMOTE_SWITCH, LOW);

    // Determine TEST/FLIGHT mode from defs.h with optional grounded pin override.
    checkRunTestToggle();

    // Initialize operation mode to SAFE_MODE by default
    operation_mode = OPERATION_MODE::SAFE_MODE;

    /* core to run the tasks */
    uint8_t app_core_id = xPortGetCoreID();

	// 
	#if USE_SIMULATION
		if (SPIFFS.exists("/Flight Simulation from Open Rocket.csv")) {
			if (load_sim_file("/Flight Simulation from Open Rocket.csv")) {
				debugln("Simulation data ready.");
			}
		}
	#endif

    // SPIFFS Must be initialized first to allow event logging from the word go
    uint8_t spiffs_init_state = InitSPIFFS();
    g_spiffs_ready = (spiffs_init_state != 0);

    // SYSTEM LOG FILE
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::WRITE, "FC1", LOG_LEVEL::INFO, system_log_file, "Flight computer Event log\r\n");

    debugln();
    debugln(F("=============================================="));
    debugln(F("========= CREATING DYNAMIC WIFI ==========="));
    debugln(F("=============================================="));
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "==CREATING DYNAMIC WIFI==\r\n");
    
    #if BEACON_MODE_SAFETY_CHECKS
    // Safety check: Only initialize WiFi/MQTT if MQTT flag is enabled
    if (MQTT == 1) {
        debugln("[+] MQTT enabled - initializing WiFi and MQTT");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "MQTT enabled - full WiFi init\r\n");
        initDynamicWIFI(); // TODO - uncomment on live testing and production
    } else {
        debugln("[+] MQTT disabled - strict beacon mode (WiFi setup for ESP-NOW only)");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "Strict beacon mode - ESP-NOW WiFi setup\r\n");
        // Initialize WiFi for ESP-NOW in beacon-only mode using existing beacon setup
        uint8_t wifi_result = wifi_config.WifiConnect(true, ROCKET_MAC); // use_beacon_mode = true
        if(wifi_result) {
            debugln("[+] WiFi initialized for ESP-NOW beacon mode");
        } else {
            debugln("[-] WiFi beacon mode initialization failed");
        }
    }
    #else
    initDynamicWIFI(); // TODO - uncomment on live testing and production
    #endif

    #if MQTT

    // create and wait for dynamic WIFI connection - only if MQTT is enabled
    #if BEACON_MODE_SAFETY_CHECKS
    if (MQTT == 1) {
    #endif
        MQTTInit(wifi_config.getBaseStationIP(), wifi_config.getMQTTPort());
        MQTT_Reconnect();  
        debugln("[+]Dynamic WIFI created OK.");
    #if BEACON_MODE_SAFETY_CHECKS
    } else {
        debugln("[+] MQTT initialization skipped - running in strict beacon mode");
    }
    #endif

    #endif // MQTT


    debugln();
    debugln(F("=============================================="));
    debugln(F("========= INITIALIZING PERIPHERALS ==========="));
    debugln(F("=============================================="));
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "==Initializing peripherals==\r\n");
    
    // Initialize transmitter for beacon capability (will be managed by communication manager)
    // ESP-NOW requires WiFi to be initialized first (done above)
    debugln("[DEBUG] Starting ESP-NOW transmitter initialization...");
    if (transmitter.begin()) {
        debugln("[+] ESP-NOW transmitter initialized for communication manager");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "ESP-NOW transmitter initialized for comm manager\r\n");
    } else {
        debugln("[-] ESP-NOW transmitter initialization failed - beacon functionality disabled");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING, system_log_file, "ESP-NOW transmitter init failed\r\n");
    }
    
    // 🔥 INITIALIZE COMMUNICATION MANAGER for isolated mode control
    debugln("[DEBUG] Starting Communication Manager initialization...");
    comm_manager.init();
    debugln("[+] Communication Manager initialized with isolated mode control");
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "Communication Manager initialized\r\n");
    
    debugln("[DEBUG] Starting BMP initialization...");
    uint8_t bmp_init_state = BMPInit();
    g_bmp_ready = (bmp_init_state != 0);
    debugln("[DEBUG] Starting IMU initialization...");
    uint8_t imu_init_state = imu.init();
    g_imu_ready = (imu_init_state != 0);
    
    // // 🔥 Initialize DMP (Digital Motion Processor) for stable quaternion-based angles
    // debugln("[DEBUG] Starting DMP initialization...");
    // uint8_t dmp_init_state = imu.initDMP();
    // if (dmp_init_state == 0) {
    //     debugln("[+] DMP initialized successfully - using quaternion-derived angles");
    // } else {
    //     debugln("[-] DMP initialization failed - falling back to direct angle calculations");
    //     SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING,
    //                             system_log_file, "[-] DMP init failed - using legacy angle mode.\r\n");
    // }
    
    debugln("[DEBUG] Starting GPS initialization...");
    uint8_t gps_init_state = GPSInit();
    g_gps_ready = (gps_init_state != 0);
    
    debugln("[DEBUG] Starting ADS1115 initialization...");
    uint8_t ads_init_state = ADSInit();
    g_ads_ready = (ads_init_state != 0);
    if (ads_init_state) {
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                                system_log_file, "[+] ADS1115 init OK.\r\n");
    } else {
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::WARNING,
                                system_log_file, "[-] ADS1115 init failed - using fallback battery voltage.\r\n");
    }

    debugln("[DEBUG] Disabling all devices...");
    disableAllDevices();
    debugln("[DEBUG] Starting SD initialization...");
    uint8_t sd_init_state = initSD();
    debugln("[DEBUG] Disabling all devices again...");
    disableAllDevices();
    debugln("[DEBUG] Starting flash logger initialization...");
    #if ENABLE_FLASH_LOGGING
    uint8_t flash_init_state = data_logger.loggerInit();
    debug("Flash memory init state:"); debugln(flash_init_state);
    #else
    debugln("[DEBUG] Flash logging disabled - skipping flash logger initialization");
    uint8_t flash_init_state = 1; // Set to success since we're not using it
    #endif

    debugln(F("=============================================="));
    Serial.print("Available heap: ");
    debugln(F("=============================================="));
    Serial.println(esp_get_free_heap_size());
    debugln(F("=============================================="));
    debugln(F("=============================================="));
    Serial.print("Task stack watermark: ");
    debugln(F("=============================================="));
    Serial.println(uxTaskGetStackHighWaterMark(NULL));
    debugln(F("=============================================="));
    //For Debugging to be deleted
    esp_task_wdt_init(10, true);  // Set timeout to 10 seconds instead of default 5

    /* initialize mqtt */
    //MQTTInit(MQTT_SERVER, MQTT_PORT);

    /* update the sub-systems init state table */
    // check if BMP init OK
    // if(bmp_init_state) { 
    //     SUBSYSTEM_INIT_MASK |= (1 << BMP_CHECK_BIT);
    // }

    // // check if MPU init OK
    // if(imu_init_state)  {
    //     SUBSYSTEM_INIT_MASK |= (1 << IMU_CHECK_BIT);
    // }

    // // check if flash memory init OK
    // if (flash_init_state) {
    //     SUBSYSTEM_INIT_MASK |= (1 << FLASH_CHECK_BIT);
    // }

    // // check if GPS init OK
    // if(gps_init_state) {
    //     SUBSYSTEM_INIT_MASK |= (1 << GPS_CHECK_BIT);
    // }

    // // check if SD CARD init OK
    // if(sd_init_state) {
    //     SUBSYSTEM_INIT_MASK |= (1 << SD_CHECK_BIT);
    // } 

    // // check if SPIFFS init OK
    // if(spiffs_init_state) {
    //     SUBSYSTEM_INIT_MASK |= (1 << SPIFFS_CHECK_BIT);
    // }

    /* register the baseline pressure at launch site - check docs to see how this works */
    debugln(F("==============================================="));
    debugln(F("==================== BASELINE ================="));
    debugln(F("=============================================="));
    baseline = altimeter_get_pressure();
    Serial.print("\n");Serial.print("Baseline ");Serial.print(baseline);Serial.print("\n");
   

    /* initialize the ring buffer - used for apogee detection */
    ring_buffer_init(&altitude_ring_buffer);

    //Initialize Kalman Matrices 
    init_kalman_matrices();

    // TODO: if toggle pin in RUN mode, set to wait for arming 
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO,
                            system_log_file, g_test_mode ? "TEST MODE\r\n" : "FLIGHT MODE\r\n");
    
    debugln();
    debugln(F("=============================================="));
    debugln(F("===== INITIALIZING COMMUNICATION MANAGER ===="));
    debugln(F("=============================================="));
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "==INITIALIZING COMMUNICATION MANAGER==\r\n");
    
    // Initialize the smart communication manager
    comm_manager.init();
    debugln("[+] Communication Manager initialized");
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+] Communication Manager initialized\r\n");

    /* mode 0 resets the system log file by clearing all the current contents */
    // system_logger.logToFile(SPIFFS, 0, rocket_ID, level, system_log_file, "Game Time!"); // TODO: DEBUG

    debugln();
    debugln(F("=============================================="));
    debugln(F("===== INITIALIZING DATA LOGGING SYSTEM ======="));
    debugln(F("=============================================="));
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "==INITIALIZING DATA LOGGING SYSTEM==\r\n");
    sdLogger.begin();
    debug("===== SDlogger initialization =======");
        
    debugln();
    debugln(F("=============================================="));
    debugln(F("============== CREATING QUEUES ==============="));
    debugln(F("=============================================="));
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "==CREATING QUEUES==\r\n");

    /* Every producer task sends queue to a different queue to avoid data popping issue */
    // FIX: explicit queue lengths tuned for telemetry flow
    telemetry_data_queue_handle = xQueueCreate(4, sizeof(telemetry_type_t));           // MQTT queue (small buffer)
    log_to_mem_queue_handle = xQueueCreate(8, sizeof(telemetry_type_t));               // FIX: larger queue for slower flash/SD I/O
    check_state_queue_handle = xQueueCreate(4, sizeof(telemetry_type_t));             // state machine queue
    debug_to_term_queue_handle = xQueueCreate(2, sizeof(telemetry_type_t));           // terminal output (can be small)
    kalman_filter_queue_handle = xQueueCreate(1, sizeof(telemetry_type_t));           // FIX: latest-only (overwrite) for accel -> Kalman
    kalman2d_input_queue_handle = xQueueCreate(1, sizeof(float));                     // FIX: latest-only for altitude input to Kalman

    if(telemetry_data_queue_handle == NULL) {
        debugln("[-]telemetry_data_queue_handle creation failed");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[-]telemetry_data_queue_handle creation failed\r\n");
    } else {
        debugln("[+]telemetry_data_queue_handle creation OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]telemetry_data_queue_handle creation OK.\r\n");
    }

    if(log_to_mem_queue_handle == NULL) {
        debugln("[-]telemetry_data_queue_handle creation failed");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[-]telemetry_data_queue_handle creation failed\r\n");
    } else {
        debugln("[+]telemetry_data_queue_handle creation OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]telemetry_data_queue_handle creation OK.\r\n");
    }

    // CSV queue removed — logging centralized to `logToMemory` task

    if(check_state_queue_handle == NULL) {
        debugln("[-]check_state_queue_handle creation failed");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[-]check_state_queue_handle creation failed\r\n");
    } else {
        debugln("[+]check_state_queue_handle creation OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]check_state_queue_handle creation OK.\r\n");
    }

    if(debug_to_term_queue_handle == NULL) {
        debugln("[-]debug_to_term_queue_handle creation failed");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[-]debug_to_term_queue_handle creation failed\r\n");
    } else {
        debugln("[+]debug_to_term_queue_handle creation OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]debug_to_term_queue_handle creation OK.\r\n");
    }

    if(kalman_filter_queue_handle == NULL) {
        debugln("[-]kalman_filter_queue_handle creation failed");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[-]kalman_filter_queue_handle creation failed\r\n");
    } else {
        debugln("[+]kalman_filter_queue_handle creation OK.");
        SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "[+]kalman_filter_queue_handle creation OK.\r\n");
    }

    debugln();
    debugln(F("=============================================="));
    debugln(F("============== CREATING TASKS ==============="));
    debugln(F("==============================================\n"));
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "==CREATING TASKS==\r\n");

    /* Create tasks
    * All tasks have a stack size of 1024 words - not bytes!
    * ESP32 is 32 bit, therefore 32bits x 1024 = 4096 bytes
    * So the stack size is 4096 bytes
    * 
    * TASK CREATION PARAMETERS
    * function that executes this task
    * Function name - for debugging 
    * Stack depth in words 
    * parameter to be passed to the task 
    * Task priority 
    * task handle that can be passed to other tasks to reference the task 
    *
    *
    */


    xCreateAllTasks();


    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "==FINISHED CREATING TASKS==\r\n");
    SYSTEM_LOGGER.logToFile(SPIFFS, LOG_MODE::APPEND, "FC1", LOG_LEVEL::INFO, system_log_file, "\nEND OF INITIALIZATION\r\n");

    /* buzz to indicate start of setup */
    //blocking_buzz(BUZZ_INTERVALS::SETUP_INIT);

    //Simultaneous memory logging prerequisites
    pinMode(flash_cs_pin, OUTPUT);
    pinMode(SD_CS_PIN, OUTPUT);
    disableAllDevices();
    SPI.begin(18, 19, 23);
    
} /* End of setup */


/*!****************************************************************************
 * @brief Main loop
 *******************************************************************************/
void loop() {
    if (MQTT) {
        /* enable MQTT transmit loop */
        if (!client.connected()) {
            MQTT_Reconnect();
        }
        client.loop();
    }

    /* Update communication manager */
    comm_manager.update();
    
    /* Handle incoming commands from serial */
    handleIncomingCommands();

    /* check if the transmitter is armed */
    // Remove this line as it was overriding operation_mode set by ARM/DISARM commands
    // operation_mode = (transmitter.isArmed()) ? OPERATION_MODE::ARMED_MODE : OPERATION_MODE::SAFE_MODE;

    /* LED indication is handled by xOperationModeIndicateTask - no need to duplicate here */
    
    /* Feed the watchdog to prevent timeout */
    esp_task_wdt_reset();
    
    /* Add a small delay to prevent watchdog timeout */
    vTaskDelay(pdMS_TO_TICKS(10));
    
/* End of main loop*/
}
