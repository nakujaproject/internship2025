#include <SoftwareSerial.h>

// RX=2, TX=3
SoftwareSerial xbee(2, 3);

void setup() {
  Serial.begin(9600);
  xbee.begin(9600);
  Serial.println("--- Receiver Waiting ---");
}

void loop() {
  if (xbee.available()) {
    String data = xbee.readStringUntil('\n'); // Read until newline (if sent)
    // Or just read everything:
    if (data.length() > 0) {
       Serial.print("RECEIVED: ");
       Serial.println(data);
    }
    // Simple blink to show activity
    digitalWrite(13, HIGH); delay(50); digitalWrite(13, LOW);
  }
}