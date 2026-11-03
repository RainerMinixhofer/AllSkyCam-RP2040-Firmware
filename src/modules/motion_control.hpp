#pragma once

#include <Arduino.h>

struct AxisPins {
  uint8_t step_pin;
  uint8_t dir_pin;
};

struct TmcAxisStatus {
  bool configured = false;
  bool uart_ok = false;
  uint32_t ifcnt = 0;
  uint32_t drv_status = 0;
};

class MotionControl {
 public:
  static constexpr uint8_t kAxisCount = 4;

  void begin();
  void enableDrivers(bool enabled);
  bool driversEnabled() const { return drivers_enabled_; }

  void queueRelativeMove(uint8_t axis_index, int32_t steps, bool direction);
  void tick();

  bool configureAllDrivers();
  bool testUartAxis(uint8_t axis_index);
  bool readDriverRegister(uint8_t axis_index, uint8_t reg_addr, uint32_t& value);
  bool writeDriverRegister(uint8_t axis_index, uint8_t reg_addr, uint32_t value);

  TmcAxisStatus axisStatus(uint8_t axis_index) const;
  const char* lastError() const { return last_error_; }

 private:
  static const uint8_t kUartAddresses[kAxisCount];
  static constexpr uint8_t kRegGconf = 0x00;
  static constexpr uint8_t kRegIfcnt = 0x02;
  static constexpr uint8_t kRegIholdIrun = 0x10;
  static constexpr uint8_t kRegTpowerdown = 0x11;
  static constexpr uint8_t kRegChopconf = 0x6C;
  static constexpr uint8_t kRegDrvStatus = 0x6F;

  uint8_t crc8Atm(const uint8_t* data, size_t len) const;
  bool readFrame(uint32_t timeout_ms, uint32_t& value);
  bool clearUartRx();
  bool setError(const char* msg);

  AxisPins axis_pins_[kAxisCount];
  volatile int32_t pending_steps_[kAxisCount] = {0, 0, 0, 0};
  TmcAxisStatus axis_status_[kAxisCount];
  char last_error_[96] = {};
  bool drivers_enabled_ = false;
  uint32_t last_step_us_ = 0;
  uint16_t step_period_us_ = 500;
};
