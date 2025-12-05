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

bool faderColorDebug = false;

// Forward declarations for async callbacks
void handleBundledExecutorUpdate(LiteOSCParser& parser);
void handleColorUpdate(LiteOSCParser& parser);

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

    if (strstr(addr, "/execUpdate") != NULL) {
      handleBundledExecutorUpdate(parser);
    } else if (strstr(addr, "/colorUpdate") != NULL) {
      handleColorUpdate(parser);
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


static bool parseSimpleColorString(const char* colorString, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (!colorString) return false;

  char buffer[64];
  strncpy(buffer, colorString, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char* token = strtok(buffer, ",;");
  if (!token) return false;
  r = constrain(atoi(token), 0, 255);

  token = strtok(nullptr, ",;");
  if (!token) return false;
  g = constrain(atoi(token), 0, 255);

  token = strtok(nullptr, ",;");
  if (!token) return false;
  b = constrain(atoi(token), 0, 255);

  return true;
}

static void applyColorToExecutor(int oscId, const char* colorString) {
  uint8_t r, g, b;
  if (!parseSimpleColorString(colorString, r, g, b)) {
    return;
  }

  setExecutorColorByID(oscId, r, g, b);

  if (oscId >= 201 && oscId <= 210) {
    int faderIndex = getFaderIndexFromID(oscId);
    if (faderIndex >= 0 && faderIndex < NUM_FADERS) {
      faders[faderIndex].red = r;
      faders[faderIndex].green = g;
      faders[faderIndex].blue = b;

      if (faderColorDebug) {
        debugPrintf("Fader %d: Using RGB(%d,%d,%d)\n", oscId, r, g, b);
      }
    }
  }
}


// Handle bundled executor updates: page + 10 fader setpoints + 40 executor statuses
void handleBundledExecutorUpdate(LiteOSCParser& parser) {
  const int expectedArgs = 1 + 10 + NUM_EXECUTORS_TRACKED;

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
  bool needToMoveFaders = false;
  bool blockFaderUpdates = calibrationInProgress;

  // Fader values (201-210) occupy args 1-10
  for (int i = 0; i < 10; i++) {
    int argIndex = i + 1;
    int faderOscID = 201 + i;

    if (parser.getTag(argIndex) != 'i') {
      debugPrintf("Invalid fader value type for fader %d\n", faderOscID);
      continue;
    }

    int oscValue = parser.getInt(argIndex);
    int faderIndex = getFaderIndexFromID(faderOscID);

    if (blockFaderUpdates) {
      continue;
    }

    if (faderIndex >= 0 && faderIndex < NUM_FADERS) {
      if (!faders[faderIndex].touched) {
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

  // Executor statuses (101-410) start after the fader block
  for (int i = 0; i < NUM_EXECUTORS_TRACKED; ++i) {
    int argIndex = 11 + i;
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

  if (needToMoveFaders) {
    debugPrint("Moving faders to new setpoints");
    moveAllFadersToSetpoints();
  }
}

// Handle bundled color updates: page + 40 color strings (execs 101-410)
void handleColorUpdate(LiteOSCParser& parser) {
  const int expectedArgs = 1 + NUM_EXECUTORS_TRACKED;

  if (parser.getArgCount() < expectedArgs) {
    debugPrint("Invalid color bundle - not enough arguments");
    return;
  }

  if (parser.getTag(0) != 'i') {
    debugPrint("Invalid color bundle - page not integer");
    return;
  }

  int pageNum = parser.getInt(0);
  if (pageNum != currentOSCPage) {
    debugPrintf("Page changed from %d to %d (via color bundle)\n", currentOSCPage, pageNum);
    currentOSCPage = pageNum;
  }

  for (int i = 0; i < NUM_EXECUTORS_TRACKED; ++i) {
    int argIndex = i + 1;
    if (parser.getTag(argIndex) != 's') {
      debugPrintf("Invalid color type for executor %d\n", EXECUTOR_IDS[i]);
      continue;
    }
    applyColorToExecutor(EXECUTOR_IDS[i], parser.getString(argIndex));
  }
}



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
