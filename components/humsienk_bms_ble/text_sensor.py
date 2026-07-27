import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_HUMSIENK_BMS_BLE_ID, HUMSIENK_BMS_BLE_COMPONENT_SCHEMA

DEPENDENCIES = ["humsienk_bms_ble"]

CODEOWNERS = ["@syssi"]

CONF_MODEL = "model"
CONF_HARDWARE_VERSION = "hardware_version"
CONF_ERRORS_BITMASK_HEX = "errors_bitmask_hex"

TEXT_SENSORS = {
    CONF_MODEL: {"icon": "mdi:chip"},
    CONF_HARDWARE_VERSION: {
        "icon": "mdi:chip",
        "entity_category": ENTITY_CATEGORY_DIAGNOSTIC,
    },
    CONF_ERRORS_BITMASK_HEX: {
        "icon": "mdi:alert-circle-outline",
        "entity_category": ENTITY_CATEGORY_DIAGNOSTIC,
    },
}

CONFIG_SCHEMA = HUMSIENK_BMS_BLE_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(key): text_sensor.text_sensor_schema(**kwargs)
        for key, kwargs in TEXT_SENSORS.items()
    }
)

SETTERS = {
    CONF_MODEL: "set_model_text_sensor",
    CONF_HARDWARE_VERSION: "set_hardware_version_text_sensor",
    CONF_ERRORS_BITMASK_HEX: "set_errors_bitmask_hex_text_sensor",
}


async def to_code(config):
    hub = await cg.get_variable(config[CONF_HUMSIENK_BMS_BLE_ID])
    for key, setter in SETTERS.items():
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(hub, setter)(sens))
