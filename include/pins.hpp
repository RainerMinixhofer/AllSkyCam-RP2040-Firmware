#pragma once

#include <Arduino.h>

namespace pins {

// I2C0: MLX90640
constexpr uint8_t MLX_SDA = 0;
constexpr uint8_t MLX_SCL = 1;

// Geiger coincidence inputs (open collector, pulled up to 3V3)
constexpr uint8_t GEIGER_1 = 2;
constexpr uint8_t GEIGER_2 = 3;

// Axis 3 DIR shares GP4; camera power on GP5; dew PWM on GP6
constexpr uint8_t AXIS3_DIR = 4;
constexpr uint8_t CAM_POWER = 5;
constexpr uint8_t DEW_PWM = 6;

// TMC2209 STEP/DIR axes + shared EN/UART
constexpr uint8_t AXIS1_STEP = 7;
constexpr uint8_t AXIS1_DIR = 8;
constexpr uint8_t AXIS2_STEP = 9;
constexpr uint8_t AXIS2_DIR = 10;
constexpr uint8_t AXIS3_STEP = 11;
constexpr uint8_t TMC_UART_TX = 12;
constexpr uint8_t TMC_UART_RX = 13;

// I2C1: MCP23017
constexpr uint8_t MCP_SDA = 14;
constexpr uint8_t MCP_SCL = 15;

constexpr uint8_t AXIS4_STEP = 26;
constexpr uint8_t TMC_EN = 27;
constexpr uint8_t AXIS4_DIR = 29;

}  // namespace pins
