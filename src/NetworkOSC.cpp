// NetworkOSC.cpp

#include "NetworkOSC.h"
#include "Utils.h"
#include "FaderControl.h"
#include "Config.h"
#include "ExecutorStatus.h"
#include "KeyLedControl.h"
#include <AsyncUDP_Teensy41.h>


//================================
// GLOBAL NETWORK OBJECTS
//================================

AsyncUDP oscUdp;

// Forward declarations for async callbacks
void handleBundledFaderUpdate(LiteOSCParser& parser);
void handleBundledExecutorUpdate(LiteOSCParser& parser);

//================================
// NETWORK SETUP
//================================

static void attachUdpHandler() {
  oscUdp.onPacket([](AsyncUDPPacket &packet) {
    const uint8_t* data = packet.data();
    size_t len = packet.length();
    LiteOSCParser parser;

    if (!parser.parse(data, len)) {
      debugPrint("Invalid OSC message.");
      return;
    }

    const char* addr = parser.getAddress();

    if (strstr(addr, "/faderUpdate") != NULL) {
      handleBundledFaderUpdate(parser);
    } else if (strstr(addr, "/execUpdate") != NULL) {
      handleBundledExecutorUpdate(parser);
    } else if (strstr(addr, "/updatePage/current") != NULL) {
      if (parser.getTag(0) == 'i') {
        handlePageUpdate(addr, parser.getInt(0));
      }
    }
  });
}

void setupNetwork() {
  debugPrint("Setting up network...");
  
  delay(100); //make sure we see network debug message before looking for dhcp so we know we are booting up

  Ethernet.setHostname(kServiceName);

  // Start Ethernet with configured settings
  if (netConfig.useDHCP) {
    debugPrint("Using DHCP...");
    if (!Ethernet.begin() || !Ethernet.waitForLocalIP(kDHCPTimeout)) {
      debugPrint("Failed DHCP, switching to static IP");
      Ethernet.begin(netConfig.staticIP, netConfig.subnet, netConfig.gateway);
    }
  } else {
    debugPrint("Using static IP...");
    Ethernet.begin(netConfig.staticIP, netConfig.subnet, netConfig.gateway);
  }

  IPAddress ip = Ethernet.localIP();
  debugPrintf("IP Address: %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);

  // Set up mDNS for service discovery
  MDNS.begin(kServiceName);
  MDNS.addService("_osc", "_udp", netConfig.receivePort);

  // Start AsyncUDP listener
  if (oscUdp.listen(netConfig.receivePort)) {
    attachUdpHandler();
    debugPrintf("AsyncUDP listening on port %d\n", netConfig.receivePort);
  } else {
    debugPrint("Failed to start AsyncUDP listener");
  }
  debugPrint("OSC and mDNS initialized");
}

// Restart UDP after changes to network settings
void restartUDP() {
  debugPrint("Restarting UDP service...");

  oscUdp.close();
  delay(10);

  if (oscUdp.listen(netConfig.receivePort)) {
    attachUdpHandler();
    debugPrintf("UDP restarted on port %d\n", netConfig.receivePort);
  } else {
    debugPrint("Failed to restart UDP.");
  }

  // Re-register mDNS if needed
  MDNS.addService("_osc", "_udp", netConfig.receivePort);
}



//================================
//OSC MESSAGE HANDLING
//================================

// Page update message handling
void handlePageUpdate(const char *address, int value) {
  if (strstr(address, "/updatePage/current") != NULL) {
    if (value != currentOSCPage) {
      debugPrintf("Page changed from %d to %d (via updatePage command)\n", currentOSCPage, value);
    }
    currentOSCPage = value;
  }
}



// Handle bundled fader update messages
void handleBundledFaderUpdate(LiteOSCParser& parser) {
  // Expected format: /faderUpdate,iiiiiiiiiiissssssssss,PAGE,F201,F202...F210,C201,C202...C210
  
  if (parser.getArgCount() < 21) {
    debugPrint("Invalid bundled fader message - not enough arguments");
    return;
  }
  
  // Parse page number (argument 0)
  if (parser.getTag(0) != 'i') {
    debugPrint("Invalid bundled fader message - page not integer");
    return;
  }
  
  int pageNum = parser.getInt(0);
  
  // Update current page if it changed
  if (pageNum != currentOSCPage) {
    debugPrintf("Page changed from %d to %d (via bundled message)\n", currentOSCPage, pageNum);
    currentOSCPage = pageNum;
  }
  
  // Track if any setpoints need updating
  bool needToMoveFaders = false;
  
  // Parse fader values (arguments 1-10 for faders 201-210)
  for (int i = 0; i < 10; i++) {
    int argIndex = i + 1; // Arguments 1-10
    int faderOscID = 201 + i; // Fader IDs 201-210
    
    if (parser.getTag(argIndex) != 'i') {
      debugPrintf("Invalid fader value type for fader %d\n", faderOscID);
      continue;
    }
    
    int oscValue = parser.getInt(argIndex);
    int faderIndex = getFaderIndexFromID(faderOscID);
    
    if (faderIndex >= 0 && faderIndex < NUM_FADERS) {
      // Only update if fader is not currently being touched (avoid feedback)
      if (!faders[faderIndex].touched) {
        
        // Check if the value actually changed before updating
        int currentOscvalue = readFadertoOSC(faders[faderIndex]);

        if (abs(oscValue - currentOscvalue) > Fconfig.targetTolerance) {
          debugPrintf("Updating fader %d setpoint: %d -> %d\n", faderOscID, currentOscvalue, oscValue);
          setFaderSetpoint(faderIndex, oscValue);
          needToMoveFaders = true;
        }
      }
    } else {
      debugPrintf("Fader index not found for OSC ID %d\n", faderOscID);
    }
  }
  
  
  // Parse color values (arguments 11-20 for faders 201-210)
  for (int i = 0; i < 10; i++) {
    int argIndex = i + 11; // Arguments 11-20
    int faderOscID = 201 + i; // Fader IDs 201-210
    
    if (parser.getTag(argIndex) != 's') {
      debugPrintf("Invalid color value type for fader %d\n", faderOscID);
      continue;
    }
    
    const char* colorString = parser.getString(argIndex);
    int faderIndex = getFaderIndexFromID(faderOscID);
    
    if (faderIndex >= 0 && faderIndex < NUM_FADERS && !faders[faderIndex].touched) {
      // Parse and update color values
      parseDualColorValues(colorString, faders[faderIndex]);
      //debugPrintf("Updated color for fader %d: %s\n", faderOscID, colorString);
    } else {
      if (faders[faderIndex].touched) {
        debugPrintf("Fader %d: Update ignored - fader is touched\n", faderOscID);
      }
    }
  }

    // Move all faders to their new setpoints if any changed
  if (needToMoveFaders) {
    debugPrint("Moving faders to new setpoints");
    moveAllFadersToSetpoints();
  }
  
  debugPrint("Bundled fader update complete");
}

// Handle bundled executor on/off updates for keys 101-410
void handleBundledExecutorUpdate(LiteOSCParser& parser) {
  // Expected format: /execUpdate,iiii... (page + 40 ints for 101-410)
  const int expectedArgs = 1 + NUM_EXECUTORS_TRACKED;

  if (parser.getArgCount() < expectedArgs) {
    debugPrint("Invalid exec bundle - not enough arguments");
    return;
  }

  if (parser.getTag(0) != 'i') {
    debugPrint("Invalid exec bundle - page not integer");
    return;
  }

  int pageNum = parser.getInt(0);
  if (pageNum != currentOSCPage) {
    debugPrintf("Page changed from %d to %d (via exec bundle)\n", currentOSCPage, pageNum);
    currentOSCPage = pageNum;
  }

  bool stateChanged = false;

  for (int i = 0; i < NUM_EXECUTORS_TRACKED; ++i) {
    int argIndex = i + 1; // Skip page arg
    if (parser.getTag(argIndex) != 'i') {
      debugPrintf("Invalid exec status type for executor %d\n", EXECUTOR_IDS[i]);
      continue;
    }

    int raw = parser.getInt(argIndex);
    uint8_t status = raw < 0 ? 0 : (raw > 2 ? 2 : raw); // 0=empty,1=off,2=on
    if (setExecutorStateByIndex(i, status)) {
      stateChanged = true;
    }
  }

  if (stateChanged) {
    markKeyLedsDirty();
  }
}

// Handle osc messages comming IN

void handleIncomingOsc() {}



// Put together and send an OSC message

void sendOscMessage(const char* address, const char* typeTag, const void* value) {
  uint8_t buffer[128];
  int len = 0;

  // Write address
  int addrLen = strlen(address);
  memcpy(buffer + len, address, addrLen);
  len += addrLen;
  buffer[len++] = '\0';
  while (len % 4 != 0) buffer[len++] = '\0';

  // Write type tag
  int tagLen = strlen(typeTag);
  memcpy(buffer + len, typeTag, tagLen);
  len += tagLen;
  buffer[len++] = '\0';
  while (len % 4 != 0) buffer[len++] = '\0';

  // Add argument(s)
  if (strcmp(typeTag, ",i") == 0) {
    int v = *(int*)value;
    uint32_t netOrder = htonl(v);
    memcpy(buffer + len, &netOrder, 4);
    len += 4;
  } else if (strcmp(typeTag, ",s") == 0) {
    const char* str = (const char*)value;
    int strLen = strlen(str);
    memcpy(buffer + len, str, strLen);
    len += strLen;
    buffer[len++] = '\0';
    while (len % 4 != 0) buffer[len++] = '\0';
  } else {
    debugPrint("Unsupported OSC typeTag.");
    return;
  }

  oscUdp.writeTo(buffer, len, netConfig.sendToIP, netConfig.sendPort);
}




//================================
// OSC HELPER FUNCTIONS
//================================


//We are sending 2 colors per fader, color data for exec 101-110 and 201-210 so if we assign a fader to 101 we will stil get correct fader color

// Page color data out of a bundled message
void parseDualColorValues(const char *colorString, Fader& f) {
  char buffer[128];  // Increased buffer size for 8 color values
  strncpy(buffer, colorString, 127);
  buffer[127] = '\0'; // Ensure null-termination
  
  // Parse primary color (first 4 values: R1;G1;B1;A1)
  int primaryRed = 0, primaryGreen = 0, primaryBlue = 0;
  int secondaryRed = 0, secondaryGreen = 0, secondaryBlue = 0;
  
  char *ptr = strtok(buffer, ";");
  if (ptr != NULL) {
    primaryRed = constrain(atoi(ptr), 0, 255);
    
    ptr = strtok(NULL, ";");
    if (ptr != NULL) {
      primaryGreen = constrain(atoi(ptr), 0, 255);
      
      ptr = strtok(NULL, ";");
      if (ptr != NULL) {
        primaryBlue = constrain(atoi(ptr), 0, 255);
        
        // Skip primary alpha (4th value)
        ptr = strtok(NULL, ";");
        if (ptr != NULL) {
          // Parse secondary color (next 4 values: R2;G2;B2;A2)
          ptr = strtok(NULL, ";");
          if (ptr != NULL) {
            secondaryRed = constrain(atoi(ptr), 0, 255);
            
            ptr = strtok(NULL, ";");
            if (ptr != NULL) {
              secondaryGreen = constrain(atoi(ptr), 0, 255);
              
              ptr = strtok(NULL, ";");
              if (ptr != NULL) {
                secondaryBlue = constrain(atoi(ptr), 0, 255);
                // Secondary alpha (8th value) is ignored
              }
            }
          }
        }
      }
    }
  }
  
  // Logic to choose between primary and secondary color
  if (primaryRed == 0 && primaryGreen == 0 && primaryBlue == 0) {
    // Primary is all zeros (black/off), use secondary color
    f.red = secondaryRed;
    f.green = secondaryGreen;
    f.blue = secondaryBlue;
    
    if (debugMode) {
      debugPrintf("Fader %d: Primary color is black, using secondary RGB(%d,%d,%d)\n", 
                 f.oscID, secondaryRed, secondaryGreen, secondaryBlue);
    }
  } else {
    // Primary has color, use it
    f.red = primaryRed;
    f.green = primaryGreen;
    f.blue = primaryBlue;
    
    if (debugMode) {
      debugPrintf("Fader %d: Using primary RGB(%d,%d,%d)\n", 
                 f.oscID, primaryRed, primaryGreen, primaryBlue);
    }
  }
  
  // Mark that color has been updated
  f.colorUpdated = true;
}




// Checks if the buffer starts as a valid bundle
bool isBundleStart(const uint8_t *buf, size_t len) {
  if (len < 16 || (len & 0x03) != 0) {
    return false;
  }
  if (strncmp((const char*)buf, "#bundle", 8) != 0) {
    return false;
  }
  return true;
}


//================================
// OSC DEBUG FUNCTION
//================================

// Print an OSC message for debugging
void printOSC(Print &out, const uint8_t *b, int len) {
  LiteOSCParser osc;

  // Check if it's a bundle
  if (isBundleStart(b, len)) {
    out.println("#bundle (not parsed)");
    return;
  }

  // Parse the message
  if (!osc.parse(b, len)) {
    if (osc.isMemoryError()) {
      out.println("#MemoryError");
    } else {
      out.println("#ParseError");
    }
    return;
  }

  // Print address
  out.printf("%s", osc.getAddress());

  // Print arguments
  int size = osc.getArgCount();
  for (int i = 0; i < size; i++) {
    if (i == 0) {
      out.print(": ");
    } else {
      out.print(", ");
    }
    
    // Print based on type
    switch (osc.getTag(i)) {
      case 'i':
        out.printf("int(%d)", osc.getInt(i));
        break;
      case 'f':
        out.printf("float(%f)", osc.getFloat(i));
        break;
      case 's':
        out.printf("string(\"%s\")", osc.getString(i));
        break;
      case 'T':
        out.print("true");
        break;
      case 'F':
        out.print("false");
        break;
      default:
        out.printf("unknown(%c)", osc.getTag(i));
    }
  }
  out.println();
}
