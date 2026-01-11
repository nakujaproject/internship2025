#include <SoftwareSerial.h>

// RX (Connect to XBee DOUT), TX (Connect to XBee DIN)
SoftwareSerial xbee(2, 3);

long baudRates[] = {9600, 19200, 38400, 57600}; // SoftwareSerial is unstable above 57600
int numRates = 4;

void setup() {
  Serial.begin(9600);
  while (!Serial);
  Serial.println("=== XBee Detective Started ===");
  Serial.println("Connect XBee DOUT to Pin 2, DIN to Pin 3.");
}

void loop() {
  for (int i = 0; i < numRates; i++) {
    long speed = baudRates[i];
    Serial.println("------------------------------------------------");
    Serial.print("Probing at Baud Rate: ");
    Serial.println(speed);
    
    xbee.begin(speed);
    
    // 1. Try to wake it up (Send dummy 0xFF bytes)
    xbee.write(0xFF);
    delay(10);
    
    // Clear buffer
    while(xbee.available()) xbee.read();

    // 2. Send Command Mode Sequence
    Serial.print("Attempting to enter Command Mode (+++)...");
    delay(1100);  // Guard time before
    xbee.print("+++");
    delay(1100);  // Guard time after
    
    // 3. Listen for response
    if (xbee.available()) {
      Serial.println(" RESPONSE RECEIVED!");
      Serial.print("Raw Hex: ");
      
      String response = "";
      while(xbee.available()) {
        char c = xbee.read();
        response += c;
        // Print Hex representation
        if (c < 16) Serial.print("0");
        Serial.print(c, HEX);
        Serial.print(" ");
        delay(10);
      }
      Serial.println();
      Serial.print("ASCII: ");
      Serial.println(response);

      // Analyze the response
      if (response.indexOf("OK") >= 0) {
        Serial.println(">> SUCCESS: AT Command Mode Active!");
        readSpecs(); // Call function to get details
        while(1); // Stop here, we found it!
      } else if (response.indexOf("\x7E") >= 0 || response.indexOf("7E") >= 0) { // 0x7E is '~'
        Serial.println(">> WARNING: Module is likely in API MODE (Detected 0x7E)");
        Serial.println(">> You must use XCTU 'Recovery' but select 'API' in firmware options.");
        while(1);
      } else {
        Serial.println(">> Data received, but not 'OK'. Might be noise or weird config.");
      }
    } else {
      Serial.println(" No Response.");
    }
    
    xbee.end();
    delay(500);
  }
  Serial.println("\nCycle complete. Restarting scan...\n");
  delay(2000);
}

void readSpecs() {
  Serial.println("\n--- EXTRACTING SPECIFICATIONS ---");
  sendCommand("ATVR"); // Firmware Version
  sendCommand("ATID"); // Network ID
  sendCommand("ATSH"); // Serial High
  sendCommand("ATSL"); // Serial Low
  sendCommand("ATBD"); // Baud Rate Setting
  sendCommand("ATAP"); // API Mode Setting
  Serial.println("---------------------------------");
}

void sendCommand(String cmd) {
  xbee.print(cmd);
  xbee.write('\r'); // Send Carriage Return
  delay(100);
  
  Serial.print(cmd);
  Serial.print(" -> ");
  while(xbee.available()) {
    char c = xbee.read();
    if(c >= 32 && c <= 126) Serial.print(c); // Only print printable chars
  }
  Serial.println();
}