// KeyLedControl.cpp

#include "KeyLedControl.h"
#include "ExecutorStatus.h"
#include "Utils.h"
#include <Adafruit_NeoPixel.h>

// Separate strip for the executor keys
Adafruit_NeoPixel keyPixels(EXECUTOR_LED_COUNT, EXECUTOR_LED_PIN, NEO_RGB + NEO_KHZ800);

static bool keyLedsDirty = false;

static void fillExecutorPixels(int execIndex, uint8_t brightness) {
  uint32_t color = keyPixels.Color(brightness, brightness, brightness); // simple white with variable brightness
  int startPixel = execIndex * EXECUTOR_PIXELS_PER_KEY;

  for (int i = 0; i < EXECUTOR_PIXELS_PER_KEY; ++i) {
    keyPixels.setPixelColor(startPixel + i, color);
  }
}

void setupKeyLeds() {
  keyPixels.begin();
  keyPixels.clear();

  // Start dark; they'll light when we learn populated/off/on status
  keyPixels.show();

  debugPrintf("Key LED strip ready on pin %d with %d pixels", EXECUTOR_LED_PIN, EXECUTOR_LED_COUNT);
  
}

void markKeyLedsDirty() {
  keyLedsDirty = true;
}

void updateKeyLeds() {
  if (!keyLedsDirty) {
    return;
  }

  keyLedsDirty = false;

  for (int i = 0; i < NUM_EXECUTORS_TRACKED; ++i) {
    uint8_t status = executorStatus[i]; // 0=empty,1=populated off,2=on
    uint8_t brightness = 0;

    if (status == 2) {
      brightness = EXECUTOR_ACTIVE_BRIGHTNESS;
    } else if (status == 1) {
      brightness = EXECUTOR_BASE_BRIGHTNESS;
    } else {
      brightness = 0;
    }

    fillExecutorPixels(i, brightness);
  }

  keyPixels.show();
}
