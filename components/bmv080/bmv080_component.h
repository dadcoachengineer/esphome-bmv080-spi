/**
 * @file bmv080_component.h
 * @brief ESPHome component header for the Bosch BMV080 particulate matter sensor (SPI).
 *
 * SPI fork of @sweitzja's I2C esphome-bmv080. The Bosch SDK is bus-agnostic
 * (bmv080_open() takes generic 16-bit read/write callbacks), so only the
 * transport (read_cb_/write_cb_), the base class (SPIDevice instead of
 * I2CDevice), and the YAML schema differ from the I2C version. Everything else
 * — the dedicated 64 KB FreeRTOS task, the mutex-protected data hand-off, and
 * the sensor publishing — is identical and unchanged.
 *
 * IMPORTANT: This file is named bmv080_component.h (not bmv080.h) to avoid a
 * header name collision with the Bosch SDK's bmv080.h. The SDK header is found
 * via the -I<bosch_dir> build flag added by __init__.py.
 *
 * SPI protocol (from BlackIoT's reference firmware bmv080_io.c):
 *   - SPI mode 0 (CPOL=0, CPHA=0), MSB-first, 1 MHz, CS active-low.
 *   - Each transaction: clock the 16-bit header out MSB-first (the SDK's
 *     "address phase"), used AS-IS — NO I2C <<1 shift — then read/write the
 *     payload as 16-bit words, MSB-first (big-endian on the wire).
 *
 * Architecture Overview (unchanged from the I2C version):
 *   1. Main Thread (ESPHome loop): setup() launches the task; loop() checks for
 *      failure; update() reads cached data (under mutex) and publishes.
 *   2. BMV080 Task Thread (dedicated, 64 KB stack): waits 10 s, runs the SDK
 *      init sequence, then loops bmv080_serve_interrupt() every 500 ms. The
 *      64 KB stack is required because bmv080_serve_interrupt() uses ~60 KB.
 *
 * Thread Safety:
 *   - last_output_ / data_available_ are protected by data_mutex_.
 *   - sensor_initialized_ / sensor_failed_ are volatile (single-writer pattern).
 *   - ESPHome SPI transactions (enable/write_array/read_array/disable) run
 *     entirely inside the task thread; CS is asserted only for the duration of
 *     a single callback, so they do not interleave with other SPI devices.
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
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

/**
 * @brief Measurement mode for the BMV080 sensor.
 *
 * - CONTINUOUS: Sensor runs constantly, data every ~1 s. Real-time, higher power.
 * - DUTY_CYCLE: Cycles ON (integration_time) / sleep (rest of duty_cycling_period).
 *   Lower power; data once per period; forces FAST_RESPONSE.
 */
enum MeasurementMode {
  MEASUREMENT_MODE_CONTINUOUS = 0,
  MEASUREMENT_MODE_DUTY_CYCLE = 1,
};

/**
 * @brief Measurement algorithm — precision vs. response time.
 * Values map to the Bosch SDK's bmv080_measurement_algorithm_t (1..3).
 */
enum MeasurementAlgorithm {
  ALGORITHM_FAST_RESPONSE = 1,
  ALGORITHM_BALANCED = 2,
  ALGORITHM_HIGH_PRECISION = 3,
};

/**
 * @brief ESPHome component for the Bosch BMV080 over SPI.
 *
 * Inherits PollingComponent (setup/loop/update + update_interval) and
 * SPIDevice (CS handling + enable/disable/write_array/read_array). The
 * SPIDevice template parameters encode the BMV080 SPI protocol: MSB-first,
 * mode 0 (CLOCK_POLARITY_LOW + CLOCK_PHASE_LEADING), 1 MHz. These MUST match
 * the class signature declared in __init__.py.
 */
class BMV080Component
    : public PollingComponent,
      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                            spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
 public:
  // --- ESPHome Component Lifecycle ---
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;

  /** Run after network components so OTA stays reachable during SDK init. */
  float get_setup_priority() const override { return setup_priority::LATE; }

  // --- Configuration Setters (called by generated code from __init__.py) ---
  void set_mode(MeasurementMode mode) { this->mode_ = mode; }
  void set_measurement_algorithm(MeasurementAlgorithm algo) { this->algorithm_ = algo; }
  void set_integration_time(float time) { this->integration_time_ = time; }
  void set_duty_cycling_period(uint16_t period) { this->duty_cycling_period_ = period; }
  void set_obstruction_detection(bool enabled) { this->obstruction_detection_ = enabled; }
  void set_vibration_filtering(bool enabled) { this->vibration_filtering_ = enabled; }

  // --- Sensor Setters (called by sensor.py / binary_sensor.py) ---
  void set_pm_1_0_sensor(sensor::Sensor *sensor) { this->pm_1_0_sensor_ = sensor; }
  void set_pm_2_5_sensor(sensor::Sensor *sensor) { this->pm_2_5_sensor_ = sensor; }
  void set_pm_10_sensor(sensor::Sensor *sensor) { this->pm_10_sensor_ = sensor; }
  void set_pm_1_0_count_sensor(sensor::Sensor *sensor) { this->pm_1_0_count_sensor_ = sensor; }
  void set_pm_2_5_count_sensor(sensor::Sensor *sensor) { this->pm_2_5_count_sensor_ = sensor; }
  void set_pm_10_count_sensor(sensor::Sensor *sensor) { this->pm_10_count_sensor_ = sensor; }
  void set_runtime_sensor(sensor::Sensor *sensor) { this->runtime_sensor_ = sensor; }
  void set_obstructed_binary_sensor(binary_sensor::BinarySensor *sensor) { this->obstructed_sensor_ = sensor; }
  void set_out_of_range_binary_sensor(binary_sensor::BinarySensor *sensor) { this->out_of_range_sensor_ = sensor; }

  /** Stores new SDK data under mutex (called from the task thread). */
  void on_data_ready(bmv080_output_t output);

 protected:
  // --- Bosch SDK Static Callbacks ---
  // C-compatible statics. The SDK passes back the sercom_handle we registered
  // in bmv080_open() (a BMV080Component*), which we cast to reach the ESPHome
  // SPI methods.

  /**
   * @brief SPI read callback for the Bosch SDK.
   *
   * One CS-asserted transaction: clock the 16-bit header out MSB-first
   * (address phase, used as-is — no I2C shift), then read payload_length*2
   * bytes and reassemble into MSB-first uint16_t words.
   */
  static int8_t read_cb_(bmv080_sercom_handle_t handle, uint16_t header,
                         uint16_t *payload, uint16_t payload_length);

  /**
   * @brief SPI write callback for the Bosch SDK.
   *
   * One CS-asserted transaction: clock the 16-bit header out MSB-first, then
   * the payload words MSB-first.
   */
  static int8_t write_cb_(bmv080_sercom_handle_t handle, uint16_t header,
                          const uint16_t *payload, uint16_t payload_length);

  /** Delay callback — vTaskDelay (runs in the FreeRTOS task context). */
  static int8_t delay_cb_(uint32_t duration_ms);

  /** Data-ready callback invoked by bmv080_serve_interrupt(). */
  static void data_ready_cb_(bmv080_output_t output, void *user_data);

  /** Tick callback (ms since boot) — for duty cycling timing. */
  static uint32_t tick_cb_();

  /** FreeRTOS task running all SDK operations (64 KB stack). */
  static void sensor_task_(void *arg);

  // --- Internal Helpers (task thread) ---
  bool init_sensor_();
  bool configure_parameters_();
  bool start_measurement_();
  void service_sensor_();

  // --- Member Variables ---
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
