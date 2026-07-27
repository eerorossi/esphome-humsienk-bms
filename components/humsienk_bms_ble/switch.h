#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/switch/switch.h"
#include "humsienk_bms_ble.h"

namespace esphome::humsienk_bms_ble {

// Switch entity that drives a Humsienk write command (charge/discharge FET, balancer).
// NOTE: the underlying write commands are not yet verified against a live device;
// the hub also gates them behind `enable_fet_control`.
class HumsienkSwitch : public switch_::Switch, public Parented<HumsienkBmsBle> {
 public:
  void set_control(HumsienkControl control) { this->control_ = control; }

 protected:
  void write_state(bool state) override;
  HumsienkControl control_{HUMSIENK_CONTROL_CHARGING};
};

}  // namespace esphome::humsienk_bms_ble
