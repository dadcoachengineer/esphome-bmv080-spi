# esphome-bmv080-spi

An ESPHome external component for the **Bosch BMV080** particulate-matter (PM1/PM2.5/PM10)
sensor wired over **SPI** — for boards like the [BlackIoT Polverine](https://github.com/BlackIoT/Polverine)
that connect the BMV080 to SPI rather than I²C.

This is an **SPI fork** of [`sweitzja/esphome-bmv080`](https://github.com/sweitzja/esphome-bmv080)
(I²C). The Bosch BMV080 SDK is bus-agnostic — `bmv080_open()` takes generic 16-bit
read/write callbacks — so only the transport callbacks, the base class, and the YAML
schema differ from the I²C version. All of the SDK orchestration, the dedicated 64 KB
FreeRTOS task, the mutex hand-off, and the sensor publishing are unchanged from sweitzja's
work, which is gratefully reused.

## Why a separate component

The BMV080 driver is shipped only as Bosch precompiled static libraries
(`lib_bmv080.a` + `lib_postProcessor.a`, per architecture — bundled in `bosch/`).
No published ESPHome BMV080 component (sweitzja's, goeki04's) supported SPI; they are
all hardcoded I²C, which can't reach an SPI-wired sensor. This adds the SPI transport.

## SPI protocol

Matches BlackIoT's reference firmware (`POLVERINE_HOMEASSISTANT_DEMO/src/sensors/bmv080_io.c`):

- **SPI mode 0** (CPOL=0, CPHA=0), **MSB-first**, **1 MHz**, CS active-low.
- Each transaction: the 16-bit `header` is clocked out MSB-first (the SDK's "address
  phase"), used **as-is** — the `header << 1` shift is an I²C-only requirement — then the
  payload is read/written as 16-bit words, MSB-first (big-endian on the wire).

In ESPHome `SPIDevice` terms (verified against ESPHome 2026.6.1 `spi.h`):
`enable()` → `write_array(header, 2)` → `read_array`/`write_array(payload)` → `disable()`,
with template params `<BIT_ORDER_MSB_FIRST, CLOCK_POLARITY_LOW, CLOCK_PHASE_LEADING, DATA_RATE_1MHZ>`.

## Wiring (BlackIoT Polverine, ESP32-S3)

| Signal | GPIO |
|---|---|
| SCLK | 12 |
| MOSI (SDO) | 11 |
| MISO (SDI) | 13 |
| CS | 10 |

(The BME690 is a separate I²C device on SDA 14 / SCL 21 @ 0x76.)

## Usage

```yaml
external_components:
  - source:
      type: local
      path: components        # or: type: git, url: github://<you>/esphome-bmv080-spi

spi:
  clk_pin: 12
  mosi_pin: 11
  miso_pin: 13

bmv080:
  id: bmv080_sensor
  cs_pin: 10
  mode: continuous                 # or duty_cycle
  measurement_algorithm: high_precision
  update_interval: 5s

sensor:
  - platform: bmv080
    bmv080_id: bmv080_sensor
    pm_2_5:   { name: "PM 2.5" }
    pm_1_0:   { name: "PM 1.0" }
    pm_10:    { name: "PM 10" }

binary_sensor:
  - platform: bmv080
    bmv080_id: bmv080_sensor
    obstructed:   { name: "Sensor Obstructed" }
    out_of_range: { name: "Out of Range" }
```

## Status / upstream

Built 2026-06; verified against ESPHome 2026.6.1's SPI API; **pending on-hardware
validation** on the Polverine. Intended for upstreaming once proven — ideally by adding
SPI support back to sweitzja's component (so I²C + SPI live in one component) and/or as a
core PR (precedent: `bme68x_bsec2` is in ESPHome core depending on a closed Bosch lib).

## Credits / license

- Component architecture, Bosch SDK wrapping, FreeRTOS task: © [@sweitzja](https://github.com/sweitzja/esphome-bmv080) (MIT).
- SPI transport: this fork.
- `bosch/*.a`, `bosch/*.h`: Bosch Sensortec, under Bosch's SDK license (not MIT). See Bosch Sensortec terms before redistributing.
