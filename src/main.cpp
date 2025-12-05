// EvoFaderWing
// Main.cpp

// Teensy 4.1

#include <Arduino.h>
#include <QNEthernet.h>
#include <LiteOSCParser.h>

#include "Config.h"
#include "EEPROMStorage.h"
#include "TouchSensor.h"
#include "NetworkOSC.h"
#include "FaderControl.h"
#include "NeoPixelControl.h"
#include "WebServer.h"
#include "Utils.h"
#include "i2cPolling.h"
#include "OLED.h"
#include "Keysend.h"
#include "KeyLedControl.h"

using namespace qindesign::network;
using qindesign::osc::LiteOSCParser;


unsigned long lastI2CPollTime = 0;     // Time of last I2C poll cycle

OLED display;             // define display 
IPAddress currentIP;      // define currentIP

//================================
// MAIN ARDUINO FUNCTIONS
//================================

void setup() {
  // Start up keyboard first to make sure of enum in windows
  initKeyboard();

  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 4000) {}
  
  debugPrint("EvoFaderWing init...");

  // Initialize faders
  initializeFaders();
  configureFaderPins();
  
  // Initialize Touch MPR121 
  if (!setupTouch()) {
    debugPrint("Touch sensor init failed!");
  }

  // Start NeoPixels
  setupNeoPixels();
  setupKeyLeds();

    // Check calibration will load calibration data if present ortherwise it will run calibration
  checkCalibration(); 

  // Load configurations from EEPROM
  loadAllConfig();

  moveAllFadersToSetpoints();

  //Setup I2C Slaves so we can also check for network reset
  setupI2cPolling();
  
  // Setup OLED before network to watch for no dhcp server and know were booting
  display.setupOLED();

  // Set up network connection
  setupNetwork();

  displayIPAddress();

  // Start web server for configuration
  startWebServer();

  fadeSequence(50,1000); // Cool effect so we know we are booted up

  //Network reset check
  resetCheckStartTime = millis();

  debugPrint("Initialization complete");

}

void loop() {
  // Network reset check exiry PRESS 401 5 times during this time for network reset

  if (checkForReset && (millis() - resetCheckStartTime > 5000)) {
    checkForReset = false;
    debugPrint("[RESET] Reset check window expired.");
  }
  
  checkFaderRetry();  // Check for hung fader

  // Check for manual fader movement
  handleFaders();

  // Handle I2C Polling for encoders keypresses and encoder key press
  handleI2c();

    
  // Process touch changes 
  if (processTouchChanges()) {
    updateBrightnessOnFaderTouchChange();
    printFaderTouchStates();                //verbose debug output
  }

  // Check for web requests
  pollWebServer();
  

  // Handle touch sensor errors, no longer needed used for debugging
  if (hasTouchError()) {
    debugPrint(getLastTouchError().c_str());
    clearTouchError();
  }
  
    // Update NeoPixels
  updateNeoPixels();
  updateKeyLeds();

  // Check for reboot from serial, used for uploading firmware without having to press physical button
  checkSerialForReboot();

}


// oled display functions

void displayIPAddress(){
  currentIP = Ethernet.localIP();
  display.showIPAddress(currentIP,netConfig.receivePort,netConfig.sendToIP,netConfig.sendPort);

}

void displayShowResetHeader(){
  display.clear();
  display.showHeader("Network Reset");
}
