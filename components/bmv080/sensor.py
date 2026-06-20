"""
ESPHome BMV080 Particulate Matter Sensor — Sensor Platform

Defines the sensor entities exposed by the BMV080 component. All sensors are optional —
only sensors explicitly listed in the YAML config will be created and published to
Home Assistant.

Available Sensors:
  - pm_1_0:       PM1.0 mass concentration (ug/m3, device_class: pm1)
  - pm_2_5:       PM2.5 mass concentration (ug/m3, device_class: pm25)
  - pm_10:        PM10 mass concentration  (ug/m3, device_class: pm10)
  - pm_1_0_count: PM1.0 number concentration (#/cm3)
  - pm_2_5_count: PM2.5 number concentration (#/cm3)
  - pm_10_count:  PM10 number concentration  (#/cm3)
  - runtime:      Time since measurement start (seconds, diagnostic entity)

YAML Example:
  sensor:
    - platform: bmv080
      bmv080_id: bmv080_sensor
      pm_2_5:
        name: "PM 2.5"
      pm_10:
        name: "PM 10"
"""

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_PM1,
    DEVICE_CLASS_PM25,
    DEVICE_CLASS_PM10,
    ICON_CHEMICAL_WEAPON,
    STATE_CLASS_MEASUREMENT,
    UNIT_MICROGRAMS_PER_CUBIC_METER,
)

from . import CONF_BMV080_ID, BMV080Component, bmv080_ns

DEPENDENCIES = ["bmv080"]

# Configuration keys matching the C++ setter names
CONF_PM_1_0 = "pm_1_0"
CONF_PM_2_5 = "pm_2_5"
CONF_PM_10 = "pm_10"
CONF_PM_1_0_COUNT = "pm_1_0_count"
CONF_PM_2_5_COUNT = "pm_2_5_count"
CONF_PM_10_COUNT = "pm_10_count"
CONF_RUNTIME = "runtime"

# Custom units not defined in esphome.const
UNIT_PARTICLES_PER_CM3 = "#/cm\u00b3"  # Unicode superscript 3
UNIT_SECONDS = "s"

# YAML configuration schema for sensor entities.
# All sensors are optional — only configured sensors will be created.
CONFIG_SCHEMA = cv.Schema(
    {
        # Reference to the parent BMV080 hub component
        cv.GenerateID(CONF_BMV080_ID): cv.use_id(BMV080Component),

        # Mass concentration sensors (ug/m3)
        # These have Home Assistant device classes for proper display and graphing
        cv.Optional(CONF_PM_1_0): sensor.sensor_schema(
            unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
            icon=ICON_CHEMICAL_WEAPON,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_PM1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PM_2_5): sensor.sensor_schema(
            unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
            icon=ICON_CHEMICAL_WEAPON,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_PM25,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PM_10): sensor.sensor_schema(
            unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
            icon=ICON_CHEMICAL_WEAPON,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_PM10,
            state_class=STATE_CLASS_MEASUREMENT,
        ),

        # Number concentration sensors (#/cm3)
        # No HA device class for particle count — uses generic measurement
        cv.Optional(CONF_PM_1_0_COUNT): sensor.sensor_schema(
            unit_of_measurement=UNIT_PARTICLES_PER_CM3,
            icon=ICON_CHEMICAL_WEAPON,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PM_2_5_COUNT): sensor.sensor_schema(
            unit_of_measurement=UNIT_PARTICLES_PER_CM3,
            icon=ICON_CHEMICAL_WEAPON,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PM_10_COUNT): sensor.sensor_schema(
            unit_of_measurement=UNIT_PARTICLES_PER_CM3,
            icon=ICON_CHEMICAL_WEAPON,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),

        # Runtime sensor — diagnostic entity showing measurement uptime
        # Useful for verifying the sensor is running and tracking measurement duration
        cv.Optional(CONF_RUNTIME): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECONDS,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category="diagnostic",
        ),
    }
)


async def to_code(config):
    """Generate C++ code to create sensor instances and register them with the hub.

    For each sensor key present in the YAML config, creates an ESPHome Sensor object
    and passes it to the corresponding C++ setter method on BMV080Component.
    """
    # Get reference to the parent BMV080 hub component
    hub = await cg.get_variable(config[CONF_BMV080_ID])

    # Map of YAML config keys to C++ setter method names
    for key, setter in [
        (CONF_PM_1_0, "set_pm_1_0_sensor"),
        (CONF_PM_2_5, "set_pm_2_5_sensor"),
        (CONF_PM_10, "set_pm_10_sensor"),
        (CONF_PM_1_0_COUNT, "set_pm_1_0_count_sensor"),
        (CONF_PM_2_5_COUNT, "set_pm_2_5_count_sensor"),
        (CONF_PM_10_COUNT, "set_pm_10_count_sensor"),
        (CONF_RUNTIME, "set_runtime_sensor"),
    ]:
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(hub, setter)(sens))
