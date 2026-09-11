# WALL-E Reborn

**Current release: 2.0.0.** See [current architecture, safety and limitations](docs/RELEASE-2.md).
Canonical build: `tools/build.ps1`; web tests: `node --test tests/web.test.cjs`.
Only the brushed N32R16V target is maintained. Older build directories are stale artifacts.

Clean ESP-IDF rewrite of the Chillibasket WALL-E controller for an ESP32-S3
on the Waveshare ESP32-S3-DEV-KIT-N32R16V board (ESP32-S3-WROOM-2, 16 MB Octal PSRAM /
32 MB Octal flash, without headers). The original Arduino and community ESP32 ports
were used as behavioural references, but this project does not depend on
Arduino, PCA9685, AsyncWebServer, or Raspberry Pi.

## Implemented baseline

- two independently mixed and acceleration-limited track motors;
- one maintained brushed H-bridge motor backend;
- nine SC/ST TTL serial-bus servos: eight SC09 and one SC15;
- nine logical joints: arms, neck tilt, head tilt/pan, eyes, and eyebrows;
- Waveshare ESP32-S3 AI Smart media board over dedicated UART;
- five illuminated panel switches split between the motion S3 and media S3, without a
  GPIO expander;
- 3S battery sensing and native 240x280 ST7789V2 SPI display, with a
  WALL-E-style Play screen and a dedicated-button Engineering screen;
- saved-network Wi-Fi with automatic provisioning-AP fallback and embedded
  HTTP control UI;
- onboard RGB status LED: blue for the provisioning AP and green for a connected
  home network;
- assisted serial-servo commissioning and joint calibration, persisted in NVS;
- assembly/calibration interlock: motion, effects and remote control remain
  locked until all nine servos are assigned and all joints have valid limits;
- 300 ms drive-command watchdog and local slew limiting;
- browser OTA update with actuator interlock, redundant slots and boot rollback;
- web-managed choreography catalog with SD upload, CRC, play/delete controls and
  persistent controller-chord automations (hold, cooldown, release-to-rearm and
  optional neutral-stick gating);
- 4 MB factory recovery image plus two 4 MB OTA images;
- separate LittleFS areas for UI/settings and 16 MB of future local audio.

The optional Pi Zero 2 W video feed, ESP-NOW remote, and BLE Stadia controller
are defined as expansion boundaries in `docs/ARCHITECTURE.md`; they are not
required for the baseline controller.

## Build

Use ESP-IDF 5.5 or later:

```powershell
idf.py set-target esp32s3
idf.py build
```

The repository defaults already select 32 MB Octal flash, OPI/DTR mode and
16 MB Octal PSRAM for the **N32R16V** module. Do not flash this build to the old
N16R8 board without changing both `sdkconfig.defaults` and `partitions.csv`.

After the first flash, connect to Wi-Fi `WALL-E-Reborn` using password
`walle-reborn`, then open `http://192.168.4.1`. Under **Wi-Fi settings**, save
the home-network SSID and password. WALL-E restarts and joins that network; its
assigned IP is printed on USB serial. If the saved network is unavailable after
the bounded connection attempts, the `WALL-E-Reborn` setup access point returns
automatically at `http://192.168.4.1`. **Forget network** clears the saved
credentials and restarts directly into the setup AP.

Subsequent updates can be installed from **Firmware update** in that page.
Select the application `.bin`, keep the robot powered and do not close the
page until it reports success. Tracks stop and all motion/calibration commands
are locked while flash is written. ESP-IDF validates the image before selecting
it; if the next boot cannot initialize the essential robot services, the
bootloader rolls back to the previous valid image.

On an erased device the firmware enters commissioning mode and sends no
broadcast motion. Connect one factory-ID 1 servo at a time, scan, assign a
unique ID starting at 2 and bind it to a movement. After a final scan shows no
ID 1 and all nine roles are assigned, calibration unlocks. Centering is an
explicit action. Then move one joint at a time, capture and save MIN, REST and
MAX, and press **Enable WALL-E**. The
profile is kept in NVS and restored after reboot. Opening calibration again
locks normal motion until calibration is finished.

## Before connecting mechanisms

1. Leave motor power and servo power disconnected.
2. Verify all configured GPIOs against the exact ESP32-S3 board.
3. Commission every bus servo through the web UI, one new ID-1 servo at a time.
4. Use explicit center commands before attaching each horn.
5. Test the physical STOP and 300 ms watchdog with wheels off the floor.
6. Install horns/linkages, capture all nine joint limits in the calibration UI,
   then enable normal control.

## Tests

- `tests/target`: executable unit and integration tests for the production C
  modules, designed to run on an ESP32 target or the ESP-IDF QEMU ESP32 model.
- the normal `idf.py build` is the firmware integration build for ESP32-S3.

See `docs/TESTING.md` for exact commands and recorded results.
See `docs/OTA.md` for first-flash, web-update and recovery instructions.
See `docs/CHOREOGRAPHY.md` for the profile schema, controller bindings and the
dual-S3 playback safety boundary.
