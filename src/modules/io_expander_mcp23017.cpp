#include "modules/io_expander_mcp23017.hpp"

#include <Wire.h>

#include "pins.hpp"

namespace {
constexpr uint8_t MCP23017_IODIRA = 0x00;
constexpr uint8_t MCP23017_GPPUA = 0x0C;
constexpr uint8_t MCP23017_GPIOA = 0x12;
arduino::MbedI2C mcp_wire(pins::MCP_SDA, pins::MCP_SCL);
}  // namespace

bool IoExpanderMcp23017::begin(uint8_t i2c_address) {
  i2c_address_ = i2c_address;
  mcp_wire.begin();

  mcp_wire.beginTransmission(i2c_address_);
  mcp_wire.write(MCP23017_IODIRA);
  mcp_wire.write(0x07);  // GPA0..2 inputs
  const bool iodir_ok = mcp_wire.endTransmission() == 0;

  mcp_wire.beginTransmission(i2c_address_);
  mcp_wire.write(MCP23017_GPPUA);
  mcp_wire.write(0x07);  // pull-ups on GPA0..2
  const bool gppu_ok = mcp_wire.endTransmission() == 0;

  configured_ = iodir_ok && gppu_ok;
  comm_ok_ = false;
  read_ok_count_ = 0;
  read_error_count_ = 0;

  uint8_t raw = 0;
  if (configured_ && readInputs(raw)) {
    comm_ok_ = true;
  }
  return configured_;
}

void IoExpanderMcp23017::poll() {
  uint8_t raw = 0;
  if (!readInputs(raw)) {
    return;
  }
}

Mcp23017Stats IoExpanderMcp23017::stats() const {
  Mcp23017Stats out;
  out.configured = configured_;
  out.comm_ok = comm_ok_;
  out.raw_gpioa = raw_gpioa_;
  out.read_ok_count = read_ok_count_;
  out.read_error_count = read_error_count_;
  out.switches = switches_;
  return out;
}

bool IoExpanderMcp23017::test() {
  uint8_t raw = 0;
  return readInputs(raw);
}

bool IoExpanderMcp23017::readInputs(uint8_t& gpioa_raw) {
  mcp_wire.beginTransmission(i2c_address_);
  mcp_wire.write(MCP23017_GPIOA);
  if (mcp_wire.endTransmission(false) != 0) {
    comm_ok_ = false;
    read_error_count_++;
    return false;
  }

  if (mcp_wire.requestFrom(static_cast<uint8_t>(i2c_address_), static_cast<size_t>(1)) != 1) {
    comm_ok_ = false;
    read_error_count_++;
    return false;
  }

  const uint8_t val = static_cast<uint8_t>(mcp_wire.read());
  gpioa_raw = val;
  raw_gpioa_ = val;
  comm_ok_ = true;
  read_ok_count_++;
  switches_.sw0 = (val & (1u << 0)) == 0;
  switches_.sw1 = (val & (1u << 1)) == 0;
  switches_.sw2 = (val & (1u << 2)) == 0;
  return true;
}
