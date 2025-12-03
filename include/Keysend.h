// Keysend.h
#ifndef KEYSEND
#define KEYSEND

#include <Arduino.h>
#include <Keyboard.h>

void initKeyboard();
void sendKeyPress(const String& keyID);
void sendKeyRelease(const String& keyID);

#endif