# Bill of materials

Quantities cover the complete requested architecture. Items marked **TBD after
measurement** cannot safely be selected before motor stall current and servo
voltage are known.

## Core and control

| Qty | Item | Baseline choice | Selection note |
|---:|---|---|---|
| 1 | Main controller | **Waveshare ESP32-S3-DEV-KIT-N32R16V, SKU 32837, without header** | Selector option `2-N32R16V`; ESP32-S3-WROOM-2, 32 MB Octal flash + 16 MB Octal PSRAM |
| 1 | Serial-servo interface | **Waveshare Bus Servo Adapter (A), SKU 25514** | UART mode; provides the half-duplex bus interface and USB servo setup, but does not regulate servo voltage |
| 1 | Front display | **Waveshare 1.69inch LCD Module, SKU 24382, 240x280, ST7789V2, SPI** | Selected for the 24x33 mm visible window; PCB clearance verified in CAD, mounting detail still to print |
| 5 used (20 purchased) | Illuminated 6-pin 6x6x7.2 mm momentary switches | Amazon lot, EUR 10.69 / 20 | Top: REC, PLAY, STOP, SOLAR; front: Engineering. Verify switch pins, LED polarity and forward voltage before wiring |
| 1 | Media controller | **Existing Waveshare ESP32-S3 AI Smart media board** | ESP32-S3 board with TF/microSD, microphones, audio output/amplifier path and reliable UART to the motion S3; exact SKU/revision to record from the board/order page |
| 5 | LED current resistors | Start with 1 kohm from the driving motion/media S3 GPIO | Tune only after measuring LED polarity, forward voltage and brightness |
| 1 | Speaker | 4 ohm, 3 W starting candidate | Verify impedance and power against the ESP32-S3-AUDIO-Board onboard amplifier before connection |
| 1 | microSD | 8-32 GB, FAT32 | Installed in the ESP32-S3-AUDIO-Board; recordings, effects and choreography profiles |

## Actuation

| Qty | Item | Candidate | Electrical note |
|---:|---|---|---|
| 2 | Arm servos | **Waveshare SC09 baseline trial** | External bearings carry radial/axial load; SC15 is fallback only if measured dynamic margin is insufficient |
| 1 | Neck-tilt servo | **Waveshare SC15** | Base of neck; serial bus, mechanically adapted linkage |
| 1 | Head-tilt servo | **Waveshare SC09** | Additional joint inside neck; this is the eighth required SC09 |
| 1 | Head-pan servo | **Waveshare SC09 baseline trial** | Separately supported shaft with radial + thrust bearing; minimize inertia and avoid structural load on servo |
| 2 | Eye servos | Waveshare SC09 | 4-6 V, 2.3 kg.cm stall, 1 A stall each |
| 2 | Eyebrow servos | Waveshare SC09 | Same 6 V bus as the eyes |
| 2 | Track motors | **TBD after measurement** | Record nominal voltage, no-load current, stall current, encoder availability |
| 1 | Brushed driver | Dual H-bridge >= 1.5x measured stall current/channel | Cytron MDD10A-class if currents justify it; avoid L298N voltage loss |

Servo references:

- SC09: https://www.waveshare.com/wiki/SC09_Servo
- SC15: https://www.waveshare.com/wiki/SC15_Servo
- Bus Servo Adapter (A): https://docs.waveshare.com/Bus_Servo_Adapter_A
- Bus Servo Adapter (A) FAQ (5 A board limit, no regulator): https://docs.waveshare.com/Bus_Servo_Adapter_A/FAQ
- published bus circuit: https://files.waveshare.com/wiki/CF35-12/Bus_servo_control_circuit.pdf
- selected S3 board: https://www.waveshare.com/esp32-s3.htm
- 1.69inch LCD: https://www.waveshare.com/1.69inch-lcd-module.htm

## Power and protection

| Qty | Item | Minimum requirement |
|---:|---|---|
| 1 | **3S battery pack** | 12.6 V fully charged; exact pack/BMS/charger to record once selected |
| 1 | 3S charger / balance charger | Use the pack's intended charger; no improvised onboard charging |
| 1 | Main fuse + holder | Sized above normal peak, below wire/connector safe current |
| 1 | Main power switch | Existing Pololu MP: exact model and current rating must be confirmed for the 3S source before wiring; SOLAR uses external-button pads |
| 1 | Physical battery disconnect / e-stop | Interrupts actuator power, not merely a GPIO command |
| 1 | Protected regulated branch for SC15 | 7.2 V nominal, locally fused; common DATA and ground with SC09 bus |
| 1 | 6.0 V high-current buck for eight SC09 | >=9 A continuous starting point, 12 A preferred, with measured transient margin |
| 1 | 5.0 V buck | >=3 A without Pi, >=5 A with Pi and camera |
| 1 | Battery divider | 100k high side, 33k low side, 100 nF at ADC, 1% or better; nominal ratio 0.248; calibrate ratio against a meter |
| 3+ | Bulk capacitors | 1000-2200 uF at servo rail entry; 470-1000 uF near the 5 V/audio rail |
| several | 100 nF ceramics | At every logic board/transceiver supply |

The no-scale baseline uses eight SC09 servos on one regulated 6 V rail. Their
published 1 A stall current each makes 8 A a theoretical locked-rotor sum, so
the regulator, fuse, connector and harness need real transient margin; do not
route that current through the 5 A Bus Servo Adapter traces. Use fused power
injection close to the servo branches. The SC15 neck-tilt servo uses its own
regulated positive branch while sharing DATA and ground.

The fixed baseline is 3S; ensure the existing brushed motor backend accepts
12.6 V. Keep a 7.2 V branch for SC15 and a >=9 A 6 V regulator for SC09. The
Waveshare mini DC buck modules accept 9-36 V and are limited to 3 A peak; on
3S they can power the SC15 branch, but multiple SC09s still need a properly
rated centralized 6 V regulator or separate fused injection branches. Do not
buy a replacement track driver until motor stall current has been measured.

## Wiring and mechanical integration

| Qty | Item | Note |
|---:|---|---|
| 10+ | Waveshare/Feetech compatible 5264-3P leads | Nine servos plus injection/service spare; confirm polarity before power |
| 1 | Bus/power-injection harness | Common DATA/GND and regulated 6 V SC09 rail; provision an isolated positive branch only for an SC15 fallback |
| 1 set | Locking high-current connectors | XT30/XT60/JST-VH class according to measured current |
| 1 set | Silicone wire | Gauge selected from fused current and run length |
| 1 | Star-ground/power distribution PCB | Separate motor, servo, logic return paths |
| 1 per major joint | Bearings/bushings | Servo output shaft must not be the structural bearing |
| as needed | Ferrites/LC filtering | Audio and radio noise mitigation after measurement |

## Optional

| Qty | Item | Purpose |
|---:|---|---|
| 1 | Raspberry Pi Zero 2 W | Independent video streaming only; official board is 65x30 mm |
| 1 | Pi camera + Zero camera cable | Video feed |
| 1 | ESP32-C3/S3 + controls + battery | ESP-NOW handheld remote |
| 1 | nRF24L01+ module | Alternative remote transport; reserve SPI header first |
| 1 | Google Stadia controller | BLE input after controller Bluetooth conversion/mode is confirmed |

Pi Zero 2 W reference: https://www.raspberrypi.com/products/raspberry-pi-zero-2-w/

## Explicitly not required

| Item | Reason |
|---|---|
| Waveshare Servo Driver with ESP32 | Would replace the selected S3 and fragment the firmware architecture |
| MCP23017 | The audio board already includes a TCA9555 GPIO expander; direct dual-S3 allocation remains sufficient unless the final pin map proves otherwise |
| Custom SN74LVC1G125/126 servo transceiver | Replaced by the Bus Servo Adapter (A); keep the reference circuit only as a repair/custom-PCB option |
| Bluetooth Classic controller board | Stadia Bluetooth mode uses BLE, supported directly by ESP32-S3 |
| Brushless/ESC firmware variant | One maintained S3 image targets the existing brushed motors and H-bridge |

## Controller purchase configuration

Order the board as **2-N32R16V** and **without header**. Do not substitute the
N16R8 variant: the firmware partition table and memory bus are deliberately
configured for 32 MB Octal flash and 16 MB Octal PSRAM. The 32 MB map contains
a 4 MB recovery image, two 4 MB OTA slots, about 1.9 MB general LittleFS and a
dedicated 16 MB LittleFS audio partition.

On this N32R16V board, GPIO35/36/37 are unavailable and GPIO47/48 belong to the
1.8 V flash/PSRAM power domain. Do not use GPIO47/48 as ordinary 3.3 V outputs
without level translation. The current pin map avoids all five pins.

## Cost tracking

Actual purchases, allocated per-robot cost and minimum repurchase cost are
tracked separately in [COST_LEDGER.md](COST_LEDGER.md). This prevents spare
parts and supplier pack sizes from distorting the replicable BOM.
