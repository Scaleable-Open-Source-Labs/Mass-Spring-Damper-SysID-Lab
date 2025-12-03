/*
 Mount the SPI flash
 format/load a filesystem
 save data to a CSV file
 dump the file contents to serial
*/

#include <SPI.h>
#include <LittleFS.h>

// SPI Flash Configuration for 25Q128
#define FLASH_CS_PIN 17
#define SPI_CLOCK_SPEED 8000000  // 8MHz

// 25Q128 Flash Commands
#define CMD_READ_DATA        0x03
#define CMD_PAGE_PROGRAM     0x02
#define CMD_SECTOR_ERASE     0x20
#define CMD_WRITE_ENABLE     0x06
#define CMD_READ_STATUS1     0x05

// CSV Configuration
#define CSV_FILENAME "/data_log.csv"
#define RECORDS_PER_BATCH 10  // Save 10 records then dump

// Global variables
File csvFile;
int recordCount = 0;
int totalRecords = 0;
unsigned long startTime = 0;

// Custom LittleFS implementation for external SPI flash
class ExternalSPIFlash {
public:
    static bool begin() {
        // Initialize SPI
        SPI.begin();
        SPI.beginTransaction(SPISettings(SPI_CLOCK_SPEED, MSBFIRST, SPI_MODE0));
        pinMode(FLASH_CS_PIN, OUTPUT);
        digitalWrite(FLASH_CS_PIN, HIGH);
        
        // Test flash connectivity
        if (!testFlashConnection()) {
            Serial.println("ERROR: Cannot communicate with external flash!");
            return false;
        }
        
        Serial.println("External SPI flash detected successfully");
        return true;
    }
    
    static bool testFlashConnection() {
        // Read JEDEC ID to verify connection
        digitalWrite(FLASH_CS_PIN, LOW);
        SPI.transfer(0x9F);  // JEDEC ID command
        uint8_t mfg = SPI.transfer(0x00);
        uint8_t type = SPI.transfer(0x00);
        uint8_t capacity = SPI.transfer(0x00);
        digitalWrite(FLASH_CS_PIN, HIGH);
        
        Serial.print("Flash JEDEC ID: 0x");
        Serial.print(mfg, HEX);
        Serial.print(" 0x");
        Serial.print(type, HEX);
        Serial.print(" 0x");
        Serial.println(capacity, HEX);
        
        return (mfg != 0xFF && mfg != 0x00);
    }
    
    static bool read(uint32_t addr, uint8_t* buffer, uint32_t size) {
        digitalWrite(FLASH_CS_PIN, LOW);
        SPI.transfer(CMD_READ_DATA);
        SPI.transfer((addr >> 16) & 0xFF);
        SPI.transfer((addr >> 8) & 0xFF);
        SPI.transfer(addr & 0xFF);
        
        for (uint32_t i = 0; i < size; i++) {
            buffer[i] = SPI.transfer(0x00);
        }
        
        digitalWrite(FLASH_CS_PIN, HIGH);
        return true;
    }
    
    static bool write(uint32_t addr, const uint8_t* buffer, uint32_t size) {
        uint32_t remaining = size;
        uint32_t offset = 0;
        
        while (remaining > 0) {
            uint32_t pageOffset = (addr + offset) % 256;
            uint32_t writeSize = min(remaining, 256 - pageOffset);
            
            writeEnable();
            
            digitalWrite(FLASH_CS_PIN, LOW);
            SPI.transfer(CMD_PAGE_PROGRAM);
            SPI.transfer(((addr + offset) >> 16) & 0xFF);
            SPI.transfer(((addr + offset) >> 8) & 0xFF);
            SPI.transfer((addr + offset) & 0xFF);
            
            for (uint32_t i = 0; i < writeSize; i++) {
                SPI.transfer(buffer[offset + i]);
            }
            
            digitalWrite(FLASH_CS_PIN, HIGH);
            waitForReady();
            
            offset += writeSize;
            remaining -= writeSize;
        }
        
        return true;
    }
    
    static bool erase(uint32_t addr, uint32_t size) {
        uint32_t sectorStart = addr & ~0xFFF;  // Align to 4KB boundary
        uint32_t sectorEnd = (addr + size + 0xFFF) & ~0xFFF;
        
        for (uint32_t sector = sectorStart; sector < sectorEnd; sector += 0x1000) {
            writeEnable();
            
            digitalWrite(FLASH_CS_PIN, LOW);
            SPI.transfer(CMD_SECTOR_ERASE);
            SPI.transfer((sector >> 16) & 0xFF);
            SPI.transfer((sector >> 8) & 0xFF);
            SPI.transfer(sector & 0xFF);
            digitalWrite(FLASH_CS_PIN, HIGH);
            
            waitForReady();
        }
        
        return true;
    }
    
private:
    static void writeEnable() {
        digitalWrite(FLASH_CS_PIN, LOW);
        SPI.transfer(CMD_WRITE_ENABLE);
        digitalWrite(FLASH_CS_PIN, HIGH);
    }
    
    static bool waitForReady() {
        unsigned long startTime = millis();
        while (readStatus() & 0x01) {
            delay(1);
            if (millis() - startTime > 5000) {
                Serial.println("Flash timeout!");
                return false;
            }
        }
        return true;
    }
    
    static uint8_t readStatus() {
        digitalWrite(FLASH_CS_PIN, LOW);
        SPI.transfer(CMD_READ_STATUS1);
        uint8_t status = SPI.transfer(0x00);
        digitalWrite(FLASH_CS_PIN, HIGH);
        return status;
    }
};

// LittleFS configuration for external flash
static lfs_t lfs;
static lfs_config lfs_cfg;
static uint8_t lfs_read_buffer[256];
static uint8_t lfs_prog_buffer[256];
static uint8_t lfs_lookahead_buffer[16];

// LittleFS callback functions
int lfs_flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
    uint32_t addr = block * c->block_size + off;
    return ExternalSPIFlash::read(addr, (uint8_t*)buffer, size) ? 0 : -1;
}

int lfs_flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    uint32_t addr = block * c->block_size + off;
    return ExternalSPIFlash::write(addr, (const uint8_t*)buffer, size) ? 0 : -1;
}

int lfs_flash_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t addr = block * c->block_size;
    return ExternalSPIFlash::erase(addr, c->block_size) ? 0 : -1;
}

int lfs_flash_sync(const struct lfs_config *c) {
    return 0;  // No sync needed for SPI flash
}

bool initializeLittleFS() {
    // Configure LittleFS for external flash
    lfs_cfg.read = lfs_flash_read;
    lfs_cfg.prog = lfs_flash_prog;
    lfs_cfg.erase = lfs_flash_erase;
    lfs_cfg.sync = lfs_flash_sync;
    
    lfs_cfg.read_size = 256;
    lfs_cfg.prog_size = 256;
    lfs_cfg.block_size = 4096;     // 4KB sectors
    lfs_cfg.block_count = 4096;    // 16MB total (4096 * 4KB)
    lfs_cfg.cache_size = 256;
    lfs_cfg.lookahead_size = 16;
    lfs_cfg.block_cycles = 500;
    
    lfs_cfg.read_buffer = lfs_read_buffer;
    lfs_cfg.prog_buffer = lfs_prog_buffer;
    lfs_cfg.lookahead_buffer = lfs_lookahead_buffer;
    
    // Try to mount existing filesystem
    int err = lfs_mount(&lfs, &lfs_cfg);
    
    if (err) {
        // No filesystem found, format and mount
        Serial.println("No filesystem found, formatting flash...");
        err = lfs_format(&lfs, &lfs_cfg);
        if (err) {
            Serial.println("Format failed!");
            return false;
        }
        
        err = lfs_mount(&lfs, &lfs_cfg);
        if (err) {
            Serial.println("Mount after format failed!");
            return false;
        }
        
        Serial.println("Flash formatted and mounted successfully");
    } else {
        Serial.println("Existing filesystem mounted successfully");
    }
    
    return true;
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(100);
    
    Serial.println("=== RP2040 CSV Logger with LittleFS ===");
    Serial.println();
    
    // Initialize external flash
    if (!ExternalSPIFlash::begin()) {
        Serial.println("Failed to initialize external flash!");
        while (1) delay(1000);
    }
    
    // Initialize LittleFS
    if (!initializeLittleFS()) {
        Serial.println("Failed to initialize LittleFS!");
        while (1) delay(1000);
    }
    
    // Create/open CSV file
    lfs_file_t file;
    int err = lfs_file_open(&lfs, &file, CSV_FILENAME, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        Serial.println("Failed to create CSV file!");
        while (1) delay(1000);
    }
    
    // Write CSV headers
    const char* header = "Time_ms,Displacement_mm\n";
    lfs_file_write(&lfs, &file, header, strlen(header));
    lfs_file_close(&lfs, &file);
    
    Serial.println("CSV file created with headers");
    Serial.println("Starting data logging...");
    
    startTime = millis();
}

void loop() {
    // Generate sample data (replace with your sensor reading)
    unsigned long currentTime = millis() - startTime;
    float displacement = getDisplacementReading();
    
    // Add record to CSV file
    addCSVRecord(currentTime, displacement);
    recordCount++;
    totalRecords++;
    
    // Print current record to serial
    Serial.print("Record ");
    Serial.print(totalRecords);
    Serial.print(": ");
    Serial.print(currentTime);
    Serial.print(",");
    Serial.println(displacement, 3);
    
    // After 10 records, dump the entire file contents
    if (recordCount >= RECORDS_PER_BATCH) {
        Serial.println("\n=== DUMPING CSV FILE CONTENTS ===");
        dumpCSVFile();
        Serial.println("=== END FILE DUMP ===\n");
        recordCount = 0;  // Reset counter for next batch
        
        for(;;){}
    }
    
    delay(500);  // Wait 500ms between records for demo purposes
}

void addCSVRecord(unsigned long timeMs, float displacement) {
    lfs_file_t file;
    
    // Open file in append mode
    int err = lfs_file_open(&lfs, &file, CSV_FILENAME, LFS_O_WRONLY | LFS_O_APPEND);
    if (err < 0) {
        Serial.println("Failed to open CSV file for writing!");
        return;
    }
    
    // Format CSV line
    char csvLine[64];
    snprintf(csvLine, sizeof(csvLine), "%lu,%.3f\n", timeMs, displacement);
    
    // Write to file
    lfs_file_write(&lfs, &file, csvLine, strlen(csvLine));
    lfs_file_close(&lfs, &file);
}

void dumpCSVFile() {
    lfs_file_t file;
    
    // Open file for reading
    int err = lfs_file_open(&lfs, &file, CSV_FILENAME, LFS_O_RDONLY);
    if (err < 0) {
        Serial.println("Failed to open CSV file for reading!");
        return;
    }
    
    // Get file size
    lfs_soff_t fileSize = lfs_file_size(&lfs, &file);
    Serial.print("CSV file size: ");
    Serial.print(fileSize);
    Serial.println(" bytes");
    Serial.println();
    
    // Read and print file contents
    char buffer[256];
    lfs_ssize_t bytesRead;
    
    while ((bytesRead = lfs_file_read(&lfs, &file, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytesRead] = '\0';  // Null terminate
        Serial.print(buffer);
    }
    
    lfs_file_close(&lfs, &file);
    Serial.println();
}

// Sample data generation (replace with your actual sensor readings)
float getDisplacementReading() {
    // Generate sample sine wave data with some noise
    static float phase = 0;
    phase += 0.2;
    return 10.0 * sin(phase) + random(-50, 50) / 100.0;
}