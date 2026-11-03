# AllSkyCam RP2040Zero Controller

This project is a draft firmware structure for an RP2040Zero controller connected to:

- 4x TMC2209 (STEP/DIR + shared EN + shared UART0 topology)
- MCP23017 on I2C1
- Dual Geiger pulse inputs for coincidence detection
- MLX90640 on dedicated I2C0
- MOSFET control outputs for camera power and dew heater
- USB CDC link to Raspberry Pi 5

Implementation notes:

- TMC2209 UART framing/CRC follows the same 8-byte write / 4-byte read-request / 8-byte read-response pattern used in the reference project at [TMC2209/](/home/rainer/Projects/Breakout-Demos/TMC2209).
- Build is configured against PlatformIO RP2040 Arduino-mbed. For non-default pin mappings, modules instantiate dedicated UART/I2C objects on the required GPIOs.
- MCP23017 default address in firmware is currently `0x27` (decimal `39`).

## Pin map

- MLX90640: GP0 (SDA), GP1 (SCL)
- Geiger inputs: GP2, GP3
- Axis3 DIR: GP4
- Camera power MOSFET: GP5
- Dew PWM MOSFET: GP6
- Axis1 STEP/DIR: GP7/GP8
- Axis2 STEP/DIR: GP9/GP10
- Axis3 STEP: GP11
- TMC UART0 TX/RX: GP12/GP13
- MCP23017 I2C1 SDA/SCL: GP14/GP15
- Axis4 STEP/DIR: GP26/GP29
- TMC shared EN: GP27

## Module layout

- [main.cpp](./src/main.cpp): startup and cooperative scheduler
- [power_outputs.hpp](./src/modules/power_outputs.hpp): safe camera/dew output control
- [motion_control.hpp](./src/modules/motion_control.hpp): 4-axis step pulse draft
- [geiger_coincidence.hpp](./src/modules/geiger_coincidence.hpp): debounced pulse capture with one-pair-per-coincidence matching
- [io_expander_mcp23017.hpp](./src/modules/io_expander_mcp23017.hpp): park switch polling via MCP23017
- [thermal_camera_mlx90640.hpp](./src/modules/thermal_camera_mlx90640.hpp): MLX interface placeholder
- [usb_protocol.hpp](./src/modules/usb_protocol.hpp): USB CDC command protocol

## USB CDC commands

- `HELP`
- `GET STATUS`
- `GET GEIGER_STATS`
- `MCP TEST`
- `I2C SCAN`
- `SET CAM_PWR <0|1>`
- `SET DEW_PWM <0..100>`
- `SET DRV_EN <0|1>`
- `SET COINC_WINDOW_US <1..1000000>`
- `SET GEIGER_DEBOUNCE_US <1..1000000>`
- `MOVE <axis:0..3> <steps> <dir:0|1>`
- `TMC CFG`
- `TMC TEST <axis:0..3>`
- `TMC READ <axis:0..3> <reg:0..127|0x00..0x7F>`

## Telemetry framing

Periodic and requested status is emitted as a single-line JSON object:

```json
{"type":"status","cam":0,"dew":0,"drv_en":0,"geiger":{"ch1":0,"ch2":0,"coinc":0,"uncorr_ch1":0,"uncorr_ch2":0,"window_us":50,"debounce_us":120,"ch1_bounce":0,"ch2_bounce":0,"last_dt_us":0},"park":[0,0,0],"mcp":{"cfg":1,"ok":1,"raw_gpioa":7,"read_ok":42,"read_err":0},"thermal":{"valid":1,"min_c":0.00,"max_c":0.00,"avg_c":0.00},"tmc":[{"axis":0,"cfg":1,"uart_ok":1,"ifcnt":9,"drv_status":12345}],"tmc_error":""}
```

Command replies are JSON, e.g.:

```json
{"type":"ack","ok":true}
```

`GET GEIGER_STATS` returns total counts and a 1-second-binned time series (`series`) with correlated events (`corr`) and uncorrelated counts per channel (`uncorr_ch1`, `uncorr_ch2`).
`MCP TEST` returns a structured MCP23017 communication test result (`mcp_test`) including `ok`, `comm_ok`, `raw_gpioa`, `park`, and read counters.
`I2C SCAN` returns discovered 7-bit I2C addresses for both configured buses (`i2c0` GP0/1 and `i2c1` GP14/15).

## Build

```bash
platformio run --environment rp2040_zero
```

Upload:

```bash
platformio run --environment rp2040_zero --target upload
```

## USB CDC smoke test (Python)

Install dependency:

```bash
python3 -m pip install pyserial
```

Run test (auto-detect RP2040 CDC port):

```bash
python3 tools/test_usb_cdc.py
```

Or specify port explicitly:

```bash
python3 tools/test_usb_cdc.py --port /dev/ttyACM0
```

Verbose command/response logging:

```bash
python3 tools/test_usb_cdc.py --port /dev/ttyACM0 --verbose
```

Soak test examples:

```bash
# Run 100 iterations
python3 tools/test_usb_cdc.py --port /dev/ttyACM0 --loop 100

# Run for 5 minutes
python3 tools/test_usb_cdc.py --port /dev/ttyACM0 --duration 300
```

Notes:
- Close any serial monitor first (PlatformIO monitor, minicom, screen), otherwise the test can fail due to port contention.
- If your board re-enumerates slowly after open, increase startup wait:

```bash
python3 tools/test_usb_cdc.py --startup-timeout 12
```

The smoke test includes:
- status schema validation
- power/dew/driver control roundtrip
- TMC read check
- MCP23017 test command check

## Geiger statistics soak logging + visualization

Install dependencies:

```bash
python3 -m pip install pyserial matplotlib
```

Start indefinite Geiger statistics polling (for soak testing):

```bash
python3 tools/poll_geiger_stats.py --port /dev/ttyACM0
```

This appends the complete observed time series to:

```text
tools/data/geiger_timeseries.csv
```

Visualize logged events over time:

```bash
python3 tools/plot_geiger_timeseries.py --input tools/data/geiger_timeseries.csv
```

Save plot to file without opening a window:

```bash
python3 tools/plot_geiger_timeseries.py --input tools/data/geiger_timeseries.csv --save tools/data/geiger_plot.png --no-show
```

## Web frontend for periphery status + commands

Start local dashboard:

```bash
python3 tools/usb_webfrontend.py --port /dev/ttyACM0
```

or auto-detect:

```bash
python3 tools/usb_webfrontend.py
```

Then open:

```text
http://127.0.0.1:8765
```

## Known warnings / pitfalls

- **Linker warnings `_getpid/_kill`**  
  These were addressed by [syscalls_stubs.c](./src/syscalls_stubs.c). If they reappear, ensure this file is still compiled in the environment.

- **Linux upload warning about `99-platformio-udev.rules`**  
  Install PlatformIO udev rules as documented here:  
  <https://docs.platformio.org/en/latest/core/installation/udev-rules.html>

- **`No new RPxxxx device found yet` during upload reset**  
  This can still appear transiently on Linux. If upload proceeds (`Loading into Flash`), it is usually harmless.

- **Serial port changed after flashing**  
  RP2040 can re-enumerate from `/dev/ttyACM0` to `/dev/ttyACM1` (or vice versa).  
  Prefer auto-detection in tools, or re-check with:
  ```bash
  ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
  ```

- **Port contention (`device disconnected or multiple access on port`)**  
  Close other serial clients (PlatformIO monitor, minicom, screen, another tool instance) before running test scripts or web frontend.
