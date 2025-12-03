/*********************************************************************
 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 Copyright (c) 2019 Ha Thach for Adafruit Industries
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

#include "Adafruit_TinyUSB.h"

// 8KB is the smallest size that windows allow to mount
#define DISK_BLOCK_NUM  32  // Use to set disk size. 32*512 = 16kB => 2000 data points. Each data entry is 8 bytes: 6 bytes (uint32_t, int) + 1-byte comma + 1-byte newline
#define DISK_BLOCK_SIZE 512 // leave block size fixed

#include "ramdisk.h"

Adafruit_USBD_MSC usb_msc;

// Eject button to demonstrate medium is not ready e.g SDCard is not present
// whenever this button is pressed and hold, it will report to host as not ready
#if defined(ARDUINO_SAMD_CIRCUITPLAYGROUND_EXPRESS) || defined(ARDUINO_NRF52840_CIRCUITPLAY)
  #define BTN_EJECT   4   // Left Button
  bool activeState = true;

#elif defined(ARDUINO_FUNHOUSE_ESP32S2)
  #define BTN_EJECT   BUTTON_DOWN
  bool activeState = true;

#elif defined PIN_BUTTON1
  #define BTN_EJECT   PIN_BUTTON1
  bool activeState = false;
#endif



// Data collection structure
#define MAX_DATAPOINTS 50
struct DataPoint {
  uint32_t millisecond;
  int displacement_mm;
};

DataPoint timeseries[MAX_DATAPOINTS];
int datapoint_count = 0;

// Function to add datapoint
void addDataPoint(uint32_t ms, int disp) {
  if (datapoint_count < MAX_DATAPOINTS) {
    timeseries[datapoint_count].millisecond = ms;
    timeseries[datapoint_count].displacement_mm = disp;
    datapoint_count++;
  }
}

// Generate CSV content from timeseries data
void generateCSV() {
  char* csv_buffer = (char*)msc_disk[3]; // Block 3 contains file data
  int offset = 0;
  
  // Write header
  offset += sprintf(csv_buffer + offset, "Time [ms],Displacement [mm]\n");
  
  // Write data rows
  for (int i = 0; i < datapoint_count; i++) {
    offset += sprintf(csv_buffer + offset, "%lu,%.2f\n", 
                     timeseries[i].millisecond, 
                     timeseries[i].displacement_mm);
  }
  
  // Update file size in directory entry (Block 2, offset 60-63)
  uint32_t file_size = offset;
  msc_disk[2][60] = file_size & 0xFF;
  msc_disk[2][61] = (file_size >> 8) & 0xFF;
  msc_disk[2][62] = (file_size >> 16) & 0xFF;
  msc_disk[2][63] = (file_size >> 24) & 0xFF;
}


// the setup function runs once when you press reset or power the board
void setup() {
  // Manual begin() is required on core without built-in support e.g. mbed rp2040
  if (!TinyUSBDevice.isInitialized()) { 
    TinyUSBDevice.begin(0);
  }

  Serial.begin(115200);

#ifdef BTN_EJECT
  pinMode(BTN_EJECT, activeState ? INPUT_PULLDOWN : INPUT_PULLUP);
#endif

  // Set disk vendor id, product id and revision with string up to 8, 16, 4 characters respectively
  usb_msc.setID("Adafruit", "Mass Storage", "1.0");

  // Set disk size
  usb_msc.setCapacity(DISK_BLOCK_NUM, DISK_BLOCK_SIZE);

  // Set callback
  usb_msc.setReadWriteCallback(msc_read_callback, msc_write_callback, msc_flush_callback);
  usb_msc.setStartStopCallback(msc_start_stop_callback);
  usb_msc.setReadyCallback(msc_ready_callback);

  // Set Lun ready (RAM disk is always ready)
  usb_msc.setUnitReady(true);




// Collect your timeseries data
  addDataPoint(0, 0.0);
  addDataPoint(100, 5.2);
  addDataPoint(200, 10.8);
  // ... add more datapoints as needed
  
  // Generate CSV before mounting drive
  generateCSV();




  usb_msc.begin();

  // If already enumerated, additional class driverr begin() e.g msc, hid, midi won't take effect until re-enumeration
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

//  while ( !Serial ) delay(10);   // wait for native usb
  Serial.println("Adafruit TinyUSB Mass Storage RAM Disk example");
}

void loop() {
  #ifdef TINYUSB_NEED_POLLING_TASK
  // Manual call tud_task since it isn't called by Core's background
  TinyUSBDevice.task();
  #endif
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_callback(uint32_t lba, void* buffer, uint32_t bufsize) {
  uint8_t const* addr = msc_disk[lba];
  memcpy(buffer, addr, bufsize);

  return bufsize;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_callback(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
  uint8_t* addr = msc_disk[lba];
  memcpy(addr, buffer, bufsize);

  return bufsize;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_callback(void) {
  // nothing to do
}

bool msc_start_stop_callback(uint8_t power_condition, bool start, bool load_eject) {
  Serial.printf("Start/Stop callback: power condition %u, start %u, load_eject %u\n", power_condition, start, load_eject);
  return true;
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool msc_ready_callback(void) {
  #ifdef BTN_EJECT
  // button not active --> medium ready
  return digitalRead(BTN_EJECT) != activeState;
  #else
  return true;
  #endif
}
