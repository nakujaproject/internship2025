#include <SPI.h>

// --- PINS ---
const int CS_PIN = 5;

// SPI Config: 1MHz, MSB First, Mode 0
SPISettings xbeeSPI(1000000, MSBFIRST, SPI_MODE0);

void setup() {
  Serial.begin(115200);
  
  // Setup Chip Select
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH); // Default to OFF

  // Start SPI Bus
  SPI.begin(); // Uses default VSPI pins (18, 19, 23)
  
  Serial.println("--- Simple SPI Sender Started ---");
  delay(1000);
}

void loop() {
  Serial.println("Sending 'Hello World'...");
  sendStringPacket("Hello World");
  delay(1000); // Wait 1 second
}

void sendStringPacket(String msg) {
  int payloadLen = msg.length();
  int totalLen = 14 + payloadLen; // 14 bytes API Overhead
  long checksumTotal = 0;

  SPI.beginTransaction(xbeeSPI);
  
  // 1. Wake up XBee
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(100); 

  // 2. Header
  SPI.transfer(0x7E); // Start
  SPI.transfer((totalLen >> 8) & 0xFF); // Length MSB
  SPI.transfer(totalLen & 0xFF);        // Length LSB

  // 3. API Frame Data
  // Frame Type 0x10 (Transmit Request)
  SPI.transfer(0x10); checksumTotal += 0x10; 
  // Frame ID (0 = No Response)
  SPI.transfer(0x00); checksumTotal += 0x00;

  // Destination (Broadcast)
  for(int i=0; i<6; i++) { SPI.transfer(0x00); checksumTotal += 0x00; }
  SPI.transfer(0xFF); checksumTotal += 0xFF;
  SPI.transfer(0xFF); checksumTotal += 0xFF;

  // 16-bit Dest & Options
  SPI.transfer(0xFF); checksumTotal += 0xFF;
  SPI.transfer(0xFE); checksumTotal += 0xFE;
  SPI.transfer(0x00); checksumTotal += 0x00;
  SPI.transfer(0x00); checksumTotal += 0x00;

  // 4. The Message Payload
  for (int i = 0; i < payloadLen; i++) {
    char c = msg.charAt(i);
    SPI.transfer(c);
    checksumTotal += c;
  }

  // 5. Checksum
  byte checksum = 0xFF - (checksumTotal & 0xFF);
  SPI.transfer(checksum);

  // 6. Release XBee
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
}