#include <SoftwareSerial.h>

// RX (Connect to XBee DOUT), TX (Connect to XBee DIN)
SoftwareSerial xbee(2, 3);

int counter = 0;

void setup() {
  // Debug Serial (To PC)
  Serial.begin(9600);
  
  // XBee Serial (Wireless)
  xbee.begin(9600);
  
  Serial.println("--- XBee Sender Started ---");
}

void loop() {
  // Create a message
  String msg = "Packet #" + String(counter);
  
  // 1. Send wirelessly
  xbee.println(msg); 
  
  // 2. Print to PC for confirmation
  Serial.print("Sent: ");
  Serial.println(msg);
  
  // Increment and wait
  counter++;
  delay(1000);
}