"""
ESPHome BMV080 Particulate Matter Sensor — bus-agnostic hub.

Unified I2C + SPI design, mirroring ESPHome's pn532 / pn532_i2c / pn532_spi:

  - this package (bmv080)  -> bus-agnostic hub class + shared schema + setup
                              helper + the sensor / binary_sensor platforms.
                              NOT usable as a top-level `bmv080:` component.
  - bmv080_i2c             -> I2C leaf (BMV080I2C : BMV080Component, I2CDevice)
  - bmv080_spi             -> SPI leaf (BMV080SPI : BMV080Component, SPIDevice)

The Bosch SDK is bus-agnostic — bmv080_open() takes generic 16-bit read/write
callbacks — so the hub owns ALL the device logic (SDK orchestration, the
dedicated 64 KB FreeRTOS task, the mutex hand-off, publishing). Each leaf only
implements the two transport methods (the bus framing) and registers itself on
its bus. Sensors reference the leaf instance via `bmv080_id`.

The BMV080 ships only as Bosch precompiled static libraries (lib_bmv080.a +
lib_postProcessor.a), bundled per-architecture in the bmv080_sdk/ subdirectory
(Xtensa for ESP32/S2/S3, RISC-V for C3/C6); the linker selects the matching .a
and skips the rest. setup_bmv080() adds the -I/-L/-l build flags.
"""

import logging
import os

import esphome.codegen as cg
from esphome.components import esp32
import esphome.config_validation as cv

_LOGGER = logging.getLogger(__name__)

CODEOWNERS = ["@sweitzja", "@dadcoachengineer"]
AUTO_LOAD = ["sensor", "binary_sensor"]

# Configuration keys
CONF_BMV080_ID = "bmv080_id"
CONF_MODE = "mode"
CONF_MEASUREMENT_ALGORITHM = "measurement_algorithm"
CONF_INTEGRATION_TIME = "integration_time"
CONF_DUTY_CYCLING_PERIOD = "duty_cycling_period"
CONF_OBSTRUCTION_DETECTION = "obstruction_detection"
CONF_VIBRATION_FILTERING = "vibration_filtering"

bmv080_ns = cg.esphome_ns.namespace("bmv080")
# Abstract base — never instantiated directly; the i2c/spi leaves derive from it.
BMV080Component = bmv080_ns.class_("BMV080Component", cg.PollingComponent)

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

# Shared, bus-agnostic schema. The leaf components extend this with their own
# `cv.GenerateID(): cv.declare_id(<leaf>)` and bus device schema.
BMV080_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MODE, default="continuous"): cv.enum(MODE_OPTIONS, lower=True),
        cv.Optional(CONF_MEASUREMENT_ALGORITHM, default="high_precision"): cv.enum(
            ALGORITHM_OPTIONS, lower=True
        ),
        cv.Optional(CONF_INTEGRATION_TIME, default=10.0): cv.float_range(
            min=1.0, max=300.0
        ),
        cv.Optional(CONF_DUTY_CYCLING_PERIOD, default=30): cv.int_range(
            min=12, max=3600
        ),
        cv.Optional(CONF_OBSTRUCTION_DETECTION, default=True): cv.boolean,
        cv.Optional(CONF_VIBRATION_FILTERING, default=False): cv.boolean,
    }
).extend(cv.polling_component_schema("1s"))


def _add_bosch_sdk():
    """Register the bundled Bosch precompiled SDK as an ESP-IDF component.

    Headers (bmv080.h, bmv080_defs.h) + the per-arch static libs (lib_bmv080.a,
    lib_postProcessor.a) live in this package's bmv080_sdk/ subdirectory,
    which carries its own CMakeLists.txt.

    DO NOT go back to cg.add_build_flag("-I.../-L.../-l...") here. ESPHome
    2026.x builds ESP-IDF natively with CMake and emits no platformio.ini;
    CORE.build_flags is read only by esphome/build_gen/platformio.py and is
    ignored by build_gen/espidf.py. Those flags were therefore silently
    dropped, and the build failed with:

        bmv080_component.h:27:10: fatal error: bmv080_defs.h:
        No such file or directory

    ...while the header sat present on disk and the "found library path for
    esp32s3" INFO lines still printed, because they only report that Python
    stat()'d the directory -- not that any flag took effect.

    The same applies to the `esphome: build_flags:` YAML option: it funnels
    into the identical CORE.add_build_flag() sink and is equally inert here.
    """
    sdk_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bmv080_sdk")
    if not os.path.isfile(os.path.join(sdk_dir, "bmv080_defs.h")):
        raise cv.Invalid(
            f"BMV080: bundled Bosch SDK headers not found in {sdk_dir}. "
            "The external component copy looks incomplete."
        )
    # The IDF component manager derives the component name from the LAST
    # PATH SEGMENT, so the directory name and `name=` must agree -- with a
    # mismatch the build fails at "Failed to resolve component ... unknown
    # name". The directory is `bmv080_sdk` rather than `bosch` because IDF
    # component names are global and this config also builds BSEC.
    esp32.add_idf_component(name="bmv080_sdk", path=sdk_dir)
    _LOGGER.info("BMV080: registered Bosch SDK as IDF component from %s", sdk_dir)


async def setup_bmv080(var, config):
    """Register the hub + apply the bus-agnostic config + link the Bosch SDK.

    Called by each leaf's to_code() after cg.new_Pvariable() and before the
    bus registration (register_i2c_device / register_spi_device).
    """
    await cg.register_component(var, config)

    cg.add(var.set_measurement_mode(config[CONF_MODE]))
    cg.add(var.set_measurement_algorithm(config[CONF_MEASUREMENT_ALGORITHM]))
    cg.add(var.set_integration_time(config[CONF_INTEGRATION_TIME]))
    cg.add(var.set_duty_cycling_period(config[CONF_DUTY_CYCLING_PERIOD]))
    cg.add(var.set_obstruction_detection(config[CONF_OBSTRUCTION_DETECTION]))
    cg.add(var.set_vibration_filtering(config[CONF_VIBRATION_FILTERING]))

    _add_bosch_sdk()
