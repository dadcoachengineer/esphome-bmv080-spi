/**
 * @file bmv080_spi.h
 * @brief SPI leaf for the BMV080. Implements the hub's transport methods over
 *        an spi::SPIDevice (mode 0, MSB-first, 1 MHz — fixed in the template).
 */

#pragma once

#include "esphome/components/bmv080/bmv080_component.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace bmv080_spi {

class BMV080SPI : public bmv080::BMV080Component,
                  public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                        spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
 public:
  void setup() override;
  void dump_config() override;

 protected:
  int8_t transport_read(uint16_t header, uint16_t *payload, uint16_t payload_length) override;
  int8_t transport_write(uint16_t header, const uint16_t *payload, uint16_t payload_length) override;
};

}  // namespace bmv080_spi
}  // namespace esphome
