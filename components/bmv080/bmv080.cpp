/**
 * @file bmv080.cpp
 * @brief Bus-agnostic BMV080 base implementation.
 *
 * All Bosch-SDK orchestration, the dedicated 64 KB FreeRTOS task, the mutex
 * hand-off, and the publishing logic. Bus I/O is delegated to the leaf's
 * transport_read/transport_write overrides, so nothing here touches a bus directly.
 */

#include "bmv080_component.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"

namespace esphome {
namespace bmv080 {

static const char *const TAG = "bmv080";

// =============================================================================
// Bosch SDK static callbacks — delegate to the active transport
// =============================================================================
// During bmv080_open() we register the BMV080Component* as the sercom handle.
// The SDK passes it back here; we cast it back and dispatch the 16-bit-word
// read/write to the leaf's transport override, which encapsulates the bus framing.

int8_t BMV080Component::read_cb_(bmv080_sercom_handle_t handle, uint16_t header,
                                 uint16_t *payload, uint16_t payload_length) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "read_cb: null handle");
    return -1;
  }
  auto *comp = (BMV080Component *) handle;
  return comp->transport_read(header, payload, payload_length);
}

int8_t BMV080Component::write_cb_(bmv080_sercom_handle_t handle, uint16_t header,
                                  const uint16_t *payload, uint16_t payload_length) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "write_cb: null handle");
    return -1;
  }
  auto *comp = (BMV080Component *) handle;
  return comp->transport_write(header, payload, payload_length);
}

int8_t BMV080Component::delay_cb_(uint32_t duration_ms) {
  ESP_LOGVV(TAG, "delay_cb: %u ms", duration_ms);
  vTaskDelay(pdMS_TO_TICKS(duration_ms));
  return 0;
}

void BMV080Component::data_ready_cb_(bmv080_output_t output, void *user_data) {
  auto *comp = (BMV080Component *) user_data;
  ESP_LOGD(TAG, "Data ready: PM1=%.1f PM2.5=%.1f PM10=%.1f ug/m3, runtime=%.1fs, obstructed=%s, oor=%s",
           output.pm1_mass_concentration, output.pm2_5_mass_concentration, output.pm10_mass_concentration,
           output.runtime_in_sec, output.is_obstructed ? "YES" : "no",
           output.is_outside_measurement_range ? "YES" : "no");
  comp->on_data_ready(output);
}

uint32_t BMV080Component::tick_cb_() { return millis(); }

// =============================================================================
// Data transfer (task thread -> main loop thread)
// =============================================================================

void BMV080Component::on_data_ready(bmv080_output_t output) {
  if (this->data_mutex_ != nullptr) {
    xSemaphoreTake(this->data_mutex_, portMAX_DELAY);
  }
  this->last_output_ = output;
  this->data_available_ = true;
  if (this->data_mutex_ != nullptr) {
    xSemaphoreGive(this->data_mutex_);
  }
}

// =============================================================================
// Lifecycle
// =============================================================================

void BMV080Component::setup() {
  // The SPI leaf's setup() calls this->spi_setup() before chaining here; the I2C
  // leaf needs no per-device bus init and inherits this setup() directly. This
  // base setup() launches the dedicated SDK task.
  ESP_LOGCONFIG(TAG, "Setting up BMV080...");

  this->data_mutex_ = xSemaphoreCreateMutex();
  if (this->data_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create data mutex");
    this->mark_failed();
    return;
  }

  // Dedicated 64 KB-stack task — bmv080_serve_interrupt() uses ~60 KB.
  BaseType_t ret = xTaskCreatePinnedToCore(
      sensor_task_, "bmv080", 64 * 1024, this, 1, &this->task_handle_, 0);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create BMV080 task (not enough memory for 64KB stack?)");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "BMV080 task created with 64KB stack, will initialize in ~10 seconds");
}

void BMV080Component::sensor_task_(void *arg) {
  auto *comp = (BMV080Component *) arg;

  ESP_LOGI(TAG, "BMV080 task started, waiting 10 seconds for system startup...");
  vTaskDelay(pdMS_TO_TICKS(10000));

  ESP_LOGI(TAG, "Starting BMV080 initialization...");
  if (!comp->init_sensor_()) {
    ESP_LOGE(TAG, "Failed to initialize BMV080 sensor");
    comp->sensor_failed_ = true;
    vTaskDelete(nullptr);
    return;
  }
  if (!comp->configure_parameters_()) {
    ESP_LOGE(TAG, "Failed to configure BMV080 parameters");
    comp->sensor_failed_ = true;
    vTaskDelete(nullptr);
    return;
  }
  if (!comp->start_measurement_()) {
    ESP_LOGE(TAG, "Failed to start BMV080 measurement");
    comp->sensor_failed_ = true;
    vTaskDelete(nullptr);
    return;
  }

  comp->sensor_initialized_ = true;
  ESP_LOGI(TAG, "BMV080 initialized and measurement started successfully");

  while (true) {
    comp->service_sensor_();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

bool BMV080Component::init_sensor_() {
  uint16_t major, minor, patch;
  char git_hash[12];
  int32_t commits_ahead;

  ESP_LOGD(TAG, "Getting BMV080 driver version...");
  bmv080_status_code_t status = bmv080_get_driver_version(&major, &minor, &patch, git_hash, &commits_ahead);
  if (status != E_BMV080_OK) {
    ESP_LOGE(TAG, "Failed to get driver version, status=%d", status);
    return false;
  }
  ESP_LOGI(TAG, "BMV080 SDK driver version: %d.%d.%d (git: %.11s, +%d)", major, minor, patch, git_hash, commits_ahead);

  ESP_LOGD(TAG, "Opening BMV080 sensor handle...");
  status = bmv080_open(&this->handle_, (bmv080_sercom_handle_t) this,
                       (bmv080_callback_read_t) read_cb_,
                       (bmv080_callback_write_t) write_cb_,
                       (bmv080_callback_delay_t) delay_cb_);
  if (status != E_BMV080_OK) {
    ESP_LOGE(TAG, "bmv080_open failed, status=%d", status);
    return false;
  }
  ESP_LOGD(TAG, "bmv080_open succeeded, handle=%p", this->handle_);

  ESP_LOGD(TAG, "Resetting BMV080 sensor...");
  status = bmv080_reset(this->handle_);
  if (status != E_BMV080_OK) {
    ESP_LOGE(TAG, "bmv080_reset failed, status=%d", status);
    return false;
  }
  ESP_LOGD(TAG, "BMV080 reset complete");

  char sensor_id[13];
  ESP_LOGD(TAG, "Reading BMV080 sensor ID...");
  status = bmv080_get_sensor_id(this->handle_, sensor_id);
  if (status != E_BMV080_OK) {
    ESP_LOGE(TAG, "bmv080_get_sensor_id failed, status=%d", status);
    return false;
  }
  ESP_LOGI(TAG, "BMV080 sensor ID: %s", sensor_id);
  return true;
}

bool BMV080Component::configure_parameters_() {
  bmv080_status_code_t status;
  ESP_LOGD(TAG, "Configuring BMV080 parameters...");

  ESP_LOGD(TAG, "  Setting integration_time=%.1f s", this->integration_time_);
  status = bmv080_set_parameter(this->handle_, "integration_time", &this->integration_time_);
  if (status != E_BMV080_OK) { ESP_LOGE(TAG, "Failed to set integration_time, status=%d", status); return false; }

  ESP_LOGD(TAG, "  Setting duty_cycling_period=%u s", this->duty_cycling_period_);
  status = bmv080_set_parameter(this->handle_, "duty_cycling_period", &this->duty_cycling_period_);
  if (status != E_BMV080_OK) { ESP_LOGE(TAG, "Failed to set duty_cycling_period, status=%d", status); return false; }

  ESP_LOGD(TAG, "  Setting do_obstruction_detection=%s", YESNO(this->obstruction_detection_));
  status = bmv080_set_parameter(this->handle_, "do_obstruction_detection", &this->obstruction_detection_);
  if (status != E_BMV080_OK) { ESP_LOGE(TAG, "Failed to set do_obstruction_detection, status=%d", status); return false; }

  ESP_LOGD(TAG, "  Setting do_vibration_filtering=%s", YESNO(this->vibration_filtering_));
  status = bmv080_set_parameter(this->handle_, "do_vibration_filtering", &this->vibration_filtering_);
  if (status != E_BMV080_OK) { ESP_LOGE(TAG, "Failed to set do_vibration_filtering, status=%d", status); return false; }

  bmv080_measurement_algorithm_t algo = (bmv080_measurement_algorithm_t) this->algorithm_;
  ESP_LOGD(TAG, "  Setting measurement_algorithm=%d", (int) algo);
  status = bmv080_set_parameter(this->handle_, "measurement_algorithm", &algo);
  if (status != E_BMV080_OK) { ESP_LOGE(TAG, "Failed to set measurement_algorithm, status=%d", status); return false; }

  ESP_LOGD(TAG, "All BMV080 parameters configured successfully");
  return true;
}

bool BMV080Component::start_measurement_() {
  bmv080_status_code_t status;
  if (this->mode_ == MEASUREMENT_MODE_CONTINUOUS) {
    ESP_LOGD(TAG, "Starting continuous measurement...");
    status = bmv080_start_continuous_measurement(this->handle_);
  } else {
    ESP_LOGD(TAG, "Starting duty-cycle measurement (period=%u s)...", this->duty_cycling_period_);
    bmv080_duty_cycling_mode_t dc_mode = E_BMV080_DUTY_CYCLING_MODE_0;
    status = bmv080_start_duty_cycling_measurement(this->handle_, (bmv080_callback_tick_t) tick_cb_, dc_mode);
  }
  if (status != E_BMV080_OK) { ESP_LOGE(TAG, "Failed to start measurement, status=%d", status); return false; }
  ESP_LOGI(TAG, "BMV080 measurement started in %s mode",
           this->mode_ == MEASUREMENT_MODE_CONTINUOUS ? "CONTINUOUS" : "DUTY_CYCLE");
  return true;
}

void BMV080Component::service_sensor_() {
  if (this->handle_ == nullptr)
    return;
  bmv080_status_code_t status = bmv080_serve_interrupt(
      this->handle_, (bmv080_callback_data_ready_t) data_ready_cb_, (void *) this);
  if (status == E_BMV080_OK) {
    ESP_LOGV(TAG, "bmv080_serve_interrupt: OK");
  } else if (status <= 4) {
    ESP_LOGW(TAG, "bmv080_serve_interrupt warning: %d", status);
  } else {
    ESP_LOGE(TAG, "bmv080_serve_interrupt error: %d", status);
  }
}

void BMV080Component::loop() {
  if (this->sensor_failed_) {
    this->sensor_failed_ = false;
    this->mark_failed();
  }
}

void BMV080Component::update() {
  if (!this->sensor_initialized_) {
    ESP_LOGD(TAG, "Update: sensor not initialized yet (task still starting)");
    return;
  }
  if (!this->data_available_) {
    ESP_LOGD(TAG, "Update: no new data available yet");
    return;
  }

  bmv080_output_t output;
  if (this->data_mutex_ != nullptr) {
    xSemaphoreTake(this->data_mutex_, portMAX_DELAY);
  }
  output = this->last_output_;
  this->data_available_ = false;
  if (this->data_mutex_ != nullptr) {
    xSemaphoreGive(this->data_mutex_);
  }

  ESP_LOGD(TAG, "Publishing: PM1=%.1f PM2.5=%.1f PM10=%.1f ug/m3 | counts %.0f/%.0f/%.0f #/cm3 | runtime=%.1fs",
           output.pm1_mass_concentration, output.pm2_5_mass_concentration, output.pm10_mass_concentration,
           output.pm1_number_concentration, output.pm2_5_number_concentration, output.pm10_number_concentration,
           output.runtime_in_sec);

  if (this->pm_1_0_sensor_ != nullptr)
    this->pm_1_0_sensor_->publish_state(output.pm1_mass_concentration);
  if (this->pm_2_5_sensor_ != nullptr)
    this->pm_2_5_sensor_->publish_state(output.pm2_5_mass_concentration);
  if (this->pm_10_sensor_ != nullptr)
    this->pm_10_sensor_->publish_state(output.pm10_mass_concentration);
  if (this->pm_1_0_count_sensor_ != nullptr)
    this->pm_1_0_count_sensor_->publish_state(output.pm1_number_concentration);
  if (this->pm_2_5_count_sensor_ != nullptr)
    this->pm_2_5_count_sensor_->publish_state(output.pm2_5_number_concentration);
  if (this->pm_10_count_sensor_ != nullptr)
    this->pm_10_count_sensor_->publish_state(output.pm10_number_concentration);
  if (this->runtime_sensor_ != nullptr)
    this->runtime_sensor_->publish_state(output.runtime_in_sec);
  if (this->obstructed_sensor_ != nullptr)
    this->obstructed_sensor_->publish_state(output.is_obstructed);
  if (this->out_of_range_sensor_ != nullptr)
    this->out_of_range_sensor_->publish_state(output.is_outside_measurement_range);
}

void BMV080Component::dump_config() {
  // Leaf components override dump_config() to call this base, then append their
  // bus specifics (LOG_I2C_DEVICE / the CS pin).
  const char *algo_str = "unknown";
  switch (this->algorithm_) {
    case ALGORITHM_FAST_RESPONSE: algo_str = "fast_response"; break;
    case ALGORITHM_BALANCED: algo_str = "balanced"; break;
    case ALGORITHM_HIGH_PRECISION: algo_str = "high_precision"; break;
  }
  ESP_LOGCONFIG(TAG, "  Mode: %s",
                this->mode_ == MEASUREMENT_MODE_CONTINUOUS ? "continuous" : "duty_cycle");
  ESP_LOGCONFIG(TAG, "  Measurement Algorithm: %s", algo_str);
  ESP_LOGCONFIG(TAG, "  Integration Time: %.1f s", this->integration_time_);
  ESP_LOGCONFIG(TAG, "  Duty Cycling Period: %u s", this->duty_cycling_period_);
  ESP_LOGCONFIG(TAG, "  Obstruction Detection: %s", YESNO(this->obstruction_detection_));
  ESP_LOGCONFIG(TAG, "  Vibration Filtering: %s", YESNO(this->vibration_filtering_));
  ESP_LOGCONFIG(TAG, "  Task Stack: 64KB (dedicated FreeRTOS task)");
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with BMV080 failed!");
  }
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "PM 1.0", this->pm_1_0_sensor_);
  LOG_SENSOR("  ", "PM 2.5", this->pm_2_5_sensor_);
  LOG_SENSOR("  ", "PM 10", this->pm_10_sensor_);
  LOG_SENSOR("  ", "PM 1.0 Count", this->pm_1_0_count_sensor_);
  LOG_SENSOR("  ", "PM 2.5 Count", this->pm_2_5_count_sensor_);
  LOG_SENSOR("  ", "PM 10 Count", this->pm_10_count_sensor_);
  LOG_SENSOR("  ", "Runtime", this->runtime_sensor_);
  LOG_BINARY_SENSOR("  ", "Obstructed", this->obstructed_sensor_);
  LOG_BINARY_SENSOR("  ", "Out of Range", this->out_of_range_sensor_);
}

}  // namespace bmv080
}  // namespace esphome
