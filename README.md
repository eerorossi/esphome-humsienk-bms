# esphome-humsienk-bms (slim branch)

ESPHome external component for **Humsienk** LiFePO4 battery BMS over Bluetooth LE.

This branch contains **only** the component and its protocol documentation, so that
`external_components` can clone it on memory-constrained hosts (Home Assistant OS,
Raspberry Pi). The full development history lives on the `add-humsienk-bms-ble`
branch, which carries ~160 MB of upstream docs, BLE snoop logs and images and can
make `git index-pack` fail on such hosts.

## Usage

```yaml
external_components:
  - source: github://eerorossi/esphome-humsienk-bms@humsienk-component
    components: [humsienk_bms_ble]

esp32_ble_tracker:

ble_client:
  - mac_address: !secret humsienk_mac
    id: client0

humsienk_bms_ble:
  ble_client_id: client0
  id: bms0
  update_interval: 5s
  enable_fet_control: true
```

See [`esp32-humsienk-ble-example.yaml`](esp32-humsienk-ble-example.yaml) for a
complete configuration with all sensors, and
[`components/humsienk_bms_ble/README.md`](components/humsienk_bms_ble/README.md)
for the protocol summary and implementation status.

## Credits

Forked from [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms)
(Apache-2.0). The Humsienk protocol was reverse-engineered by the
[aiobmsble](https://github.com/patman15/aiobmsble) project (Apache-2.0); this
component is an independent C++ port of that work for ESPHome.
