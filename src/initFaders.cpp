// InitFaders.cpp

#include "FaderControl.h"
#include "Utils.h"
#include "WebServer.h"
#include "NeoPixelControl.h"

// Calibration timeout in milliseconds
const unsigned long calibrationTimeout = 2000;

//================================
// FADER INITIALIZATION
//================================

void initializeFaders() {
  // Initialize struct array fields
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].analogPin = ANALOG_PINS[i];
    faders[i].pwmPin = PWM_PINS[i];
    faders[i].dirPin1 = DIR_PINS1[i];
    faders[i].dirPin2 = DIR_PINS2[i];
    faders[i].minVal = 10;    // Keep default range small to avoid not being able to hit 0 and 100 percent
    faders[i].maxVal = 245;  // we might lose a little precision but its better
    faders[i].setpoint = 0;
    faders[i].motorEnabled = true;
    faders[i].failureCount = 0;
    faders[i].lastFailureTime = 0;
    faders[i].lastReportedValue = -1;
    faders[i].lastOscSendTime = 0;
    faders[i].oscID = OSC_IDS[i];
    
    
    faders[i].lastSentOscValue = -1;
    
    // Initialize color
    faders[i].red = Fconfig.baseBrightness;
    faders[i].green = Fconfig.baseBrightness;
    faders[i].blue = Fconfig.baseBrightness;

    // Initialize touch timing values
    faders[i].touched = false;
    faders[i].touchStartTime = 0;
    faders[i].touchDuration = 0;
    faders[i].releaseTime = 0;

    // Initialize brightness values
    faders[i].currentBrightness = Fconfig.baseBrightness;
    faders[i].targetBrightness = Fconfig.baseBrightness;
    faders[i].brightnessStartTime = 0;
    faders[i].lastReportedBrightness = 0;
  
  }
}

//================================
// Cconfigure all fader pins
//================================

void configureFaderPins() {
  // Configure pins for each fader
  
  analogReadResolution(8);  // Set resolution to 8bit it will return 0-255
  analogReadAveraging(16);  // Sets the ADC to always average 16 samples so we dont need to smooth reading ourselves

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    pinMode(f.pwmPin, OUTPUT);
    pinMode(f.dirPin1, OUTPUT);
    pinMode(f.dirPin2, OUTPUT);
  
    f.setpoint = 50;  // Set faders to center point
    
    // Initialize state
    f.touched = false;
  }

}



//================================
// CALIBRATION
//================================

void calibrateFaders() {
  debugPrintf("Calibration started at PWM: %d\n", Fconfig.calibratePwm);
  calibrationInProgress = true;
  
  // Store original colors before calibration
  uint8_t originalColors[NUM_FADERS][3];
  uint8_t originalPosition[NUM_FADERS];
  bool failedFaders[NUM_FADERS] = {false};

  for (int i = 0; i < NUM_FADERS; i++) {
    originalColors[i][0] = faders[i].red;
    originalColors[i][1] = faders[i].green;
    originalColors[i][2] = faders[i].blue;

    originalPosition[i] = faders[i].setpoint;

    faders[i].red = 255;
    faders[i].green = 0;
    faders[i].blue = 0;
  }
  updateNeoPixels();
  
  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    
 
    
    // ==================== MAX VALUE CALIBRATION ====================
    debugPrintf("Fader %d → Calibrating Max...\n", i);
    
    // SET YELLOW - Calibrating max
    f.red = 255; f.green = 255; f.blue = 0;
    updateNeoPixels();
    
    analogWrite(f.pwmPin, Fconfig.calibratePwm);
    digitalWrite(f.dirPin1, HIGH); digitalWrite(f.dirPin2, LOW);
    int last = 0, plateau = 0;
    
    // Add timeout for max calibration
    unsigned long startTime = millis();
    bool maxCalibrationSuccess = false;
    
    while (plateau < PLATEAU_COUNT) {
      // Check for timeout (10 seconds)
      if ((millis() - startTime) > calibrationTimeout) {
        debugPrintf("ERROR: Fader %d MAX calibration timed out! Using default value of 245.\n", i);
        f.maxVal = 245;  // Use default max value
        break;  // Exit the loop
      }
      
      int val = analogRead(f.analogPin);
      plateau = (abs(val - last) < PLATEAU_THRESH) ? plateau + 1 : 0;
      last = val;
      delay(10);
      
      pollWebServer();  // Allow web UI to remain responsive
      yield();          // Let MPR121 and Ethernet process in background
      
      // If we reach this point with required plateau count, calibration succeeded
      if (plateau >= PLATEAU_COUNT) {
        maxCalibrationSuccess = true;
        f.maxVal = last - 2;  //subtract a litle value to create a dead zone at top (sometimes required to reach)
      }
    }
    
    // Stop motor
    analogWrite(f.pwmPin, 0);
    delay(500);

    // ==================== MIN VALUE CALIBRATION ====================
    debugPrint("→ Calibrating Min...");
    
    // SET ORANGE - Calibrating min
    f.red = 0; f.green = 0; f.blue = 255;
    updateNeoPixels();
    
    analogWrite(f.pwmPin, Fconfig.calibratePwm);
    digitalWrite(f.dirPin1, LOW); digitalWrite(f.dirPin2, HIGH);
    plateau = 0;
    
    // Reset for min calibration
    startTime = millis();
    bool minCalibrationSuccess = false;
    
    while (plateau < PLATEAU_COUNT) {
      // Check for timeout 
      if ((millis() - startTime) > calibrationTimeout) {
        debugPrintf("ERROR: Fader %d MIN calibration timed out! Using default value of 10.\n", i);
        f.minVal = 10;  // Use default min value
        break;  // Exit the loop
      }
      
      int val = analogRead(f.analogPin);
      plateau = (abs(val - last) < PLATEAU_THRESH) ? plateau + 1 : 0;
      last = val;
      delay(10);

      pollWebServer();  // Allow web UI to remain responsive
      yield();          // Let MPR121 and Ethernet process in background
      
      // If we reach this point with required plateau count, calibration succeeded
      if (plateau >= PLATEAU_COUNT) {
        minCalibrationSuccess = true;
        f.minVal = last + 3;  //Add a litle value to make a deadzone at the bottom
      }
    }
    
    // Stop motor
    analogWrite(f.pwmPin, 0);
    
    // SET GREEN - This fader is done
    f.red = 0; f.green = 255; f.blue = 0;
    updateNeoPixels();
    
    // Output results with status indicator
    bool rangeValid = true;
    if (maxCalibrationSuccess && minCalibrationSuccess) {
      // Validate min and max values: expect near full travel on 8-bit range (~20% margins)
      bool minTooHigh = f.minVal > 51;    // >20% from bottom (255*0.2 ≈ 51)
      bool maxTooLow = f.maxVal < 204;    // <80% of top (255-51)
      bool spanTooSmall = (f.maxVal - f.minVal) < 153; // <60% span (255*0.6)

      if (minTooHigh || maxTooLow || spanTooSmall) {
        debugPrintf("ERROR: Fader %d has invalid range! Min=%d, Max=%d (minTooHigh=%d maxTooLow=%d spanTooSmall=%d). Using defaults.\n", 
                    i, f.minVal, f.maxVal, minTooHigh, maxTooLow, spanTooSmall);
        f.minVal = 10;
        f.maxVal = 245;
        rangeValid = false;
      }
    }

    bool faderFailed = !maxCalibrationSuccess || !minCalibrationSuccess || !rangeValid;

    if (maxCalibrationSuccess && minCalibrationSuccess && rangeValid) {
      debugPrintf("→ Calibration Done: Min=%d Max=%d\n", f.minVal, f.maxVal);
    } else {
      debugPrintf("→ Calibration INCOMPLETE for Fader %d: Min=%d Max=%d (Defaults applied where needed)\n", 
                  i, f.minVal, f.maxVal);
      failedFaders[i] = true;
      // Keep failure indicated in red
      f.red = 255; f.green = 0; f.blue = 0;
      updateNeoPixels();
    }
    
    // Reset setpoint
    f.setpoint = analogRead(f.analogPin);
  }

  // Flash failed faders at 10Hz for ~3 seconds to highlight issues
  bool anyFailed = false;
  for (int i = 0; i < NUM_FADERS; i++) {
    if (failedFaders[i]) {
      anyFailed = true;
      break;
    }
  }

  if (anyFailed) {
    const int flashCycles = 30; // 10 Hz for ~3 seconds
    for (int cycle = 0; cycle < flashCycles; cycle++) {
      bool on = (cycle % 2 == 0);
      for (int i = 0; i < NUM_FADERS; i++) {
        if (!failedFaders[i]) {
          continue;
        }
        uint32_t color = on ? pixels.Color(255, 0, 0) : pixels.Color(0, 0, 0);
        for (int j = 0; j < PIXELS_PER_FADER; j++) {
          pixels.setPixelColor(i * PIXELS_PER_FADER + j, color);
        }
      }
      pixels.show();
      delay(100);
    }
  }
  
  // All faders done - restore original colors
  
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].red = originalColors[i][0];
    faders[i].green = originalColors[i][1];
    faders[i].blue = originalColors[i][2];

    faders[i].setpoint = originalPosition[i];
  }
  
  fadeSequence(25,500);

  //updateNeoPixels();
  moveAllFadersToSetpoints();

  calibrationInProgress = false;
}
