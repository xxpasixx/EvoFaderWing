// ExecutorStatus.h
#ifndef EXECUTOR_STATUS_H
#define EXECUTOR_STATUS_H

#include <Arduino.h>
#include "Config.h"

// Status codes: 0 = not populated, 1 = populated/off, 2 = populated/on
extern uint8_t executorStatus[NUM_EXECUTORS_TRACKED];
extern const uint16_t EXECUTOR_IDS[NUM_EXECUTORS_TRACKED];
extern uint8_t executorColors[NUM_EXECUTORS_TRACKED][3]; // RGB per executor key

// Helpers for mapping executor IDs (101-410) to array slots
int executorIndexFromID(uint16_t execId);
bool setExecutorStateByIndex(int index, uint8_t status);
bool setExecutorStateByID(uint16_t execId, uint8_t status);
bool setExecutorColorByIndex(int index, uint8_t r, uint8_t g, uint8_t b);
bool setExecutorColorByID(uint16_t execId, uint8_t r, uint8_t g, uint8_t b);

#endif // EXECUTOR_STATUS_H
