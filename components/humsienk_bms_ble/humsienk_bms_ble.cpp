#include "humsienk_bms_ble.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/version.h"
#include <cinttypes>
#include <cmath>
#include <cstdio>

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 12, 0)
#define ADDR_STR(x) x
#else
#define ADDR_STR(x) (x).c_str()
#endif

namespace esphome::humsienk_bms_ble {

static const char *const TAG = "humsienk_bms_ble";

static const uint16_t HUMSIENK_SERVICE_UUID = 0x0001;
static const uint16_t HUMSIENK_NOTIFY_CHAR_UUID = 0x0003;  // notify/read
static const uint16_t HUMSIENK_WRITE_CHAR_UUID = 0x0002;   // write

static const uint8_t HUMSIENK_SOF = 0xAA;
static const uint8_t HUMSIENK_MIN_FRAME_LEN = 5;  // SOF + CMD + LEN + CRC_LO + CRC_HI

// Poll cycle: status -> battery info -> cell info, one request per reply.
static const uint8_t POLL_CMDS[] = {HUMSIENK_CMD_STATUS, HUMSIENK_CMD_BATTERY_INFO, HUMSIENK_CMD_CELL_INFO};
static const uint8_t POLL_CMD_COUNT = sizeof(POLL_CMDS) / sizeof(POLL_CMDS[0]);

// Connection handshake: init -> model -> hardware version. The BMS answers one
// command at a time, so these are chained on each reply like the poll cycle
// instead of being written back to back, which risks the extra writes being
// dropped before the device has finished handling the first one.
static const uint8_t INIT_CMDS[] = {HUMSIENK_CMD_INIT, HUMSIENK_CMD_DEVICE_MODEL, HUMSIENK_CMD_HW_VERSION};
static const uint8_t INIT_CMD_COUNT = sizeof(INIT_CMDS) / sizeof(INIT_CMDS[0]);

static const uint8_t MAX_NO_RESPONSE = 4;

// How long a request is assumed to be in flight. The BMS handles one command at
// a time, so anything written while a reply is outstanding risks being dropped.
static const uint32_t REPLY_TIMEOUT_MS = 1000;

// Little-endian readers (frame is guaranteed long enough by the caller).
static uint16_t le16(const std::vector<uint8_t> &d, size_t i) { return d[i] | (uint16_t(d[i + 1]) << 8); }
static uint32_t le32(const std::vector<uint8_t> &d, size_t i) {
  return uint32_t(d[i]) | (uint32_t(d[i + 1]) << 8) | (uint32_t(d[i + 2]) << 16) | (uint32_t(d[i + 3]) << 24);
}

uint16_t HumsienkBmsBle::checksum_(const uint8_t *data, uint16_t begin, uint16_t end) {
  uint16_t sum = 0;
  for (uint16_t i = begin; i < end; i++)
    sum += data[i];
  return sum;
}

#ifdef USE_ESP32
void HumsienkBmsBle::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                         esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      break;
    case ESP_GATTC_DISCONNECT_EVT: {
      this->node_state = espbt::ClientState::IDLE;
      if (this->notify_handle_ != 0) {
        auto status = esp_ble_gattc_unregister_for_notify(this->parent()->get_gattc_if(),
                                                          this->parent()->get_remote_bda(), this->notify_handle_);
        if (status)
          ESP_LOGW(TAG, "esp_ble_gattc_unregister_for_notify failed, status=%d", status);
      }
      this->notify_handle_ = 0;
      this->write_handle_ = 0;
      this->request_pending_ = false;
      this->confirm_pending_ = false;
      this->has_pending_control_ = false;
      this->frame_buffer_.clear();
      this->publish_device_unavailable_();
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *notify_chr = this->parent_->get_characteristic(HUMSIENK_SERVICE_UUID, HUMSIENK_NOTIFY_CHAR_UUID);
      auto *write_chr = this->parent_->get_characteristic(HUMSIENK_SERVICE_UUID, HUMSIENK_WRITE_CHAR_UUID);
      if (notify_chr == nullptr || write_chr == nullptr) {
        ESP_LOGE(TAG, "[%s] Humsienk service (0x0001) not found, not a Humsienk BMS?",
                 ADDR_STR(this->parent_->address_str()));
        break;
      }
      this->notify_handle_ = notify_chr->handle;
      this->write_handle_ = write_chr->handle;
      // A characteristic that only advertises plain Write silently drops writes
      // sent without a response, so follow what the peer actually supports.
      this->write_type_ = (write_chr->properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) ? ESP_GATT_WRITE_TYPE_NO_RSP
                                                                                    : ESP_GATT_WRITE_TYPE_RSP;
      ESP_LOGD(TAG, "Notify handle 0x%04X (properties 0x%02X), write handle 0x%04X (properties 0x%02X, using %s)",
               this->notify_handle_, notify_chr->properties, this->write_handle_, write_chr->properties,
               this->write_type_ == ESP_GATT_WRITE_TYPE_NO_RSP ? "write without response" : "write request");

      auto status = esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(),
                                                      this->notify_handle_);
      if (status)
        ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", status);
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.status != ESP_GATT_OK)
        ESP_LOGE(TAG, "Notification registration failed, status=%d; no measurements will arrive",
                 param->reg_for_notify.status);
      this->node_state = espbt::ClientState::ESTABLISHED;
      this->frame_buffer_.clear();
      this->request_pending_ = false;
      // Handshake and one-time device information, chained on each reply.
      this->init_index_ = 0;
      this->write_command_(INIT_CMDS[this->init_index_]);
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle != this->notify_handle_)
        break;
      ESP_LOGVV(TAG, "RX: %s", format_hex_pretty(param->notify.value, param->notify.value_len).c_str());
      this->assemble(param->notify.value, param->notify.value_len);
      break;
    }
    default:
      break;
  }
}
#endif

void HumsienkBmsBle::update() {
  this->track_online_status_();

#ifdef USE_ESP32
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    ESP_LOGW(TAG, "[%s] Not connected", ADDR_STR(this->parent_->address_str()));
    return;
  }
  // A control command that was queued while the bus was busy gets priority. This
  // also covers the case where the reply it was waiting for never arrived.
  if (this->flush_pending_control_())
    return;

  // Start a fresh poll cycle.
  this->poll_index_ = 0;
  this->write_command_(POLL_CMDS[this->poll_index_]);
#endif
}

void HumsienkBmsBle::assemble(const uint8_t *data, uint16_t length) {
  if (this->frame_buffer_.size() > 512) {
    ESP_LOGW(TAG, "Frame buffer overflow, discarding");
    this->frame_buffer_.clear();
  }
  this->frame_buffer_.insert(this->frame_buffer_.end(), data, data + length);

  // Extract as many complete frames as available.
  while (true) {
    // Drop leading garbage until a start-of-frame byte.
    while (!this->frame_buffer_.empty() && this->frame_buffer_.front() != HUMSIENK_SOF)
      this->frame_buffer_.erase(this->frame_buffer_.begin());

    if (this->frame_buffer_.size() < HUMSIENK_MIN_FRAME_LEN)
      return;  // wait for more data

    const uint16_t frame_len = this->frame_buffer_[2] + HUMSIENK_MIN_FRAME_LEN;
    if (this->frame_buffer_.size() < frame_len)
      return;  // wait for the rest of the frame

    std::vector<uint8_t> frame(this->frame_buffer_.begin(), this->frame_buffer_.begin() + frame_len);
    this->frame_buffer_.erase(this->frame_buffer_.begin(), this->frame_buffer_.begin() + frame_len);

    // Checksum: 16-bit LE sum over CMD..end-of-DATA (everything except SOF and the two CRC bytes).
    const uint16_t computed = checksum_(frame.data(), 1, frame_len - 2);
    const uint16_t received = le16(frame, frame_len - 2);
    if (computed != received) {
      ESP_LOGW(TAG, "CRC mismatch (got 0x%04X, want 0x%04X): %s", received, computed,
               format_hex_pretty(frame.data(), frame.size()).c_str());
      continue;
    }
    this->decode_(frame);
  }
}

void HumsienkBmsBle::decode_(const std::vector<uint8_t> &frame) {
  this->no_response_count_ = 0;
  this->request_pending_ = false;
  this->publish_state_(this->online_status_binary_sensor_, true);

  bool control_ack = false;
  const uint8_t cmd = frame[1];
  switch (cmd) {
    case HUMSIENK_CMD_STATUS:
      this->decode_status_(frame);
      break;
    case HUMSIENK_CMD_BATTERY_INFO:
      this->decode_battery_info_(frame);
      break;
    case HUMSIENK_CMD_CELL_INFO:
      this->decode_cell_info_(frame);
      break;
    case HUMSIENK_CMD_DEVICE_MODEL:
      this->decode_string_(frame, this->model_text_sensor_);
      break;
    case HUMSIENK_CMD_HW_VERSION:
      this->decode_string_(frame, this->hardware_version_text_sensor_);
      break;
    case HUMSIENK_CMD_INIT:
      break;  // handshake ack, nothing to decode
    case HUMSIENK_CMD_CHARGE_FET:
    case HUMSIENK_CMD_DISCHARGE_FET:
    case HUMSIENK_CMD_BALANCE:
    case HUMSIENK_CMD_CLEAR_ERRORS:
      // Zero-payload echo of the command, e.g. AA 50 00 50 00. This only means the
      // BMS received it; whether it actually switched the FET shows up in 0x20.
      ESP_LOGI(TAG, "Control command 0x%02X acknowledged by the BMS", cmd);
      control_ack = true;
      break;
    default:
      ESP_LOGD(TAG, "Unhandled frame type 0x%02X", cmd);
      break;
  }

#ifdef USE_ESP32
  // Queued control commands go out before any read, so a switch press is never
  // delayed by a full poll cycle.
  if (this->flush_pending_control_())
    return;

  if (control_ack) {
    // Re-read the status immediately so the switch shows the real FET state
    // instead of the optimistic one for up to a whole update interval.
    this->poll_index_ = 0;
    this->write_command_(POLL_CMDS[this->poll_index_]);
    return;
  }

  // Advance the handshake chain only for the expected handshake reply.
  if (this->init_index_ < INIT_CMD_COUNT && cmd == INIT_CMDS[this->init_index_]) {
    this->init_index_++;
    if (this->init_index_ < INIT_CMD_COUNT)
      this->write_command_(INIT_CMDS[this->init_index_]);
  }

  // Advance the poll chain only for the expected read reply.
  if (this->poll_index_ < POLL_CMD_COUNT && cmd == POLL_CMDS[this->poll_index_]) {
    this->poll_index_++;
    if (this->poll_index_ < POLL_CMD_COUNT)
      this->write_command_(POLL_CMDS[this->poll_index_]);
  }
#endif
}

void HumsienkBmsBle::decode_status_(const std::vector<uint8_t> &frame) {
  // operation_status u32 LE at data offset 4 (frame offset 7)
  if (frame.size() < 11)
    return;
  const uint32_t op = le32(frame, 7);

  // Full frame while the FET status bits are still being reverse engineered: the
  // parsed operation_status alone does not show which byte tracks the real state.
  ESP_LOGD(TAG, "Status frame: %s", format_hex_pretty(frame.data(), frame.size()).c_str());

  // Charge FET state is bit 3 on a live BMC-04S001b, not bit 7 as the aiobmsble bit
  // table has it. Verified 2026-07-28 by driving the 0x50 command and reading the
  // status one round trip later: data 0x01 -> 0x00000008, data 0x00 -> 0x00000000,
  // with charge current flowing only in the former state. Bit 7 was set on an idle,
  // fully charged pack, so it is something else (charge complete?), not the FET.
  const bool charging = (op & HUMSIENK_STATUS_CHARGE_FET) != 0;
  const bool balancing = (op & (1UL << 15)) != 0;  // bit 15: balance active
  // Not yet verified against a toggle; the charge side turned out to be off by four
  // bits, so this is the documented offset rather than a confirmed one.
  const bool discharging = (op & HUMSIENK_STATUS_DISCHARGE_FET) != 0;

  // The FET bit reports the switch state, not whether current is flowing.
  ESP_LOGD(TAG, "Operation status 0x%08" PRIX32 ": charge FET %s, discharge FET %s, balancing %s%s%s", op,
           charging ? "on" : "off", discharging ? "on" : "off", balancing ? "on" : "off",
           (op & (1UL << 6)) != 0 ? ", charging stopped" : "", (op & (1UL << 22)) != 0 ? ", discharging stopped" : "");

  this->check_control_result_(op);

  this->publish_state_(this->charging_binary_sensor_, charging);
  this->publish_state_(this->discharging_binary_sensor_, discharging);
  this->publish_state_(this->balancing_binary_sensor_, balancing);
  this->publish_state_(this->charging_switch_, charging);
  this->publish_state_(this->discharging_switch_, discharging);
  this->publish_state_(this->balancer_switch_, balancing);

  const uint32_t problem_code = op & HUMSIENK_ALARM_MASK;

  // cell_disconnect bitmap: data offset 11..13 (frame 14..16)
  bool disconnect = false;
  if (frame.size() >= 17)
    disconnect = frame[14] != 0 || frame[15] != 0 || frame[16] != 0;

  this->publish_state_(this->problem_bitmask_sensor_, (float) problem_code);
  this->publish_state_(this->problem_binary_sensor_, problem_code != 0 || disconnect);
  this->publish_state_(this->errors_bitmask_hex_text_sensor_, this->to_hex_string_(problem_code));
}

void HumsienkBmsBle::decode_battery_info_(const std::vector<uint8_t> &frame) {
  // Requires 26 data bytes -> frame length 31.
  if (frame.size() < 31) {
    ESP_LOGW(TAG, "0x21 frame too short (%d)", (int) frame.size());
    return;
  }
  const float voltage = le32(frame, 3) / 1000.0f;
  const float current = (int32_t) le32(frame, 7) / 1000.0f;  // + = charging
  this->publish_state_(this->total_voltage_sensor_, voltage);
  this->publish_state_(this->current_sensor_, current);
  this->publish_state_(this->power_sensor_, voltage * current);
  this->publish_state_(this->state_of_charge_sensor_, (float) frame[11]);
  this->publish_state_(this->state_of_health_sensor_, (float) frame[12]);
  this->publish_state_(this->capacity_remaining_sensor_, le32(frame, 13) / 1000.0f);
  this->publish_state_(this->full_charge_capacity_sensor_, le32(frame, 17) / 1000.0f);
  this->publish_state_(this->charging_cycles_sensor_, (float) le16(frame, 21));

  // Temperatures: cell temps 1..4 (frame 23..26), MOSFET (27), environment (28); signed 8-bit °C.
  for (uint8_t i = 0; i < 4; i++)
    this->publish_state_(this->temperatures_[i].temperature_sensor_, (float) (int8_t) frame[23 + i]);
  this->publish_state_(this->mosfet_temperature_sensor_, (float) (int8_t) frame[27]);
  this->publish_state_(this->environment_temperature_sensor_, (float) (int8_t) frame[28]);
}

void HumsienkBmsBle::decode_cell_info_(const std::vector<uint8_t> &frame) {
  // Cell voltages: 2 bytes each (u16 LE, mV), starting at frame offset 3.
  const uint8_t data_len = frame[2];
  uint8_t cells = data_len / 2;
  if (cells > HUMSIENK_MAX_CELLS)
    cells = HUMSIENK_MAX_CELLS;

  float min_v = NAN, max_v = NAN, sum_v = 0;
  uint8_t min_cell = 0, max_cell = 0, counted = 0;
  for (uint8_t i = 0; i < cells; i++) {
    const size_t pos = 3 + i * 2;
    if (pos + 1 >= frame.size())
      break;
    const float v = le16(frame, pos) / 1000.0f;
    this->publish_state_(this->cells_[i].cell_voltage_sensor_, v);
    if (v <= 0.0f)
      continue;  // skip unpopulated cells for statistics
    if (std::isnan(min_v) || v < min_v) {
      min_v = v;
      min_cell = i + 1;
    }
    if (std::isnan(max_v) || v > max_v) {
      max_v = v;
      max_cell = i + 1;
    }
    sum_v += v;
    counted++;
  }

  if (counted > 0) {
    this->publish_state_(this->min_cell_voltage_sensor_, min_v);
    this->publish_state_(this->max_cell_voltage_sensor_, max_v);
    this->publish_state_(this->min_voltage_cell_sensor_, (float) min_cell);
    this->publish_state_(this->max_voltage_cell_sensor_, (float) max_cell);
    this->publish_state_(this->delta_cell_voltage_sensor_, max_v - min_v);
    this->publish_state_(this->average_cell_voltage_sensor_, sum_v / counted);
  }
}

void HumsienkBmsBle::decode_string_(const std::vector<uint8_t> &frame, text_sensor::TextSensor *target) {
  if (target == nullptr || frame.size() < HUMSIENK_MIN_FRAME_LEN)
    return;
  std::string s(frame.begin() + 3, frame.end() - 2);  // ASCII payload without SOF/CMD/LEN and CRC
  // Trim trailing NULs / whitespace.
  while (!s.empty() && (s.back() == '\0' || s.back() == ' '))
    s.pop_back();
  this->publish_state_(target, s);
}

// ---- command / write path --------------------------------------------------

#ifdef USE_ESP32
bool HumsienkBmsBle::send_frame_(const std::vector<uint8_t> &frame) {
  if (this->node_state != espbt::ClientState::ESTABLISHED || this->write_handle_ == 0) {
    ESP_LOGW(TAG, "[%s] cannot write, not connected", ADDR_STR(this->parent_->address_str()));
    return false;
  }
  ESP_LOGD(TAG, "TX: %s", format_hex_pretty(frame.data(), frame.size()).c_str());
  auto status = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(),
                                         this->write_handle_, frame.size(), const_cast<uint8_t *>(frame.data()),
                                         this->write_type_, ESP_GATT_AUTH_REQ_NONE);
  if (status) {
    ESP_LOGW(TAG, "[%s] esp_ble_gattc_write_char failed, status=%d", ADDR_STR(this->parent_->address_str()), status);
    return false;
  }
  this->request_pending_ = true;
  this->last_request_ms_ = millis();
  return true;
}

bool HumsienkBmsBle::write_command_(uint8_t command) {
  // Frame: AA CMD 00 CRC_LO CRC_HI ; CRC = LE sum of {CMD, 0x00}
  std::vector<uint8_t> frame = {HUMSIENK_SOF, command, 0x00};
  const uint16_t crc = command + 0x00;
  frame.push_back(crc & 0xFF);
  frame.push_back((crc >> 8) & 0xFF);
  return this->send_frame_(frame);
}

bool HumsienkBmsBle::write_command_(uint8_t command, uint8_t data) {
  // Frame: AA CMD 01 DATA CRC_LO CRC_HI ; CRC = LE sum of {CMD, 0x01, DATA}
  std::vector<uint8_t> frame = {HUMSIENK_SOF, command, 0x01, data};
  const uint16_t crc = command + 0x01 + data;
  frame.push_back(crc & 0xFF);
  frame.push_back((crc >> 8) & 0xFF);
  return this->send_frame_(frame);
}
#endif

// Compare the reported status against the last command so a BMS that acks but
// refuses to switch (protection active, pack full, ...) is visible in the log.
void HumsienkBmsBle::check_control_result_(uint32_t operation_status) {
  if (!this->confirm_pending_)
    return;
  this->confirm_pending_ = false;

  uint32_t mask;
  const char *name;
  switch (this->confirm_control_) {
    case HUMSIENK_CONTROL_CHARGING:
      mask = HUMSIENK_STATUS_CHARGE_FET;
      name = "Charge FET";
      break;
    case HUMSIENK_CONTROL_DISCHARGING:
      mask = HUMSIENK_STATUS_DISCHARGE_FET;
      name = "Discharge FET";
      break;
    default:
      mask = 1UL << 15;
      name = "Balancer";
      break;
  }

  const bool actual = (operation_status & mask) != 0;
  if (actual == this->confirm_state_) {
    ESP_LOGI(TAG, "%s is now %s", name, actual ? "on" : "off");
    return;
  }
  ESP_LOGW(TAG,
           "%s is still %s after the %s command (operation_status 0x%08" PRIX32
           "). The BMS accepted the command but refused to switch, most likely because a protection or "
           "charge-full condition is active.",
           name, actual ? "on" : "off", this->confirm_state_ ? "on" : "off", operation_status);
}

bool HumsienkBmsBle::write_control(HumsienkControl control, bool state) {
  if (!this->enable_fet_control_) {
    ESP_LOGW(TAG, "FET/balance control is disabled (enable_fet_control: true). Command ignored.");
    return false;
  }
#ifdef USE_ESP32
  // Frame bytes verified against the Humsienk Android app via a BLE HCI snoop log
  // (no unlock/auth step required). See docs/humsienk-protocol/.
  ESP_LOGI(TAG, "Sending control command (control=%d, state=%d)", (int) control, (int) state);
  uint8_t cmd;
  switch (control) {
    case HUMSIENK_CONTROL_CHARGING:
      cmd = HUMSIENK_CMD_CHARGE_FET;
      break;
    case HUMSIENK_CONTROL_DISCHARGING:
      cmd = HUMSIENK_CMD_DISCHARGE_FET;
      break;
    case HUMSIENK_CONTROL_BALANCER:
      cmd = HUMSIENK_CMD_BALANCE;
      break;
    default:
      return false;
  }

  // A write sent while a read is still outstanding is silently dropped by the BMS,
  // so hold it back until the pending reply arrives.
  if (this->request_in_flight_()) {
    ESP_LOGD(TAG, "Request in flight, queuing control command 0x%02X", cmd);
    this->pending_control_cmd_ = cmd;
    this->pending_control_data_ = state ? 0x01 : 0x00;
    this->has_pending_control_ = true;
  } else if (!this->write_command_(cmd, state ? 0x01 : 0x00)) {
    return false;
  }

  // Verify against the next status frame instead of trusting the ack.
  this->confirm_control_ = control;
  this->confirm_state_ = state;
  this->confirm_pending_ = true;
  return true;
#else
  return false;
#endif
}

#ifdef USE_ESP32
bool HumsienkBmsBle::request_in_flight_() {
  return this->request_pending_ && (millis() - this->last_request_ms_) < REPLY_TIMEOUT_MS;
}

bool HumsienkBmsBle::flush_pending_control_() {
  if (!this->has_pending_control_)
    return false;
  this->has_pending_control_ = false;
  ESP_LOGD(TAG, "Sending queued control command 0x%02X", this->pending_control_cmd_);
  this->write_command_(this->pending_control_cmd_, this->pending_control_data_);
  return true;
}
#endif

// ---- helpers ---------------------------------------------------------------

void HumsienkBmsBle::track_online_status_() {
  if (this->no_response_count_ < MAX_NO_RESPONSE)
    this->no_response_count_++;
  if (this->no_response_count_ == MAX_NO_RESPONSE)
    this->publish_device_unavailable_();
}

void HumsienkBmsBle::publish_device_unavailable_() {
  this->publish_state_(this->online_status_binary_sensor_, false);
  this->publish_state_(this->total_voltage_sensor_, NAN);
  this->publish_state_(this->current_sensor_, NAN);
  this->publish_state_(this->power_sensor_, NAN);
  this->publish_state_(this->state_of_charge_sensor_, NAN);
}

void HumsienkBmsBle::publish_state_(binary_sensor::BinarySensor *s, bool state) {
  if (s != nullptr)
    s->publish_state(state);
}
void HumsienkBmsBle::publish_state_(sensor::Sensor *s, float value) {
  if (s != nullptr)
    s->publish_state(value);
}
void HumsienkBmsBle::publish_state_(switch_::Switch *s, bool state) {
  if (s != nullptr)
    s->publish_state(state);
}
void HumsienkBmsBle::publish_state_(text_sensor::TextSensor *s, const std::string &state) {
  if (s != nullptr)
    s->publish_state(state);
}

std::string HumsienkBmsBle::to_hex_string_(uint32_t value) {
  char buf[11];
  snprintf(buf, sizeof(buf), "0x%08" PRIX32, value);
  return std::string(buf);
}

void HumsienkBmsBle::dump_config() {
  ESP_LOGCONFIG(TAG, "Humsienk BMS BLE:");
  ESP_LOGCONFIG(TAG, "  FET/balancer control: %s", this->enable_fet_control_ ? "ENABLED" : "disabled");
  LOG_BINARY_SENSOR("  ", "Charging", this->charging_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Discharging", this->discharging_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Balancing", this->balancing_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Online status", this->online_status_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Problem", this->problem_binary_sensor_);
  LOG_SENSOR("  ", "Total voltage", this->total_voltage_sensor_);
  LOG_SENSOR("  ", "Current", this->current_sensor_);
  LOG_SENSOR("  ", "State of charge", this->state_of_charge_sensor_);
}

}  // namespace esphome::humsienk_bms_ble
