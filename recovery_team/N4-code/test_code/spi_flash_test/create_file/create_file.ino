#include <SPI.h>
#include <string.h>

// Custom MIN/MAX macros
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

const int FlashCS = 5;   // Chip Select
const int FlashWP = 22;  // Write Protect (pull high)
const int FlashHOLD = 21; // Hold (pull high)
const uint32_t CHIP_SIZE = 1048576; // 1MB flash

// File System Structure
#define FILE_SYSTEM_START 0x10000
#define METADATA_ADDRESS 0x00000
#define MAX_FILES 10
#define MAX_FILENAME_LEN 16
#define SECTOR_SIZE 4096

typedef struct {
  char filename[MAX_FILENAME_LEN];
  uint32_t start_address;
  uint32_t size;
  uint32_t crc;
} FileEntry;

FileEntry file_table[MAX_FILES];
uint32_t next_free_address = FILE_SYSTEM_START;
bool first_run = true;

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
  delay(100);

  // Initialize file system
  if (!initFileSystem()) {
    Serial.println("Formatting file system...");
    formatFileSystem();
    first_run = true;
  } else {
    first_run = false;
  }

  // Only create files on first run
  if (first_run) {
    createFile("config.txt", "DeviceID:ESP32_001", 18);
    createFile("data.bin", "\xAA\x55\xAA\x55", 4);
    Serial.println("Setup complete. Reset to verify persistence.");
    while(1); // Force reset
  }

  // Normal operation
  listFiles();
  readFile("config.txt");
}

void loop() {}

// ================== IMPROVED CRC FUNCTION ================== //
uint32_t calculateCRC(const void* data, uint32_t len) {
  uint32_t crc = 0xFFFFFFFF;
  const uint8_t* p = (const uint8_t*)data;
  while (len--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++) 
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  return ~crc;
}

// ================== FILE SYSTEM OPERATIONS ================== //
bool initFileSystem() {
  readData(METADATA_ADDRESS, &file_table, sizeof(file_table));
  
  // Validate CRC for each file entry
  for (int i = 0; i < MAX_FILES; i++) {
    if (file_table[i].filename[0] != 0) {
      FileEntry temp = file_table[i];
      temp.crc = 0; // Clear CRC before calculation
      uint32_t expected_crc = calculateCRC(&temp, sizeof(temp));
      
      if (file_table[i].crc != expected_crc) {
        return false;
      }
    }
  }
  
  // Calculate next free address
  next_free_address = FILE_SYSTEM_START;
  for (int i = 0; i < MAX_FILES; i++) {
    if (file_table[i].filename[0] != 0) {
      uint32_t end = file_table[i].start_address + file_table[i].size;
      if (end > next_free_address) next_free_address = end;
    }
  }
  return true;
}

void formatFileSystem() {
  // Erase first 4 sectors (16KB)
  for (uint32_t addr = 0; addr < 0x4000; addr += SECTOR_SIZE) {
    eraseSector(addr);
  }
  
  memset(file_table, 0, sizeof(file_table));
  next_free_address = FILE_SYSTEM_START;
  saveFileTable();
}

void saveFileTable() {
  // Calculate CRCs
  for (int i = 0; i < MAX_FILES; i++) {
    if (file_table[i].filename[0] != 0) {
      FileEntry temp = file_table[i];
      temp.crc = 0;
      file_table[i].crc = calculateCRC(&temp, sizeof(temp));
    }
  }
  
  eraseSector(METADATA_ADDRESS);
  writeData(METADATA_ADDRESS, &file_table, sizeof(file_table));
}

// ================== FILE OPERATIONS ================== //
bool createFile(const char* filename, const void* data, uint32_t size) {
  // Validate input
  if (strlen(filename) >= MAX_FILENAME_LEN) {
    Serial.println("Filename too long!");
    return false;
  }
  
  // Check for existing file
  for (int i = 0; i < MAX_FILES; i++) {
    if (strcmp(file_table[i].filename, filename) == 0) {
      return false; // Silent return - expected on reboot
    }
  }
  
  // Find empty slot
  int free_slot = -1;
  for (int i = 0; i < MAX_FILES; i++) {
    if (file_table[i].filename[0] == 0) {
      free_slot = i;
      break;
    }
  }
  
  if (free_slot == -1 || (next_free_address + size) > CHIP_SIZE) {
    return false;
  }
  
  // Sector alignment
  if (next_free_address % SECTOR_SIZE == 0) {
    eraseSector(next_free_address);
  }
  
  // Write data
  writeData(next_free_address, data, size);
  
  // Update file table
  strncpy(file_table[free_slot].filename, filename, MAX_FILENAME_LEN);
  file_table[free_slot].start_address = next_free_address;
  file_table[free_slot].size = size;
  
  saveFileTable();
  next_free_address += size;
  return true;
}

void readFile(const char* filename) {
  for (int i = 0; i < MAX_FILES; i++) {
    if (strcmp(file_table[i].filename, filename) == 0) {
      Serial.printf("\n%s (%lu bytes):\n", filename, file_table[i].size);
      
      uint8_t* buffer = new uint8_t[file_table[i].size];
      readData(file_table[i].start_address, buffer, file_table[i].size);
      
      // Hex dump
      for (uint32_t j = 0; j < file_table[i].size; j++) {
        if (j % 16 == 0) Serial.printf("\n%04lX: ", j);
        Serial.printf("%02X ", buffer[j]);
      }
      
      // ASCII view
      Serial.println("\nASCII:");
      for (uint32_t j = 0; j < file_table[i].size; j++) {
        if (j % 16 == 0 && j > 0) Serial.println();
        Serial.write(isprint(buffer[j]) ? buffer[j] : '.');
      }
      Serial.println();
      
      delete[] buffer;
      return;
    }
  }
  Serial.printf("File %s not found!\n", filename);
}

void listFiles() {
  Serial.println("\nFile System Contents:");
  Serial.println("====================");
  
  bool has_files = false;
  for (int i = 0; i < MAX_FILES; i++) {
    if (file_table[i].filename[0] != 0) {
      Serial.printf("%-16s @ 0x%06lX (%lu bytes)\n", 
                  file_table[i].filename, 
                  file_table[i].start_address, 
                  file_table[i].size);
      has_files = true;
    }
  }
  
  if (!has_files) Serial.println("No files found");
  Serial.printf("\nNext free: 0x%06lX\n", next_free_address);
}

// ================== LOW-LEVEL FLASH OPERATIONS ================== //
void writeEnable() {
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x06); // WREN
  digitalWrite(FlashCS, HIGH);
  delayMicroseconds(5);
}

bool waitReady(uint32_t timeout = 500) {
  uint32_t start = millis();
  while (millis() - start < timeout) {
    digitalWrite(FlashCS, LOW);
    SPI.transfer(0x05); // Read status
    uint8_t status = SPI.transfer(0);
    digitalWrite(FlashCS, HIGH);
    
    if (!(status & 0x01)) return true;
    delay(1);
  }
  return false;
}

void eraseSector(uint32_t address) {
  if (address >= CHIP_SIZE) return;
  
  writeEnable();
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x20); // Sector erase
  SPI.transfer(address >> 16);
  SPI.transfer(address >> 8);
  SPI.transfer(address);
  digitalWrite(FlashCS, HIGH);
  
  if (!waitReady(5000)) {
    Serial.println("Erase timeout!");
  }
}

void writeData(uint32_t address, const void* data, uint32_t len) {
  if (address >= CHIP_SIZE) return;
  len = MIN(len, CHIP_SIZE - address);
  
  const uint8_t* p = (const uint8_t*)data;
  uint32_t bytes_written = 0;
  
  while (len > 0) {
    writeEnable();
    
    // Calculate chunk size (max 256 bytes, page aligned)
    uint32_t chunk = MIN(len, 256);
    uint32_t page_end = (address | 0xFF) + 1;
    chunk = MIN(chunk, page_end - address);
    
    digitalWrite(FlashCS, LOW);
    SPI.transfer(0x02); // Page program
    SPI.transfer(address >> 16);
    SPI.transfer(address >> 8);
    SPI.transfer(address);
    
    for (uint32_t i = 0; i < chunk; i++) {
      SPI.transfer(*p++);
    }
    
    digitalWrite(FlashCS, HIGH);
    
    if (!waitReady()) {
      Serial.println("Write timeout!");
      break;
    }
    
    address += chunk;
    len -= chunk;
    bytes_written += chunk;
  }
}

void readData(uint32_t address, void* data, uint32_t len) {
  if (address >= CHIP_SIZE) return;
  len = MIN(len, CHIP_SIZE - address);
  
  digitalWrite(FlashCS, LOW);
  SPI.transfer(0x03); // Read data
  SPI.transfer(address >> 16);
  SPI.transfer(address >> 8);
  SPI.transfer(address);
  
  uint8_t* p = (uint8_t*)data;
  while (len--) {
    *p++ = SPI.transfer(0);
  }
  
  digitalWrite(FlashCS, HIGH);
}