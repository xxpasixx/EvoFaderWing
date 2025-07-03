// FaderControl.cpp
#include "FaderControl.h"
#include "NetworkOSC.h"
#include "TouchSensor.h"
#include "WebServer.h"
#include "Utils.h"
#include "NeoPixelControl.h"


const unsigned long RETRY_INTERVAL = 1000;  // 5 seconds before retry
bool FaderRetryPending = false;
unsigned long FaderRetryTime = 0;

//================================
// MOTOR CONTROL
//================================


void driveMotorWithPWM(Fader& f, int direction, int pwmValue) {
  if (direction == 0) {
    // Stop the motor
    digitalWrite(f.dirPin1, LOW);
    digitalWrite(f.dirPin2, LOW);
    analogWrite(f.pwmPin, 0);
    return;
  }
  
  // Set direction pins
  if (direction > 0) {
    // Move up/forward
    digitalWrite(f.dirPin1, HIGH);
    digitalWrite(f.dirPin2, LOW);
  } else {
    // Move down/backward
    digitalWrite(f.dirPin1, LOW);
    digitalWrite(f.dirPin2, HIGH);
  }
  
  // Apply custom PWM speed
  analogWrite(f.pwmPin, pwmValue);
  
  if (debugMode) {
    debugPrintf("Fader %d: Motor PWM: %d, Dir: %s, Setpoint: %d\n", 
               f.oscID, pwmValue, direction > 0 ? "UP" : "DOWN", f.setpoint);
  }
}

int calculateVelocityPWM(int difference) {
  int absDifference = abs(difference);
  
  // Define PWM ranges
  const int minPWM = Fconfig.minPwm;   // Minimum PWM to ensure movement (adjust as needed)
  const int maxPWM = Fconfig.defaultPwm;  // Use your existing max PWM
  
  // Define distance thresholds for different speeds
  const int slowZone = 5;   // OSC units - when to start slowing down DEFUALT GOOD MOVMENT WITH THIS UNIT
  const int fastZone = 20;  // OSC units - when to use full speed DEFUALT GOOD MOVMENT WITH THIS UNIT
  
  
  int pwmValue;
  
  if (absDifference >= fastZone) {
    // Far from target - use full speed
    pwmValue = maxPWM;
  } else if (absDifference <= slowZone) {
    // Close to target - use minimum speed
    pwmValue = minPWM;
  } else {
    // In between - linear interpolation
    float ratio = (float)(absDifference - slowZone) / (fastZone - slowZone);
    pwmValue = minPWM + (int)(ratio * (maxPWM - minPWM));
  }
  
  return pwmValue;
}


//================================
// MOVE ALL FADERs TO SETPOINT
//================================

void moveAllFadersToSetpoints() {
  bool allFadersAtTarget = false;
  unsigned long moveStartTime = millis();

  while (!allFadersAtTarget) {
    allFadersAtTarget = true; // Assume all are at target until proven otherwise
    

    for (int i = 0; i < NUM_FADERS; i++) {
      Fader& f = faders[i];
      
      // Read current position as OSC value
      int currentOscValue = readFadertoOSC(f);
      
      
      // Calculate difference in OSC units
      int difference = f.setpoint - currentOscValue;
      
      // Check if we need to move this fader (using a smaller tolerance for OSC units) IF NOT TOUCHING IT
      if (abs(difference) > Fconfig.targetTolerance && !f.touched) {
        allFadersAtTarget = false; // At least one fader is not at target
        
        

        if (difference > 0) {
          // Need to move up
          int pwm = calculateVelocityPWM(difference);
          driveMotorWithPWM(f, 1, pwm);
        } else {
          // Need to move down  
          int pwm = calculateVelocityPWM(difference);
          driveMotorWithPWM(f, -1, pwm);
        }

        if (debugMode) {
          debugPrintf("Fader %d: Current OSC: %d, Target OSC: %d, Diff: %d\n", 
                     f.oscID, currentOscValue, f.setpoint, difference);
        }

        } else {
          // Fader is at target, stop motor
          driveMotorWithPWM(f, 0, 0);
        }

    }
    
    // Small delay to prevent overwhelming the system
    delay(5);
    
    // Add timeout protection to prevent infinite loops

    if (millis() - moveStartTime > FADER_MOVE_TIMEOUT) {
      // Stop all motors and flash red on faders that didn't reach target
      for (int i = 0; i < NUM_FADERS; i++) {
        driveMotorWithPWM(faders[i], 0, 0);
        
        // Check if this fader failed to reach target
        int currentOscValue = readFadertoOSC(faders[i]);
        int difference = faders[i].setpoint - currentOscValue;
        
        if (abs(difference) > Fconfig.targetTolerance && !faders[i].touched) {
          // Flash red 3 times for failed faders
          uint8_t origR = faders[i].red, origG = faders[i].green, origB = faders[i].blue;
          for (int flash = 0; flash < 3; flash++) {
            faders[i].red = 255; faders[i].green = 0; faders[i].blue = 0;
            updateNeoPixels();
            delay(100);
            faders[i].red = origR; faders[i].green = origG; faders[i].blue = origB;
            updateNeoPixels();
            delay(100);
          }
        }
      }
      
      // Set retry flag
      FaderRetryPending = true;
      FaderRetryTime = millis() + RETRY_INTERVAL;
      
      if (debugMode) {
        debugPrintf("Fader movement timeout - will retry in %lu seconds\n", RETRY_INTERVAL/1000);
      }
      break;
    }

  }
  
  if (debugMode && allFadersAtTarget) {
    debugPrintf("All faders have reached their setpoints\n");
  }
}

// Function to set a new setpoint for a specific fader (called when OSC message received)
void setFaderSetpoint(int faderIndex, int oscValue) {
  if (faderIndex >= 0 && faderIndex < NUM_FADERS) {
    // Store the OSC value (0-100) directly as setpoint
    faders[faderIndex].setpoint = constrain(oscValue, 0, 100);
    
    if (debugMode) {
      debugPrintf("Fader %d setpoint set to OSC value: %d\n", 
                 faders[faderIndex].oscID, oscValue);
    }
  }
}



void handleFaders() {
  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];

    if (!f.touched){    
      continue;
    }

    // Read current position and get OSC value in one call
    int currentOscValue = readFadertoOSC(f);

      // Force send when at top or bottom and ignore rate limiting
    bool forceSend = (currentOscValue == 0 && f.lastReportedValue != 0) ||
                    (currentOscValue == 100 && f.lastReportedValue != 100);

    if (abs(currentOscValue - f.lastReportedValue) >= Fconfig.sendTolerance || forceSend) {
        f.lastReportedValue = currentOscValue;
        
        // If forcesend because fast move to top or bottom then ignore rate limiting
        if (forceSend) {
          sendFaderOsc(f, currentOscValue, true);
        } else {
          sendFaderOsc(f, currentOscValue, false);
        }
        
        f.setpoint = currentOscValue;

        if (debugMode) {
          debugPrintf("Fader %d position update: %d\n", f.oscID, currentOscValue);
        }
    }
    }
  }





// Read fader analog pin and return OSC value (0-100) using fader's calibrated range, with clamping at both ends
int readFadertoOSC(Fader& f) {
  int analogValue = analogRead(f.analogPin);

  // Clamp near-bottom analog values to force OSC = 0
  if (analogValue <= f.minVal + 4) {
    if (debugMode) {
      //debugPrintf("Fader %d: Clamped to 0 (analog=%d, minVal=%d)\n", f.oscID, analogValue, f.minVal);
    }
    return 0;
  }

  // Clamp near-top analog values to force OSC = 100
  if (analogValue >= f.maxVal - 4) {
    if (debugMode) {
      //debugPrintf("Fader %d: Clamped to 100 (analog=%d, maxVal=%d)\n", f.oscID, analogValue, f.maxVal);
    }
    return 100;
  }

  int oscValue = map(analogValue, f.minVal, f.maxVal, 0, 100);
  return constrain(oscValue, 0, 100);
}




void sendFaderOsc(Fader& f, int value, bool force) {
  unsigned long now = millis();

  // Only send if value changed significantly or enough time passed or force flag is set
  if (force || (abs(value - f.lastSentOscValue) >= Fconfig.sendTolerance && 
      now - f.lastOscSendTime > OSC_RATE_LIMIT)) {
    
    char oscAddress[32];
    snprintf(oscAddress, sizeof(oscAddress), "/Page%d/Fader%d", currentOSCPage, f.oscID);
    
    debugPrintf("Sending OSC update for Fader %d on Page %d → value: %d\n", f.oscID, currentOSCPage, value);
    
    // Use the existing sendOscMessage function instead of manual buffer building
    sendOscMessage(oscAddress, ",i", &value);
    
    f.lastOscSendTime = now;
    f.lastSentOscValue = value;
  }
}


// Returns the index of the fader with the given OSC ID, or -1 if not found
int getFaderIndexFromID(int id) {
  for (int i = 0; i < NUM_FADERS; i++) {
    if (faders[i].oscID == id) {
      return i;
    }
  }
  return -1;
}


void checkFaderRetry() {
  if (FaderRetryPending && millis() >= FaderRetryTime) {
    FaderRetryPending = false;
    if (debugMode) {
      debugPrint("Retrying fader movement...");
    }
    moveAllFadersToSetpoints();
  }
}