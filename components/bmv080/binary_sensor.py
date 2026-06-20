"""
ESPHome BMV080 Particulate Matter Sensor — Binary Sensor Platform

Defines binary sensor entities for the BMV080's diagnostic flags. Both sensors
are optional and use the "problem" device class for proper display in Home Assistant
(shown as an alert when true).

Available Binary Sensors:
  - obstructed:    Optical path is blocked (something covering the sensor aperture)
  - out_of_range:  PM2.5 concentration exceeds 1000 ug/m3 (measurement unreliable)

YAML Example:
  binary_sensor:
    - platform: bmv080
      bmv080_id: bmv080_sensor
      obstructed:
        name: "Sensor Obstructed"
      out_of_range:
        name: "Out of Range"
"""

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

from . import CONF_BMV080_ID, BMV080Component, bmv080_ns

DEPENDENCIES = ["bmv080"]

# Configuration keys matching the C++ setter names
CONF_OBSTRUCTED = "obstructed"
CONF_OUT_OF_RANGE = "out_of_range"

# YAML configuration schema for binary sensor entities.
# Both sensors are optional — only configured sensors will be created.
CONFIG_SCHEMA = cv.Schema(
    {
        # Reference to the parent BMV080 hub component
        cv.GenerateID(CONF_BMV080_ID): cv.use_id(BMV080Component),

        # Obstruction detection — indicates the sensor's optical path is blocked.
        # This can be caused by dust buildup, physical objects, or condensation.
        # The BMV080 has built-in obstruction detection (enabled by default via
        # the "do_obstruction_detection" parameter in the hub config).
        cv.Optional(CONF_OBSTRUCTED): binary_sensor.binary_sensor_schema(
            device_class="problem",
        ),

        # Out-of-range detection — indicates PM2.5 concentration exceeds the
        # specified measurement range of 0 to 1000 ug/m3. When true, the PM
        # mass readings may not be accurate.
        cv.Optional(CONF_OUT_OF_RANGE): binary_sensor.binary_sensor_schema(
            device_class="problem",
        ),
    }
)


async def to_code(config):
    """Generate C++ code to create binary sensor instances and register them with the hub.

    For each binary sensor key present in the YAML config, creates an ESPHome
    BinarySensor object and passes it to the corresponding C++ setter method.
    """
    # Get reference to the parent BMV080 hub component
    hub = await cg.get_variable(config[CONF_BMV080_ID])

    if conf := config.get(CONF_OBSTRUCTED):
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(hub.set_obstructed_binary_sensor(sens))

    if conf := config.get(CONF_OUT_OF_RANGE):
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(hub.set_out_of_range_binary_sensor(sens))
