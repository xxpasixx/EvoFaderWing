// KeyLedControl.cpp

#include "KeyLedControl.h"
#include "ExecutorStatus.h"
#include "Utils.h"
#include <Adafruit_NeoPixel.h>

// Separate strip for the executor keys
Adafruit_NeoPixel keyPixels(EXECUTOR_LED_COUNT, EXECUTOR_LED_PIN, NEO_RGB + NEO_KHZ800);

static bool keyLedsDirty = false;

// Physical strip order for the mounted keys (serpentine):
// 401-410, 310-301, 201-210, 110-101
static const uint8_t EXEC_LED_MAP[NUM_EXECUTORS_TRACKED] = {
  // 101-110 (bottom row, reversed)
  39, 38, 37, 36, 35, 34, 33, 32, 31, 30,
  // 201-210
  20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
  // 301-310 (reversed)
  19, 18, 17, 16, 15, 14, 13, 12, 11, 10,
  // 401-410 (top row)
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9
};

static uint32_t buildExecColor(int execIndex, uint8_t brightness) {
  if (brightness == 0) {
    return keyPixels.Color(0, 0, 0);
  }

  if (!execConfig.useStaticColor) {
    uint8_t baseR = executorColors[execIndex][0];
    uint8_t baseG = executorColors[execIndex][1];
    uint8_t baseB = executorColors[execIndex][2];

    // If no color received yet, default to white scaled by brightness
    if (baseR == 0 && baseG == 0 && baseB == 0) {
      return keyPixels.Color(brightness, brightness, brightness);
    }

    uint8_t r = (uint16_t)baseR * brightness / 255;
    uint8_t g = (uint16_t)baseG * brightness / 255;
    uint8_t b = (uint16_t)baseB * brightness / 255;
    return keyPixels.Color(r, g, b);
  }

  uint8_t r = (uint16_t)execConfig.staticRed * brightness / 255;
  uint8_t g = (uint16_t)execConfig.staticGreen * brightness / 255;
  uint8_t b = (uint16_t)execConfig.staticBlue * brightness / 255;
  return keyPixels.Color(r, g, b);
}

static void fillExecutorPixels(int execIndex, uint8_t brightness) {
  if (execIndex < 0 || execIndex >= NUM_EXECUTORS_TRACKED) {
    return;
  }

  uint32_t color = buildExecColor(execIndex, brightness);
  int startPixel = EXEC_LED_MAP[execIndex] * EXECUTOR_PIXELS_PER_KEY;

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
      brightness = execConfig.activeBrightness;
    } else if (status == 1) {
      brightness = execConfig.baseBrightness;
    } else {
      brightness = 0;
    }

    fillExecutorPixels(i, brightness);
  }

  keyPixels.show();
}
