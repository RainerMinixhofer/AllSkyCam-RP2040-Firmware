#include <Arduino.h>

#include "config.hpp"
#include "modules/geiger_coincidence.hpp"
#include "modules/io_expander_mcp23017.hpp"
#include "modules/motion_control.hpp"
#include "modules/power_outputs.hpp"
#include "modules/thermal_camera_mlx90640.hpp"
#include "modules/usb_protocol.hpp"

PowerOutputs power_outputs;
MotionControl motion_control;
IoExpanderMcp23017 io_expander;
ThermalCameraMlx90640 thermal_camera;
UsbProtocol usb(power_outputs, motion_control, geiger, io_expander, thermal_camera);

uint32_t last_status_ms = 0;

void setup()
{
  Serial.begin(cfg::USB_BAUD);
  delay(50);

  power_outputs.begin();
  motion_control.begin();
  geiger.begin(cfg::DEFAULT_COINC_WINDOW_US, cfg::DEFAULT_GEIGER_DEBOUNCE_US);

  const bool mcp_ok = io_expander.begin(0x27);
  const bool mlx_ok = thermal_camera.begin(0x33);

  Serial.println("RP2040 controller boot");
  Serial.print("MCP23017=");
  Serial.println(mcp_ok ? "OK" : "ERR");
  Serial.print("MLX90640=");
  Serial.println(mlx_ok ? "OK" : "ERR");
  Serial.print("TMC_UART=");
  bool tmc_ok = true;
  for (uint8_t i = 0; i < MotionControl::kAxisCount; ++i)
  {
    tmc_ok = tmc_ok && motion_control.axisStatus(i).configured;
  }
  Serial.println(tmc_ok ? "OK" : "ERR");
  Serial.println("Type HELP");
}

void loop()
{
  usb.poll();

  motion_control.tick();
  geiger.poll();
  io_expander.poll();
  thermal_camera.poll();

  const uint32_t now = millis();
  if (now - last_status_ms >= cfg::STATUS_PERIOD_MS)
  {
    last_status_ms = now;
    usb.publishStatus();
  }
}
