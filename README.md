# esphome-bmv080

ESPHome external components for the **Bosch BMV080** particulate-matter (PM1/PM2.5/PM10)
sensor — over **I²C or SPI**.

The Bosch BMV080 SDK is bus-agnostic (`bmv080_open()` takes generic 16-bit read/write
callbacks), so this repo mirrors ESPHome's `pn532` / `pn532_i2c` / `pn532_spi` layout:

| Component | Role |
|---|---|
| `bmv080` | Bus-agnostic hub: Bosch SDK orchestration, the dedicated 64 KB FreeRTOS task, the mutex hand-off, sensor publishing. Not used directly. |
| `bmv080_i2c` | I²C leaf — the original transport from [`sweitzja/esphome-bmv080`](https://github.com/sweitzja/esphome-bmv080). |
| `bmv080_spi` | SPI leaf — for boards that wire the BMV080 to SPI (e.g. the [BlackIoT Polverine](https://github.com/BlackIoT/Polverine)). |

The I²C transport, the SDK orchestration, the FreeRTOS task, the mutex hand-off, and the
sensor publishing are all from [@sweitzja](https://github.com/sweitzja/esphome-bmv080)'s
work, gratefully reused. This repo adds the **SPI transport** and refactors the component
into a bus-agnostic hub + two bus leaves.

## Why these components

The BMV080 driver ships only as Bosch precompiled static libraries
(`lib_bmv080.a` + `lib_postProcessor.a`, per architecture — bundled in
`components/bmv080/bosch/`). No published ESPHome BMV080 component supported SPI; they were
all hardcoded I²C, which can't reach an SPI-wired sensor. This unifies both buses behind one
hub.

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

(On the Polverine the BME690 is a separate I²C device on SDA 14 / SCL 21 @ 0x76.)

## Usage

```yaml
external_components:
  - source: github://dadcoachengineer/esphome-bmv080-spi
    components: [bmv080, bmv080_spi]   # or bmv080_i2c
```

### SPI (e.g. BlackIoT Polverine)

```yaml
spi:
  clk_pin: 12
  mosi_pin: 11
  miso_pin: 13

bmv080_spi:
  id: bmv080_sensor
  cs_pin: 10
  mode: continuous                 # or duty_cycle
  measurement_algorithm: high_precision
  update_interval: 5s

sensor:
  - platform: bmv080
    bmv080_id: bmv080_sensor
    pm_2_5: { name: "PM 2.5" }
    pm_1_0: { name: "PM 1.0" }
    pm_10:  { name: "PM 10" }

binary_sensor:
  - platform: bmv080
    bmv080_id: bmv080_sensor
    obstructed:   { name: "Sensor Obstructed" }
    out_of_range: { name: "Out of Range" }
```

### I²C

```yaml
i2c:
  sda: 14
  scl: 21

bmv080_i2c:
  id: bmv080_sensor
  address: 0x57
  mode: continuous
  update_interval: 5s

# sensor / binary_sensor platforms are identical (platform: bmv080, bmv080_id: ...)
```

## Configuration

The following options live on the `bmv080_i2c` / `bmv080_spi` hub (shared, bus-agnostic):

| Option | Default | Notes |
|---|---|---|
| `mode` | `continuous` | `continuous` or `duty_cycle` |
| `measurement_algorithm` | `high_precision` | `fast_response`, `balanced`, `high_precision` |
| `integration_time` | `10.0` | seconds (1–300) |
| `duty_cycling_period` | `30` | seconds (12–3600), duty-cycle mode |
| `obstruction_detection` | `true` | |
| `vibration_filtering` | `false` | |
| `update_interval` | `1s` | `PollingComponent` |

Plus the bus options: `cs_pin` (SPI, required) or `address` (I²C, default `0x57`).

## Status / upstream

**✅ SPI transport hardware-validated on a BlackIoT Polverine (ESP32-S3) — 2026-06-20,
ESPHome 2026.6.1.** First flash: compiled clean, `bmv080_open` succeeded over SPI, sensor
ID read (`D0ML6909261C`), continuous PM1/PM2.5/PM10 + particle counts + obstructed/out-of-range
publishing to Home Assistant. SDK driver 24.1.0; SPI2 @ 1 MHz, CS GPIO10.

This is an **external component** today. It's already in the bus-agnostic hub + leaf shape
ESPHome core asks for — the one thing blocking a core PR is that the Bosch BMV080 libraries
are behind a registration form rather than published on Bosch's GitHub (the way BSEC2 is,
which is why the in-tree `bme68x_bsec2` component can exist). Once Bosch publishes the libs,
core can fetch them at build and this drops in.

### Tracking

| Item | Where | Why |
|---|---|---|
| **Bosch: publish the libs** | [boschsensortec/BMV_BME#1](https://github.com/boschsensortec/BMV_BME/issues/1) | The unblock for ESPHome core — asks Bosch to publish the prebuilt libs per-arch, as they already do for BSEC2. 👍 there if you want this upstream. |
| **ESPHome feature request** | [esphome/feature-requests#3178](https://github.com/esphome/feature-requests/issues/3178) | Community demand for BMV080 support; where this component is announced. |
| **Related — BME690 / BSEC3** | [esphome/esphome#13480](https://github.com/esphome/esphome/pull/13480) ([our review](https://github.com/esphome/esphome/pull/13480#issuecomment-4762512842)) | The in-progress core BME690 component (different sensor, same closed-Bosch-lib-in-core question). Our review pins the working BSEC version and flags three fixes. |

#### Same gate, across the ecosystem

The BMV080 isn't the only Bosch part stuck behind the download-form library model — the BSEC3 / BME690 libs are too. These are open discussions with the same root cause, and they all point back at the one fix in [BMV_BME#1](https://github.com/boschsensortec/BMV_BME/issues/1):

| Where | What's discussed |
|---|---|
| [Bosch forum — Zephyr drivers for BMV080 + BME690](https://community.bosch-sensortec.com/mems-sensors-forum-jrmujtaw/post/zephyr-drivers-for-bmv080-and-bme690-G3c9CiSME9J3py4) | The SDK ships only Arduino / Linux / Windows builds — no Zephyr — because the libs aren't fetchable at build time. |
| [Bosch forum — BSEC3 3.3.0.0 linking regression](https://community.bosch-sensortec.com/mems-sensors-forum-jrmujtaw/post/bsec3-3-0-0-linking-issue-ISO2FGK1nfUZk9M) | `bsec_get_instance_size()` was present in 3.2.1.0 and silently dropped from the 3.3.0.0 prebuilt lib — the kind of regression versioned GitHub releases would surface. |
| [Bosch-BSEC2-Library #57](https://github.com/boschsensortec/Bosch-BSEC2-Library/issues/57) | The GitHub-published BSEC stopped at v2.6.1.0; v3.2.1.0 is form-only. Asks Bosch to publish the newer libs the same way. |
| [Bosch-BSEC2-Library #61](https://github.com/boschsensortec/Bosch-BSEC2-Library/issues/61) | Request for a first-class, fetch-at-build ESP-IDF component — exactly what ESPHome core consumes. |
| [BlackIoT/Polverine #1](https://github.com/BlackIoT/Polverine/issues/1) | A downstream open-air-quality project ([CanAirIO](https://github.com/kike-canaries/canairio_firmware)) blocked by the closed BMV080 lib. |

## Credits / license

- Component architecture, Bosch SDK wrapping, I²C transport, FreeRTOS task:
  © [@sweitzja](https://github.com/sweitzja/esphome-bmv080) (MIT).
- SPI transport + bus-agnostic hub refactor: this fork.
- SPI protocol reference + validation hardware: the [BlackIoT Polverine](https://github.com/BlackIoT/Polverine)
  and its open reference firmware (`POLVERINE_HOMEASSISTANT_DEMO/src/sensors/bmv080_io.c`) — the SPI
  transport was matched against it, and every flash was validated on a Polverine. Thanks to the BlackIoT team.
- `bosch/*.a`, `bosch/*.h`: Bosch Sensortec, under Bosch's SDK license (not MIT).
  See Bosch Sensortec terms before redistributing.
