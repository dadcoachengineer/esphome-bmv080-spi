/**
 * @file bmv080_i2c.h
 * @brief I2C leaf for the BMV080. Implements the hub's transport methods over
 *        an i2c::I2CDevice. The original transport from @sweitzja's component.
 */

#pragma once

#include "esphome/components/bmv080/bmv080_component.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace bmv080_i2c {

class BMV080I2C : public bmv080::BMV080Component, public i2c::I2CDevice {
 public:
  void dump_config() override;

 protected:
  int8_t transport_read(uint16_t header, uint16_t *payload, uint16_t payload_length) override;
  int8_t transport_write(uint16_t header, const uint16_t *payload, uint16_t payload_length) override;
};

}  // namespace bmv080_i2c
}  // namespace esphome
