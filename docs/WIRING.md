# Wiring and bring-up

## GPIO map

This map targets the Waveshare ESP32-S3-DEV-KIT-N32R16V. Every value is configurable with
`idf.py menuconfig -> WALL-E Reborn`.

| Signal | GPIO |
|---|---:|
| Bus servo TX | 17 |
| Bus servo RX | 18 |
| GPIO16 | Unused; adapter A handles direction internally |
| ESP32-S3 AI Smart media UART TX / RX | 14 / 13 |
| LCD ST7789 SPI SCLK / MOSI (DIN) | 8 / 9 |
| LCD CS / DC / RST / BL | 15 / 41 / 42 / 2 |
| Engineering button / LED | 4 / 5 |
| Direct safety STOP input | 6 |
| SOLAR power-alive LED | 21 |
| Left PWM / IN1 / IN2 | 10 / 11 / 12 |
| Right PWM / IN1 / IN2 | 7 / 39 / 40 |
| Onboard WS2812 network status | 38 |
| Battery ADC | ADC1 channel 0 / GPIO1 |

ADC1 channel 0 is GPIO1 on ESP32-S3. GPIO35/36/37 are
unavailable on the N32R16V board. GPIO47/48 have 1.8 V output levels and are
also intentionally unused.

GPIO38 is hard-wired to the board's onboard WS2812B. It is blue while the
provisioning AP is active, green after the S3 receives a station IP, and off
during a station connection/retry transition. Right-track PWM was moved to the
free, header-accessible GPIO7 so the two functions never drive the same pin.

The selected Waveshare 1.69-inch ST7789V2 display is connected in four-wire
write-only SPI mode. Connect `VCC -> 3V3`, `GND -> GND`, `DIN -> GPIO9`,
`CLK -> GPIO8`, `CS -> GPIO15`, `DC -> GPIO41`, `RST -> GPIO42` and
`BL -> GPIO2`. MISO is not required. The former SSD1306/I2C allocation and
driver have been removed.

The mechanical carrier holds the LCD in its native 240x280 portrait
orientation, so firmware applies neither XY swap nor mirroring. The ESP32-S3
and serial-servo adapter are mounted vertically on opposite side walls, with
their USB-C connectors facing the service openings. Keep cable loops clear of
the tracks and leave enough straight clearance to insert both USB plugs after
the body is assembled.

In normal Play mode the LCD is monochrome yellow-green: it shows
`SOLAR CHARGE LEVEL`, WALL-E's solar symbol in the upper-left area, and a
ten-cell battery bar near the bottom. Active cells remain yellow at every
charge level; Play mode never changes them to green or red.
The front button beside the display toggles Engineering mode with a normal
press. It is read directly on GPIO4. Calibration, OTA and error states override
Play mode and show the engineering screen automatically. The top PLAY button
keeps its sound-effect action. STOP remains a direct ESP32 input with no I2C or
display function in its safety path.

The LEDs are split by functional owner: the ESP32-S3 AI Smart media board drives REC, PLAY and media STOP;
S3 drives SOLAR and Engineering. Connect each output through a
1-kohm resistor to its LED anode and connect the cathode to ground. A high GPIO
turns that LED on. At 3.3 V this is only about 0.3-1.3 mA for an LED forward
voltage between 3.0 V and 2.0 V, so no ULN2003 or discrete driver is required.
Bench-test LED pinout, polarity, forward voltage and brightness because the
marketplace listing does not establish whether a resistor is built in. If a
particular colour is too dim, select its resistor from measured current rather
than adding a driver pre-emptively.

The SOLAR switch contact connects directly to the external pushbutton pads of
the verified existing Pololu MP module so it can start the robot while the ESP32 is unpowered. Its LED
is electrically separate: firmware turns it on after startup as the switched
power-alive indicator. Do not route Pololu VIN or load current through the
switch's signal contacts.

Default S3 LED policy: SOLAR is on whenever firmware is running; Engineering
indicates that screen or a forced calibration, OTA or error screen. The ESP32-S3 AI Smart media board owns the
recording/playback LED policy.

An empty battery (`0%`) or missing/invalid ADC feedback forces the red
Engineering warning screen. ADC read failures are kept distinct from a valid
zero-percent calculation internally, even though both intentionally share the
same operator-facing warning screen.

## Serial bus

- UART: 1,000,000 baud, 8-N-1.
- DATA is half-duplex; the purchased adapter A handles direction. Connect host TX/RX in UART mode and common ground. No ESP TXEN wire.
- Servo power is injected directly with wire and connectors rated for stall
  current. It does not pass through the ESP32 board.
- A factory servo uses ID 1. Connect exactly one new ID-1 servo at a time;
  already commissioned IDs may remain connected.

Suggested IDs 2..10 are assigned in the web UI to: left arm, right arm, neck
tilt (SC15), head tilt, head pan, left/right eyes and left/right eyebrows. The
mapping, rather than a hard-coded order, is stored in NVS. ID zero means
unassigned and is never sent on the bus.

## Safe commissioning order

1. Power the S3 only from USB; verify logs and web UI.
2. Verify battery ADC with a bench supply and multimeter.
3. Connect the ST7789V2 display and verify the blue boot band, orange
   calibration band, battery glyph and red disarmed bar. Connect and verify
   the audio/media S3 UART separately.
4. Power one bus servo from a current-limited bench supply.
5. In **Commissioning**, scan, change factory ID 1 to the proposed ID starting
   at 2, assign the movement, save, then rescan before adding the next servo.
6. After all nine assignments, perform one final scan. Calibration unlocks only
   at 9/9 assignments and with no ID 1 detected.
7. Test brushed H-bridge signals without motors.
8. Test motors with tracks lifted and a low current limit.
9. Verify loss of HTTP heartbeats latches STOP within 300 ms plus scheduler latency. Rearm is always explicit.
10. Use the explicit **Center selected movement** action to fit horns and
    linkages; keep
    mechanisms unloaded and be ready to remove servo power.
11. In the web calibration section, move one joint at a time and capture safe
    MIN, REST and MAX. Save all nine joints and inspect reported position/load.
12. Press **Enable WALL-E** only with tracks lifted, then verify rest positions,
    direction and endpoints. Normal motion and remote inputs remain locked
    until this step succeeds.
