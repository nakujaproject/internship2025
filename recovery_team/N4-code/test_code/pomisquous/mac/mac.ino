#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("ESP MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void loop() {
  // Nothing to do here
}