/**
 * @file bmv080_component.h
 * @brief Bus-agnostic base for the Bosch BMV080 particulate matter sensor.
 *
 * Unified I2C + SPI design, mirroring ESPHome's pn532 / pn532_i2c / pn532_spi
 * pattern:
 *   - This base (`bmv080`) owns ALL the device logic: the Bosch SDK
 *     orchestration, the dedicated 64 KB FreeRTOS task, the mutex hand-off, and
 *     the sensor publishing. It is bus-agnostic.
 *   - The actual bus I/O is two pure-virtual methods (`transport_read` /
 *     `transport_write`) that the leaf components `bmv080_i2c` (i2c::I2CDevice)
 *     and `bmv080_spi` (spi::SPIDevice) override. The Bosch SDK's static
 *     read/write callbacks cast their sercom handle back to a BMV080Component*
 *     and call the virtual, so the SDK never knows which bus it's on.
 *
 * (Named bmv080_component.h, NOT bmv080.h, to avoid colliding with the Bosch
 * SDK's own bmv080.h, which ESPHome resolves via the -I<bosch_dir> build flag.)
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

// Bosch SDK headers — resolved via -I<bosch_dir> build flag
#include "bmv080_defs.h"
#include "bmv080.h"

// FreeRTOS headers for dedicated task and mutex
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace esphome {
namespace bmv080 {

enum MeasurementMode {
  MEASUREMENT_MODE_CONTINUOUS = 0,
  MEASUREMENT_MODE_DUTY_CYCLE = 1,
};

enum MeasurementAlgorithm {
  ALGORITHM_FAST_RESPONSE = 1,
  ALGORITHM_BALANCED = 2,
  ALGORITHM_HIGH_PRECISION = 3,
};

/**
 * @brief Bus-agnostic BMV080 hub. Abstract — the i2c/spi leaf components inherit
 *        this + a bus mixin (i2c::I2CDevice / spi::SPIDevice) and implement the
 *        two transport methods. Never instantiated directly.
 */
class BMV080Component : public PollingComponent {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Configuration setters
  // NB: set_measurement_mode (not set_mode) — the leaf components multiply-inherit
  // spi::SPIDevice, whose set_mode(SPIMode) would otherwise collide at name lookup.
  void set_measurement_mode(MeasurementMode mode) { this->mode_ = mode; }
  void set_measurement_algorithm(MeasurementAlgorithm algo) { this->algorithm_ = algo; }
  void set_integration_time(float time) { this->integration_time_ = time; }
  void set_duty_cycling_period(uint16_t period) { this->duty_cycling_period_ = period; }
  void set_obstruction_detection(bool enabled) { this->obstruction_detection_ = enabled; }
  void set_vibration_filtering(bool enabled) { this->vibration_filtering_ = enabled; }

  // Sensor setters
  void set_pm_1_0_sensor(sensor::Sensor *sensor) { this->pm_1_0_sensor_ = sensor; }
  void set_pm_2_5_sensor(sensor::Sensor *sensor) { this->pm_2_5_sensor_ = sensor; }
  void set_pm_10_sensor(sensor::Sensor *sensor) { this->pm_10_sensor_ = sensor; }
  void set_pm_1_0_count_sensor(sensor::Sensor *sensor) { this->pm_1_0_count_sensor_ = sensor; }
  void set_pm_2_5_count_sensor(sensor::Sensor *sensor) { this->pm_2_5_count_sensor_ = sensor; }
  void set_pm_10_count_sensor(sensor::Sensor *sensor) { this->pm_10_count_sensor_ = sensor; }
  void set_runtime_sensor(sensor::Sensor *sensor) { this->runtime_sensor_ = sensor; }
  void set_obstructed_binary_sensor(binary_sensor::BinarySensor *sensor) { this->obstructed_sensor_ = sensor; }
  void set_out_of_range_binary_sensor(binary_sensor::BinarySensor *sensor) { this->out_of_range_sensor_ = sensor; }

  void on_data_ready(bmv080_output_t output);

 protected:
  /**
   * @brief Bus I/O for the SDK's 16-bit word protocol — implemented by the leaf.
   *
   * Each leaf encapsulates its bus framing: I2C shifts the header (header<<1),
   * writes it separately, and reads in 32-byte chunks; SPI clocks the header out
   * as the address phase, used as-is. Named transport_read/transport_write to
   * avoid colliding with i2c::I2CDevice::read/write in the I2C leaf.
   */
  virtual int8_t transport_read(uint16_t header, uint16_t *payload, uint16_t payload_length) = 0;
  virtual int8_t transport_write(uint16_t header, const uint16_t *payload, uint16_t payload_length) = 0;

  // Bosch SDK static callbacks — cast the sercom handle to a BMV080Component*
  // and dispatch to the leaf's transport via the virtuals above.
  static int8_t read_cb_(bmv080_sercom_handle_t handle, uint16_t header,
                         uint16_t *payload, uint16_t payload_length);
  static int8_t write_cb_(bmv080_sercom_handle_t handle, uint16_t header,
                          const uint16_t *payload, uint16_t payload_length);
  static int8_t delay_cb_(uint32_t duration_ms);
  static void data_ready_cb_(bmv080_output_t output, void *user_data);
  static uint32_t tick_cb_();

  static void sensor_task_(void *arg);

  bool init_sensor_();
  bool configure_parameters_();
  bool start_measurement_();
  void service_sensor_();

  bmv080_handle_t handle_{nullptr};

  MeasurementMode mode_{MEASUREMENT_MODE_CONTINUOUS};
  MeasurementAlgorithm algorithm_{ALGORITHM_HIGH_PRECISION};
  float integration_time_{10.0f};
  uint16_t duty_cycling_period_{30};
  bool obstruction_detection_{true};
  bool vibration_filtering_{false};

  bmv080_output_t last_output_{};
  volatile bool data_available_{false};
  volatile bool sensor_initialized_{false};
  volatile bool sensor_failed_{false};
  SemaphoreHandle_t data_mutex_{nullptr};
  TaskHandle_t task_handle_{nullptr};

  sensor::Sensor *pm_1_0_sensor_{nullptr};
  sensor::Sensor *pm_2_5_sensor_{nullptr};
  sensor::Sensor *pm_10_sensor_{nullptr};
  sensor::Sensor *pm_1_0_count_sensor_{nullptr};
  sensor::Sensor *pm_2_5_count_sensor_{nullptr};
  sensor::Sensor *pm_10_count_sensor_{nullptr};
  sensor::Sensor *runtime_sensor_{nullptr};
  binary_sensor::BinarySensor *obstructed_sensor_{nullptr};
  binary_sensor::BinarySensor *out_of_range_sensor_{nullptr};
};

}  // namespace bmv080
}  // namespace esphome
