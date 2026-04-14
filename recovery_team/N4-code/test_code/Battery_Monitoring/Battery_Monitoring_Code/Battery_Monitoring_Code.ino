#include <Wire.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;

float batteryVoltage;
int chute1, chute2;

void setup() {
  Serial.begin(9600);
  Wire.begin();

  ads.begin();
  ads.setGain(GAIN_ONE);  // ±4.096 V
}

void loop() {
  int16_t adcBattery = ads.readADC_SingleEnded(0); // Battery
  int16_t adcChute1  = ads.readADC_SingleEnded(1); // Chute 1
  int16_t adcChute2  = ads.readADC_SingleEnded(2); // Chute 2

  // Convert ADC to voltage at ADS pin
  float vADC = adcBattery * 4.096 / 32768.0;

  // Reverse voltage divider (47k / 10k)
  batteryVoltage = vADC * (57.0 / 10.0);

  int batteryPercent = constrain(
    map((int)(batteryVoltage * 100), 1200, 1680, 0, 100),
    0, 100
  );

  chute1 = (adcChute1 > 1000) ? 1 : 0;
  chute2 = (adcChute2 > 1000) ? 1 : 0;

  Serial.print("Battery: ");
  Serial.print(batteryVoltage, 2);
  Serial.print(" V (");
  Serial.print(batteryPercent);
  Serial.print("%)");

  Serial.print(" | Chute1: ");
  Serial.print(chute1 ? "ON" : "OFF");
  Serial.print(" | Chute2: ");
  Serial.println(chute2 ? "ON" : "OFF");

  delay(1000);
}
