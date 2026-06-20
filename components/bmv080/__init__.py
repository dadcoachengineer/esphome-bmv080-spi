"""
ESPHome BMV080 Particulate Matter Sensor — Hub Component (SPI)

SPI fork of @sweitzja's I2C esphome-bmv080 component, for boards that wire the
Bosch BMV080 over SPI (e.g. the BlackIoT Polverine: SPI2, CLK/MOSI/MISO + a CS
pin). The Bosch SDK is bus-agnostic — bmv080_open() takes generic 16-bit
read/write callbacks — so only the transport (read_cb_/write_cb_), the base
class, and this schema differ from the I2C version. The SPI framing matches
BlackIoT's reference firmware (bmv080_io.c): the 16-bit header is the SPI
address phase used AS-IS (no I2C <<1 shift), payload is 16-bit words MSB-first,
SPI mode 0, 1 MHz.

The BMV080 uses the Bosch precompiled SDK (lib_bmv080.a + lib_postProcessor.a)
bundled per-architecture in the bosch/ subdirectory (Xtensa for ESP32/S3,
RISC-V for C3/C6); the linker selects the matching .a and skips the rest.

Hub/Platform Pattern:
  - this file (__init__.py)  -> hub component (BMV080Component, SPI)
  - sensor.py                -> sensor platform (PM mass, number, runtime)  [unchanged]
  - binary_sensor.py         -> binary sensors (obstructed, out_of_range)   [unchanged]
  Sensors reference the hub via bmv080_id.

YAML Example (Polverine):
  spi:
    clk_pin: 12
    mosi_pin: 11
    miso_pin: 13

  bmv080:
    id: bmv080_sensor
    cs_pin: 10
    mode: continuous
    measurement_algorithm: high_precision
    update_interval: 5s
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi
from esphome.const import CONF_ID
import os
import logging

_LOGGER = logging.getLogger(__name__)

# Component metadata
CODEOWNERS = ["@sweitzja"]  # original I2C component; SPI fork pending upstream credit
DEPENDENCIES = ["spi"]  # this fork communicates over SPI
AUTO_LOAD = ["sensor", "binary_sensor"]  # auto-load sensor platforms
MULTI_CONF = True  # allow multiple BMV080 instances (different cs_pin)

# Configuration keys
CONF_BMV080_ID = "bmv080_id"
CONF_MODE = "mode"
CONF_MEASUREMENT_ALGORITHM = "measurement_algorithm"
CONF_INTEGRATION_TIME = "integration_time"
CONF_DUTY_CYCLING_PERIOD = "duty_cycling_period"
CONF_OBSTRUCTION_DETECTION = "obstruction_detection"
CONF_VIBRATION_FILTERING = "vibration_filtering"

# C++ namespace + class. The SPIDevice template params (MSB-first, mode 0, 1 MHz)
# are fixed in bmv080_component.h and must match this class signature.
bmv080_ns = cg.esphome_ns.namespace("bmv080")
BMV080Component = bmv080_ns.class_(
    "BMV080Component", cg.PollingComponent, spi.SPIDevice
)

MeasurementMode = bmv080_ns.enum("MeasurementMode")
MODE_OPTIONS = {
    "continuous": MeasurementMode.MEASUREMENT_MODE_CONTINUOUS,
    "duty_cycle": MeasurementMode.MEASUREMENT_MODE_DUTY_CYCLE,
}

MeasurementAlgorithm = bmv080_ns.enum("MeasurementAlgorithm")
ALGORITHM_OPTIONS = {
    "fast_response": MeasurementAlgorithm.ALGORITHM_FAST_RESPONSE,
    "balanced": MeasurementAlgorithm.ALGORITHM_BALANCED,
    "high_precision": MeasurementAlgorithm.ALGORITHM_HIGH_PRECISION,
}

# Extends PollingComponent (update_interval) and SPIDevice (cs_pin, spi_id).
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BMV080Component),
            cv.Optional(CONF_MODE, default="continuous"): cv.enum(
                MODE_OPTIONS, lower=True
            ),
            cv.Optional(
                CONF_MEASUREMENT_ALGORITHM, default="high_precision"
            ): cv.enum(ALGORITHM_OPTIONS, lower=True),
            cv.Optional(CONF_INTEGRATION_TIME, default=10.0): cv.float_range(
                min=1.0, max=300.0
            ),
            cv.Optional(CONF_DUTY_CYCLING_PERIOD, default=30): cv.int_range(
                min=12, max=3600
            ),
            cv.Optional(CONF_OBSTRUCTION_DETECTION, default=True): cv.boolean,
            cv.Optional(CONF_VIBRATION_FILTERING, default=False): cv.boolean,
        }
    )
    .extend(cv.polling_component_schema("1s"))
    # SPI device: requires a cs_pin; clk/mosi/miso come from the top-level `spi:`.
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    """Generate C++ code from the YAML configuration."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # Register as an SPI device (binds cs_pin + the shared SPI bus).
    await spi.register_spi_device(var, config)

    cg.add(var.set_mode(config[CONF_MODE]))
    cg.add(var.set_measurement_algorithm(config[CONF_MEASUREMENT_ALGORITHM]))
    cg.add(var.set_integration_time(config[CONF_INTEGRATION_TIME]))
    cg.add(var.set_duty_cycling_period(config[CONF_DUTY_CYCLING_PERIOD]))
    cg.add(var.set_obstruction_detection(config[CONF_OBSTRUCTION_DETECTION]))
    cg.add(var.set_vibration_filtering(config[CONF_VIBRATION_FILTERING]))

    # --- Bosch SDK Linking ---
    # Headers (bmv080.h, bmv080_defs.h) + precompiled static libs
    # (lib_bmv080.a, lib_postProcessor.a) live in bosch/<arch>/.
    component_dir = os.path.dirname(os.path.abspath(__file__))
    bosch_dir = os.path.join(component_dir, "bosch")
    cg.add_build_flag(f"-I{bosch_dir}")

    arch_dirs = ["esp32", "esp32s2", "esp32s3", "esp32c3", "esp32c6"]
    found_any = False
    for arch in arch_dirs:
        lib_path = os.path.join(bosch_dir, arch)
        if os.path.isdir(lib_path):
            cg.add_build_flag(f"-L{lib_path}")
            _LOGGER.info("BMV080: Found library path for %s: %s", arch, lib_path)
            found_any = True
    if not found_any:
        _LOGGER.error(
            "BMV080: No prebuilt library directories found in %s "
            "(expected esp32/, esp32s3/, esp32c3/ ...)",
            bosch_dir,
        )

    # lib_bmv080.a -> -l_bmv080 ; lib_postProcessor.a -> -l_postProcessor
    cg.add_build_flag("-l_bmv080")
    cg.add_build_flag("-l_postProcessor")
