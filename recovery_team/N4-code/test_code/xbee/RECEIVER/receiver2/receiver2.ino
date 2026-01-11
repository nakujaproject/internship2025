#include <SoftwareSerial.h>

// RX (Connect to XBee DOUT), TX (Connect to XBee DIN)
SoftwareSerial xbee(2, 3);

void setup() {
  // Debug Serial (To PC)
  Serial.begin(9600);
  
  // XBee Serial (Wireless)
  xbee.begin(9600);
  
  Serial.println("--- XBee Receiver Listening ---");
}

void loop() {
  // If data is waiting in the XBee buffer
  if (xbee.available()) {
    // Read the incoming line
    String receivedMsg = xbee.readStringUntil('\n');
    
    // Print it to the PC
    Serial.print("RECEIVED: ");
    Serial.println(receivedMsg);
    
    // Optional: Flash LED on receipt
    digitalWrite(13, HIGH);
    delay(50);
    digitalWrite(13, LOW);
  }
}