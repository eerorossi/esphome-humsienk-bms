# Humsienk BLE snoop analysis

Source: `humsienk_snoop.zip` (Android bug report) → `btsnoop_hci.log`.
Device: `BMC-04S001b`, firmware `BMC4S-20251021-OTAV14` (4S, 12 V LiFePO4, 150 Ah).
All BMS traffic uses the write characteristic (handle `0x000c`, UUID `0x0002`) for
commands and notifications on UUID `0x0003`. **No unlock/auth step precedes control.**

## Verified control commands (charge/discharge FET)

Captured directly from the Humsienk Android app toggling the switches:

| Action | Bytes on the wire |
|--------|-------------------|
| Discharge OFF | `aa 51 01 00 52 00` |
| Discharge ON  | `aa 51 01 01 53 00` |
| Charge OFF    | `aa 50 01 00 51 00` |
| Charge ON     | `aa 50 01 01 52 00` |

Frame = `AA CMD 01 DATA CRC_LO CRC_HI`, CRC = 16-bit LE sum of `{CMD, 0x01, DATA}`.
The BMS acknowledges with a zero-payload echo frame, e.g. `aa 50 00 50 00`.

## GATT characteristics as seen on a live BMC-04S001b

Confirmed by the ESPHome component on 2026-07-27 (ESP32-S3, MTU 251):

| Characteristic | Handle | Properties |
|----------------|--------|------------|
| Write (UUID `0x0002`) | `0x000C` | `0x08` — plain Write **only** |
| Notify (UUID `0x0003`) | `0x000E` | `0x10` — Notify |

The write characteristic does **not** advertise Write Without Response (`0x04`).
Writing with `ESP_GATT_WRITE_TYPE_NO_RSP` is accepted by the local stack and then
dropped silently by the BMS: requests appear on the wire, no notification ever
comes back. The component therefore derives the write type from the discovered
properties. The BMS also answers one command at a time, so both the handshake and
the poll cycle send the next request only after the previous reply arrives.

## App polling loop (for reference)

One-time on connect: `0x00` (init), `0xf5` (fw), `0x58` (config), `0x10`, a `0x5c`
config write, `0x11` (model). Then it repeats `0x21`, `0x23`, `0x20`, `0x22`.

## Read frames validated against live data

- `0x21`: voltage 13.74 V, current 0 A, SOC 100 %, SOH 100 %, remaining 149.23 Ah,
  design 150.0 Ah, cycles 1, temps `[20, 26, 26, 26, 20, 26]` °C.
- `0x20`: `operation_status = 0x00800080` → charge FET (bit 7) on, discharge FET
  (bit 23) on, balancing (bit 15) off.
- `0x22`: 24 cell slots, first four populated `3.426 / 3.461 / 3.459 / 3.400 V`,
  remainder `0.000 V` (unused on a 4S pack).

The C++ component's frame builder and decode offsets match all of the above exactly.
