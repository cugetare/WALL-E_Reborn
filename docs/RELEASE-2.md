# Version 2 — current implementation and boundaries

This document supersedes historical architecture and testing notes where they conflict.

## One maintained hardware target

Motion/web: Waveshare ESP32-S3 N32R16V, 32 MiB flash and 16 MiB PSRAM.
Eight SC09 + one SC15 (neck tilt), adapter A on UART17/18, brushed H-bridge,
native portrait 240x280 ST7789V2, existing ESP32-S3 AI Smart media board
connected by UART14/13. There is one maintained brushed/H-bridge build variant.

Power source is a 3S pack, maximum 12.6 V. All regulators and the existing
Pololu MP must be verified for this input and measured peak current before
wiring. Motor voltage is not yet known: do not connect tracks or any servo
directly to the pack unless the motor/servo rail has been selected for it.
Firmware cannot replace a BMS, hardware undervoltage cutoff, fuse, or actuator
power disconnect. GPIO1 sensing uses 100k/33k + 100 nF, calibrated ADC,
nominal ratio 0.248.

## User interface and safety

Four tabs: Control, Profiles & automations, Commissioning & calibration, Settings.
Calibration drafts survive status refresh. Capture reads measured servo position;
sweeps wait for feedback and are interruptible. Factory ID1 must be isolated to
one new servo at a time. Scanning cannot prove absence of duplicate IDs.
Full scan covers IDs1–253. All nine assigned IDs must respond, and ID1 must be
absent before calibration can be completed. Explicit centering is available for
assembly; nothing automatically homes on boot.

STOP is latched. Service, Play, OTA and physical STOP have central interlocks.
A high-priority task controls track PWM independently of HTTP/media/servo I/O.
Commands carry a generation to invalidate queued movements after STOP. Browser
heartbeats have a 300 ms lease; reconnecting never automatically rearms.
Servo STOP attempts to hold measured positions, not drop a loaded neck. This is
not a certified emergency stop and still needs a physical actuator cutoff.
SC09/SC15 two-byte values use big-endian SC encoding, not ST encoding.

Device-specific browser key is persisted in NVS and printed on USB serial.
Writes require that key and one browser control lease. HTTP is not encrypted;
use a trusted LAN/AP only, never internet port-forwarding. Browser Gamepad API
provides the controller path (browser/secure-context support varies). Direct
Stadia BLE pairing to the S3 is not implemented.

## OTA

Build with `tools/build.ps1`; the canonical output is `build/walle_reborn.bin`.
OTA signatures are verified using a locally generated RSA key. Back up
`secure_boot_signing_key.pem` privately; it is excluded from Git. No eFuses or
hardware Secure Boot are enabled. USB recovery remains possible.
Upload from Settings after STOP. The new image waits 30 seconds before boot
confirmation and checks the motion task is still alive; that is a basic health
check, not proof of peripheral operation. Until confirmation, reset permits
rollback. Existing NVS credentials/calibration are not erased by a normal flash.

## Media contract — integration still required on the second S3

UART921600, 8N1, common ground. Requests are `REQ <sequence> <verb> <args>\n`;
success must reply `ACK <sequence> OK\n` after the operation succeeds.
HELLO1 acknowledges protocol compatibility and SD readiness. PUT(id,size),
CHUNK(size + binary), COMMIT(CRC32), ABORT form a checked transfer; GET(id)
returns ACK then `DATA <size> <hex-crc>\n` followed by bytes.
STOP, PLAY(track), ASSET(relative-path), DELETE(id) are also supported by the host.
The host serializes transactions and refuses to claim upload success without ACK.
**The matching ESP32-S3 AI Smart media-board firmware is not delivered/tested by
this main-board release. Its previous send-only protocol is not compatible.** Media remains
offline until that peer is implemented and connected.

Main S3 now validates and schedules `walle.choreography/v1` profiles (max64KiB,
256 sorted events, 120s); joint, drive, audio and rest events have bounded inputs.
Profiles are fetched from media SD and rejected if CRC/schema/timing is invalid.
Real simultaneous motion/audio timing still requires hardware tests; blocking
audio acknowledgements can stop the sequence via safety watchdog on peer failure.

## Verification limits

Portable C logic is exercised in ESP32 QEMU, not simulated S3 electronics.
Browser tests use the actual JavaScript plus mocked APIs. `tests/preview.cjs`
is a local visual fixture, not a hardware emulator. Real UART feedback, physical
STOP timing, ADC accuracy, LED colours, display and servo loads require bench tests.
