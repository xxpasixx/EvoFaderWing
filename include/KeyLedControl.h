// KeyLedControl.h
#ifndef KEY_LED_CONTROL_H
#define KEY_LED_CONTROL_H

#include <Arduino.h>
#include "Config.h"

// Setup and periodic refresh for the executor key LED strip
void setupKeyLeds();
void updateKeyLeds();

// Call when executor states change so the LEDs can refresh on the next loop
void markKeyLedsDirty();

#endif // KEY_LED_CONTROL_H
