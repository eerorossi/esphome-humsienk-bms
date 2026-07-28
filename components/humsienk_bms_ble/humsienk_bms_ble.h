#pragma once

#include <vector>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"

#ifdef USE_ESP32
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include <esp_gattc_api.h>
namespace espbt = esphome::esp32_ble_tracker;
#endif

namespace esphome::humsienk_bms_ble {

static const uint8_t HUMSIENK_MAX_CELLS = 24;
static const uint8_t HUMSIENK_MAX_TEMPS = 6;

// Read commands (verified against a live device, see docs/humsienk-protocol/)
static const uint8_t HUMSIENK_CMD_INIT = 0x00;         // handshake/init
static const uint8_t HUMSIENK_CMD_DEVICE_MODEL = 0x11;  // ASCII model
static const uint8_t HUMSIENK_CMD_STATUS = 0x20;        // FETs, alarms, balance, disconnect
static const uint8_t HUMSIENK_CMD_BATTERY_INFO = 0x21;  // voltage, current, SOC, SOH, capacity, cycles, temps
static const uint8_t HUMSIENK_CMD_CELL_INFO = 0x22;     // cell voltages
static const uint8_t HUMSIENK_CMD_HW_VERSION = 0xF5;    // ASCII hw/fw version

// Write commands (charge/discharge FET verified via BLE HCI snoop log; no unlock
// step required). Balance/clear are documented but not individually captured yet.
static const uint8_t HUMSIENK_CMD_CHARGE_FET = 0x50;     // data [0x00]=off, [0x01]=on (verified)
static const uint8_t HUMSIENK_CMD_DISCHARGE_FET = 0x51;  // data [0x00]=off, [0x01]=on (verified)
static const uint8_t HUMSIENK_CMD_BALANCE = 0x52;        // data [0x00]=off, [0x01]=on (documented)
static const uint8_t HUMSIENK_CMD_CLEAR_ERRORS = 0x53;   // clear protection status (documented)

// Alarm bitmask with FET (7, 23) and balance (15) status bits masked out.
static const uint32_t HUMSIENK_ALARM_MASK = 0xFF7F7F7F;

class HumsienkBmsBle;

// Control channel used by a switch entity to drive a write command.
enum HumsienkControl {
  HUMSIENK_CONTROL_CHARGING,
  HUMSIENK_CONTROL_DISCHARGING,
  HUMSIENK_CONTROL_BALANCER,
};

class HumsienkBmsBle :
#ifdef USE_ESP32
    public esphome::ble_client::BLEClientNode,
#endif
    public PollingComponent {
 public:
#ifdef USE_ESP32
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
#endif
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_enable_fet_control(bool enable) { this->enable_fet_control_ = enable; }

  // --- sensors (0x21 battery info) ---
  void set_total_voltage_sensor(sensor::Sensor *s) { total_voltage_sensor_ = s; }
  void set_current_sensor(sensor::Sensor *s) { current_sensor_ = s; }
  void set_power_sensor(sensor::Sensor *s) { power_sensor_ = s; }
  void set_state_of_charge_sensor(sensor::Sensor *s) { state_of_charge_sensor_ = s; }
  void set_state_of_health_sensor(sensor::Sensor *s) { state_of_health_sensor_ = s; }
  void set_capacity_remaining_sensor(sensor::Sensor *s) { capacity_remaining_sensor_ = s; }
  void set_full_charge_capacity_sensor(sensor::Sensor *s) { full_charge_capacity_sensor_ = s; }
  void set_charging_cycles_sensor(sensor::Sensor *s) { charging_cycles_sensor_ = s; }
  void set_temperature_sensor(uint8_t i, sensor::Sensor *s) { temperatures_[i].temperature_sensor_ = s; }
  void set_mosfet_temperature_sensor(sensor::Sensor *s) { mosfet_temperature_sensor_ = s; }
  void set_environment_temperature_sensor(sensor::Sensor *s) { environment_temperature_sensor_ = s; }

  // --- sensors (0x22 cell info) ---
  void set_cell_voltage_sensor(uint8_t cell, sensor::Sensor *s) { cells_[cell].cell_voltage_sensor_ = s; }
  void set_min_cell_voltage_sensor(sensor::Sensor *s) { min_cell_voltage_sensor_ = s; }
  void set_max_cell_voltage_sensor(sensor::Sensor *s) { max_cell_voltage_sensor_ = s; }
  void set_min_voltage_cell_sensor(sensor::Sensor *s) { min_voltage_cell_sensor_ = s; }
  void set_max_voltage_cell_sensor(sensor::Sensor *s) { max_voltage_cell_sensor_ = s; }
  void set_delta_cell_voltage_sensor(sensor::Sensor *s) { delta_cell_voltage_sensor_ = s; }
  void set_average_cell_voltage_sensor(sensor::Sensor *s) { average_cell_voltage_sensor_ = s; }

  // --- sensors (0x20 status) ---
  void set_problem_bitmask_sensor(sensor::Sensor *s) { problem_bitmask_sensor_ = s; }

  // --- binary sensors (0x20 status) ---
  void set_charging_binary_sensor(binary_sensor::BinarySensor *s) { charging_binary_sensor_ = s; }
  void set_discharging_binary_sensor(binary_sensor::BinarySensor *s) { discharging_binary_sensor_ = s; }
  void set_balancing_binary_sensor(binary_sensor::BinarySensor *s) { balancing_binary_sensor_ = s; }
  void set_online_status_binary_sensor(binary_sensor::BinarySensor *s) { online_status_binary_sensor_ = s; }
  void set_problem_binary_sensor(binary_sensor::BinarySensor *s) { problem_binary_sensor_ = s; }

  // --- text sensors ---
  void set_model_text_sensor(text_sensor::TextSensor *s) { model_text_sensor_ = s; }
  void set_hardware_version_text_sensor(text_sensor::TextSensor *s) { hardware_version_text_sensor_ = s; }
  void set_errors_bitmask_hex_text_sensor(text_sensor::TextSensor *s) { errors_bitmask_hex_text_sensor_ = s; }

  // --- switches (FET control hooks) ---
  void set_charging_switch(switch_::Switch *s) { charging_switch_ = s; }
  void set_discharging_switch(switch_::Switch *s) { discharging_switch_ = s; }
  void set_balancer_switch(switch_::Switch *s) { balancer_switch_ = s; }

  // Entry point for switch entities. Returns false if the write was rejected
  // (e.g. FET control disabled or not connected).
  bool write_control(HumsienkControl control, bool state);

  void assemble(const uint8_t *data, uint16_t length);

  struct Cell {
    sensor::Sensor *cell_voltage_sensor_{nullptr};
  } cells_[HUMSIENK_MAX_CELLS];
  struct Temperature {
    sensor::Sensor *temperature_sensor_{nullptr};
  } temperatures_[HUMSIENK_MAX_TEMPS];

 protected:
  // Sensors
  sensor::Sensor *total_voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *state_of_charge_sensor_{nullptr};
  sensor::Sensor *state_of_health_sensor_{nullptr};
  sensor::Sensor *capacity_remaining_sensor_{nullptr};
  sensor::Sensor *full_charge_capacity_sensor_{nullptr};
  sensor::Sensor *charging_cycles_sensor_{nullptr};
  sensor::Sensor *mosfet_temperature_sensor_{nullptr};
  sensor::Sensor *environment_temperature_sensor_{nullptr};
  sensor::Sensor *min_cell_voltage_sensor_{nullptr};
  sensor::Sensor *max_cell_voltage_sensor_{nullptr};
  sensor::Sensor *min_voltage_cell_sensor_{nullptr};
  sensor::Sensor *max_voltage_cell_sensor_{nullptr};
  sensor::Sensor *delta_cell_voltage_sensor_{nullptr};
  sensor::Sensor *average_cell_voltage_sensor_{nullptr};
  sensor::Sensor *problem_bitmask_sensor_{nullptr};

  binary_sensor::BinarySensor *charging_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *discharging_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *balancing_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *online_status_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *problem_binary_sensor_{nullptr};

  text_sensor::TextSensor *model_text_sensor_{nullptr};
  text_sensor::TextSensor *hardware_version_text_sensor_{nullptr};
  text_sensor::TextSensor *errors_bitmask_hex_text_sensor_{nullptr};

  switch_::Switch *charging_switch_{nullptr};
  switch_::Switch *discharging_switch_{nullptr};
  switch_::Switch *balancer_switch_{nullptr};

  std::vector<uint8_t> frame_buffer_;
  bool enable_fet_control_{false};
  uint8_t no_response_count_{0};
  uint8_t poll_index_{0};
  uint8_t init_index_{0};

  // Command serialisation: the BMS answers one command at a time.
  bool request_pending_{false};
  uint32_t last_request_ms_{0};
  bool has_pending_control_{false};
  uint8_t pending_control_cmd_{0};
  uint8_t pending_control_data_{0};

  // Verification of the last control command against the next 0x20 status frame.
  bool confirm_pending_{false};
  bool confirm_state_{false};
  HumsienkControl confirm_control_{HUMSIENK_CONTROL_CHARGING};
#ifdef USE_ESP32
  uint16_t write_handle_{0};
  uint16_t notify_handle_{0};
  esp_gatt_write_type_t write_type_{ESP_GATT_WRITE_TYPE_NO_RSP};
#endif

  // Frame handling
  void decode_(const std::vector<uint8_t> &frame);
  void decode_status_(const std::vector<uint8_t> &frame);          // 0x20
  void decode_battery_info_(const std::vector<uint8_t> &frame);    // 0x21
  void decode_cell_info_(const std::vector<uint8_t> &frame);       // 0x22
  void decode_string_(const std::vector<uint8_t> &frame, text_sensor::TextSensor *target);
  void check_control_result_(uint32_t operation_status);

#ifdef USE_ESP32
  bool write_command_(uint8_t command);                    // zero-payload (read) command
  bool write_command_(uint8_t command, uint8_t data);      // one-byte-payload (write) command
  bool send_frame_(const std::vector<uint8_t> &frame);
  bool request_in_flight_();
  bool flush_pending_control_();
#endif

  void publish_state_(binary_sensor::BinarySensor *s, bool state);
  void publish_state_(sensor::Sensor *s, float value);
  void publish_state_(switch_::Switch *s, bool state);
  void publish_state_(text_sensor::TextSensor *s, const std::string &state);
  void publish_device_unavailable_();
  void track_online_status_();

  static uint16_t checksum_(const uint8_t *data, uint16_t begin, uint16_t end);
  std::string to_hex_string_(uint32_t value);
};

}  // namespace esphome::humsienk_bms_ble
