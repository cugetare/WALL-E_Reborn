# System architecture

## Decision

The baseline uses the Waveshare ESP32-S3-DEV-KIT-N32R16V (32 MB flash / 16 MB PSRAM)
as the safety owner, motion controller, web
server, and audio coordinator. A Raspberry Pi is not in the control loop. An
optional Pi Zero 2 W may publish video over Wi-Fi, but loss or reboot of the Pi
must not affect motion safety.

```text
Browser / optional remote / optional BLE controller
                         |
                  command arbitration
                         |
               ESP32-S3 motion task (100 Hz)
                  /       |             \
        brushed H-bridge servo bus    ESP32-S3 audio/media
          2 tracks       9 servos     UART 921600
                                      SD + I2S
```

## Actuators

| Function | Quantity | Baseline interface | Notes |
|---|---:|---|---|
| Tracks | 2 | H-bridge PWM+DIR | Single maintained brushed-motor implementation; existing motors and bridge |
| Arms | 2 | SC/ST serial bus | SC09 baseline, with load carried by bearings; SC15 only as measured fallback |
| Neck tilt | 1 | SC/ST serial bus | SC15 at the neck base |
| Head tilt | 1 | SC/ST serial bus | SC09 inside the neck |
| Head pan | 1 | SC/ST serial bus | SC09; radial + thrust bearings carry the head |
| Eyes | 2 | SC/ST serial bus | SC09 candidate |
| Eyebrows | 2 | SC/ST serial bus | SC09 candidate |

The serial link is one-wire half-duplex TTL at 1 Mbps. The reference
transceiver uses an SN74LVC1G126 transmit buffer, SN74LVC1G125 receive buffer,
and a TX-enable signal, following Waveshare's published bus-servo circuit:
https://files.waveshare.com/wiki/CF35-12/Bus_servo_control_circuit.pdf

The code sends one broadcast Sync Write beginning at register 42. ID zero means
unassigned and is omitted until commissioning is complete. Runtime IDs 2..10
are persisted as a role-to-ID map; ID 1 remains the factory staging address.

## Real-time ownership

- `motion` task: 100 Hz, core 1, priority 12.
- serial servo updates: 50 Hz, emitted from the motion task.
- track output: 100 Hz with 2.5 full-scale units/s slew limit.
- joint interpolation: 1.5 normalized units/s.
- browser joystick heartbeat: 10 Hz while moving.
- command watchdog: tracks stop after 300 ms without a fresh drive command.
- top STOP button: direct local hold-to-stop on GPIO6, independent of Wi-Fi and I2C.

Only the ESP32 is allowed to produce final actuator commands. Browser, BLE,
ESP-NOW, and Pi messages are untrusted input sources and pass through the same
range checks, slew limits, and watchdog.

## Commissioning state

An erased controller starts in `ASSEMBLY/CALIBRATION`, never in normal motion:

```text
erased NVS -> commission 9 IDs -> verify no ID 1 -> calibrate 9 joints -> READY
                    ^                                      |             |
                    +---------------- reset ---------------+-- edit -----+
```

Commissioning connects exactly one new ID-1 servo at a time, assigns a unique
ID starting at 2, binds it to a named movement and saves that mapping. Servos
already assigned to IDs 2+ may remain on the bus. After every ID write a fresh
scan must show no ID 1 and all nine movements assigned before calibration is
enabled. This also provides recovery if the servo EEPROM write succeeds but the
NVS role save is interrupted: rescan and bind the already changed ID.

The profile stores MIN, REST, MAX, direction and servo ID for every joint in
NVS. A joint save invalidates READY. `READY` is allowed only when all nine
records validate and have been explicitly saved. While not
ready, track commands, animation/effect commands, normal joint commands,
remote inputs and sound buttons are rejected; the physical stop button remains
active. Calibration motion is slow and bounded to the raw actuator range.

Normal positions are mapped in two pieces, `MIN -> REST` and `REST -> MAX`, so
logical position 0.5 always resolves to the mechanically measured rest point.

## Web control

The S3 stores one home-network credential set in NVS. At boot it first attempts
station mode for 15 seconds with bounded reconnects. Success exposes the compact
flash-resident UI at its DHCP address (also printed on USB serial); exhaustion
switches the radio to the WPA2 `WALL-E-Reborn` provisioning AP at
`192.168.4.1`. A later loss of the known network follows the same bounded retry
and AP fallback path. The Wi-Fi settings page can replace or erase the saved
credentials, but never sends the stored password back to the browser.
The onboard WS2812B on GPIO38 mirrors only network state for now: blue means
the provisioning AP is active, green means station mode has a valid IP, and it
is off during connection attempts. Error signalling remains reserved for a
later policy extension.
Commands use bounded HTTP API actions:

```text
POST /api/command        drive, joint, sound, stop, service, play-mode, arm
POST /api/servos/*       scan, assign, center, jog, save, test
POST /api/wifi           save known network and restart
POST /api/ota            signed firmware update
POST /api/profiles/*     upload, play and delete choreography profiles
```

Animations should be added as local timestamped waypoints. The browser must
trigger an animation, not stream every servo position. This keeps behaviour
deterministic when Wi-Fi latency changes.

## Firmware update and recovery

The 32 MB flash has a 4 MB factory/recovery application, two 4 MB OTA slots,
about 1.9 MB general LittleFS storage, a dedicated 16 MB audio LittleFS area,
and a coredump partition. The web UI streams an application binary directly
into the inactive OTA slot; it never buffers the complete image in RAM.

Starting an upload establishes a global actuator interlock: queued commands
are discarded, track outputs are forced to zero, and normal or calibration
motion is rejected. ESP-IDF checks the image before the boot partition changes.
Rollback remains armed on the first boot and is cancelled only after NVS,
calibration, hardware, servo bus, audio, Wi-Fi, web server and the motion task
have initialized successfully. The factory image is retained as a serial-flash
recovery path; ordinary OTA alternates only between `ota_0` and `ota_1`.

## Service access and display orientation

The 1.69-inch ST7789V2 is installed in its native portrait orientation. The
ESP32-S3 controller and serial-servo adapter sit on opposing body side walls,
with both USB-C ports accessible through the side service openings. Firmware
therefore uses the panel's native 240x280 coordinates without rotation or
mirroring. Wiring must preserve USB plug clearance and must not cross into the
track envelopes.

## Local panel

Five illuminated momentary switches preserve the front-panel appearance. The
The ESP32-S3 AI Smart media board owns REC, PLAY and media STOP plus their three LEDs. The motion S3 owns the front
Engineering button/LED and sees the physical STOP safety signal directly.
SOLAR operates the external input of the power-switch module and its LED is
driven by S3 when powered. Each 3 V LED has its own initial 1-kohm resistor.
No MCP23017 is required. Media commands cross a dedicated 921600-baud UART;
ESP-NOW is deliberately not used for this internal link.

## Optional integrations

### Pi Zero 2 W video

The Pi is a network peer powered from the protected 5 V rail. It publishes a
camera stream independently. The UI can embed its URL later. No motor or servo
wire terminates at the Pi.

### ESP-NOW remote

Recommended first remote implementation: another ESP32-C3/S3 sends a packet
containing sequence, timestamp, throttle, steering, axes, buttons, and CRC.
The receiver translates it into the same internal command queue. The existing
300 ms watchdog remains authoritative.

### Google Stadia controller

The S3 supports BLE but not Bluetooth Classic. A Stadia controller must be in
its Bluetooth/BLE mode. Treat HID support as a separate input adapter; never
let its callbacks touch hardware directly. If ESP-NOW and Wi-Fi are active,
measure radio coexistence before enabling all three input paths.

### Choreography profiles

Profiles are uploaded from the S3 web server and stored on the media MCU SD
card. Controller automations stay in S3 NVS because the S3 owns controller
matching and all motion interlocks. A rule references a stable profile ID and
contains a digital button mask, trigger thresholds, hold time, cooldown and an
optional neutral-stick requirement. Matching is edge-triggered and requires a
full release before rearming.

The media MCU is storage/audio/timeline transport, never an actuator owner. It
streams logical, timestamped profile events to the S3; the S3 validates them,
maps normalized joint values through commissioning/calibration and emits the
only final motor/servo commands. Physical STOP, OTA and incomplete calibration
cancel or reject playback. See `CHOREOGRAPHY.md` for the package and UART
contracts.

## Power domains

```text
3S battery (fixed baseline, 12.6 V full)
  +-- fuse + latching switch + physical disconnect
  +-- motor rail -> H-bridge
  +-- 6.0 V high-current buck -> 8x SC09 positive rail
  +-- 7.2 V regulator -> SC15 positive rail
  +-- 5.0 V / 3 A buck -> motion ESP32-S3, audio ESP32-S3, display, optional Pi
                         +-- 3.3 V logic from ESP32 regulator
```

Neither SC09 nor SC15 may see the raw 12.6 V pack voltage.
All servo and logic rails must be regulated. Grounds join at a low-impedance
star point; motor current must not return through the ESP32 boards or audio traces.

The BMS is a last-resort protection device, not a charger and not the normal
low-battery cutoff. Firmware voltage display is informative; the pack, fuse,
charger, wiring, and BMS must be selected together.
