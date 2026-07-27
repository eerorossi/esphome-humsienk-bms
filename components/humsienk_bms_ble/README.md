# humsienk_bms_ble

ESPHome component to monitor a **Humsienk** LiFePO4 battery BMS over Bluetooth LE.

## Status

| Area | State |
|------|-------|
| Reading measurements (voltage, current, SOC, SOH, capacity, cycles, temps) | ✅ implemented (protocol verified on a live device) |
| Reading cell voltages (up to 24) | ✅ implemented |
| Reading status (charge/discharge FET, balancing, alarms, cell disconnect) | ✅ implemented |
| Device model / hardware version | ✅ implemented |
| **Charge FET control (write)** | ✅ verified on hardware — switch toggled, BMS reported the new state back |
| Discharge FET control (write) | ✅ same command family, verified via BLE HCI snoop log |
| Balancer / clear-errors control (write) | ⚠️ documented, not individually captured |

The charge FET (`0x50`) and discharge FET (`0x51`) write commands were confirmed
byte-for-byte against the Humsienk Android app (no unlock/auth step is required).
Writes are gated behind `enable_fet_control: true` so a monitoring-only setup can
never write to the BMS by accident. The balancer (`0x52`) and clear-errors (`0x53`)
commands follow the same frame format but were not individually captured yet.

Command format (verified): `AA CMD 01 DATA CRC_LO CRC_HI`, e.g. discharge off =
`AA 51 01 00 52 00`, discharge on = `AA 51 01 01 53 00`.

## BLE protocol summary

- Service UUID `0x0001`, notify characteristic `0x0003`, write characteristic `0x0002`
- The write characteristic advertises plain Write only (properties `0x08`), so
  requests must be sent as Write Request; Write Without Response is silently
  dropped by the BMS. The write type is taken from the discovered properties.
- One request at a time: the next command is sent when the previous reply arrives
- Advertised name starts with `HS`
- Frame: `AA | CMD | LEN | DATA… | CRC_LO | CRC_HI`, CRC = 16-bit LE sum over `CMD…DATA`
- Read commands polled each cycle: `0x20` (status), `0x21` (battery info), `0x22` (cells)

Full register map: [`docs/humsienk-protocol/humsienk_bms.md`](../../docs/humsienk-protocol/humsienk_bms.md).

## Example

See [`esp32-humsienk-ble-example.yaml`](../../esp32-humsienk-ble-example.yaml).

## Credits

The protocol was reverse-engineered by the
[aiobmsble](https://github.com/patman15/aiobmsble) project (Apache-2.0). This
component is an independent C++ port of that work for ESPHome. The reference parser,
protocol docs, and captured test frames are archived under `docs/humsienk-protocol/`.
