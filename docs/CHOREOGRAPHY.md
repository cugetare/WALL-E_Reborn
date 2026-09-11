# Choreography profiles and controller automations

## Ownership and safety

The SD card on the media MCU is the durable profile store. The ESP32-S3 remains
the only motion authority: a media-side player may deliver timestamped logical
events, but it may never write the H-bridge or servo bus. Every event is checked
against commissioning, calibration, OTA and physical STOP state on the S3.
Profile coordinates are normalized logical values, never raw servo positions or
factory IDs.

The current S3 firmware implements the web catalog, upload transport, CRC,
play/delete requests, persistent automation rules and controller-chord matcher.
The media firmware must implement the UART transaction below and stream profile
events back to the S3 before a package can produce physical choreography.

## Web workflow

1. Open **Choreographies**, choose a `.json` or `.walle` package, assign a stable
   ID such as `hello_eve`, and upload it. The S3 forwards the body to the SD card
   and stores only catalog metadata in NVS.
2. Test the profile only after commissioning and calibration are complete.
3. Under **Controller automations**, choose the profile and either enter a chord
   or hold it on the controller and press **Use buttons held now**.
4. Configure hold time, cooldown and optional neutral-stick requirement. A chord
   fires once and cannot fire again until it is released.

Recommended defaults:

- both triggers: LT 920, RT 920, hold 600 ms, cooldown 1000 ms;
- L3+R3: captured digital button mask, hold 350 ms, cooldown 750 ms, require
  neutral sticks.

Rules are persisted in the S3 NVS, refer to profiles by stable ID, and are
ignored whenever motion is not ready. The physical STOP remains independent of
the web server, SD card, media MCU and controller.

## Package schema, version 1

```json
{
  "schema": "walle.choreography/v1",
  "name": "Hello EVE",
  "durationMs": 4200,
  "events": [
    {"atMs": 0, "type": "audio", "asset": "sounds/walle.wav"},
    {"atMs": 100, "type": "joint", "joint": "head_pan", "value": 0.62, "durationMs": 500},
    {"atMs": 650, "type": "joint", "joint": "head_tilt", "value": 0.38, "durationMs": 450},
    {"atMs": 1200, "type": "drive", "throttle": 0.18, "steering": 0.0, "durationMs": 700},
    {"atMs": 2000, "type": "rest", "group": "head", "durationMs": 500}
  ]
}
```

Valid joint names are `arm_left`, `arm_right`, `neck_tilt`, `head_tilt`,
`head_pan`, `eye_left`, `eye_right`, `brow_left`, and `brow_right`. Joint values
are 0.0–1.0 and are transformed through saved MIN/REST/MAX calibration. Drive
values are -1.0–1.0. Unknown events, out-of-range values, non-monotonic
timestamps or excessive duration must be rejected before playback.

## UART storage protocol

The dedicated 921600-baud UART uses these transactions:

- `PROFILE_PUT,<id>,<size>\n`, exactly `<size>` opaque bytes, then
  `\nPROFILE_COMMIT,<crc32-hex>\n`;
- `PROFILE_ABORT` discards the temporary file;
- `PROFILE_DELETE,<id>,0` removes the committed package;
- `PROFILE_PLAY,<id>,0` requests validation and event streaming;
- media writes to a temporary file and atomically renames only after size and
  CRC match, so power loss cannot replace a valid profile with a partial one.

The next media-firmware increment must add explicit ACK/NACK and sequence
numbers. Until then, the web catalog confirms transfer from the S3 UART, not a
durable SD write; this limitation must remain visible during bench tests.

