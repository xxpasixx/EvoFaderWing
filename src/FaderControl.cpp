// FaderControl.cpp
#include "FaderControl.h"
#include "NetworkOSC.h"
#include "TouchSensor.h"
#include "WebServer.h"
#include "Utils.h"
#include "NeoPixelControl.h"



bool FaderRetryPending = false;
unsigned long FaderRetryTime = 0;
static bool FaderMoveActive = false;

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
  int slowZone = Fconfig.slowZone;   // OSC units - start slowing earlier for smoother approach
  int fastZone = Fconfig.fastZone;   // OSC units - when to use full speed
  // Guard against invalid values (OSC is 0-100)
  if (slowZone < 0) slowZone = 0;
  if (fastZone < 0) fastZone = 0;
  if (slowZone > 100) slowZone = 100;
  if (fastZone > 100) fastZone = 100;
  if (fastZone <= slowZone) {
    slowZone = SLOW_ZONE;
    fastZone = FAST_ZONE;
  }
  
  
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
  if (FaderMoveActive) {
    // Already running; let the current pass finish using updated setpoints
    return;
  }
  FaderMoveActive = true;

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
    delay(1);
    
    // Add timeout protection to prevent infinite loops

    if (millis() - moveStartTime > FADER_MOVE_TIMEOUT) {
      // Stop all motors and flash red on faders that didn't reach target
      bool failed[NUM_FADERS] = {false};
      uint8_t origColors[NUM_FADERS][3] = {{0}};
      uint8_t scaledRed = (uint8_t)((255UL * Fconfig.touchedBrightness) / 255UL);

      for (int i = 0; i < NUM_FADERS; i++) {
        driveMotorWithPWM(faders[i], 0, 0);
        int currentOscValue = readFadertoOSC(faders[i]);
        int difference = faders[i].setpoint - currentOscValue;
        if (abs(difference) > Fconfig.targetTolerance && !faders[i].touched) {
          failed[i] = true;
          origColors[i][0] = faders[i].red;
          origColors[i][1] = faders[i].green;
          origColors[i][2] = faders[i].blue;
        }
      }

      // Flash all failed faders together (full strip red) without using
      for (int flash = 0; flash < 3; flash++) {
        for (int i = 0; i < NUM_FADERS; i++) {
          if (!failed[i]) continue;
          for (int j = 0; j < PIXELS_PER_FADER; j++) {
            pixels.setPixelColor(i * PIXELS_PER_FADER + j, pixels.Color(scaledRed, 0, 0));
          }
        }
        pixels.show();
        delay(150);

        for (int i = 0; i < NUM_FADERS; i++) {
          if (!failed[i]) continue;
          for (int j = 0; j < PIXELS_PER_FADER; j++) {
            pixels.setPixelColor(i * PIXELS_PER_FADER + j, pixels.Color(origColors[i][0], origColors[i][1], origColors[i][2]));
          }
        }
        pixels.show();
        delay(50);
      }
      
      // Set retry flag - Todo: Set a max retry, maybe a failed fader flag and visual feedback of failed fader all red leds?
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
  FaderMoveActive = false;
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
