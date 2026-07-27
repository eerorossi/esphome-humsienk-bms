import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import (
    CONF_HUMSIENK_BMS_BLE_ID,
    HUMSIENK_BMS_BLE_COMPONENT_SCHEMA,
    humsienk_bms_ble_ns,
)

DEPENDENCIES = ["humsienk_bms_ble"]

CODEOWNERS = ["@syssi"]

CONF_CHARGING = "charging"
CONF_DISCHARGING = "discharging"
CONF_BALANCER = "balancer"

HumsienkSwitch = humsienk_bms_ble_ns.class_("HumsienkSwitch", switch.Switch)
HumsienkControl = humsienk_bms_ble_ns.enum("HumsienkControl")

# key -> (control enum value, icon)
SWITCHES = {
    CONF_CHARGING: (HumsienkControl.HUMSIENK_CONTROL_CHARGING, "mdi:battery-charging"),
    CONF_DISCHARGING: (HumsienkControl.HUMSIENK_CONTROL_DISCHARGING, "mdi:power-plug"),
    CONF_BALANCER: (HumsienkControl.HUMSIENK_CONTROL_BALANCER, "mdi:battery-heart-variant"),
}

CONFIG_SCHEMA = HUMSIENK_BMS_BLE_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(key): switch.switch_schema(
            HumsienkSwitch,
            icon=icon,
            entity_category=ENTITY_CATEGORY_CONFIG,
        )
        for key, (_, icon) in SWITCHES.items()
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_HUMSIENK_BMS_BLE_ID])
    for key, (control, _) in SWITCHES.items():
        if key in config:
            var = await switch.new_switch(config[key])
            await cg.register_parented(var, config[CONF_HUMSIENK_BMS_BLE_ID])
            cg.add(var.set_control(control))
            cg.add(getattr(hub, f"set_{key}_switch")(var))
