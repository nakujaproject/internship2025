#include <HardwareSerial.h>

HardwareSerial BTSerial(2);

#define BT_RX 16
#define BT_TX 17

String rxBuffer = "";  // buffer for incoming BT data

void setup() {
  Serial.begin(115200);
  delay(1000);

  // HC-05 AT mode default baud = 38400
  BTSerial.begin(38400, SERIAL_8N1, BT_RX, BT_TX);

  Serial.println("=== ESP32 ↔ Bluetooth UART Monitor ===");
  Serial.println("RX: GPIO16  TX: GPIO17");
  Serial.println("BT Baud: 38400");
  Serial.println("Line Ending: Both NL & CR");
  Serial.println("=====================================");
}

void loop() {
  // ---------------------
  // Forward from PC → BT
  // ---------------------
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n'); // read a full line
    cmd.trim();                                // remove \r or whitespace

    if (cmd.length() > 0) {
      Serial.print("[TX → BT] ");
      Serial.println(cmd);                     // print nicely

      BTSerial.print(cmd);                     // send command
      BTSerial.print("\r\n");                  // ensure AT command line ending
    }
  }

  // ---------------------
  // Forward from BT → PC
  // ---------------------
  while (BTSerial.available()) {
    char c = BTSerial.read();
    rxBuffer += c;

    // Print full line once we get newline
    if (c == '\n') {
      rxBuffer.trim();                         // remove \r and \n
      if (rxBuffer.length() > 0) {
        Serial.print("[RX ← BT] ");
        Serial.println(rxBuffer);
      }
      rxBuffer = "";                           // reset buffer
    }
  }
}
