#include "switch.h"
#include "esphome/core/log.h"

namespace esphome::humsienk_bms_ble {

static const char *const TAG = "humsienk_bms_ble.switch";

void HumsienkSwitch::write_state(bool state) {
  if (this->parent_->write_control(this->control_, state)) {
    // Optimistic update; the real FET state is confirmed on the next 0x20 read.
    this->publish_state(state);
  } else {
    ESP_LOGW(TAG, "Control command was not sent; leaving state unchanged");
  }
}

}  // namespace esphome::humsienk_bms_ble
