#include "modules/thermal_camera_mlx90640.hpp"

#include <Wire.h>

#include "pins.hpp"

namespace {
arduino::MbedI2C mlx_wire(pins::MLX_SDA, pins::MLX_SCL);
}

bool ThermalCameraMlx90640::begin(uint8_t i2c_address) {
  i2c_address_ = i2c_address;
  mlx_wire.begin();

  mlx_wire.beginTransmission(i2c_address_);
  return mlx_wire.endTransmission() == 0;
}

void ThermalCameraMlx90640::poll() {
  const uint32_t now = millis();
  if (now - last_poll_ms_ < 250) {
    return;
  }
  last_poll_ms_ = now;

  // Placeholder until MLX90640 frame decoder is added.
  stats_.frame_valid = true;
  stats_.min_c = 0.0f;
  stats_.max_c = 0.0f;
  stats_.avg_c = 0.0f;
}
