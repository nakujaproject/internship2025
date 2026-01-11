void setup() {
  // Start the serial communication.
  // IMPORTANT: Ensure this matches the baud rate you set in XCTU (default is often 9600).
  Serial.begin(9600); 
}

void loop() {
  // Check if data is coming in from the XBee
  if (Serial.available() > 0) {
    // Read the incoming byte
    char incomingByte = Serial.read();
    
    // Print it to the Serial Monitor
    Serial.write(incomingByte);
  }
}