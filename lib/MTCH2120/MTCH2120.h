#pragma once

#include <Arduino.h>
#include <Wire.h>

// MTCH2120 driver by Shawn R
class MTCH2120 {
public:
  static constexpr uint8_t KEY_COUNT = 12;          // Capacitive keys
  static constexpr uint8_t DEFAULT_I2C_ADDR = 0x20; // ADDR0 strapped low

  // Register map (16-bit addressing: MSB, then LSB)
  enum Register : uint16_t {
    ADDR_DEVICE_ID = 0x0000,
    ADDR_STATUS = 0x0100,
    ADDR_NODE_ACQ_SIGNALS = 0x0200,
    ADDR_CHANNEL_REFERENCE = 0x0300,
    ADDR_SENSOR_STATE = 0x0400,
    ADDR_NODE_CC = 0x0500,
    ADDR_SENSOR_CONTROL = 0x0E00,
    ADDR_CSD = 0x0F00,
    ADDR_MEASUREMENT_CLK_FREQ = 0x1000,
    ADDR_OVERSAMPLING = 0x1100,
    ADDR_THRESHOLD = 0x1200,
    ADDR_GAIN = 0x1300,
    ADDR_HYSTERESIS = 0x1400,
    ADDR_AKS = 0x1500,
    ADDR_GROUP_CONFIGURATION = 0x1600,
    ADDR_DEVICE_CONTROL = 0x1F00,
    ADDR_LUMP_CONFIG = 0x2000,
    ADDR_GPIO_CONFIG = 0x2100
  };

  // Per-channel Sensor Control bits
  static constexpr uint8_t SENCTRL_EN = (1u << 0);      // Enable channel
  static constexpr uint8_t SENCTRL_CAL = (1u << 1);     // Force cal on channel
  static constexpr uint8_t SENCTRL_SUSPEND = (1u << 2); // Suspend channel
  static constexpr uint8_t SENCTRL_LP = (1u << 7);      // Low-power mode

  // Device Control bits (global)
  static constexpr uint16_t DEVCTRL_CAL = (1u << 0);      // Global calibration
  static constexpr uint16_t DEVCTRL_LP = (1u << 1);       // Low power
  static constexpr uint16_t DEVCTRL_DLPLB = (1u << 2);    // Driven line pullup
  static constexpr uint16_t DEVCTRL_DS = (1u << 3);       // Driven Shield
  static constexpr uint16_t DEVCTRL_DSP = (1u << 4);      // Driven Shield Plus
  static constexpr uint16_t DEVCTRL_DRIFTGAIN = (1u << 5); // Drift scales by gain
  static constexpr uint16_t DEVCTRL_FREQHOP = (1u << 6);  // Frequency hopping
  static constexpr uint16_t DEVCTRL_AT = (1u << 7);       // AutoTune
  static constexpr uint16_t DEVCTRL_ET = (1u << 8);       // EasyTune
  static constexpr uint16_t DEVCTRL_WDT = (1u << 9);      // Watchdog
  static constexpr uint16_t DEVCTRL_BOD = (1u << 10);     // Brown-out detect
  static constexpr uint16_t DEVCTRL_SMCFG = (1u << 11);   // Manufacturer config
  static constexpr uint16_t DEVCTRL_SAVE = (1u << 12);    // Save to NVM
  static constexpr uint16_t DEVCTRL_RESET = (1u << 13);   // Soft reset

  struct Status {
    uint16_t device;  // Device status bits
    uint16_t buttons; // Key touch bits (keys 0-11)
  };

  // Group/global configuration block at 0x1600.
  struct GroupConfig {
    uint16_t touchMeasurementPeriod;   // MP. Recommended start: ~30 ms (power-on loads NVM; min enforced by channel count).
    uint16_t lowPowerMeasurementPeriod; // LPMP. Recommended start: ~100 ms when LP is used (power-on loads NVM).
    uint16_t timeoutConfig;            // MOD/other timers. Recommended start: 0 (disable max-on watchdog unless needed).
    uint8_t sensorReburstMode;         // Reburst behavior. Recommended start: 0 (per Microchip factory config).
    uint8_t detectIntegration;         // DI. Recommended start: 4 samples to confirm touch/release.
    uint8_t sensorAntiTouchIntegration; // ANTITCHINT. Recommended start: 5 samples to confirm anti-touch.
    uint8_t sensorMaxOnTime;           // MOD (0 disables). Recommended start: 0 (no forced recal).
    uint8_t sensorDriftHoldTime;       // DHT. Recommended start: 25 (≈5s hold; value*200ms).
    uint8_t sensorTouchDriftRate;      // TCHDR. Recommended start: 20 (≈4s cadence; value*200ms).
    uint8_t sensorAntiTouchDriftRate;  // ANTITCHDR. Recommended start: 5 (≈1s cadence; value*200ms).
    uint8_t sensorAntiTouchRecalThr;   // ANTITCHRECALTHR. Recommended start: 0 (100% of threshold).
    uint16_t noiseThreshold;           // NOISETHR. Recommended start: 15.
    uint8_t noiseIntegration;          // NOISEINT. Recommended start: 3.
    uint8_t hopFrequency[3];           // FREQ registers. Recommended start: {0,3,7}.
  } __attribute__((packed));

  struct RawKeyData {
    uint16_t reference; // Baseline
    uint16_t signal;    // Instantaneous signal
  };

  MTCH2120(TwoWire &wire = Wire, uint8_t address = DEFAULT_I2C_ADDR, int8_t changePin = -1);

  // Initialize I2C and optional interrupt pin; returns false if the device does not ACK.
  bool begin();
  // Attach an ISR to /CHANGE (active low, open drain).
  void attachChangeCallback(void (*cb)());

  // Quick health checks and identity.
  bool communicating();
  bool readDeviceId(uint8_t &id);
  bool readDeviceVersion(uint8_t &ver);
  bool readStatus(Status &status);
  bool readButtons(uint16_t &mask); // Bitmask of touched keys (0-11)

  // Raw key metrics.
  bool readKeySignal(uint8_t key, uint16_t &value);
  bool readKeyReference(uint8_t key, uint16_t &value);
  bool readRawKey(uint8_t key, RawKeyData &data);

  // Per-key tuning (defaults are NVM/factory unless noted).
  bool getThreshold(uint8_t key, uint8_t &value);          // THRESHOLD. Higher = less sensitive.
  bool setThreshold(uint8_t key, uint8_t value);
  bool setThresholdAll(uint8_t value);

  bool getGain(uint8_t key, uint8_t &value);               // GAIN. 0 = 1x (factory default 1x).
  bool setGain(uint8_t key, uint8_t value);

  bool getOversampling(uint8_t key, uint8_t &value);       // FILTERLEVEL/oversampling. Higher = more filtering, slower.
  bool setOversampling(uint8_t key, uint8_t value);

  bool getMeasurementClock(uint8_t key, uint8_t &value);   // MCLKFREQ. Factory default depends on config 0-3; 0=2MHz, 1/2=1mhz, 3=500kHz per datasheet table.
  bool setMeasurementClock(uint8_t key, uint8_t value);

  bool getCSD(uint8_t key, uint8_t &value);                // Charge share delay. Recommended start: 1 (small delay).
  bool setCSD(uint8_t key, uint8_t value);

  bool getHysteresis(uint8_t key, uint8_t &value);         // Release hysteresis (coded % of threshold). Recommended start: 1 (25%).
  bool setHysteresis(uint8_t key, uint8_t value);

  bool getAKS(uint8_t key, uint8_t &value);                // Adjacent Key Suppression group. Recommended start: 0 (no AKS grouping).
  bool setAKS(uint8_t key, uint8_t value);

  bool getSensorControl(uint8_t key, uint8_t &value);      // Read Sensor Control byte (EN/SUSPEND/LP bits)
  bool setSensorControl(uint8_t key, uint8_t value);
  bool setKeyEnabled(uint8_t key, bool enabled);           // Convenience: sets EN bit only.
  bool clearKeySuspend(uint8_t key);                       // Clears SUSPEND bit.

  // Group/global configuration helpers (all defaults: NVM/factory).
  bool readGroupConfig(GroupConfig &cfg);
  bool writeGroupConfig(const GroupConfig &cfg);
  bool setTouchMeasurementPeriod(uint16_t v);      // MP. Units per datasheet (ms).
  bool setLowPowerMeasurementPeriod(uint16_t v);   // LPMP. Units per datasheet (ms).
  bool setDetectIntegration(uint8_t v);            // DI. Samples to confirm touch/release.
  bool setAntiTouchIntegration(uint8_t v);         // ANTITCHINT. Samples to confirm anti-touch.
  bool setMaxOnTime(uint8_t v);                    // MOD. 0=disabled, else v*200ms watchdog.
  bool setDriftHoldTime(uint8_t v);                // DHT. v*200ms hold after touch.
  bool setTouchDriftRate(uint8_t v);               // TCHDR. v*200ms cadence toward touch.
  bool setAntiTouchDriftRate(uint8_t v);           // ANTITCHDR. v*200ms cadence away from touch.
  bool setAntiTouchRecalThreshold(uint8_t v);      // ANTITCHRECALTHR. Coded % of threshold (0=100%,1=50%,2=25%,3=12.5%,4=6.25%).
  bool setNoiseThreshold(uint16_t v);              // NOISETHR. Noise replacement threshold.
  bool setNoiseIntegration(uint8_t v);             // NOISEINT. Samples to confirm noise.
  bool setHopFrequencies(uint8_t f0, uint8_t f1, uint8_t f2); // Hop pool entries 0-15.

  // Device control.
  bool readDeviceControl(uint16_t &ctrl);          // DEVCTRL. Recommended start: FREQHOP+WDT+BOD on, AT/ET off until tuning; power-on loads NVM.
  bool writeDeviceControl(uint16_t ctrl);
  bool setAutoTune(bool enable);                   // AT bit (AutoTune).
  bool setEasyTune(bool enable);                   // ET bit (EasyTune).
  bool setFrequencyHop(bool enable);               // FREQHOP bit.
  bool setWatchdog(bool enable);                   // WDT bit.
  bool saveToNvm();                                // Sets SAVE bit to store current config.
  bool loadFactoryConfig();                        // Sets SMCFG bit (restores factory config per datasheet).
  bool triggerCalibration();                       // Sets CAL bit.
  bool softReset();                                // Sets RESET bit.

private:
  TwoWire &wire_;
  uint8_t address_;
  int8_t changePin_;

  uint8_t clampKey(uint8_t key) const { return (key >= KEY_COUNT) ? (KEY_COUNT - 1) : key; }
  bool writeBlock(uint16_t reg, const uint8_t *data, size_t len);
  bool readBlock(uint16_t reg, uint8_t *data, size_t len);
  bool write8(uint16_t reg, uint8_t value);
  bool read8(uint16_t reg, uint8_t &value);
  bool write16(uint16_t reg, uint16_t value);
  bool read16(uint16_t reg, uint16_t &value);

  template <typename Fn>
  bool updateGroupConfig(Fn mutator) {
    GroupConfig cfg{};
    if (!readGroupConfig(cfg)) return false;
    mutator(cfg);
    return writeGroupConfig(cfg);
  }

  bool setDeviceControlBit(uint16_t mask, bool enable);
};
