#include <SPI.h>

// --- PINS (ESP32 VSPI) ---
const int CS_PIN = 5;
const int ATTN_PIN = 4;
const int LED_PIN = 2; // Onboard LED

// --- DATA STRUCTURE ---
// MUST MATCH RECEIVER EXACTLY
struct __attribute__((packed)) TelemetryData {
    uint32_t record_number;
    uint8_t operation_mode;
    uint8_t state;
    float ax, ay, az;
    float pitch, roll;
    float gx, gy, gz;
    float latitude, longitude, gps_altitude;
    float pressure, temperature, altitude_agl;
    uint8_t drogue_pin_state, main_chute_pin_state;
};

SPISettings xbeeSPI(1000000, MSBFIRST, SPI_MODE0);
unsigned long lastTxTime = 0;

void setup() {
  Serial.begin(115200);
  
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  pinMode(ATTN_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  
  SPI.begin();
  
  Serial.println("--- ESP32 SPI Sender Started ---");
}

void loop() {
  // LED Logic: Light up if XBee ATTN is Active (Low)
  digitalWrite(LED_PIN, !digitalRead(ATTN_PIN));

  // Send packet every 1 second
  if (millis() - lastTxTime >= 1000) {
    lastTxTime = millis();
    sendTelemetry();
  }
}

void sendTelemetry() {
  static TelemetryData data;
  static uint32_t counter = 0;

  // --- 1. SIMULATE SENSOR DATA ---
  data.record_number = counter++;
  data.operation_mode = 1; // ARMED
  data.state = 5;          // DROGUE_DESCENT
  
  // Simulate some movement
  data.ax = 0.05; data.ay = 9.81; data.az = 0.12;
  data.pitch = 12.5; data.roll = -3.2;
  
  data.latitude = -1.2921; data.longitude = 36.8219;
  data.altitude_agl = 150.0 + (counter * 0.5); // Rising altitude
  data.pressure = 101325; data.temperature = 26.5;

  Serial.print("Sending Packet #"); Serial.println(data.record_number);

  // --- 2. SEND VIA SPI ---
  sendXBeeFrame((uint8_t*)&data, sizeof(TelemetryData));
}

void sendXBeeFrame(uint8_t* data, int length) {
  int totalLen = 14 + length;
  long checksumTotal = 0;
  
  SPI.beginTransaction(xbeeSPI);
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(50);
  
  // Header
  SPI.transfer(0x7E);
  SPI.transfer((totalLen >> 8) & 0xFF);
  SPI.transfer(totalLen & 0xFF);
  
  // Frame Type & ID
  SPI.transfer(0x10); checksumTotal += 0x10;
  SPI.transfer(0x00); checksumTotal += 0x00;
  
  // Address (Broadcast)
  for(int i=0; i<6; i++) { SPI.transfer(0x00); checksumTotal += 0x00; }
  SPI.transfer(0xFF); checksumTotal += 0xFF;
  SPI.transfer(0xFF); checksumTotal += 0xFF;
  
  // Options
  SPI.transfer(0xFF); checksumTotal += 0xFF;
  SPI.transfer(0xFE); checksumTotal += 0xFE;
  SPI.transfer(0x00); checksumTotal += 0x00;
  SPI.transfer(0x00); checksumTotal += 0x00;
  
  // Payload
  for (int i = 0; i < length; i++) {
    SPI.transfer(data[i]);
    checksumTotal += data[i];
  }
  
  // Checksum
  SPI.transfer(0xFF - (checksumTotal & 0xFF));
  
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
}