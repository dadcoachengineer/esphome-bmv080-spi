/**
 * @file bmv080_spi.cpp
 * @brief SPI transport for the BMV080.
 *
 * SPI protocol (from BlackIoT's reference firmware bmv080_io.c):
 *   - SPI mode 0, MSB-first, 1 MHz, CS active-low (set via the SPIDevice template).
 *   - Read:  one CS-asserted transaction = clock the 16-bit header out MSB-first
 *            (address phase, used AS-IS — no I2C <<1 shift), then read N*2 bytes.
 *   - Write: one CS-asserted transaction = clock the 16-bit header out MSB-first,
 *            then N*2 payload bytes MSB-first.
 *   - Words are MSB-first (big-endian) on the wire.
 */

#include "bmv080_spi.h"
#include "esphome/core/log.h"

namespace esphome {
namespace bmv080_spi {

static const char *const TAG = "bmv080_spi";

void BMV080SPI::setup() {
  // Init the SPI peripheral/CS for this device, then chain to the bus-agnostic
  // base, which launches the dedicated SDK task.
  this->spi_setup();
  bmv080::BMV080Component::setup();
}

void BMV080SPI::dump_config() {
  bmv080::BMV080Component::dump_config();
  LOG_PIN("  CS Pin: ", this->cs_);
}

int8_t BMV080SPI::transport_read(uint16_t header, uint16_t *payload, uint16_t payload_length) {
  // Header bytes (MSB-first). SPI uses the header as-is — no I2C shift.
  uint8_t header_bytes[2];
  header_bytes[0] = (uint8_t) (header >> 8);
  header_bytes[1] = (uint8_t) (header & 0xFF);

  size_t byte_count = (size_t) payload_length * 2;
  uint8_t buffer[512];  // max 256 words
  if (byte_count > sizeof(buffer)) {
    ESP_LOGE(TAG, "read: payload too large: %u bytes", (unsigned) byte_count);
    return -2;
  }

  ESP_LOGVV(TAG, "read: header=0x%04X len=%u", header, payload_length);

  // One transaction, CS held low across the address + data phases.
  this->enable();
  this->write_array(header_bytes, 2);    // address phase
  this->read_array(buffer, byte_count);  // data phase (MOSI idle)
  this->disable();

  // Bytes -> words, MSB-first
  for (uint16_t i = 0; i < payload_length; i++) {
    payload[i] = ((uint16_t) buffer[i * 2] << 8) | buffer[i * 2 + 1];
  }

  return 0;
}

int8_t BMV080SPI::transport_write(uint16_t header, const uint16_t *payload, uint16_t payload_length) {
  size_t total_bytes = 2 + (size_t) payload_length * 2;
  uint8_t buffer[512];
  if (total_bytes > sizeof(buffer)) {
    ESP_LOGE(TAG, "write: payload too large: %u bytes", (unsigned) total_bytes);
    return -3;
  }

  ESP_LOGVV(TAG, "write: header=0x%04X len=%u", header, payload_length);

  // Header (MSB-first), as-is for SPI
  buffer[0] = (uint8_t) (header >> 8);
  buffer[1] = (uint8_t) (header & 0xFF);
  // Payload words -> bytes (MSB-first)
  for (uint16_t i = 0; i < payload_length; i++) {
    buffer[2 + i * 2] = (uint8_t) (payload[i] >> 8);
    buffer[2 + i * 2 + 1] = (uint8_t) (payload[i] & 0xFF);
  }

  this->enable();
  this->write_array(buffer, total_bytes);
  this->disable();

  return 0;
}

}  // namespace bmv080_spi
}  // namespace esphome
