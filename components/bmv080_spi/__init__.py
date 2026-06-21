"""
ESPHome BMV080 — SPI leaf component.

Wires the bus-agnostic `bmv080` hub onto SPI for boards that connect the BMV080
over SPI (e.g. the BlackIoT Polverine: SPI2, CLK/MOSI/MISO + a CS pin). The SPI
framing matches BlackIoT's reference firmware (bmv080_io.c): the 16-bit header
is the SPI address phase used AS-IS (no I2C <<1 shift), payload is 16-bit words
MSB-first, SPI mode 0, 1 MHz. Those fixed transfer params live in the C++
SPIDevice template in bmv080_spi.h.

YAML:
  spi:
    clk_pin: 12
    mosi_pin: 11
    miso_pin: 13

  bmv080_spi:
    id: bmv080_sensor
    cs_pin: 10
    mode: continuous
    measurement_algorithm: high_precision
    update_interval: 5s
"""

import esphome.codegen as cg
from esphome.components import bmv080, spi
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["bmv080"]
CODEOWNERS = ["@sweitzja", "@dadcoachengineer"]
DEPENDENCIES = ["spi"]
MULTI_CONF = True

bmv080_spi_ns = cg.esphome_ns.namespace("bmv080_spi")
BMV080SPI = bmv080_spi_ns.class_("BMV080SPI", bmv080.BMV080Component, spi.SPIDevice)

CONFIG_SCHEMA = cv.All(
    bmv080.BMV080_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(BMV080SPI),
        }
    ).extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await bmv080.setup_bmv080(var, config)
    await spi.register_spi_device(var, config)
