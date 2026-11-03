#pragma once

#include <Arduino.h>

class PowerOutputs {
 public:
  void begin();
  void setCameraPower(bool on);
  void setDewPercent(uint8_t percent);

  bool cameraPower() const { return camera_power_on_; }
  uint8_t dewPercent() const { return dew_percent_; }

 private:
  bool camera_power_on_ = false;
  uint8_t dew_percent_ = 0;
};
