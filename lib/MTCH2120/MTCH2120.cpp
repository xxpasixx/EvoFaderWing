#include "MTCH2120.h"

/*
Example usage: interrupt-driven with polling fallback

#include "MTCH2120.h"

volatile bool mtchChange = true;
MTCH2120 touch(Wire, MTCH2120::DEFAULT_I2C_ADDR, 2); // /CHANGE on pin 2

void qtISR() { mtchChange = true; }

void setup() {
  Serial.begin(115200);
  touch.begin();
  touch.attachChangeCallback(qtISR);
  touch.setThresholdAll(128);
  touch.setAutoTune(true);
  touch.triggerCalibration();
  touch.saveToNvm(); // optional: persist current config
}

void loop() {
  static uint32_t lastPoll = 0;
  const uint32_t now = millis();
  const bool due = mtchChange || (now - lastPoll) >= 50; // fallback poll
  if (!due) return;
  mtchChange = false;
  lastPoll = now;

  uint16_t buttons = 0;
  if (touch.readButtons(buttons)) {
    // buttons bitmask: bit k = key k touched
    Serial.print("Buttons: 0x"); Serial.println(buttons, HEX);
  }

  MTCH2120::RawKeyData data{};
  if (touch.readRawKey(0, data)) {
    // Use data.reference and data.signal
  }
}
*/

MTCH2120::MTCH2120(TwoWire &wire, uint8_t address, int8_t changePin)
  : wire_(wire), address_(address), changePin_(changePin) {}

bool MTCH2120::begin() {
  if (changePin_ >= 0) {
    pinMode(changePin_, INPUT_PULLUP);
  }
  wire_.begin();
  return communicating();
}

void MTCH2120::attachChangeCallback(void (*cb)()) {
  if (changePin_ >= 0) {
    attachInterrupt(digitalPinToInterrupt(changePin_), cb, FALLING);
  }
}

bool MTCH2120::communicating() {
  uint8_t id = 0;
  return read8(ADDR_DEVICE_ID, id);
}

bool MTCH2120::readDeviceId(uint8_t &id) { return read8(ADDR_DEVICE_ID, id); }
bool MTCH2120::readDeviceVersion(uint8_t &ver) { return read8(ADDR_DEVICE_ID | 0x01, ver); }

bool MTCH2120::readStatus(Status &status) {
  uint8_t buf[4] = {0};
  if (!readBlock(ADDR_STATUS, buf, sizeof(buf))) return false;
  status.device = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  status.buttons = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
  return true;
}

bool MTCH2120::readButtons(uint16_t &mask) {
  return read16(ADDR_STATUS | 0x02, mask);
}

bool MTCH2120::readKeySignal(uint8_t key, uint16_t &value) {
  key = clampKey(key);
  return read16(ADDR_NODE_ACQ_SIGNALS | (uint16_t)(key * 2u), value);
}

bool MTCH2120::readKeyReference(uint8_t key, uint16_t &value) {
  key = clampKey(key);
  return read16(ADDR_CHANNEL_REFERENCE | (uint16_t)(key * 2u), value);
}

bool MTCH2120::readRawKey(uint8_t key, RawKeyData &data) {
  return readKeyReference(key, data.reference) && readKeySignal(key, data.signal);
}

bool MTCH2120::getThreshold(uint8_t key, uint8_t &value) {
  key = clampKey(key);
  return read8(ADDR_THRESHOLD | key, value);
}

bool MTCH2120::setThreshold(uint8_t key, uint8_t value) {
  key = clampKey(key);
  return write8(ADDR_THRESHOLD | key, value);
}

bool MTCH2120::setThresholdAll(uint8_t value) {
  uint8_t buf[KEY_COUNT];
  for (uint8_t i = 0; i < KEY_COUNT; ++i) buf[i] = value;
  return writeBlock(ADDR_THRESHOLD, buf, KEY_COUNT);
}

bool MTCH2120::getGain(uint8_t key, uint8_t &value) {
  key = clampKey(key);
  return read8(ADDR_GAIN | key, value);
}

bool MTCH2120::setGain(uint8_t key, uint8_t value) {
  key = clampKey(key);
  return write8(ADDR_GAIN | key, value);
}

bool MTCH2120::getOversampling(uint8_t key, uint8_t &value) {
  key = clampKey(key);
  return read8(ADDR_OVERSAMPLING | key, value);
}

bool MTCH2120::setOversampling(uint8_t key, uint8_t value) {
  key = clampKey(key);
  return write8(ADDR_OVERSAMPLING | key, value);
}

bool MTCH2120::getMeasurementClock(uint8_t key, uint8_t &value) {
  key = clampKey(key);
  return read8(ADDR_MEASUREMENT_CLK_FREQ | key, value);
}

bool MTCH2120::setMeasurementClock(uint8_t key, uint8_t value) {
  key = clampKey(key);
  return write8(ADDR_MEASUREMENT_CLK_FREQ | key, value);
}

bool MTCH2120::getCSD(uint8_t key, uint8_t &value) {
  key = clampKey(key);
  return read8(ADDR_CSD | key, value);
}

bool MTCH2120::setCSD(uint8_t key, uint8_t value) {
  key = clampKey(key);
  return write8(ADDR_CSD | key, value);
}

bool MTCH2120::getHysteresis(uint8_t key, uint8_t &value) {
  key = clampKey(key);
  return read8(ADDR_HYSTERESIS | key, value);
}

bool MTCH2120::setHysteresis(uint8_t key, uint8_t value) {
  key = clampKey(key);
  return write8(ADDR_HYSTERESIS | key, value);
}

bool MTCH2120::getAKS(uint8_t key, uint8_t &value) {
  key = clampKey(key);
  return read8(ADDR_AKS | key, value);
}

bool MTCH2120::setAKS(uint8_t key, uint8_t value) {
  key = clampKey(key);
  return write8(ADDR_AKS | key, value);
}

bool MTCH2120::getSensorControl(uint8_t key, uint8_t &value) {
  key = clampKey(key);
  return read8(ADDR_SENSOR_CONTROL | key, value);
}

bool MTCH2120::setSensorControl(uint8_t key, uint8_t value) {
  key = clampKey(key);
  return write8(ADDR_SENSOR_CONTROL | key, value);
}

bool MTCH2120::setKeyEnabled(uint8_t key, bool enabled) {
  uint8_t ctrl = 0;
  if (!getSensorControl(key, ctrl)) return false;
  if (enabled) {
    ctrl |= SENCTRL_EN;
  } else {
    ctrl &= ~SENCTRL_EN;
  }
  return setSensorControl(key, ctrl);
}

bool MTCH2120::clearKeySuspend(uint8_t key) {
  uint8_t ctrl = 0;
  if (!getSensorControl(key, ctrl)) return false;
  ctrl &= ~SENCTRL_SUSPEND;
  return setSensorControl(key, ctrl);
}

bool MTCH2120::readGroupConfig(GroupConfig &cfg) {
  return readBlock(ADDR_GROUP_CONFIGURATION, reinterpret_cast<uint8_t *>(&cfg), sizeof(cfg));
}

bool MTCH2120::writeGroupConfig(const GroupConfig &cfg) {
  return writeBlock(ADDR_GROUP_CONFIGURATION, reinterpret_cast<const uint8_t *>(&cfg), sizeof(cfg));
}

bool MTCH2120::setTouchMeasurementPeriod(uint16_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.touchMeasurementPeriod = v; });
}

bool MTCH2120::setLowPowerMeasurementPeriod(uint16_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.lowPowerMeasurementPeriod = v; });
}

bool MTCH2120::setDetectIntegration(uint8_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.detectIntegration = v; });
}

bool MTCH2120::setAntiTouchIntegration(uint8_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.sensorAntiTouchIntegration = v; });
}

bool MTCH2120::setMaxOnTime(uint8_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.sensorMaxOnTime = v; });
}

bool MTCH2120::setDriftHoldTime(uint8_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.sensorDriftHoldTime = v; });
}

bool MTCH2120::setTouchDriftRate(uint8_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.sensorTouchDriftRate = v; });
}

bool MTCH2120::setAntiTouchDriftRate(uint8_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.sensorAntiTouchDriftRate = v; });
}

bool MTCH2120::setAntiTouchRecalThreshold(uint8_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.sensorAntiTouchRecalThr = v; });
}

bool MTCH2120::setNoiseThreshold(uint16_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.noiseThreshold = v; });
}

bool MTCH2120::setNoiseIntegration(uint8_t v) {
  return updateGroupConfig([&](GroupConfig &cfg) { cfg.noiseIntegration = v; });
}

bool MTCH2120::setHopFrequencies(uint8_t f0, uint8_t f1, uint8_t f2) {
  return updateGroupConfig([&](GroupConfig &cfg) {
    cfg.hopFrequency[0] = f0;
    cfg.hopFrequency[1] = f1;
    cfg.hopFrequency[2] = f2;
  });
}

bool MTCH2120::readDeviceControl(uint16_t &ctrl) {
  return read16(ADDR_DEVICE_CONTROL, ctrl);
}

bool MTCH2120::writeDeviceControl(uint16_t ctrl) {
  return write16(ADDR_DEVICE_CONTROL, ctrl);
}

bool MTCH2120::setDeviceControlBit(uint16_t mask, bool enable) {
  uint16_t ctrl = 0;
  if (!readDeviceControl(ctrl)) return false;
  if (enable) ctrl |= mask;
  else ctrl &= ~mask;
  return writeDeviceControl(ctrl);
}

bool MTCH2120::setAutoTune(bool enable) { return setDeviceControlBit(DEVCTRL_AT, enable); }
bool MTCH2120::setEasyTune(bool enable) { return setDeviceControlBit(DEVCTRL_ET, enable); }
bool MTCH2120::setFrequencyHop(bool enable) { return setDeviceControlBit(DEVCTRL_FREQHOP, enable); }
bool MTCH2120::setWatchdog(bool enable) { return setDeviceControlBit(DEVCTRL_WDT, enable); }

bool MTCH2120::saveToNvm() {
  uint16_t ctrl = 0;
  if (!readDeviceControl(ctrl)) return false;
  ctrl |= DEVCTRL_SAVE;
  return writeDeviceControl(ctrl);
}

bool MTCH2120::loadFactoryConfig() {
  uint16_t ctrl = 0;
  if (!readDeviceControl(ctrl)) return false;
  ctrl |= DEVCTRL_SMCFG;
  return writeDeviceControl(ctrl);
}

bool MTCH2120::triggerCalibration() { return setDeviceControlBit(DEVCTRL_CAL, true); }

bool MTCH2120::softReset() { return setDeviceControlBit(DEVCTRL_RESET, true); }

bool MTCH2120::writeBlock(uint16_t reg, const uint8_t *data, size_t len) {
  wire_.beginTransmission(address_);
  wire_.write(reg >> 8);
  wire_.write(reg & 0xFF);
  for (size_t i = 0; i < len; ++i) {
    wire_.write(data[i]);
  }
  return wire_.endTransmission() == 0;
}

bool MTCH2120::readBlock(uint16_t reg, uint8_t *data, size_t len) {
  wire_.beginTransmission(address_);
  wire_.write(reg >> 8);
  wire_.write(reg & 0xFF);
  if (wire_.endTransmission(false) != 0) return false;

  const size_t readLen = wire_.requestFrom(address_, (uint8_t)len);
  if (readLen != len) return false;
  for (size_t i = 0; i < len && wire_.available(); ++i) {
    data[i] = wire_.read();
  }
  return true;
}

bool MTCH2120::write8(uint16_t reg, uint8_t value) {
  return writeBlock(reg, &value, 1);
}

bool MTCH2120::read8(uint16_t reg, uint8_t &value) {
  return readBlock(reg, &value, 1);
}

bool MTCH2120::write16(uint16_t reg, uint16_t value) {
  uint8_t buf[2];
  buf[0] = value & 0xFF;
  buf[1] = (value >> 8) & 0xFF;
  return writeBlock(reg, buf, 2);
}

bool MTCH2120::read16(uint16_t reg, uint16_t &value) {
  uint8_t buf[2] = {0};
  if (!readBlock(reg, buf, 2)) return false;
  value = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  return true;
}
