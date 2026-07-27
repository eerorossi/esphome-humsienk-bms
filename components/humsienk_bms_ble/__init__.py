import esphome.codegen as cg
from esphome.components import ble_client
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@syssi"]
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = [
    "binary_sensor",
    "sensor",
    "switch",
    "text_sensor",
]
MULTI_CONF = True

CONF_HUMSIENK_BMS_BLE_ID = "humsienk_bms_ble_id"
CONF_ENABLE_FET_CONTROL = "enable_fet_control"

humsienk_bms_ble_ns = cg.esphome_ns.namespace("humsienk_bms_ble")
HumsienkBmsBle = humsienk_bms_ble_ns.class_(
    "HumsienkBmsBle", ble_client.BLEClientNode, cg.PollingComponent
)

HUMSIENK_BMS_BLE_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_HUMSIENK_BMS_BLE_ID): cv.use_id(HumsienkBmsBle),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(HumsienkBmsBle),
            # FET/balancer write commands are verified but gated behind this flag,
            # so monitoring-only setups can never write to the BMS by accident.
            cv.Optional(CONF_ENABLE_FET_CONTROL, default=False): cv.boolean,
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.polling_component_schema("5s")),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    cg.add(var.set_enable_fet_control(config[CONF_ENABLE_FET_CONTROL]))
