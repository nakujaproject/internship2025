#include <TinyGPS++.h>

#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD   9600

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
bool fixPrinted = false;

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("Waiting for GPS fix...");
}

void loop() {
  // Feed GPS data to the library
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // When we get a valid fix and haven't printed yet
  if (gps.location.isValid() && !fixPrinted) {
    fixPrinted = true;
    
    Serial.println("\nGPS SUCCESS - Fix acquired!");
    Serial.println("----------------------------");
    
    // Location data
    Serial.print("Latitude:  "); Serial.println(gps.location.lat(), 6);
    Serial.print("Longitude: "); Serial.println(gps.location.lng(), 6);
    Serial.print("Altitude:  "); Serial.print(gps.altitude.meters()); Serial.println(" meters");
    
    // Time data
    if (gps.time.isValid()) {
      Serial.print("Time:      ");
      Serial.print(gps.time.hour()); Serial.print(":");
      Serial.print(gps.time.minute()); Serial.print(":");
      Serial.println(gps.time.second());
      
      Serial.print("Date:      ");
      Serial.print(gps.date.day()); Serial.print("/");
      Serial.print(gps.date.month()); Serial.print("/");
      Serial.println(gps.date.year());
    }
    
    // Additional info
    Serial.print("Speed:     "); Serial.print(gps.speed.kmph()); Serial.println(" km/h");
    Serial.print("Course:    "); Serial.print(gps.course.deg()); Serial.println("°");
    Serial.print("Satellites:"); Serial.println(gps.satellites.value());
    Serial.print("HDOP:      "); Serial.println(gps.hdop.value()/100.0, 2);
    Serial.println("----------------------------\n");
  }

  // Optional: Print updates when we lose fix
  if (!gps.location.isValid() && fixPrinted) {
    fixPrinted = false;
    Serial.println("Lost GPS fix... waiting to reacquire");
  }

  delay(100);
}