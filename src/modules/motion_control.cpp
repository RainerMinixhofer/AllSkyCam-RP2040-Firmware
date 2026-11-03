#include "modules/motion_control.hpp"

#include <Serial.h>
#include <string.h>

#include "config.hpp"
#include "pins.hpp"

namespace {
constexpr uint8_t kSync = 0x05;
constexpr uint8_t kReadFrameLen = 4;
constexpr uint8_t kWriteFrameLen = 8;
constexpr uint8_t kResponseFrameLen = 8;
arduino::UART tmc_uart(pins::TMC_UART_TX, pins::TMC_UART_RX);
}  // namespace

const uint8_t MotionControl::kUartAddresses[MotionControl::kAxisCount] = {0, 1, 2, 3};

void MotionControl::begin() {
  axis_pins_[0] = {pins::AXIS1_STEP, pins::AXIS1_DIR};
  axis_pins_[1] = {pins::AXIS2_STEP, pins::AXIS2_DIR};
  axis_pins_[2] = {pins::AXIS3_STEP, pins::AXIS3_DIR};
  axis_pins_[3] = {pins::AXIS4_STEP, pins::AXIS4_DIR};

  for (const auto& axis : axis_pins_) {
    pinMode(axis.step_pin, OUTPUT);
    pinMode(axis.dir_pin, OUTPUT);
    digitalWrite(axis.step_pin, LOW);
    digitalWrite(axis.dir_pin, LOW);
  }

  pinMode(pins::TMC_EN, OUTPUT);
  enableDrivers(false);

  tmc_uart.begin(cfg::TMC_UART_BAUD);
  delay(2);
  clearUartRx();
  configureAllDrivers();
}

void MotionControl::enableDrivers(bool enabled) {
  drivers_enabled_ = enabled;
  // TMC2209 ENN is active-low.
  digitalWrite(pins::TMC_EN, enabled ? LOW : HIGH);
}

void MotionControl::queueRelativeMove(uint8_t axis_index, int32_t steps, bool direction) {
  if (axis_index >= kAxisCount || steps <= 0) {
    return;
  }

  digitalWrite(axis_pins_[axis_index].dir_pin, direction ? HIGH : LOW);
  noInterrupts();
  pending_steps_[axis_index] += steps;
  interrupts();
}

void MotionControl::tick() {
  if (!drivers_enabled_) {
    return;
  }

  const uint32_t now_us = micros();
  if (now_us - last_step_us_ < step_period_us_) {
    return;
  }
  last_step_us_ = now_us;

  for (uint8_t axis = 0; axis < kAxisCount; ++axis) {
    if (pending_steps_[axis] <= 0) {
      continue;
    }
    digitalWrite(axis_pins_[axis].step_pin, HIGH);
    delayMicroseconds(2);
    digitalWrite(axis_pins_[axis].step_pin, LOW);
    noInterrupts();
    pending_steps_[axis]--;
    interrupts();
  }
}

bool MotionControl::configureAllDrivers() {
  bool ok = true;
  const uint32_t ihold_irun = (static_cast<uint32_t>(cfg::TMC_DEFAULT_IHOLDDELAY & 0x0F) << 16) |
                              (static_cast<uint32_t>(cfg::TMC_DEFAULT_IRUN & 0x1F) << 8) |
                              (static_cast<uint32_t>(cfg::TMC_DEFAULT_IHOLD & 0x1F));

  const uint8_t mres = []() -> uint8_t {
    switch (cfg::TMC_DEFAULT_MICROSTEPS) {
      case 256: return 0;
      case 128: return 1;
      case 64: return 2;
      case 32: return 3;
      case 16: return 4;
      case 8: return 5;
      case 4: return 6;
      case 2: return 7;
      case 1: return 8;
      default: return 4;
    }
  }();

  // toff=3, hstrt=4, hend=1, tbl=2, mres=cfg default, intpol=1
  const uint32_t chopconf = (3u << 0) | (4u << 4) | (1u << 7) | (2u << 15) | (static_cast<uint32_t>(mres) << 24) |
                            (1u << 28);

  for (uint8_t axis = 0; axis < kAxisCount; ++axis) {
    bool axis_ok = true;
    axis_ok &= writeDriverRegister(axis, kRegGconf, 0x00000000u);
    axis_ok &= writeDriverRegister(axis, kRegIholdIrun, ihold_irun);
    axis_ok &= writeDriverRegister(axis, kRegTpowerdown, 0x00000014u);
    axis_ok &= writeDriverRegister(axis, kRegChopconf, chopconf);

    uint32_t ifcnt = 0;
    if (readDriverRegister(axis, kRegIfcnt, ifcnt)) {
      axis_status_[axis].ifcnt = ifcnt;
    } else {
      axis_ok = false;
    }

    uint32_t drv_status = 0;
    if (readDriverRegister(axis, kRegDrvStatus, drv_status)) {
      axis_status_[axis].drv_status = drv_status;
    } else {
      axis_ok = false;
    }

    axis_status_[axis].configured = axis_ok;
    axis_status_[axis].uart_ok = axis_ok;
    ok &= axis_ok;
  }
  return ok;
}

bool MotionControl::testUartAxis(uint8_t axis_index) {
  uint32_t sample1 = 0;
  uint32_t sample2 = 0;
  uint32_t sample3 = 0;
  if (!readDriverRegister(axis_index, kRegDrvStatus, sample1)) {
    return false;
  }
  if (!readDriverRegister(axis_index, kRegDrvStatus, sample2)) {
    return false;
  }
  if (!readDriverRegister(axis_index, kRegDrvStatus, sample3)) {
    return false;
  }

  const bool stable = (sample1 == sample2) && (sample2 == sample3);
  const bool suspicious = (sample1 == 0x00000000u) || (sample1 == 0xFFFFFFFFu);
  axis_status_[axis_index].uart_ok = stable && !suspicious;
  axis_status_[axis_index].drv_status = sample1;

  if (!axis_status_[axis_index].uart_ok) {
    return setError("TMC UART test unstable/suspicious");
  }
  return true;
}

bool MotionControl::readDriverRegister(uint8_t axis_index, uint8_t reg_addr, uint32_t& value) {
  if (axis_index >= kAxisCount) {
    return setError("TMC axis index out of range");
  }

  const uint8_t addr = kUartAddresses[axis_index] & 0x03;
  uint8_t frame[kReadFrameLen] = {kSync, addr, static_cast<uint8_t>(reg_addr & 0x7Fu), 0u};
  frame[3] = crc8Atm(frame, 3);

  clearUartRx();
  tmc_uart.write(frame, kReadFrameLen);
  tmc_uart.flush();
  delayMicroseconds(3000);

  if (!readFrame(20, value)) {
    axis_status_[axis_index].uart_ok = false;
    return false;
  }
  axis_status_[axis_index].uart_ok = true;
  return true;
}

bool MotionControl::writeDriverRegister(uint8_t axis_index, uint8_t reg_addr, uint32_t value) {
  if (axis_index >= kAxisCount) {
    return setError("TMC axis index out of range");
  }

  const uint8_t addr = kUartAddresses[axis_index] & 0x03;
  uint8_t frame[kWriteFrameLen] = {
      kSync,
      addr,
      static_cast<uint8_t>((reg_addr & 0x7Fu) | 0x80u),
      static_cast<uint8_t>((value >> 24) & 0xFFu),
      static_cast<uint8_t>((value >> 16) & 0xFFu),
      static_cast<uint8_t>((value >> 8) & 0xFFu),
      static_cast<uint8_t>(value & 0xFFu),
      0u,
  };
  frame[7] = crc8Atm(frame, 7);

  clearUartRx();
  tmc_uart.write(frame, kWriteFrameLen);
  tmc_uart.flush();
  delayMicroseconds(1500);
  axis_status_[axis_index].uart_ok = true;
  return true;
}

TmcAxisStatus MotionControl::axisStatus(uint8_t axis_index) const {
  if (axis_index >= kAxisCount) {
    return {};
  }
  return axis_status_[axis_index];
}

uint8_t MotionControl::crc8Atm(const uint8_t* data, size_t len) const {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    uint8_t byte = data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint8_t feedback = static_cast<uint8_t>(((crc >> 7) ^ (byte & 0x01u)) & 0x01u);
      crc = static_cast<uint8_t>((crc << 1) & 0xFFu);
      if (feedback != 0u) {
        crc ^= 0x07u;
      }
      byte >>= 1;
    }
  }
  return crc;
}

bool MotionControl::readFrame(uint32_t timeout_ms, uint32_t& value) {
  uint8_t buf[32] = {};
  size_t len = 0;
  const uint32_t start = millis();

  while ((millis() - start) < timeout_ms && len < sizeof(buf)) {
    while (tmc_uart.available() > 0 && len < sizeof(buf)) {
      buf[len++] = static_cast<uint8_t>(tmc_uart.read());
    }
  }

  if (len < kResponseFrameLen) {
    return setError("TMC UART timeout/short frame");
  }

  for (size_t i = 0; i + kResponseFrameLen <= len; ++i) {
    if (buf[i] != kSync) {
      continue;
    }
    const uint8_t expected = crc8Atm(&buf[i], kResponseFrameLen - 1);
    if (buf[i + 7] != expected) {
      continue;
    }
    value = (static_cast<uint32_t>(buf[i + 3]) << 24) | (static_cast<uint32_t>(buf[i + 4]) << 16) |
            (static_cast<uint32_t>(buf[i + 5]) << 8) | static_cast<uint32_t>(buf[i + 6]);
    return true;
  }
  return setError("TMC UART CRC/sync mismatch");
}

bool MotionControl::clearUartRx() {
  while (tmc_uart.available() > 0) {
    tmc_uart.read();
  }
  return true;
}

bool MotionControl::setError(const char* msg) {
  strncpy(last_error_, msg, sizeof(last_error_) - 1);
  last_error_[sizeof(last_error_) - 1] = '\0';
  return false;
}
