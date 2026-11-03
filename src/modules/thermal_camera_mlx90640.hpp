#pragma once

#include <Arduino.h>

struct ThermalStats {
  bool frame_valid = false;
  float min_c = 0.0f;
  float max_c = 0.0f;
  float avg_c = 0.0f;
};

class ThermalCameraMlx90640 {
 public:
  bool begin(uint8_t i2c_address = 0x33);
  void poll();
  ThermalStats stats() const { return stats_; }

 private:
  uint8_t i2c_address_ = 0x33;
  ThermalStats stats_;
  uint32_t last_poll_ms_ = 0;
};
