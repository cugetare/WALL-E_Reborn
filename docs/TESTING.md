# Testing

## Layers

1. Target unit tests exercise mixer, watchdog, calibration defaults and bounds,
   commissioning gates, NVS save/reload, parser, 3S battery conversion,
   SC/ST ping/read/write/status and nine-servo Sync Write packet generation,
   OTA image-size limits,
   progress calculation, the OTA motion interlock policy, and the ST7789
   status-screen model (Play sun/battery artwork, engineering state, battery
   fill and motion indicator), dedicated Engineering-button semantics, and the
   five-LED panel policy for Play, REC, STOP, Engineering, error and OTA states;
   Wi-Fi credential validation covers secured, open, empty, short-password and
   overlength inputs; onboard network-status LED colour mapping covers AP,
   station and transitional-off states;
   choreography tests cover catalog insert/update/delete, safe profile IDs,
   incremental CRC-32, trigger and digital-button chords, hold/cooldown,
   release-to-rearm, neutral-stick gating and the motion-ready interlock.
2. Target integration tests run `HTTP command text -> parsed command -> motion
   interpolation -> calibrated servo output -> serial packet` and verify that
   an active OTA state prevents a parsed drive command from arming motion.
3. ESP32-S3 firmware build compiles every hardware adapter and the web server
   against the selected ESP-IDF.
4. Hardware-in-loop commissioning is intentionally gated by the checklist in
   `WIRING.md`; it cannot be truthfully automated without the physical robot.

## Commands

Browser regression tests: `node --test tests/web.test.cjs`. These validate
the complete C-embedded script syntax (not only isolated functions), credential
submission, HTTP/network failures, and scan-result selection. A choreography
string-escaping error previously prevented the entire browser script from parsing.
The script extracted from the compiled firmware binary also passes syntax validation.
Physical network scan and successful home-network association still require
verification through the robot AP; the test host's HTTP route returns an unrelated
nginx HTTPS redirect instead of the ESP32 server.

```powershell
cd tests/target
idf.py set-target esp32
idf.py build
idf.py qemu
```

The target suite prints `ALL TESTS PASSED`. QEMU is used only for portable
logic; the final integration compile is:

```powershell
cd ../..
idf.py -B build-n32r16v set-target esp32s3
idf.py -B build-n32r16v build
```

Hardware tests after assembly must record: motor stall current, rail droop,
watchdog stop time, safe joint endpoints, servo temperature/load, Wi-Fi loss,
and I2S/media noise while all actuators move.

## Recorded validation (2026-09-11)

- Portable target suite: passed under Espressif QEMU 9.0.0 (`ALL TESTS PASSED`).
- ESP32-S3 N32R16V brushed/H-bridge firmware with web OTA, serial-servo
  commissioning, ST7789V2, ESP32-S3 AI Smart media UART, web profile upload/catalog and
  controller automations and saved-network/AP-fallback Wi-Fi provisioning:
  build passed, binary `0xef640` bytes (980,544 bytes).
- The image leaves 77% of each 4 MiB factory/OTA application slot free.
- Generated configuration verified: 32 MB flash, Octal OPI/DTR, Octal PSRAM
  at 80 MHz, and bootloader rollback enabled.
- Hardware-in-loop remains pending until the S3, power rails, actuators and
  motor driver are connected.

On this Windows installation QEMU also needs `C:\Program Files\Git\mingw64\bin`
on `PATH`; that directory supplies `libiconv-2.dll` missing from the local QEMU
package.
