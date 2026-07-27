import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_HUMSIENK_BMS_BLE_ID, HUMSIENK_BMS_BLE_COMPONENT_SCHEMA

DEPENDENCIES = ["humsienk_bms_ble"]

CODEOWNERS = ["@syssi"]

CONF_CHARGING = "charging"
CONF_DISCHARGING = "discharging"
CONF_BALANCING = "balancing"
CONF_ONLINE_STATUS = "online_status"
CONF_PROBLEM = "problem"

BINARY_SENSORS = {
    CONF_CHARGING: {"icon": "mdi:battery-charging"},
    CONF_DISCHARGING: {"icon": "mdi:power-plug"},
    CONF_BALANCING: {"icon": "mdi:battery-heart-variant"},
    CONF_ONLINE_STATUS: {
        "device_class": DEVICE_CLASS_CONNECTIVITY,
        "entity_category": ENTITY_CATEGORY_DIAGNOSTIC,
    },
    CONF_PROBLEM: {
        "device_class": DEVICE_CLASS_PROBLEM,
        "entity_category": ENTITY_CATEGORY_DIAGNOSTIC,
    },
}

CONFIG_SCHEMA = HUMSIENK_BMS_BLE_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(key): binary_sensor.binary_sensor_schema(**kwargs)
        for key, kwargs in BINARY_SENSORS.items()
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_HUMSIENK_BMS_BLE_ID])
    for key in BINARY_SENSORS:
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_binary_sensor")(sens))
