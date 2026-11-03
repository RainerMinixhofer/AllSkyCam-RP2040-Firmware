#include "modules/usb_protocol.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Wire.h>

#include "pins.hpp"

namespace {
bool startsWith(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }
}  // namespace

void UsbProtocol::poll() {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      line_buffer_[line_len_] = '\0';
      const bool ok = parseLine(line_buffer_);
      if (!ok) {
        Serial.println("{\"type\":\"ack\",\"ok\":false}");
      }
      line_len_ = 0;
      continue;
    }
    if (line_len_ + 1 < kLineBufferSize) {
      line_buffer_[line_len_++] = static_cast<char>(ch);
    } else {
      line_len_ = 0;
      Serial.println("{\"type\":\"ack\",\"ok\":false,\"error\":\"line_too_long\"}");
    }
  }
}

void UsbProtocol::publishStatus() {
  const GeigerStats g = geiger_.snapshot();
  const ParkSwitchState p = mcp_.parkSwitches();
  const Mcp23017Stats m = mcp_.stats();
  const ThermalStats t = thermal_.stats();

  Serial.print("{\"type\":\"status\"");
  Serial.print(",\"cam\":");
  Serial.print(power_.cameraPower() ? 1 : 0);
  Serial.print(",\"dew\":");
  Serial.print(power_.dewPercent());
  Serial.print(",\"drv_en\":");
  Serial.print(motion_.driversEnabled() ? 1 : 0);

  Serial.print(",\"geiger\":{\"ch1\":");
  Serial.print(g.ch1_count);
  Serial.print(",\"ch2\":");
  Serial.print(g.ch2_count);
  Serial.print(",\"coinc\":");
  Serial.print(g.coinc_count);
  Serial.print(",\"uncorr_ch1\":");
  Serial.print(g.ch1_uncorr_count);
  Serial.print(",\"uncorr_ch2\":");
  Serial.print(g.ch2_uncorr_count);
  Serial.print(",\"window_us\":");
  Serial.print(g.coinc_window_us);
  Serial.print(",\"debounce_us\":");
  Serial.print(g.debounce_us);
  Serial.print(",\"ch1_bounce\":");
  Serial.print(g.ch1_bounce_rejects);
  Serial.print(",\"ch2_bounce\":");
  Serial.print(g.ch2_bounce_rejects);
  Serial.print(",\"last_dt_us\":");
  Serial.print(g.last_coinc_dt_us);
  Serial.print("}");

  Serial.print(",\"park\":[");
  Serial.print(p.sw0 ? 1 : 0);
  Serial.print(",");
  Serial.print(p.sw1 ? 1 : 0);
  Serial.print(",");
  Serial.print(p.sw2 ? 1 : 0);
  Serial.print("]");
  Serial.print(",\"mcp\":{\"cfg\":");
  Serial.print(m.configured ? 1 : 0);
  Serial.print(",\"ok\":");
  Serial.print(m.comm_ok ? 1 : 0);
  Serial.print(",\"raw_gpioa\":");
  Serial.print(m.raw_gpioa);
  Serial.print(",\"read_ok\":");
  Serial.print(m.read_ok_count);
  Serial.print(",\"read_err\":");
  Serial.print(m.read_error_count);
  Serial.print("}");

  Serial.print(",\"thermal\":{\"valid\":");
  Serial.print(t.frame_valid ? 1 : 0);
  Serial.print(",\"min_c\":");
  Serial.print(t.min_c, 2);
  Serial.print(",\"max_c\":");
  Serial.print(t.max_c, 2);
  Serial.print(",\"avg_c\":");
  Serial.print(t.avg_c, 2);
  Serial.print("}");

  Serial.print(",\"tmc\":[");
  for (uint8_t i = 0; i < MotionControl::kAxisCount; ++i) {
    const TmcAxisStatus st = motion_.axisStatus(i);
    if (i > 0) {
      Serial.print(",");
    }
    Serial.print("{\"axis\":");
    Serial.print(i);
    Serial.print(",\"cfg\":");
    Serial.print(st.configured ? 1 : 0);
    Serial.print(",\"uart_ok\":");
    Serial.print(st.uart_ok ? 1 : 0);
    Serial.print(",\"ifcnt\":");
    Serial.print(st.ifcnt);
    Serial.print(",\"drv_status\":");
    Serial.print(st.drv_status);
    Serial.print("}");
  }
  Serial.print("]");

  Serial.print(",\"tmc_error\":\"");
  Serial.print(motion_.lastError());
  Serial.println("\"}");
}

void UsbProtocol::publishGeigerStats() {
  const GeigerStats stats = geiger_.snapshot();
  const GeigerSeriesSnapshot series = geiger_.seriesSnapshot();

  Serial.print("{\"type\":\"geiger_stats\"");
  Serial.print(",\"bin_ms\":");
  Serial.print(series.bin_ms);
  Serial.print(",\"total\":{\"ch1\":");
  Serial.print(stats.ch1_count);
  Serial.print(",\"ch2\":");
  Serial.print(stats.ch2_count);
  Serial.print(",\"corr\":");
  Serial.print(stats.coinc_count);
  Serial.print(",\"uncorr_ch1\":");
  Serial.print(stats.ch1_uncorr_count);
  Serial.print(",\"uncorr_ch2\":");
  Serial.print(stats.ch2_uncorr_count);
  Serial.print("},\"series\":[");
  for (uint8_t i = 0; i < series.count; ++i) {
    if (i > 0) {
      Serial.print(",");
    }
    Serial.print("{\"t_ms\":");
    Serial.print(series.points[i].t_ms);
    Serial.print(",\"corr\":");
    Serial.print(series.points[i].corr);
    Serial.print(",\"uncorr_ch1\":");
    Serial.print(series.points[i].uncorr_ch1);
    Serial.print(",\"uncorr_ch2\":");
    Serial.print(series.points[i].uncorr_ch2);
    Serial.print("}");
  }
  Serial.println("]}");
}

void UsbProtocol::publishMcpTest() {
  const bool ok = mcp_.test();
  const Mcp23017Stats m = mcp_.stats();
  Serial.print("{\"type\":\"mcp_test\",\"ok\":");
  Serial.print(ok ? "true" : "false");
  Serial.print(",\"cfg\":");
  Serial.print(m.configured ? 1 : 0);
  Serial.print(",\"comm_ok\":");
  Serial.print(m.comm_ok ? 1 : 0);
  Serial.print(",\"raw_gpioa\":");
  Serial.print(m.raw_gpioa);
  Serial.print(",\"park\":[");
  Serial.print(m.switches.sw0 ? 1 : 0);
  Serial.print(",");
  Serial.print(m.switches.sw1 ? 1 : 0);
  Serial.print(",");
  Serial.print(m.switches.sw2 ? 1 : 0);
  Serial.print("],\"read_ok\":");
  Serial.print(m.read_ok_count);
  Serial.print(",\"read_err\":");
  Serial.print(m.read_error_count);
  Serial.println("}");
}

void UsbProtocol::publishI2cScan() {
  arduino::MbedI2C i2c0_wire(pins::MLX_SDA, pins::MLX_SCL);
  arduino::MbedI2C i2c1_wire(pins::MCP_SDA, pins::MCP_SCL);
  i2c0_wire.begin();
  i2c1_wire.begin();

  auto print_bus = [](const char* name, uint8_t sda, uint8_t scl, arduino::MbedI2C& bus, bool first) {
    if (!first) {
      Serial.print(",");
    }
    Serial.print("{\"name\":\"");
    Serial.print(name);
    Serial.print("\",\"sda\":");
    Serial.print(sda);
    Serial.print(",\"scl\":");
    Serial.print(scl);
    Serial.print(",\"found\":[");
    bool first_addr = true;
    for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
      bus.beginTransmission(addr);
      if (bus.endTransmission() == 0) {
        if (!first_addr) {
          Serial.print(",");
        }
        first_addr = false;
        Serial.print(addr);
      }
    }
    Serial.print("]}");
  };

  Serial.print("{\"type\":\"i2c_scan\",\"buses\":[");
  print_bus("i2c0", pins::MLX_SDA, pins::MLX_SCL, i2c0_wire, true);
  print_bus("i2c1", pins::MCP_SDA, pins::MCP_SCL, i2c1_wire, false);
  Serial.println("]}");
}

bool UsbProtocol::parseLine(const char* line) {
  while (isspace(*line)) {
    ++line;
  }
  if (*line == '\0') {
    return true;
  }

  if (startsWith(line, "HELP")) {
    printHelp();
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "GET STATUS")) {
    publishStatus();
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "GET GEIGER_STATS")) {
    publishGeigerStats();
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "MCP TEST")) {
    publishMcpTest();
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "I2C SCAN")) {
    publishI2cScan();
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "SET CAM_PWR ")) {
    const int v = atoi(line + strlen("SET CAM_PWR "));
    power_.setCameraPower(v != 0);
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "SET DEW_PWM ")) {
    int v = atoi(line + strlen("SET DEW_PWM "));
    if (v < 0) {
      v = 0;
    }
    if (v > 100) {
      v = 100;
    }
    power_.setDewPercent(static_cast<uint8_t>(v));
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "SET DRV_EN ")) {
    const int v = atoi(line + strlen("SET DRV_EN "));
    motion_.enableDrivers(v != 0);
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "SET COINC_WINDOW_US ")) {
    const int v = atoi(line + strlen("SET COINC_WINDOW_US "));
    if (v <= 0 || v > 1000000) {
      return false;
    }
    geiger_.setCoincidenceWindowUs(static_cast<uint32_t>(v));
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "SET GEIGER_DEBOUNCE_US ")) {
    const int v = atoi(line + strlen("SET GEIGER_DEBOUNCE_US "));
    if (v <= 0 || v > 1000000) {
      return false;
    }
    geiger_.setDebounceUs(static_cast<uint32_t>(v));
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "MOVE ")) {
    uint32_t axis = 0;
    uint32_t steps = 0;
    int dir = 0;
    const int parsed = sscanf(line, "MOVE %lu %lu %d", &axis, &steps, &dir);
    if (parsed != 3 || axis >= MotionControl::kAxisCount) {
      return false;
    }
    motion_.queueRelativeMove(static_cast<uint8_t>(axis), static_cast<int32_t>(steps), dir != 0);
    Serial.println("{\"type\":\"ack\",\"ok\":true}");
    return true;
  }

  if (startsWith(line, "TMC CFG")) {
    const bool ok = motion_.configureAllDrivers();
    Serial.print("{\"type\":\"tmc_cfg\",\"ok\":");
    Serial.print(ok ? "true" : "false");
    Serial.println("}");
    return true;
  }

  if (startsWith(line, "TMC TEST ")) {
    uint32_t axis = 0;
    if (sscanf(line, "TMC TEST %lu", &axis) != 1 || axis >= MotionControl::kAxisCount) {
      return false;
    }
    const bool ok = motion_.testUartAxis(static_cast<uint8_t>(axis));
    Serial.print("{\"type\":\"tmc_test\",\"axis\":");
    Serial.print(axis);
    Serial.print(",\"ok\":");
    Serial.print(ok ? "true" : "false");
    Serial.println("}");
    return true;
  }

  if (startsWith(line, "TMC READ ")) {
    char axis_s[12] = {};
    char reg_s[12] = {};
    if (sscanf(line, "TMC READ %11s %11s", axis_s, reg_s) != 2) {
      return false;
    }
    const uint32_t axis = strtoul(axis_s, nullptr, 10);
    const uint32_t reg = strtoul(reg_s, nullptr, 0);
    if (axis >= MotionControl::kAxisCount || reg > 0x7F) {
      return false;
    }
    uint32_t value = 0;
    const bool ok = motion_.readDriverRegister(static_cast<uint8_t>(axis), static_cast<uint8_t>(reg), value);
    Serial.print("{\"type\":\"tmc_read\",\"axis\":");
    Serial.print(axis);
    Serial.print(",\"reg\":");
    Serial.print(reg);
    Serial.print(",\"ok\":");
    Serial.print(ok ? "true" : "false");
    Serial.print(",\"value\":");
    Serial.print(value);
    Serial.println("}");
    return true;
  }

  return false;
}

void UsbProtocol::printHelp() const {
  Serial.println("Commands:");
  Serial.println("  HELP");
  Serial.println("  GET STATUS");
  Serial.println("  GET GEIGER_STATS");
  Serial.println("  MCP TEST");
  Serial.println("  I2C SCAN");
  Serial.println("  SET CAM_PWR <0|1>");
  Serial.println("  SET DEW_PWM <0..100>");
  Serial.println("  SET DRV_EN <0|1>");
  Serial.println("  SET COINC_WINDOW_US <1..1000000>");
  Serial.println("  SET GEIGER_DEBOUNCE_US <1..1000000>");
  Serial.println("  MOVE <axis:0..3> <steps> <dir:0|1>");
  Serial.println("  TMC CFG");
  Serial.println("  TMC TEST <axis:0..3>");
  Serial.println("  TMC READ <axis:0..3> <reg:0..127|0x00..0x7F>");
}
