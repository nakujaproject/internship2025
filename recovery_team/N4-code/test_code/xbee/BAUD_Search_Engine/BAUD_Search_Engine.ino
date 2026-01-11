#include <SoftwareSerial.h>

// Define the pins for the XBee
// Arduino Pin 2 is RX (Connect to XBee DOUT)
// Arduino Pin 3 is TX (Connect to XBee DIN)
SoftwareSerial xbeeSerial(2, 3);

// List of baud rates to check
long baudRates[] = {9600, 19200, 38400, 57600, 115200};
int numRates = 5;

void setup() {
  // Start the Serial Monitor to report back to you
  Serial.begin(9600);
  while (!Serial) {
    ; // Wait for serial port to connect
  }
  Serial.println("--- XBee Baud Rate Scanner Started ---");
}

void loop() {
  for (int i = 0; i < numRates; i++) {
    long currentRate = baudRates[i];
    
    Serial.print("Testing Baud Rate: ");
    Serial.println(currentRate);
    
    // 1. Open connection at this speed
    xbeeSerial.begin(currentRate);
    
    // 2. Clear buffer
    while(xbeeSerial.available()) xbeeSerial.read();
    
    // 3. Send "Enter Command Mode" Sequence
    // The sequence is: Silence (1s) -> "+++" -> Silence (1s)
    delay(1100); 
    xbeeSerial.print("+++");
    delay(1100); 
    
    // 4. Check for "OK" response
    if (xbeeSerial.available() > 1) {
      char c1 = xbeeSerial.read();
      char c2 = xbeeSerial.read();
      
      if (c1 == 'O' && c2 == 'K') {
        Serial.println("******************************************");
        Serial.print("SUCCESS! XBee found at baud rate: ");
        Serial.println(currentRate);
        Serial.println("******************************************");
        
        // Stop here if found
        while(1); 
      }
    }
    
    // Close to try next rate
    xbeeSerial.end();
  }
  
  Serial.println("Cycle complete. Retrying...");
  delay(2000);
}