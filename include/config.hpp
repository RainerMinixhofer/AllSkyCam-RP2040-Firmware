#pragma once

#include <Arduino.h>

namespace cfg {

constexpr uint32_t USB_BAUD = 115200;
constexpr uint32_t STATUS_PERIOD_MS = 1000;

constexpr uint8_t DEW_PWM_BITS = 10;
constexpr uint16_t DEW_PWM_MAX = (1u << DEW_PWM_BITS) - 1u;
constexpr uint32_t DEW_PWM_FREQ_HZ = 20000;

constexpr uint16_t DEFAULT_COINC_WINDOW_US = 50;
constexpr uint16_t DEFAULT_GEIGER_DEBOUNCE_US = 120;

constexpr uint32_t TMC_UART_BAUD = 115200;
constexpr uint16_t TMC_DEFAULT_MICROSTEPS = 16;
constexpr uint8_t TMC_DEFAULT_IRUN = 20;   // 0..31
constexpr uint8_t TMC_DEFAULT_IHOLD = 8;   // 0..31
constexpr uint8_t TMC_DEFAULT_IHOLDDELAY = 6;

}  // namespace cfg
