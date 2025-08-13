#include <SD.h>
#include <SPI.h>
#include <CustomSerialFlash.h>

// Assuming SD_CS is defined as per your SDFlashread.ino
#define SD_CS 26

// The filename to delete
const char* filename = "/datalog.txt"; ; 

void listSDCardFiles(const char* dirPath = "/", uint8_t numTabs = 0) {
  File root = SD.open(dirPath);
  if (!root) {
    Serial.println("❌ Failed to open directory!");
    return;
  }

  if (!root.isDirectory()) {
    Serial.println("❌ Not a directory!");
    root.close();
    return;
  }

  File file = root.openNextFile();
  while (file) {
    for (uint8_t i = 0; i < numTabs; i++) Serial.print('\t');

    if (file.isDirectory()) {
      Serial.print("📁 ");
      Serial.println(file.name());
      listSDCardFiles(file.name(), numTabs + 1);  // Recursive listing
    } else {
      Serial.print("📄 ");
      Serial.print(file.name());
      Serial.print("\t\t");
      Serial.print(file.size());
      Serial.println(" bytes");
    }

    file = root.openNextFile();
  }

  root.close();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize SPI and SD card
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SPI.begin(18, 19, 23); // SPI_SCK, SPI_MISO, SPI_MOSI [cite: 1]

  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD card initialization failed!");
    while (1);
  }
  Serial.println("✅ SD card initialized.");

  // Check if the file exists and delete it
  if (SD.exists(filename)) {
    Serial.printf("📄 File %s found. Deleting...\n", filename);
    if (SD.remove(filename)) {
      Serial.println("✅ File deleted successfully.");
    } else {
      Serial.println("❌ Failed to delete file.");
    }
  } else {
    Serial.println("⏩ File not found. Nothing to delete.");
  }
  listSDCardFiles();
  Serial.println("✅ \Listed all files");
}

void loop() {
  // Your main loop code
}