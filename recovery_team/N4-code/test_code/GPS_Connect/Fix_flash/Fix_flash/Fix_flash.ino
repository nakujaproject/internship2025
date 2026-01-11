#include <TinyGPS++.h>
#include <SPI.h>
#include <string.h>

// GPS Configuration
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD   9600

// SPI Flash Configuration
const int FlashCS = 5;   // Chip Select
const int FlashWP = 22;  // Write Protect (pull high)
const int FlashHOLD = 21; // Hold (pull high)
const uint32_t CHIP_SIZE = 1048576; // 1MB flash

// Custom MIN/MAX macros
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// File Configuration
#define DATA_FILE "gps_data.txt"
#define MAX_LINE_LENGTH 128

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
unsigned long lastLogTime = 0;
uint32_t rebootCount = 0;
bool gpsStatus = false;

// Function prototypes
void writeToFlash(const char* data);
void appendToFlash(const char* data);
void readFromFlash();
void eraseFlash();
bool waitReady(uint32_t timeout = 500);
void eraseSector(uint32_t address);
void writeData(uint32_t address, const void* data, uint32_t len);
void readData(uint32_t address, void* data, uint32_t len);

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // Initialize SPI Flash pins
  pinMode(FlashCS, OUTPUT);
  pinMode(FlashWP, OUTPUT);
  pinMode(FlashHOLD, OUTPUT);
  digitalWrite(FlashCS, HIGH);
  digitalWrite(FlashWP, HIGH);
  digitalWrite(FlashHOLD, HIGH);
  
  SPI.begin();
  delay(100);

  // Initialize file system
  readFromFlash(); // Load existing data

  // Increment reboot count
  rebootCount++;
  updateStatusFile();
  
  Serial.print("GPS Logger Started. Reboot count: ");
  Serial.println(rebootCount);
}

void loop() {
  // Handle serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("PRINT")) {
      printStatus();
    } else if (cmd.equalsIgnoreCase("FORMAT")) {
      eraseFlash();
      Serial.println("Flash memory erased");
    } else if (cmd.equalsIgnoreCase("EXTRACT")) {
      extractData();
    }
  }

  // Read GPS data
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Update GPS status
  if (gps.location.isValid() && !gpsStatus) {
    gpsStatus = true;
    updateStatusFile();
    Serial.println("GPS connected and fix acquired.");
  }

  // Log location every hour if GPS fix is valid
  if (gps.location.isValid() && millis() - lastLogTime > 3600000UL) {
    lastLogTime = millis();
    logLocation();
  }

  delay(1000);
}

void updateStatusFile() {
  char buffer[MAX_LINE_LENGTH];
  snprintf(buffer, MAX_LINE_LENGTH, "STATUS:GPS=%d,REBOOTS=%lu\n", gpsStatus ? 1 : 0, rebootCount);
  writeToFlash(buffer);
}

void logLocation() {
  char buffer[MAX_LINE_LENGTH];
  snprintf(buffer, MAX_LINE_LENGTH, "LOG:LAT=%.6f,LNG=%.6f,TIME=%lu\n", 
          gps.location.lat(), gps.location.lng(), gps.time.value());
  appendToFlash(buffer);
  
  Serial.println("Location logged:");
  Serial.print("Lat: "); Serial.println(gps.location.lat(), 6);
  Serial.print("Lng: "); Serial.println(gps.location.lng(), 6);
  Serial.print("Unix Time: "); Serial.println(gps.time.value());
}

void printStatus() {
  Serial.println("\nCurrent Status:");
  Serial.println("==============");
  Serial.print("GPS Status: ");
  Serial.println(gpsStatus ? "Connected" : "Not Connected");
  Serial.print("Reboot Count: ");
  Serial.println(rebootCount);
  Serial.print("Last Location: ");
  if (gps.location.isValid()) {
    Serial.print(gps.location.lat(), 6);
    Serial.print(", ");
    Serial.println(gps.location.lng(), 6);
  } else {
    Serial.println("No fix");
  }
  Serial.println("\nUse 'EXTRACT' command to view all stored data");
}

void extractData() {
  Serial.println("\nExtracting all stored data:");
  Serial.println("=========================");
  readFromFlash();
}

// ================== FLASH STORAGE FUNCTIONS ================== //
void writeToFlash(const char* data) {
  // Erase the first sector (4KB) where we'll store our data
  eraseSector(0);
  
  // Write the data
  writeData(0, data, strlen(data));
}

void appendToFlash(const char* data) {
  // Read existing data to find the end
  char existingData[CHIP_SIZE] = {0};
  readData(0, existingData, CHIP_SIZE);
  size_t currentLength = strlen(existingData);
  
  // Check if we have space
  if (currentLength + strlen(data) >= CHIP_SIZE) {
    Serial.println("Flash memory full!");
    return;
  }
  
  // Append new data
  strcat(existingData, data);
  
  // Write back the combined data
  writeToFlash(existingData);
}

void readFromFlash() {
  char data[CHIP_SIZE] = {0};
  readData(0, data, CHIP_SIZE);
  
  // Parse the data
  char* line = strtok(data, "\n");
  while (line != NULL) {
    if (strncmp(line, "STATUS:", 7) == 0) {
      // Parse status line
      char* part = strtok(line + 7, ",");
      while (part != NULL) {
        if (strncmp(part, "GPS=", 4) == 0) {
          gpsStatus = atoi(part + 4) == 1;
        } else if (strncmp(part, "REBOOTS=", 8) == 0) {
          rebootCount = atol(part + 8);
        }
        part = strtok(NULL, ",");
      }
    } else if (strncmp(line, "LOG:", 4) == 0) {
      // Display log entries
      Serial.println(line);
    }
    line = strtok(NULL, "\n");
  }
}

void eraseFlash() {
  // Simple implementation - just erase the first sector
  eraseSector(0);
  rebootCount = 0;
  gpsStatus = false;
}

// ================== LOW-LEVEL FLASH OPERATIONS ================== //
void writeEnable() {
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x06); // WREN
  digitalWrite(FlashCS, HIGH);
  delayMicroseconds(5);
}

bool waitReady(uint32_t timeout) {
  uint32_t start = millis();
  while (millis() - start < timeout) {
    digitalWrite(FlashCS, LOW);
    SPI.transfer(0x05); // Read status
    uint8_t status = SPI.transfer(0);
    digitalWrite(FlashCS, HIGH);
    
    if (!(status & 0x01)) return true;
    delay(1);
  }
  return false;
}

void eraseSector(uint32_t address) {
  if (address >= CHIP_SIZE) return;
  
  writeEnable();
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x20); // Sector erase
  SPI.transfer(address >> 16);
  SPI.transfer(address >> 8);
  SPI.transfer(address);
  digitalWrite(FlashCS, HIGH);
  
  if (!waitReady(5000)) {
    Serial.println("Erase timeout!");
  }
}

void writeData(uint32_t address, const void* data, uint32_t len) {
  if (address >= CHIP_SIZE) return;
  len = MIN(len, CHIP_SIZE - address);
  
  const uint8_t* p = (const uint8_t*)data;
  uint32_t bytes_written = 0;
  
  while (len > 0) {
    writeEnable();
    
    // Calculate chunk size (max 256 bytes, page aligned)
    uint32_t chunk = MIN(len, 256);
    uint32_t page_end = (address | 0xFF) + 1;
    chunk = MIN(chunk, page_end - address);
    
    digitalWrite(FlashCS, LOW);
    SPI.transfer(0x02); // Page program
    SPI.transfer(address >> 16);
    SPI.transfer(address >> 8);
    SPI.transfer(address);
    
    for (uint32_t i = 0; i < chunk; i++) {
      SPI.transfer(*p++);
    }
    
    digitalWrite(FlashCS, HIGH);
    
    if (!waitReady(500)) {
      Serial.println("Write timeout!");
      break;
    }
    
    address += chunk;
    len -= chunk;
    bytes_written += chunk;
  }
}

void readData(uint32_t address, void* data, uint32_t len) {
  if (address >= CHIP_SIZE) return;
  len = MIN(len, CHIP_SIZE - address);
  
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x03); // Read data
  SPI.transfer(address >> 16);
  SPI.transfer(address >> 8);
  SPI.transfer(address);
  
  uint8_t* p = (uint8_t*)data;
  while (len--) {
    *p++ = SPI.transfer(0);
  }
  
  digitalWrite(FlashCS, HIGH);
}