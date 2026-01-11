#include <SPI.h>

// Pin Definitions
const int FlashCS = 5;    // Chip Select
const int FlashWP = 22;   // Write Protect
const int FlashHOLD = 21; // Hold

// Memory Parameters
const uint32_t CHIP_SIZE = 16777216;  // 16MB
const uint32_t SECTOR_SIZE = 4096;    // 4KB
const uint32_t BLOCK_SIZE = 65536;    // 64KB
const uint32_t TEST_STEP = 131072;    // 128KB test interval

// Test Patterns
const byte patterns[] = {0x55, 0xAA, 0x33, 0xCC, 0x0F, 0xF0, 0xFF, 0x00};

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  // Initialize pins
  pinMode(FlashCS, OUTPUT);
  pinMode(FlashWP, OUTPUT);
  pinMode(FlashHOLD, OUTPUT);
  digitalWrite(FlashCS, HIGH);
  digitalWrite(FlashWP, HIGH);
  digitalWrite(FlashHOLD, HIGH);
  
  SPI.begin();
  delay(100); // Stabilization time

  Serial.println("\nSPI Flash Comprehensive Tester");
  Serial.println("============================");
  
  // Check basic communication
  if (!checkCommunication()) {
    Serial.println("\nERROR: No communication with flash chip!");
    suggestTroubleshooting();
    while(1);
  }

  // Run full test suite
  runFullTest();
}

void loop() {} // Nothing here

// ================== TEST SUITE ================== //

void runFullTest() {
  // 1. Basic functionality test
  Serial.println("\n[1] Basic Functionality Test");
  if (!basicFunctionTest()) {
    Serial.println("\nABORTING: Basic tests failed");
    return;
  }

  // 2. Full capacity verification
  Serial.println("\n[2] Full Capacity Verification");
  fullCapacityTest();

  // 3. Stress test
  Serial.println("\n[3] Stress Test");
  stressTest();

  // 4. Final verification
  Serial.println("\n[4] Final Verification");
  finalCheck();
}

// ================== CORE TEST FUNCTIONS ================== //

bool basicFunctionTest() {
  // Test 1.1: JEDEC ID
  Serial.println("- Reading JEDEC ID...");
  byte id[3];
  readJEDEC(id);
  Serial.printf("  JEDEC ID: %02X %02X %02X\n", id[0], id[1], id[2]);

  // Test 1.2: Status register
  Serial.println("- Checking status register...");
  byte status = readStatus();
  Serial.printf("  Status Register: %02X\n", status);

  // Test 1.3: Sector erase
  Serial.println("- Testing sector erase...");
  if (!testErase(0, SECTOR_SIZE)) {
    Serial.println("  SECTOR ERASE FAILED!");
    return false;
  }

  // Test 1.4: Block erase
  Serial.println("- Testing block erase...");
  if (!testErase(BLOCK_SIZE, BLOCK_SIZE)) {
    Serial.println("  BLOCK ERASE FAILED!");
    return false;
  }

  // Test 1.5: Write/read patterns
  Serial.println("- Testing pattern writing...");
  if (!testPatterns(0x1000)) { // Test at address 0x1000
    return false;
  }

  Serial.println("\nBASIC TESTS PASSED");
  return true;
}

void fullCapacityTest() {
  Serial.println("- Verifying full capacity...");
  uint32_t errors = 0;
  
  for (uint32_t addr = 0; addr < CHIP_SIZE; addr += TEST_STEP) {
    // Progress indicator
    Serial.printf("  Testing 0x%06X (%d%%)...\r", addr, (addr * 100) / CHIP_SIZE);
    
    // Select pattern based on address
    byte pattern = patterns[(addr / TEST_STEP) % sizeof(patterns)];
    
    // Test this location
    if (!testLocation(addr, pattern)) {
      errors++;
      Serial.printf("\n  Error at 0x%06X", addr);
    }
  }
  
  Serial.println("\n\nTEST SUMMARY:");
  Serial.printf("  Total tested: %d locations\n", CHIP_SIZE / TEST_STEP);
  Serial.printf("  Errors found: %d\n", errors);
  Serial.printf("  Error rate: %.2f%%\n", (errors * 100.0) / (CHIP_SIZE / TEST_STEP));
}

void stressTest() {
  Serial.println("- Running stress test (3 cycles)...");
  uint32_t baseAddr = 0x200000; // Test in middle of flash
  
  for (int cycle = 1; cycle <= 3; cycle++) {
    Serial.printf("  Cycle %d: ", cycle);
    
    // Erase
    if (!eraseSector(baseAddr)) {
      Serial.println("Erase failed!");
      continue;
    }
    
    // Write
    for (uint32_t offset = 0; offset < SECTOR_SIZE; offset++) {
      byte pattern = (cycle + offset) & 0xFF;
      writeByte(baseAddr + offset, pattern);
    }
    
    // Verify
    bool success = true;
    for (uint32_t offset = 0; offset < SECTOR_SIZE; offset++) {
      byte expected = (cycle + offset) & 0xFF;
      byte actual = readByte(baseAddr + offset);
      
      if (actual != expected) {
        success = false;
        Serial.printf("\n    Mismatch at +0x%04X: Wrote 0x%02X, Read 0x%02X",
                    offset, expected, actual);
        break;
      }
    }
    
    Serial.println(success ? "PASSED" : "FAILED");
  }
}

void finalCheck() {
  Serial.println("- Performing final check...");
  byte status = readStatus();
  Serial.printf("  Final Status Register: %02X\n", status);
  
  // Check if in standard SPI mode
  if ((status & 0x3C) != 0) {
    Serial.println("  WARNING: Chip may be in non-standard mode!");
  }
  
  Serial.println("\nTESTING COMPLETE");
}

// ================== LOW-LEVEL FUNCTIONS ================== //

bool testLocation(uint32_t addr, byte pattern) {
  // Erase sector if needed
  if (addr % SECTOR_SIZE == 0) {
    if (!eraseSector(addr)) return false;
  }
  
  // Write pattern
  writeByte(addr, pattern);
  
  // Verify
  byte readBack = readByte(addr);
  if (readBack != pattern) {
    Serial.printf("\n  Error at 0x%06X: Wrote 0x%02X, Read 0x%02X\n",
                addr, pattern, readBack);
    return false;
  }
  
  return true;
}

bool testPatterns(uint32_t baseAddr) {
  for (byte i = 0; i < sizeof(patterns); i++) {
    byte pattern = patterns[i];
    
    // Erase first
    if (!eraseSector(baseAddr)) {
      Serial.println("  Erase failed!");
      return false;
    }
    
    // Write pattern
    writeByte(baseAddr, pattern);
    
    // Verify
    byte readBack = readByte(baseAddr);
    if (readBack != pattern) {
      Serial.printf("  Pattern 0x%02X failed: Wrote 0x%02X, Read 0x%02X\n",
                  pattern, pattern, readBack);
      return false;
    }
    
    Serial.printf("  Pattern 0x%02X verified\n", pattern);
  }
  return true;
}

bool testErase(uint32_t addr, uint32_t size) {
  // Fill with data first
  writeByte(addr, 0xAA);
  writeByte(addr + size - 1, 0x55);
  
  // Erase
  if (size == SECTOR_SIZE) {
    if (!eraseSector(addr)) return false;
  } else {
    if (!eraseBlock(addr)) return false;
  }
  
  // Verify erased
  if (readByte(addr) != 0xFF || readByte(addr + size - 1) != 0xFF) {
    Serial.printf("  Erase verification failed at 0x%06X\n", addr);
    return false;
  }
  
  return true;
}

// ================== BASIC COMMUNICATION ================== //

bool checkCommunication() {
  // Check JEDEC ID isn't all zeros
  byte id[3];
  readJEDEC(id);
  if (id[0] == 0 && id[1] == 0 && id[2] == 0) {
    return false;
  }
  
  // Check status register responds
  byte status = readStatus();
  return true;
}

void suggestTroubleshooting() {
  Serial.println("\nTROUBLESHOOTING SUGGESTIONS:");
  Serial.println("1. Check all wiring connections (CS, CLK, MOSI, MISO)");
  Serial.println("2. Verify power supply (3.3V stable)");
  Serial.println("3. Check MISO pull-up resistor (10kΩ recommended)");
  Serial.println("4. Try reducing SPI speed (add SPISettings(1000000, MSBFIRST, SPI_MODE0))");
  Serial.println("5. Verify chip select is HIGH when not active");
}

// ================== SPI COMMANDS ================== //

void readJEDEC(byte* id) {
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x9F); // JEDEC ID command
  id[0] = SPI.transfer(0);
  id[1] = SPI.transfer(0);
  id[2] = SPI.transfer(0);
  digitalWrite(FlashCS, HIGH);
}

byte readStatus() {
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x05); // Read status
  byte status = SPI.transfer(0);
  digitalWrite(FlashCS, HIGH);
  return status;
}

void writeEnable() {
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x06); // Write enable
  digitalWrite(FlashCS, HIGH);
  delayMicroseconds(5);
}

bool waitReady(uint32_t timeout = 1000) {
  uint32_t start = millis();
  while (millis() - start < timeout) {
    if ((readStatus() & 0x01) == 0) return true;
    delay(1);
  }
  return false;
}

bool eraseSector(uint32_t addr) {
  writeEnable();
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x20); // Sector erase (4KB)
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  digitalWrite(FlashCS, HIGH);
  return waitReady();
}

bool eraseBlock(uint32_t addr) {
  writeEnable();
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0xD8); // Block erase (64KB)
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  digitalWrite(FlashCS, HIGH);
  return waitReady(5000); // Longer timeout for block erase
}

void writeByte(uint32_t addr, byte data) {
  writeEnable();
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x02); // Page program
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  SPI.transfer(data);
  digitalWrite(FlashCS, HIGH);
  waitReady();
}

byte readByte(uint32_t addr) {
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x03); // Read data
  SPI.transfer(addr >> 16);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr);
  byte data = SPI.transfer(0);
  digitalWrite(FlashCS, HIGH);
  return data;
}