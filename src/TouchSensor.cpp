// TouchSensor.cpp

#include "TouchSensor.h"
#include "Utils.h"

//================================
// GLOBAL VARIABLES DEFINITIONS
//================================


// MPR121 sensor object
Adafruit_MPR121 mpr121 = Adafruit_MPR121();

// Optional debug flag for printing raw touch data
bool touchDebug = false;
unsigned long touchDebugIntervalMs = 500;  // Minimum time between debug prints
unsigned long lastTouchDebugTime = 0;

// Interrupt and error handling
volatile bool touchStateChanged = false;
bool touchErrorOccurred = false;
String lastTouchError = "";
int reinitializationAttempts = 0;
unsigned long lastReinitTime = 0;
const int MAX_REINIT_ATTEMPTS = 5;
const unsigned long REINIT_DELAY_BASE = 1000;  // 1 second base delay

// Debounce arrays
unsigned long debounceStart[NUM_FADERS] = {0};
bool touchConfirmed[NUM_FADERS] = {false};

//================================
// INTERRUPT HANDLER
//================================

void handleTouchInterrupt() {
  touchStateChanged = true;
}

//================================
// TOUCH TIMING FUNCTIONS
//================================

void updateTouchTiming(int i, bool newTouchState) {
  unsigned long currentTime = millis();
  
  // If state changed from released to touched
  if (newTouchState && !faders[i].touched) {
    faders[i].touchStartTime = currentTime;
    faders[i].touchDuration = 0;
  }
  // If state changed from touched to released
  else if (!newTouchState && faders[i].touched) {
    faders[i].releaseTime = currentTime;
    // Calculate how long it was touched
    faders[i].touchDuration = currentTime - faders[i].touchStartTime;
  }
  // If continuing to be touched, update duration
  else if (newTouchState && faders[i].touched) {
    faders[i].touchDuration = currentTime - faders[i].touchStartTime;
  }
  
  faders[i].touched = newTouchState;
}

//================================
// MAIN SETUP FUNCTION
//================================

bool setupTouch() {
  // Configure IRQ pin as input with internal pullup resistor
  pinMode(IRQ_PIN, INPUT_PULLUP);
  
  // Start I2C communication
  Wire.begin();
  
  // Try to initialize the MPR121 sensor
  if (!mpr121.begin(MPR121_ADDRESS)) {
    touchErrorOccurred = true;
    lastTouchError = "MPR121 not found at address 0x5A. Check wiring!";
    return false;
  }

  // Configure auto-calibration using the library's autoconfig path
  configureAutoCalibration();

  // Set global touch and release thresholds for all electrodes (after autoconfig)
  mpr121.setThresholds(touchThreshold, releaseThreshold);
  
  // Initialize debounce and state arrays
  for (int i = 0; i < NUM_FADERS; i++) {
    debounceStart[i] = 0;
    touchConfirmed[i] = false;
  }
  
  // Attach interrupt handler to IRQ pin
  attachInterrupt(digitalPinToInterrupt(IRQ_PIN), handleTouchInterrupt, FALLING);
  
  // Initialization successful
  return true;
}

//================================
// MAIN PROCESSING FUNCTION
//================================

bool processTouchChanges() {
  //static uint16_t lastRawTouchBits = 0;
  uint16_t currentTouches = mpr121.touched();
  unsigned long now = millis();
  bool stateUpdated = false;

  if (touchDebug && now - lastTouchDebugTime >= touchDebugIntervalMs) {
    lastTouchDebugTime = now;
    debugPrint("Raw Touch Values:");
    for (int j = 0; j < NUM_FADERS; j++) {
      uint16_t baseline = mpr121.baselineData(j);
      uint16_t filtered = mpr121.filteredData(j);
      int16_t delta = baseline - filtered;
      debugPrintf("Fader %d - Base: %u, Filtered: %u, Delta: %d", j, baseline, filtered, delta);
    }
  }

  if (currentTouches == 0xFFFF) {
    handleTouchError();
    return false;
  }

  for (int i = 0; i < NUM_FADERS; i++) {
    int touchRead = i;
    if (i < 2) {
      touchRead = i + 10;  // Faders 0 and 1 are on electrodes 10 and 11
    }
    //touchRead = 2 + i;
    bool rawTouch = bitRead(currentTouches, touchRead);

    // === TOUCH DETECTED ===
    if (rawTouch && !touchConfirmed[i]) {
      // Start debounce timer if just now touched
      if (debounceStart[i] == 0) {
        debounceStart[i] = now;
      }
      // Confirm after debounce time
      else if (now - debounceStart[i] >= TOUCH_CONFIRM_MS) {
        touchConfirmed[i] = true;
        debounceStart[i] = 0;
        updateTouchTiming(i, true);
        stateUpdated = true;
      }
    }

    // === RELEASE DETECTED ===
    else if (!rawTouch && touchConfirmed[i]) {
      // Start debounce timer if just now released
      if (debounceStart[i] == 0) {
        debounceStart[i] = now;
      }
      // Confirm release after debounce time
      else if (now - debounceStart[i] >= RELEASE_CONFIRM_MS) {
        touchConfirmed[i] = false;
        debounceStart[i] = 0;
        updateTouchTiming(i, false);
        stateUpdated = true;
      }
    }

    // === NO CHANGE ===
    else {
      debounceStart[i] = 0;  // Reset if bouncing
    }

    // While held, update duration
    if (touchConfirmed[i]) {
      faders[i].touchDuration = now - faders[i].touchStartTime;
    }
  }

  return stateUpdated;
}

//================================
// CALIBRATION FUNCTIONS
//================================

void applyAutoconfig(bool enable) {
  if (enable) {
    // Enable Adafruit's autoconfig with standard limits for 3.3V
    mpr121.writeRegister(MPR121_AUTOCONFIG0, 0x0B);
    mpr121.writeRegister(MPR121_UPLIMIT, 200);
    mpr121.writeRegister(MPR121_TARGETLIMIT, 180);
    mpr121.writeRegister(MPR121_LOWLIMIT, 130);
  } else {
    // Disable autoconfig
    mpr121.writeRegister(MPR121_AUTOCONFIG0, 0x00);
  }
}

void manualTouchCalibration() {
  recalibrateBaselines();
}

void recalibrateBaselines() {
  configureAutoCalibration();
  mpr121.setThresholds(touchThreshold, releaseThreshold);
}

//================================
// AUTO-CALIBRATION FUNCTIONS
//================================

void setAutoTouchCalibration(int mode) {
  // Validate input
  if (mode < 0 || mode > 1) {
    touchErrorOccurred = true;
    lastTouchError = "Invalid auto-calibration mode. Use 0 or 1.";
    return;
  }
  
  // Update global mode variable
  autoCalibrationMode = mode;
  
  // Apply the new calibration settings
  configureAutoCalibration();
}

void configureAutoCalibration() {
  // Toggle autoconfig based on mode: 0 = off, 1/2 = on
  mpr121.writeRegister(MPR121_ECR, 0x00);  // Stop electrodes while reconfiguring
  bool enableAutoconfig = autoCalibrationMode != 0;
  applyAutoconfig(enableAutoconfig);
  mpr121.writeRegister(MPR121_ECR, 0x8C);  // Enable 12 electrodes (0x80 + 12)
}

//================================
// ERROR HANDLING
//================================

void handleTouchError() {
  touchErrorOccurred = true;
  // Always attempt immediate reinit; no backoff/limits
  Wire.end();
  delay(50);
  Wire.begin();
  delay(50);

  if (!mpr121.begin(MPR121_ADDRESS)) {
    lastTouchError = "MPR121 reinit failed";
    return;
  }

  // Reinit successful - restore settings
  mpr121.setThresholds(touchThreshold, releaseThreshold);
  configureAutoCalibration();

  touchErrorOccurred = false;
  reinitializationAttempts = 0;
  lastTouchError = "Recovered from error";
}

String getLastTouchError() {
  return lastTouchError;
}

bool hasTouchError() {
  return touchErrorOccurred;
}

void clearTouchError() {
  touchErrorOccurred = false;
  lastTouchError = "";
  reinitializationAttempts = 0;
}

//================================
// UTILITY FUNCTIONS
//================================

void printFaderTouchStates() {
  if (!touchDebug){
    return;
  }
  debugPrint("Fader Touch States:");
  for (int i = 0; i < NUM_FADERS; i++) {
    if (faders[i].touched) {
      debugPrintf("  Fader %d: TOUCHED (%lums)", i, faders[i].touchDuration);
    } else {
      debugPrintf("  Fader %d: released", i);
    }
  }
}
