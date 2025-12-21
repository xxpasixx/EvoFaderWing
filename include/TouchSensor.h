// TouchSensor.h
#ifndef TOUCH_SENSOR_H
#define TOUCH_SENSOR_H

#include <Arduino.h>
#include <Wire.h>
#include "Config.h"

#if defined(TOUCH_SENSOR_MTCH2120)
#include <MTCH2120.h>
#elif defined(TOUCH_SENSOR_MPR121)
#include <Adafruit_MPR121.h>
#else
#error "Select a touch sensor via TOUCH_SENSOR_MTCH2120 or TOUCH_SENSOR_MPR121"
#endif

//================================
// TOUCH SENSOR CONFIGURATION
//================================

// Debounce time (in milliseconds)
#define TOUCH_CONFIRM_MS 30
#define RELEASE_CONFIRM_MS 30

// Register addresses for MPR121 auto-calibration settings
#define MPR121_MHDR         0x2B    // Maximum Half Delta Rising
#define MPR121_NHDR         0x2C    // Noise Half Delta Rising
#define MPR121_NCLR         0x2D    // Noise Count Limit Rising
#define MPR121_FDLR         0x2E    // Filter Delay Rising
#define MPR121_MHDF         0x2F    // Maximum Half Delta Falling
#define MPR121_NHDF         0x30    // Noise Half Delta Falling
#define MPR121_NCLF         0x31    // Noise Count Limit Falling
#define MPR121_FDLF         0x32    // Filter Delay Falling
#define MPR121_NHDT         0x33    // Noise Half Delta Touched
#define MPR121_NCLT         0x34    // Noise Count Touched
#define MPR121_FDLT         0x35    // Filter Delay Touched

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
void setAutoTouchCalibration(int mode);
void configureAutoCalibration();
void recalibrateBaselines();

// Error handling functions
void handleTouchError();
String getLastTouchError();
bool hasTouchError();
void clearTouchError();

// Utility functions
void printFaderTouchStates();

#endif // TOUCH_SENSOR_H
