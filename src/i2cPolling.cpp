// i2cPolling.cpp

// === TEENSY 4.1 I2C POLLING MASTER ===
// Polls 5 ATmega slaves for keyboard and encoder data
// No interrupt pins needed - pure polling approach
// NO button press handling - encoders send rotation data, keyboard sends key events

#include <Wire.h>
#include "i2cPolling.h"
#include "Utils.h"
#include "EEPROMStorage.h"
#include "NetworkOSC.h"
#include "Keysend.h"

// There are a lot of safeguards here to handle noisy i2c lines so even the most EMI unfriendly build should behave well

bool i2cDebug = false;
bool i2cErrorDebug = true;

// Local helpers to gate all I2C logging behind i2cDebug
#define I2C_DEBUG_PRINT(msg)    do { if (i2cDebug) debugPrint(msg); } while (0)
#define I2C_DEBUG_PRINTF(...)   do { if (i2cDebug) debugPrintf(__VA_ARGS__); } while (0)
#define I2C_ERROR_PRINT(msg)    do { if (i2cErrorDebug) debugPrint(msg); } while (0)
#define I2C_ERROR_PRINTF(...)   do { if (i2cErrorDebug) debugPrintf(__VA_ARGS__); } while (0)

// Track last known pressed keys so the watchdog can force releases
static bool trackedKeyStates[NUM_EXECUTORS_TRACKED] = {false};

static int keyIndexFromNumber(uint16_t keyNumber) {
  if (keyNumber >= 101 && keyNumber <= 110) return keyNumber - 101;
  if (keyNumber >= 201 && keyNumber <= 210) return 10 + (keyNumber - 201);
  if (keyNumber >= 301 && keyNumber <= 310) return 20 + (keyNumber - 301);
  if (keyNumber >= 401 && keyNumber <= 410) return 30 + (keyNumber - 401);
  return -1;
}

static uint16_t keyNumberFromIndex(int index) {
  if (index >= 0 && index < 10) return 101 + index;
  if (index >= 10 && index < 20) return 201 + (index - 10);
  if (index >= 20 && index < 30) return 301 + (index - 20);
  if (index >= 30 && index < 40) return 401 + (index - 30);
  return 0;
}

// === I2C Slave Addresses ===
#define I2C_ADDR_KEYBOARD  0x10  // Keyboard matrix ATmega - sends keypress data
#define I2C_ADDR_ENCODER1  0x11  // First encoder ATmega (5 encoders) - sends rotation data
#define I2C_ADDR_ENCODER2  0x12  // Second encoder ATmega (5 encoders) - sends rotation data
#define I2C_ADDR_ENCODER3  0x13  // Third encoder ATmega (5 encoders) - sends rotation data
#define I2C_ADDR_ENCODER4  0x14  // Fourth encoder ATmega (5 encoders) - sends rotation data

// Array of all slave addresses for easy iteration during polling
const uint8_t slaveAddresses[] = {
  I2C_ADDR_KEYBOARD,   // First slave: keyboard matrix for key events
  I2C_ADDR_ENCODER1,   // Second slave: encoders 0-4
  I2C_ADDR_ENCODER2,   // Third slave: encoders 5-9
  I2C_ADDR_ENCODER3,   // Fourth slave: encoders 10-14
  I2C_ADDR_ENCODER4    // Fifth slave: encoders 15-19
};
const int numSlaves = sizeof(slaveAddresses) / sizeof(slaveAddresses[0]);

// === Protocol Constants ===
// These constants define the message types that slaves can send
#define DATA_TYPE_ENCODER  0x01  // Data type identifier for encoder rotation messages
#define DATA_TYPE_KEYPRESS 0x02  // Data type identifier for keypress/release messages
#define DATA_TYPE_RELEASE_ALL 0x03  // Special heartbeat to force release all (sent every 200ms per EvoFaderWing_keyboard_i2c if out of sync and all keys are released)

// === Timing Variables ===
unsigned long lastPollTimeSimple = 0;          
const unsigned long I2C_POLL_INTERVAL_SIMPLE = 10;    // Poll every 10ms instead of 1ms
int resetPressCount = 0;
static int i2cErrorStreak = 0;
static uint8_t slaveBackoff[numSlaves] = {0};
const uint8_t I2C_BACKOFF_CYCLES = 3; // skip a few cycles for a noisy slave

void resetI2CBus() {
  Wire.end();
  delay(1);                  // brief settle to let ICs reset if called from an error
  Wire.begin();                
  Wire.setClock(400000);       // 100kHz for stability (400khz is working without any errors)
  Wire.setTimeout(5);         // Short timeout to avoid bus hangs
}

// === SETUP FUNCTION ===

void setupI2cPolling() {
  resetI2CBus();
  
  I2C_DEBUG_PRINT("[I2C] Polling Init");
  I2C_DEBUG_PRINTF("Polling %d slaves every %lums...", numSlaves, I2C_POLL_INTERVAL_SIMPLE);
  
  for (int i = 0; i < numSlaves; i++) {
    if (slaveAddresses[i] == I2C_ADDR_KEYBOARD) {
      I2C_DEBUG_PRINTF("  Slave %d: 0x%02X (Keyboard Matrix)", i, slaveAddresses[i]);
    } else {
      I2C_DEBUG_PRINTF("  Slave %d: 0x%02X (Encoder Group)", i, slaveAddresses[i]);
    }
  }
  
  I2C_DEBUG_PRINT("[I2C] Ready for polling");
}

// === MAIN POLLING FUNCTION ===

void handleI2c() { 
  unsigned long currentTime = millis();  
  
  if (currentTime - lastPollTimeSimple >= I2C_POLL_INTERVAL_SIMPLE) {
    lastPollTimeSimple = currentTime;  
    
    // Poll each slave with extra safety
    for (int i = 0; i < numSlaves; i++) {
    if (slaveBackoff[i] > 0) {
        I2C_ERROR_PRINTF("[I2C BACKOFF] skipping slave 0x%02X detail=%d", slaveAddresses[i], slaveBackoff[i]);
        slaveBackoff[i]--;
        continue;
      }
      pollSlave(slaveAddresses[i], i);  
      delay(1); // Small delay between slave polls
    }
  }
}

// === SLAVE POLLING ===
void pollSlave(uint8_t address, int slaveIndex) {
  // Clear any leftover data first
  while (Wire.available()) Wire.read();
  
  // Request data from slave
  uint8_t bytesRequested = 16; // Smaller request size
  uint8_t received = Wire.requestFrom(address, bytesRequested);
  
  // Wait a bit for response Added for stability testing no longer needed
  //delay(1);

  if (received != bytesRequested) {
    I2C_ERROR_PRINTF("[I2C ERR] short read from 0x%02X: got %d/%d", address, received, bytesRequested);
    while (Wire.available()) Wire.read();
    if (slaveIndex >= 0 && slaveIndex < numSlaves) {
      slaveBackoff[slaveIndex] = I2C_BACKOFF_CYCLES;
    }
    if (++i2cErrorStreak >= 3) {
      I2C_ERROR_PRINTF("[I2C ERR] resetting bus after repeated short reads on 0x%02X", address);
      i2cErrorStreak = 0;
      resetI2CBus();
    }
    return;
  }
  
  bool errorFrame = false;
  
  // Check if we got minimum required data
  if (received < 2 || Wire.available() < 2) {
    errorFrame = true;
  }
  
  // Read header
  uint8_t dataType = errorFrame ? 0 : Wire.read();  
  uint8_t count = errorFrame ? 0 : Wire.read();     

  // Short-circuit release-all frames: they should have count==0 and no payload
  if (!errorFrame && dataType == DATA_TYPE_RELEASE_ALL) {
    if (count == 0) {
      I2C_DEBUG_PRINTF("[I2C] Release-all heartbeat from 0x%02X", address);
      processReleaseAll(address);
      while (Wire.available()) Wire.read();
      return;
    } else {
      I2C_ERROR_PRINTF("[I2C] ERR Release-all frame with non-zero count %d from slave 0x%02X", count, address);
      errorFrame = true;
    }
  }
  
  // Validate data type first
  if (!errorFrame && dataType != DATA_TYPE_ENCODER && dataType != DATA_TYPE_KEYPRESS && dataType != DATA_TYPE_RELEASE_ALL){
    I2C_ERROR_PRINTF("[I2C] ERR Invalid data type 0x%02X from slave 0x%02X", dataType, address);
    errorFrame = true;
  }
  
  // Validate count
  if (!errorFrame && count > 10) {  // Reasonable maximum
    I2C_ERROR_PRINTF("[I2C] ERR Unrealistic count %d from slave 0x%02X", count, address);
    errorFrame = true;
  }
  
  // Validate we have enough bytes for the claimed count
  int bytesPerEvent = 0;
  if (dataType == DATA_TYPE_ENCODER) {
    bytesPerEvent = 2;
  } else if (dataType == DATA_TYPE_KEYPRESS) {
    bytesPerEvent = 3;
  } else if (dataType == DATA_TYPE_RELEASE_ALL) {
    bytesPerEvent = 0; // heartbeat carries no payload
  }
  int expectedBytes = count * bytesPerEvent;
  
  if (!errorFrame && count > 0 && Wire.available() < expectedBytes) {
    I2C_ERROR_PRINTF("[I2C] ERR Not enough data: need %d, have %d from slave 0x%02X", 
               expectedBytes, Wire.available(), address);
    errorFrame = true;
  }
  
  // Additional validation: keyboard should never send encoder data
  if (!errorFrame && address == I2C_ADDR_KEYBOARD && dataType == DATA_TYPE_ENCODER) {
    I2C_ERROR_PRINTF("[I2C] ERR Keyboard slave 0x%02X sent encoder data - corrupted!", address);
    errorFrame = true;
  }
  
  if (errorFrame) {
    while (Wire.available()) Wire.read();
    if (slaveIndex >= 0 && slaveIndex < numSlaves) {
      slaveBackoff[slaveIndex] = I2C_BACKOFF_CYCLES; // give this slave a breather
      I2C_ERROR_PRINTF("[I2C ERR] bad frame, backoff applied to slave 0x%02X detail=%d", address, count);
    }
    if (++i2cErrorStreak >= 3) {
      I2C_ERROR_PRINTF("[I2C ERR] resetting bus after repeated errors on 0x%02X", address);
      i2cErrorStreak = 0;
      resetI2CBus();
    }
    return;
  }
  
  // Good frame; clear error streak
  i2cErrorStreak = 0;
  
  // Process the validated data
  switch (dataType) {
    case DATA_TYPE_ENCODER:
      processEncoderData(count, address);
      break;
      
    case DATA_TYPE_KEYPRESS:
      processKeypressData(count, address);
      break;
  }
  
  // Clean up any remaining bytes
  while (Wire.available()) Wire.read();
}

// === ENCODER PROCESSING ===
void processEncoderData(uint8_t count, uint8_t address) {
  if (count == 0) return;
  
  I2C_DEBUG_PRINTF("[ENC] Slave 0x%02X: %d encoder events", address, count);
  
  for (int i = 0; i < count; i++) {
    if (Wire.available() < 2) {
      I2C_ERROR_PRINT("[I2C] ERR Not enough encoder data");
      break;
    }
    
    uint8_t encoderWithDir = Wire.read();  
    uint8_t velocity = Wire.read();        
    
    uint8_t encoderNumber = encoderWithDir & 0x7F;  
    bool isPositive = (encoderWithDir & 0x80) != 0; 
    
    // Validate encoder number and velocity
    if (encoderNumber > 20) {
      I2C_ERROR_PRINTF("[I2C] WARN Invalid encoder number: %d", encoderNumber);
      continue;
    }
    if (velocity > 10) {
      I2C_ERROR_PRINTF("[I2C] WARN Invalid velocity: %d", velocity);
      continue;
    }
    
    I2C_DEBUG_PRINTF("  Encoder %d: %s%d", encoderNumber, isPositive ? "+" : "-", velocity);
    
    sendEncoderOSC(encoderNumber, isPositive, velocity);                                   //ENCODER OSC SEND
  }
}

// === KEYPRESS PROCESSING ===
void processKeypressData(uint8_t count, uint8_t address) {
  if (count == 0) return;
  
  I2C_DEBUG_PRINTF("[KEY] Slave 0x%02X: %d key events", address, count);
  
  for (int i = 0; i < count; i++) {
    if (Wire.available() < 3) {
      I2C_ERROR_PRINT("[I2C] WARN Not enough keypress data");
      break;
    }
    
    uint8_t keyHigh = Wire.read();  
    uint8_t keyLow = Wire.read();   
    uint8_t state = Wire.read();    
    
    uint16_t keyNumber = (keyHigh << 8) | keyLow;  
    
    // Validate key number and state
    if (keyNumber < 101 || keyNumber > 410) {
      I2C_ERROR_PRINTF("[I2C] WARN Invalid key number: %d", keyNumber);
      continue;
    }
    if (state > 1) {
      I2C_ERROR_PRINTF("[I2C] WARN Invalid key state: %d", state);
      continue;
    }
    
    I2C_DEBUG_PRINTF("  Key %d: %s", keyNumber, state ? "PRESSED" : "RELEASED");

    int keyIndex = keyIndexFromNumber(keyNumber);
    if (keyIndex >= 0 && keyIndex < NUM_EXECUTORS_TRACKED) {
      trackedKeyStates[keyIndex] = (state == 1);
    }
    
        // Check for reset condition: key 401 pressed during startup window
    if (checkForReset && keyNumber == 401 && state == 1) {
      ++resetPressCount;
        if (resetPressCount >= 5) {
          I2C_DEBUG_PRINT("[NETWORK RESET]");
          resetNetworkDefaults();
          checkForReset = false;  // Prevent multiple resets
          return;  // Skip further processing of this event
        }
    }

      sendKeyOSC(keyNumber, state);

  }
}

void processReleaseAll(uint8_t address) {
  bool hasTrackedPress = false;
  for (int i = 0; i < NUM_EXECUTORS_TRACKED; i++) {
    if (trackedKeyStates[i]) {
      hasTrackedPress = true;
      break;
    }
  }

  // Only release keys we believe are pressed
  if (!hasTrackedPress) {
    return;
  }

  // Ensure USB host sees a clean slate when keystroke mode is enabled
  if (Fconfig.sendKeystrokes) {
    releaseAllKeys();
  }

  bool releasedAny = false;
  for (int i = 0; i < NUM_EXECUTORS_TRACKED; i++) {
    if (!trackedKeyStates[i]) {
      continue;
    }

    uint16_t keyNumber = keyNumberFromIndex(i);
    if (keyNumber == 0) {
      continue;
    }

    sendKeyOSC(keyNumber, 0);
    trackedKeyStates[i] = false;
    releasedAny = true;
  }

  if (releasedAny) {
    I2C_DEBUG_PRINTF("[KEY] Slave 0x%02X: release-all watchdog cleared keys", address);
  }
}


void sendEncoderOSC(int encoderNumber, bool isPositive, int velocity) { 

    // Validate encoder number
  if (encoderNumber > 20) {
    I2C_ERROR_PRINTF("[OSC] Invalid encoder number: %d", encoderNumber);
    return;
  }

    // Create the OSC address based on encoder number mapping
  char oscAddress[32];
  int executorKnobNumber;
  if (encoderNumber < 11) {
      // Encoders 0-9 map to ExecutorKnob401-410
    executorKnobNumber = 400 + encoderNumber;
  } else {
      // Encoders 10-19 map to ExecutorKnob301-310
    executorKnobNumber = 300 + (encoderNumber - 10);
  }

    // Format the OSC address
  snprintf(oscAddress, sizeof(oscAddress), "/Encoder%d", executorKnobNumber);
    // Create signed velocity value
  int signedVelocity = isPositive ? (int)velocity : -(int)velocity;

    // Send the OSC message
  sendOscMessage(oscAddress, ",i", &signedVelocity);

    // Debug output
  I2C_DEBUG_PRINTF("[OSC] Sent: %s %d (encoder %d)", oscAddress, signedVelocity, encoderNumber);

}



// Updated this function to send OSC or Keypress data over USB if that setting is checked
// May update this function name or add a seperate function later to keep things cleaner

void sendKeyOSC(uint16_t keyNumber, uint8_t state) {
  // Validate key number is in expected ranges
  if (!((keyNumber >= 101 && keyNumber <= 110) ||
        (keyNumber >= 201 && keyNumber <= 210) ||
        (keyNumber >= 301 && keyNumber <= 310) ||
        (keyNumber >= 401 && keyNumber <= 410))) {
    I2C_ERROR_PRINTF("[OSC] Invalid key number for OSC: %d", keyNumber);
    return;
  }
  
  // Validate state
  if (state > 1) {
    I2C_ERROR_PRINTF("[OSC] Invalid key state: %d", state);
    return;
  }
  
  // Send keypress if option is checked
  if (Fconfig.sendKeystrokes){

    // Send press if 1 and release if 0
    state ? sendKeyPress(keyNumber) : sendKeyRelease(keyNumber);

    I2C_DEBUG_PRINTF("[Key] Sent: %d %s", keyNumber, state ? "PRESSED" : "RELEASED");

  } else {

    // Create the OSC address
    char oscAddress[32];
    snprintf(oscAddress, sizeof(oscAddress), "/Key%d", keyNumber);
    
    // Convert state to int for OSC message
    int keyState = (int)state;
    
    // Send the OSC message
    sendOscMessage(oscAddress, ",i", &keyState);
    
    // Debug output
    I2C_DEBUG_PRINTF("[OSC] Sent: %s %d (key %d %s)", 
              oscAddress, keyState, keyNumber, state ? "PRESSED" : "RELEASED");

  }

}

