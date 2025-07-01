// FaderControl.h
#ifndef FADER_CONTROL_H
#define FADER_CONTROL_H

#include <Arduino.h>
#include "Config.h"


// Fader initialization
void initializeFaders();
void configureFaderPins();
void calibrateFaders();

// Main fader processing
void handleFaders();


//Fader movement
void setFaderSetpoint(int faderIndex, int oscValue);
void moveAllFadersToSetpoints();


int readFadertoOSC(Fader& f);
int getFaderIndexFromID(int id);

#endif // FADER_CONTROL_H