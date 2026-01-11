#include <TinyGPS++.h>
#include <EEPROM.h>

#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD   9600

#define EEPROM_SIZE 512
#define STATUS_ADDR 0           // 1 byte: 0 = not tested, 1 = GPS OK
#define REBOOT_ADDR 1           // 4 bytes: reboot count
#define LOG_START_ADDR 10       // Start address for logs
#define LOG_ENTRY_SIZE 20       // 8 bytes lat, 8 bytes lng, 4 bytes unix time

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

unsigned long lastLogTime = 0;
int logIndex = 0;

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  EEPROM.begin(EEPROM_SIZE);

  // Increment reboot count
  uint32_t rebootCount = 0;
  EEPROM.get(REBOOT_ADDR, rebootCount);
  rebootCount++;
  EEPROM.put(REBOOT_ADDR, rebootCount);
  EEPROM.commit();

  Serial.println("GPS Test Station Started");

  // Find last log index
  logIndex = getLastLogIndex();
}

void loop() {
  // Handle serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("PRINT")) {
      printStatus();
    } else if (cmd.equalsIgnoreCase("FORMAT")) {
      formatEEPROM();
      Serial.println("EEPROM formatted.");
    }
  }

  // Read GPS data
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // If GPS fix and not yet marked as tested, mark as tested
  if (gps.location.isValid() && EEPROM.read(STATUS_ADDR) != 1) {
    EEPROM.write(STATUS_ADDR, 1);
    EEPROM.commit();
    Serial.println("GPS connected and fix acquired. Status saved to EEPROM.");
  }

  // Log location every hour if GPS fix is valid
  if (gps.location.isValid() && millis() - lastLogTime > 3600000UL) {
    lastLogTime = millis();
    logLocation(gps.location.lat(), gps.location.lng(), gps.time.value());
    Serial.println("Location logged to EEPROM:");
    Serial.print("Lat: "); Serial.println(gps.location.lat(), 6);
    Serial.print("Lng: "); Serial.println(gps.location.lng(), 6);
    Serial.print("Unix Time: "); Serial.println(gps.time.value());
  }

  delay(1000);
}

void logLocation(double lat, double lng, uint32_t unixTime) {
  int addr = LOG_START_ADDR + logIndex * LOG_ENTRY_SIZE;
  if (addr + LOG_ENTRY_SIZE > EEPROM_SIZE) {
    Serial.println("EEPROM full, cannot log more locations.");
    return;
  }
  EEPROM.put(addr, lat);
  EEPROM.put(addr + 8, lng);
  EEPROM.put(addr + 16, unixTime);
  EEPROM.commit();
  logIndex++;
}

int getLastLogIndex() {
  int idx = 0;
  while (true) {
    int addr = LOG_START_ADDR + idx * LOG_ENTRY_SIZE;
    double lat;
    EEPROM.get(addr, lat);
    if (isnan(lat) || addr + LOG_ENTRY_SIZE > EEPROM_SIZE) break;
    idx++;
  }
  return idx;
}

void printStatus() {
  // Connection status
  uint8_t status = EEPROM.read(STATUS_ADDR);
  Serial.print("GPS Connection Status: ");
  Serial.println(status == 1 ? "Connected" : "Not Connected");

  // Reboot count
  uint32_t rebootCount = 0;
  EEPROM.get(REBOOT_ADDR, rebootCount);
  Serial.print("ESP Reboot Count: ");
  Serial.println(rebootCount);

  // Logged locations
  Serial.println("Logged Locations:");
  for (int i = 0; i < logIndex; i++) {
    int addr = LOG_START_ADDR + i * LOG_ENTRY_SIZE;
    double lat, lng;
    uint32_t unixTime;
    EEPROM.get(addr, lat);
    EEPROM.get(addr + 8, lng);
    EEPROM.get(addr + 16, unixTime);
    if (isnan(lat)) break;
    Serial.print("Entry "); Serial.print(i + 1); Serial.print(": ");
    Serial.print("Lat: "); Serial.print(lat, 6);
    Serial.print(", Lng: "); Serial.print(lng, 6);
    Serial.print(", Unix Time: "); Serial.println(unixTime);
  }
}

void formatEEPROM() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);  // Standard erased state
  }
  EEPROM.commit();
  logIndex = 0;
}
