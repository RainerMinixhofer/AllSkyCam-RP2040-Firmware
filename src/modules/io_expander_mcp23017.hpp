#pragma once

#include <Arduino.h>

struct ParkSwitchState {
  bool sw0 = false;
  bool sw1 = false;
  bool sw2 = false;
};

struct Mcp23017Stats {
  bool configured = false;
  bool comm_ok = false;
  uint8_t raw_gpioa = 0xFF;
  uint32_t read_ok_count = 0;
  uint32_t read_error_count = 0;
  ParkSwitchState switches;
};

class IoExpanderMcp23017 {
 public:
  bool begin(uint8_t i2c_address = 0x20);
  void poll();
  ParkSwitchState parkSwitches() const { return switches_; }
  Mcp23017Stats stats() const;
  bool test();

 private:
  bool readInputs(uint8_t& gpioa_raw);

  uint8_t i2c_address_ = 0x20;
  ParkSwitchState switches_;
  bool configured_ = false;
  bool comm_ok_ = false;
  uint8_t raw_gpioa_ = 0xFF;
  uint32_t read_ok_count_ = 0;
  uint32_t read_error_count_ = 0;
};
