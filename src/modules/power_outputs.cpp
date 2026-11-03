#include "modules/power_outputs.hpp"

#include "config.hpp"
#include "pins.hpp"

void PowerOutputs::begin() {
  pinMode(pins::CAM_POWER, OUTPUT);
  digitalWrite(pins::CAM_POWER, LOW);
  camera_power_on_ = false;

  analogWriteResolution(cfg::DEW_PWM_BITS);
  pinMode(pins::DEW_PWM, OUTPUT);
  analogWrite(pins::DEW_PWM, 0);
  dew_percent_ = 0;
}

void PowerOutputs::setCameraPower(bool on) {
  camera_power_on_ = on;
  digitalWrite(pins::CAM_POWER, on ? HIGH : LOW);
}

void PowerOutputs::setDewPercent(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  dew_percent_ = percent;
  const uint32_t pwm = static_cast<uint32_t>(cfg::DEW_PWM_MAX) * percent / 100u;
  analogWrite(pins::DEW_PWM, pwm);
}
