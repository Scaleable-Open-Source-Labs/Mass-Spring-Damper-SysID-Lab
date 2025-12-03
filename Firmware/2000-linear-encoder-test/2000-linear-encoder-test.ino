/*
 * Print the [timestamp, displacement_mm] to the terminal
 * A proof of concept that the linear encoder is working reliably
 */

#include <Arduino.h>
#include "Adafruit_TinyUSB.h"

int chA = 29;
int chB = 28;
int chC = 27;

// Volatile variables shared between ISR and main loop
volatile bool stateChanged = false;
volatile long positionCounter = 0;

// State tracking
uint8_t currentState = 0;
uint8_t previousState = 0;

// Gray code state sequence (forward direction)
const uint8_t graySequence[6] = { 0b000, 0b100, 0b110, 0b111, 0b011, 0b001 };

unsigned long timestamp = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(100);

  pinMode(chA, INPUT);  // Use INPUT to ensure Pull-up/down resistors are disabled - else the signal from the reflectance sensor is corrupted.
  pinMode(chB, INPUT);
  pinMode(chC, INPUT);

  // Read initial state
  currentState = readEncoderState();
  previousState = currentState;

  attachInterrupt(digitalPinToInterrupt(chA), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(chB), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(chC), encoderISR, CHANGE);

  interrupts();
  Serial.println("Linear Optical Encoder initialized");
}

void loop() {

  // Print the displacement whenever an edge is detected (1mm resolution)
  if (stateChanged) {
    stateChanged = false;
    processEncoderChange();
    bool a = digitalRead(chA);
    bool b = digitalRead(chB);
    bool c = digitalRead(chC);
    Serial.printf("%8d  %d %d %d   %4d\n", timestamp, a, b, c, positionCounter);
  }
}


void encoderISR() {
  timestamp = millis();
  stateChanged = true;
}

// Read current encoder state (3-bit value)
uint8_t readEncoderState() {
  uint8_t state = 0;

  // Read pins and construct 3-bit state (A=bit2, B=bit1, C=bit0)
  if (!digitalRead(chA)) state |= 0b100;  // Assuming active LOW sensors
  if (!digitalRead(chB)) state |= 0b010;
  if (!digitalRead(chC)) state |= 0b001;

  return state;
}

// Process encoder state change and update position
void processEncoderChange() {
  currentState = readEncoderState();
  // Only process if state actually changed
  if (currentState != previousState || true) {
    int direction = getDirection(previousState, currentState);

    if (direction != 0) {
      // Disable interrupts briefly while updating position
      noInterrupts();
      positionCounter += direction;
      interrupts();
    }

    previousState = currentState;
  }
}


// Determine direction based on Gray code sequence
// Returns: +1 for forward, -1 for reverse, 0 for invalid/noise
int getDirection(uint8_t prevState, uint8_t currState) {
  // Find current and previous state indices in Gray sequence
  int prevIndex = findStateIndex(prevState);
  int currIndex = findStateIndex(currState);

  // Invalid states
  if (prevIndex == -1 || currIndex == -1) {
    return 0;
  }

  // Calculate direction based on sequence position
  int diff = currIndex - prevIndex;

  // Handle wraparound cases
  if (diff == 1 || diff == -5) {
    return 1;  // Forward
  } else if (diff == -1 || diff == 5) {
    return -1;  // Reverse
  } else if (diff == 0) {
    return 0;  // No change (noise/bounce)
  } else {
    // Multiple steps jumped - likely noise, ignore
    return 0;
  }
}

// Find index of state in Gray code sequence
int findStateIndex(uint8_t state) {
  for (int i = 0; i < 6; i++) {
    if (graySequence[i] == state) {
      return i;
    }
  }
  return -1;  // Invalid state
}

// Optional: Function to get current position (thread-safe)
long getCurrentPosition() {
  noInterrupts();
  long pos = positionCounter;
  interrupts();
  return pos;
}