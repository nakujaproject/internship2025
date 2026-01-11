#include <WiFi.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    Serial.print("Station MAC: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", mac[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();

    WiFi.softAPmacAddress(mac);
    Serial.print("SoftAP MAC: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", mac[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
}

void loop() {}