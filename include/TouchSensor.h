// TouchSensor.h
#ifndef TOUCH_SENSOR_H
#define TOUCH_SENSOR_H

#include <Arduino.h>
#include <Wire.h>
#include "Config.h"

#if defined(TOUCH_SENSOR_MTCH2120)
#include "MTCH2120.h"
#elif defined(TOUCH_SENSOR_MPR121)
#include <Adafruit_MPR121.h>
#else
#error "Select a touch sensor via TOUCH_SENSOR_MTCH2120 or TOUCH_SENSOR_MPR121"
#endif

//================================
// TOUCH SENSOR CONFIGURATION
//================================

// Backup poll interval to catch missed IRQs and clear release debound (ms)
#define TOUCH_BACKUP_POLL_MS 150
// Release debounce time (ms) applied after a raw release is seen (hold touched a little longer for the fader to stablize before release)
#define RELEASE_DEBOUNCE_MS 150

//================================
// ERROR HANDLING CONSTANTS
//================================
extern const int MAX_REINIT_ATTEMPTS;
extern const unsigned long REINIT_DELAY_BASE;

//================================
// GLOBAL VARIABLES
//================================
extern volatile bool touchStateChanged;
extern bool touchErrorOccurred;
extern String lastTouchError;
extern int reinitializationAttempts;
extern unsigned long lastReinitTime;

// Debounce arrays
extern bool touchConfirmed[NUM_FADERS];
#if defined(TOUCH_SENSOR_MPR121)
extern Adafruit_MPR121 mpr121;
extern unsigned long debounceStart[NUM_FADERS];
#elif defined(TOUCH_SENSOR_MTCH2120)
extern MTCH2120 touchSensor;
#endif

//================================
// FUNCTION DECLARATIONS
//================================

// Main setup and processing functions
bool setupTouch();
bool processTouchChanges();

// Interrupt handler
void handleTouchInterrupt();

// Touch timing functions
void updateTouchTiming(int i, bool newTouchState);

// Calibration functions
void manualTouchCalibration();
void runTouchCalibration();
void setAutoTouchCalibration(int mode);
void configureAutoCalibration();

// Error handling functions
void handleTouchError();
String getLastTouchError();
bool hasTouchError();
void clearTouchError();

// Utility functions
void printFaderTouchStates();

#endif // TOUCH_SENSOR_H
