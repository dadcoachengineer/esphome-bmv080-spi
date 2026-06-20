/**
 * @file bmv080_i2c.cpp
 * @brief I2C transport for the BMV080 — the original transport from @sweitzja's
 *        component, adapted to the bus-agnostic hub's transport interface.
 *
 * I2C protocol:
 *   - The SDK's 16-bit header is shifted (header<<1) for the I2C R/W bit framing.
 *   - Header is written first as its own 2-byte transaction, then the payload is
 *     read back in <=32-byte chunks (ESPHome I2C transfer limit) or written in a
 *     single header+payload transaction.
 *   - Words are MSB-first (big-endian) on the wire.
 *
 * The transport methods are named transport_read/transport_write (not read/write)
 * specifically to avoid colliding with i2c::I2CDevice::read / i2c::I2CDevice::write,
 * which they call.
 */

#include "bmv080_i2c.h"
#include "esphome/core/log.h"

namespace esphome {
namespace bmv080_i2c {

static const char *const TAG = "bmv080_i2c";

void BMV080I2C::dump_config() {
  bmv080::BMV080Component::dump_config();
  LOG_I2C_DEVICE(this);
}

int8_t BMV080I2C::transport_read(uint16_t header, uint16_t *payload, uint16_t payload_length) {
  uint16_t i2c_header = header << 1;

  ESP_LOGVV(TAG, "read: header=0x%04X i2c_header=0x%04X len=%u", header, i2c_header, payload_length);

  uint8_t header_bytes[2];
  header_bytes[0] = (uint8_t) (i2c_header >> 8);
  header_bytes[1] = (uint8_t) (i2c_header & 0xFF);

  auto err = this->write(header_bytes, 2);
  if (err != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "read: failed to write header, i2c error=%d", (int) err);
    return -2;
  }

  size_t byte_count = (size_t) payload_length * 2;
  uint8_t buffer[512];
  if (byte_count > sizeof(buffer)) {
    ESP_LOGE(TAG, "read: payload too large: %u bytes", (unsigned) byte_count);
    return -2;
  }

  // ESPHome's I2C transfer limit is 32 bytes per read — chunk accordingly.
  size_t bytes_read = 0;
  while (bytes_read < byte_count) {
    size_t chunk = byte_count - bytes_read;
    if (chunk > 32)
      chunk = 32;
    err = this->read(buffer + bytes_read, chunk);
    if (err != i2c::ERROR_OK) {
      ESP_LOGE(TAG, "read: failed to read chunk at offset %u, i2c error=%d", (unsigned) bytes_read, (int) err);
      return -2;
    }
    bytes_read += chunk;
  }

  // Bytes -> words, MSB-first
  for (uint16_t i = 0; i < payload_length; i++) {
    payload[i] = ((uint16_t) buffer[i * 2] << 8) | buffer[i * 2 + 1];
  }

  return 0;
}

int8_t BMV080I2C::transport_write(uint16_t header, const uint16_t *payload, uint16_t payload_length) {
  uint16_t i2c_header = header << 1;

  ESP_LOGVV(TAG, "write: header=0x%04X i2c_header=0x%04X len=%u", header, i2c_header, payload_length);

  size_t total_bytes = 2 + (size_t) payload_length * 2;
  uint8_t buffer[512];
  if (total_bytes > sizeof(buffer)) {
    ESP_LOGE(TAG, "write: payload too large: %u bytes", (unsigned) total_bytes);
    return -3;
  }

  buffer[0] = (uint8_t) (i2c_header >> 8);
  buffer[1] = (uint8_t) (i2c_header & 0xFF);
  for (uint16_t i = 0; i < payload_length; i++) {
    buffer[2 + i * 2] = (uint8_t) (payload[i] >> 8);
    buffer[2 + i * 2 + 1] = (uint8_t) (payload[i] & 0xFF);
  }

  auto err = this->write(buffer, total_bytes);
  if (err != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "write: i2c write failed, error=%d, total_bytes=%u", (int) err, (unsigned) total_bytes);
    return -3;
  }

  return 0;
}

}  // namespace bmv080_i2c
}  // namespace esphome
