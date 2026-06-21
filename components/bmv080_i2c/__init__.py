"""
ESPHome BMV080 — I2C leaf component.

Wires the bus-agnostic `bmv080` hub onto I2C — the original transport from
@sweitzja's component. The BMV080's default 7-bit I2C address is 0x57. The SDK's
16-bit header is shifted (header<<1) for the I2C R/W framing, written as a
separate header transaction, and reads are chunked to 32 bytes.

YAML:
  i2c:
    sda: 14
    scl: 21

  bmv080_i2c:
    id: bmv080_sensor
    address: 0x57
    mode: continuous
    measurement_algorithm: high_precision
    update_interval: 5s
"""

import esphome.codegen as cg
from esphome.components import bmv080, i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["bmv080"]
CODEOWNERS = ["@sweitzja", "@dadcoachengineer"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

bmv080_i2c_ns = cg.esphome_ns.namespace("bmv080_i2c")
BMV080I2C = bmv080_i2c_ns.class_("BMV080I2C", bmv080.BMV080Component, i2c.I2CDevice)

CONFIG_SCHEMA = cv.All(
    bmv080.BMV080_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(BMV080I2C),
        }
    ).extend(i2c.i2c_device_schema(0x57))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await bmv080.setup_bmv080(var, config)
    await i2c.register_i2c_device(var, config)
