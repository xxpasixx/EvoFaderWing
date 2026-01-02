// Utils.cpp

#include "OLED.h"
#include "Utils.h"
#include "Config.h"
#include <stdarg.h>

extern OLED display;

//================================
// DEBUG FUNCTIONS
//================================

void debugPrint(const char* message) {
  if (debugMode) {
    Serial.println(message);
  }
}

void debugPrintf(const char* format, ...) {
  if (debugMode) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Check if the format string already ends with a newline
    size_t len = strlen(format);
    if (len > 0 && format[len-1] == '\n') {
      Serial.print(buffer); // Already has newline
    } else {
      Serial.println(buffer); // Add newline
    }
  }
}

//================================
// IP ADDRESS UTILITIES
//================================

String ipToString(IPAddress ip) {
  char buf[16];
  sprintf(buf, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return String(buf);
}

IPAddress stringToIP(const String &str) {
  int parts[4] = {0};
  sscanf(str.c_str(), "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
  return IPAddress(parts[0], parts[1], parts[2], parts[3]);
}

//================================
// WEB PARAMETER PARSING
//================================

String getParam(String data, const char* key) {
  int start = data.indexOf(String(key) + "=");
  if (start == -1) return "";
  start += strlen(key) + 1;
  int end = data.indexOf('&', start);
  if (end == -1) end = data.length();
  return data.substring(start, end);
}

//================================
// UPLOAD Function 
//================================
//Upload without pressing button, using python script, takes one second try
void checkSerialForReboot() {
    static String commandBuffer = "";
    
    // Read all available characters and buffer them
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            // End of command received, process it
            commandBuffer.trim(); // Remove any whitespace
            String cmd = commandBuffer;
            commandBuffer = ""; // Clear buffer for next command
            
            // Process the complete command
            if (cmd.length() > 0) {
                processSerialCommand(cmd);
            }
            return;
        } else {
            // Add character to buffer
            commandBuffer += c;
        }
    }
}

// Send identiy so we can update a specific teensy when more than one is plugged in, used with teensy_auto_upload_multi.py
void processSerialCommand(String cmd) {
    if (cmd == "IDENTIFY") {
        Serial.print("[IDENT] ");
        Serial.print(PROJECT_NAME);
        Serial.print(" v");
        Serial.println(SW_VERSION);
        Serial.flush();
        
    } else if (cmd == "REBOOT_BOOTLOADER") {
        Serial.print("[REBOOT] ");
        Serial.print(PROJECT_NAME);
        Serial.print(" v");
        Serial.print(SW_VERSION);
        Serial.println(" entering bootloader...");
        Serial.flush(); // Important: ensure message is sent before reboot
        delay(100);
        
        // This is the correct method for ALL Teensy models
        _reboot_Teensyduino_();
        
    } else if (cmd == "REBOOT_NORMAL") {
        Serial.print("[REBOOT] ");
        Serial.print(PROJECT_NAME);
        Serial.print(" v");
        Serial.print(SW_VERSION);
        Serial.println(" normal reboot requested...");
        Serial.flush();
        delay(100);
        
        // Normal restart using ARM AIRCR register
        resetTeensy();
        
    } else {
        Serial.print("[REBOOT] Unknown command: ");
        Serial.println(cmd);
    }
}

void resetTeensy(){

  SCB_AIRCR = 0x05FA0004;

}
