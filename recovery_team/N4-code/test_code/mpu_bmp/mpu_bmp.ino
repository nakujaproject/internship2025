#include "Wire.h"
#include <MPU6050_light.h>
#include <Adafruit_BMP085.h>

// MPU6050 setup
MPU6050 mpu(Wire);

// BMP180 setup
Adafruit_BMP085 bmp;

// Status flags
bool mpuOK = false;
bool bmpOK = false;

long timer = 0;

void setup() {
  Serial.begin(9600);
  delay(1000);

  // Initialize I2C on ESP32 (SDA = 21, SCL = 22)
  Wire.begin(21, 22);
  Serial.println("[INFO] Starting sensor initialization...\n");

  // === MPU6050 ===
  byte status = mpu.begin();
  if (status == 0) {
    Serial.println("[OK] MPU6050 connected.");
    mpu.calcOffsets(true, true);
    Serial.println("[OK] MPU6050 calibration complete.");
    mpuOK = true;
  } else {
    Serial.print("[ERROR] MPU6050 connection failed. Status: ");
    Serial.println(status);
    mpuOK = false;
  }

  // === BMP180 ===
  if (bmp.begin()) {
    Serial.println("[OK] BMP180 connected.");
    bmpOK = true;
  } else {
    Serial.println("[ERROR] BMP180 not detected. Check wiring.");
    bmpOK = false;
  }

  Serial.println("\n[INFO] Setup complete.\n");
}

void loop() {
  if (millis() - timer > 1000) {
    Serial.println("===== Sensor Data =====");

    // --- MPU6050 block ---
    if (mpuOK) {
      mpu.update();
      Serial.println("[MPU6050]");
      Serial.print("Temp (°C): "); Serial.println(mpu.getTemp());
      Serial.print("Accel X: "); Serial.print(mpu.getAccX());
      Serial.print("\tY: "); Serial.print(mpu.getAccY());
      Serial.print("\tZ: "); Serial.println(mpu.getAccZ());
      Serial.print("Gyro X: "); Serial.print(mpu.getGyroX());
      Serial.print("\tY: "); Serial.print(mpu.getGyroY());
      Serial.print("\tZ: "); Serial.println(mpu.getGyroZ());
      Serial.print("Angle X: "); Serial.print(mpu.getAngleX());
      Serial.print("\tY: "); Serial.print(mpu.getAngleY());
      Serial.print("\tZ: "); Serial.println(mpu.getAngleZ());
    } else {
      Serial.println("[WARNING] MPU6050 not available.");
    }

    // --- BMP180 block ---
    if (bmpOK) {
      Serial.println("[BMP180]");
      Serial.print("Temperature (°C): "); Serial.println(bmp.readTemperature());
      Serial.print("Pressure (Pa): "); Serial.println(bmp.readPressure());
      Serial.print("Altitude (m): "); Serial.println(bmp.readAltitude(101325));
    } else {
      Serial.println("[WARNING] BMP180 not available.");
    }

    Serial.println("========================\n");

    timer = millis();
  }

  // Optional: retry initialization if a sensor was missing
  static unsigned long lastRetry = 0;
  if (!mpuOK && millis() - lastRetry > 5000) {
    Serial.println("[INFO] Retrying MPU6050...");
    byte status = mpu.begin();
    if (status == 0) {
      Serial.println("[OK] MPU6050 recovered.");
      mpu.calcOffsets(true, true);
      mpuOK = true;
    }
    lastRetry = millis();
  }

  if (!bmpOK && millis() - lastRetry > 5000) {
    Serial.println("[INFO] Retrying BMP180...");
    if (bmp.begin()) {
      Serial.println("[OK] BMP180 recovered.");
      bmpOK = true;
    }
    lastRetry = millis();
  }
}
