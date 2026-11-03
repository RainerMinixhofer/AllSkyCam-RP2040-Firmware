#pragma once

#include <Arduino.h>

#include "modules/geiger_coincidence.hpp"
#include "modules/io_expander_mcp23017.hpp"
#include "modules/motion_control.hpp"
#include "modules/power_outputs.hpp"
#include "modules/thermal_camera_mlx90640.hpp"

class UsbProtocol {
 public:
  UsbProtocol(PowerOutputs& power, MotionControl& motion, GeigerCoincidence& geiger,
              IoExpanderMcp23017& mcp, ThermalCameraMlx90640& thermal)
      : power_(power), motion_(motion), geiger_(geiger), mcp_(mcp), thermal_(thermal) {}

  void poll();
  void publishStatus();
  void publishGeigerStats();
  void publishMcpTest();
  void publishI2cScan();

 private:
  bool parseLine(const char* line);
  void printHelp() const;

  PowerOutputs& power_;
  MotionControl& motion_;
  GeigerCoincidence& geiger_;
  IoExpanderMcp23017& mcp_;
  ThermalCameraMlx90640& thermal_;

  static constexpr size_t kLineBufferSize = 96;
  char line_buffer_[kLineBufferSize] = {};
  size_t line_len_ = 0;
};
