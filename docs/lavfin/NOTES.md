# LAFVIN ESP32-S3 Multimedia Kit and JOYO MOMIX CAB: working notes

Running notes from the board investigation started on 2026-10-08. Nothing here is
upstream-ready; it is what we learned, where it came from, and what we decided, so it is not
lost. Photos of the actual board revision live outside the repo in `~/Downloads/lavfin/`
(`IMG_4706.jpeg` is the carrier pinout table, `IMG_4707.jpeg` the board layout). They were in
this folder for one day, and the Gea asset packer swept them into the firmware image: it
embeds every image file it finds under the project, not only the ones the UI references, so the
app image grew by 5.4 MB and overflowed its 8 MB OTA slot. Never keep images in the tree that
are not meant for the device.

## Status

- The kit can host the pedal, but only after a small reversible hardware mod (below), because
  the carrier board uses the ESP32-S3's native USB pins for the display and a battery divider.
- Milestone 1, the headless composed target, builds (see below) and has not run on hardware
  yet. The screen, touch and speaker need upstream Geastack work before they can be used.
- The working list is `BACKLOG.md` beside this file: current sprint, backlog, done log.
- The audio interface is a JOYO MOMIX CAB, a UAC1 device. Its stock firmware was dumped live on
  2026-10-09 and the pedal's generic UAC1 path adopts it unchanged; Rob Murru's custom firmware
  is only needed when the MOMIX headphone jack is the pedal's output (it removes the always-on
  cab-simmed monitor). Generic UAC1 discovery now exists in the pedal (`src/native/drivers/uac1.hpp`, wired into
  `usb_audio.cpp` as the fallback when UAC2 discovery declines a device), with a host test against
  the MOMIX fixture in `tests/usb/`. Streaming on hardware is still to be done. Measured cost: the
  driver object grows by 12.7 KB of code (46.5 KB to 59.3 KB of `.text`), no change to data, and
  discovery allocates only on the setup task. The firmware builds and links against ESP-IDF 6.0.2:
  3,068,336 bytes, 63% of the 8 MB OTA slot free, no warnings in the pedal's sources.

## The kit

- LAFVIN ESP32-S3 Multimedia Kit: a carrier board with a socketed ESP32-S3-WROOM-1 N16R8 dev
  board (16 MB flash, 8 MB octal PSRAM). The dev board has its own USB-OTG and USB-UART
  (CH343) USB-C ports, a BOOT button and the camera FPC connector.
- Carrier peripherals: 2.8" 240 x 320 SPI LCD module (vendor docs say ILI9341V, the shipped
  TFT_eSPI setup uses the ST7789 driver with BGR order at 80 MHz; the module header is
  MOSI SCK CS DC BL RST INT SDA SCL VCC GND), FT6336U capacitive touch, MAX3010x heart-rate
  sensor, microSD on SDMMC 1-bit, an audio module with a PCM5100 I2S DAC and a PAM8403 class-D
  amplifier (header SCK BCK DIN LCK VCC GND; no I2C, no codec registers; the vendor code treats
  it like a MAX98357A), stereo speakers, WS2812 LED, buzzer, 6 to 12 V DC jack with power
  switch. The heart-rate sensor, the audio module and the camera are all removable or
  ignorable for the pedal.
- Programming is through the CH343 UART port. The vendor's own examples use GPIO 19 and 20 as
  ordinary GPIOs, which is why the native USB port is unusable while the dev board is seated.

### Pin map (carrier silkscreen, confirmed by the vendor code)

The silkscreen table itself, in the socket's physical order, is in `pinouts.md`; this is the same
information grouped by function.

| Function                  | GPIOs                                                                           |
| ------------------------- | ------------------------------------------------------------------------------- |
| LCD SPI                   | MOSI 20, SCK 21, CS 47, DC 0                                                    |
| Touch, heart-rate I2C     | SCL 1, SDA 2                                                                    |
| microSD (SDMMC 1-bit)     | CLK 39, CMD 38, D0 40                                                           |
| I2S DAC/amp               | BCK 42, LCK 14, DIN 41                                                          |
| Camera (on the dev board) | SIOD 4, SIOC 5, VSYNC 6, HREF 7, XCLK 15, PCLK 13, Y2-Y9 11 9 8 10 12 18 17 16  |
| Other                     | WS2812 48, buzzer 45, ADC_BAT 19, UART 43/44, PSRAM 35-37                       |
| Free                      | 3 and 46 (strapping pins, normal I/O); camera pins when the ribbon is unplugged |

The ESP32-S3 has no input-only GPIOs (checked in ESP-IDF 5.5.4 `soc_caps.h`), so 3 and 46 are
valid outputs.

### Why the native USB matters

The pedal takes audio in and out through a USB Audio Class interface on the S3's native
USB-OTG controller, which only exists on GPIO 19 (D-) and 20 (D+). On the carrier those pins
are ADC_BAT and TFT_MOSI, and GPIO 0 (BOOT, the pedal's footswitch) is TFT_DC.

### Planned hardware mod

1. Lift dev-board pins 19 and 20 out of the carrier socket (bend, do not cut carrier traces,
   so no stubs stay on the USB data lines). The battery ADC is lost; the pedal never reads it.
2. Lift pin 0 as well, so the BOOT button stays the pedal's footswitch (hard-coded as GPIO 0 in
   `src/native/main/main.cpp`, with an interrupt input, and the touch UI has no bypass control).
3. Jumper the display module's MOSI and DC pads to free dev-board header pins (GPIO 3 and 46,
   or camera pins with the ribbon unplugged). The panel already runs through the GPIO matrix,
   so any free pin works.
4. USB: either the dev board's OTG port (check that it supplies 5 V out; many of these boards
   have an IN/OUT solder jumper on the OTG VBUS), or wire the interface's D+, D-, 5 V and GND
   straight to the header at GPIO 20, GPIO 19, 5 V and GND. The MOMIX draws 50 mA.
5. Power the kit from the DC jack so display and LED switching noise stays off the rail that
   feeds the interface's preamp.

## Boards on hand (2026-10-09)

"AMOLED" in the pedal's docs means the Waveshare ESP32-S3-Touch-AMOLED-2.06, the upstream
reference board (alias `amoled`), which we do not have. Our test vehicle is the kit's own dev
board running the headless `lafvin` composed target (the generic `s3-devkit` target cannot
build for this app until Geastack gates its ES8311 binding; see "What is missing upstream"):

- **Development vehicle: an ESP32-S3-WROOM-1 N16R8 dev board**, the kit's own or a spare,
  flashed with the `lafvin` target. Either one of the spare
  N16R8 boards, or the LAFVIN kit's own dev board pulled out of its carrier socket, which is
  reversible and frees its OTG port with no mod at all. Console and flashing go through the
  CH343 UART port; the pedal is configured over the maintenance API (Wi-Fi credentials via
  `npm run remote:configure`). It needs 8 MB PSRAM, so only R8 modules qualify; the bare
  WROOM-1 modules in the drawer count only if they are N16R8 and sit on a carrier with USB.
- **VBUS on the devkit:** a bare board does not feed the host port by itself. Use a Y-cable or
  powered hub, or check the board for an IN-OUT solder jumper on the OTG VBUS. The MOMIX needs
  50 mA.
- **Freenove ESP32-S3 (in the drawing robot):** leave it there. If it is the N8R8 variant its
  8 MB flash would need its own partition layout anyway.
- **LilyGo T-Deck, T-Embed, T-Display S3:** all ST7789-class panels, so they share the LAFVIN
  kit's Geastack gap (no SPI panel in the composable base). The T-Deck's GT911 touch is already
  composable, so it becomes a candidate once an SPI panel driver exists upstream. Only the
  T-Display S3 Long has a ready Geastack target. The T-Dongle-S3 is a USB plug, not a host
  port; unsuitable.
- Staging that keeps the fun: stage 1 is the kit's dev board alone as `s3-devkit` for the
  UAC1 and MOMIX work; stage 2 is the carrier mod and the composed target.

## Geastack side

Installed: cli 0.1.89, targets 0.1.87, chips 0.1.4, core 0.1.35, engine 0.1.10, host 0.1.12.
Latest published on 2026-10-08: targets 0.1.98, chips 0.1.6. Neither ships a target for this
kit. There is no Geastack checkout on this machine.

### App-local composed targets (what exists)

- `@geastack/targets` README, "App-local custom targets": a board alias in `.gea/boards.json`
  can point at a JSON definition (`targetDefinition`, path relative to that file) that
  `extends` the composable `esp32-s3` base and names chips from `@geastack/chips/catalog.json`.
  The CLI (`src/boards/custom-target.mjs`) generates `board.h` and `target.cmake` under the
  build directory; the base project is `targets/esp32-s3-touch-amoled-2.06`.
- `.gea/` is gitignored here, so the definition should live in the repo (for example
  `boards/esp32-s3-lafvin-multimedia-2.8.json`) and the alias references it as
  `../boards/...`.
- Definition fields: `flashSize`, `partitions` (no data payloads; the app manifest's
  `partitionsByTarget` wins anyway), `console` (uart 0 here, since the OTG port is the host),
  `psram` (octal, 80), `buses.i2c`, `chips.display` (qspi or rgb only), `chips.touch`
  (needs an esp32-idf adapter in the installed catalog: ft3168, gt911), `chips.power`,
  `chips.imu`, `chips.audio` (es8311 only), `chips.expander`, `storage.microSD`
  (`sdmmc-1bit`), `controls.launcherButton`.
- Generated macros: `GEA_BOARD_HAS_I2C/DISPLAY/TOUCH/POWER/IMU/AUDIO/MICROSD/LAUNCHER_BUTTON/
EXPANDER`, `GEA_EMBEDDED_NO_DISPLAY=1` when there is neither display nor canvas,
  `GEA_BOARD_HAS_POWER` also reaches app sources.
- The base CMake adds the ES8311 binding and `GEA_BOARD_HAS_SPEAKER=1` whenever the app's
  compiler analysis reports an `audio` binding, regardless of the board; the headless build
  must be checked against that.

### What is missing upstream

- Display: the composable base accepts only QSPI and parallel RGB panels. The SPI ST7789
  pipeline exists in directory targets (`esp32-s3-m5stack-sticks3/main/display.cpp`, reused by
  `esp32-s3-touch-lcd-1.54` with `GEA_ST7789_*` compile definitions) but `extends` only allows
  `esp32-s3`.
- Touch: `targets/esp32/chip_bindings/touch/ft6336.cpp` exists in targets 0.1.98, but the
  chips catalog (0.1.4 and 0.1.6) lists no esp32-idf adapter for ft6336, and the base CMake only
  admits ft3168 for composed targets.
- Speaker: only the ES8311 binding exists. The kit's PCM5100 needs no configuration at all,
  just an I2S transmit stream (16 or 24 bit, 48 kHz; it runs without SCK from its internal
  PLL), so a codec-less I2S DAC driver upstream is small.
- Per AGENTS.md none of this can live in this repo (no board CMakeLists, partition CSV or
  display driver here).

### Repo changes for milestone 1 (headless): done 2026-10-09

- `boards/esp32-s3-lafvin-multimedia-2.8.json`: extends esp32-s3, 16MB, octal PSRAM 80 MHz,
  console uart 0 at 115200, SDMMC clk 39 cmd 38 data0 40, launcher button GPIO 0 active low, no
  display, no touch, no power; the PCM5100 declared as an ES8311 as a build stopgap.
- `package.json`: the 16 MB partition table (two 4 MB OTA slots) and `GEA_BOARD_HAS_POWER: 0`
  for the LAFVIN and devkit targets.
- `src/native/storage/sd_models.cpp` reads the slot pins from every board's `board.h`
  (`GPIO_NUM_NC` means no slot) instead of hard-coding the AMOLED's 2/1/3.
- Still open: the `.gea/boards.json` alias needs the board's USB serial; README and skill
  updates wait until something has run on hardware. For the display later: native 240 x 320,
  landscape 320 x 240, `cssDevicePixelRatio` 1 (1.5 would leave 160 logical pixels of height
  against the UI's 205 minimum).

## USB audio in the pedal (facts from `src/native/drivers/usb_audio.cpp`, `uac2.hpp`)

- The ESP32-S3 host is Full Speed only. High Speed devices still enumerate: they attach at
  Full Speed and only switch up after a chirp handshake the S3 never answers. What differs per
  device is whether its Full Speed configuration offers 48 kHz duplex alternates.
- Generic discovery is UAC2 only (interface protocol 0x20, AudioControl header bcdADC 2.00).
  Tested at Full Speed: XTONE Pro and iRig HD X (both UAC2). The only UAC1 path is the iRig
  HD 2 profile, keyed by VID 0x1963 PID 0x0033, with hard-coded interfaces, alternates and
  endpoints, 3-byte capture frames and 4-byte playback frames, and an unconditional endpoint
  SET_CUR sampling-frequency request.
- Transport limits for any class: 48 kHz only, duplex only, one shared clock, 1 ms packets with
  at least 48 frames, capture at most 96 frames per packet, FIFO fit.
- Generic UAC1 plan: parse AC header 1.00, terminals, feature units, AS general (PCM), Format
  Type I (channels, subframe, bits, discrete rate list including 48000), isochronous endpoints
  with sync type and optional feedback, CS endpoint attributes; reuse pairing and capacity
  checks; send the endpoint rate request only when the CS endpoint reports that control;
  unmute feature units with the existing request shape; configure the same codec objects the
  UAC2 path uses and reuse its ring and pacing logic (frame cadence when there is no feedback).
  Test with a fixture in `tests/usb/` under the sanitizers before touching hardware.

### Other interfaces on hand

| Interface           | Assessment                                                                                                   |
| ------------------- | ------------------------------------------------------------------------------------------------------------ |
| Scarlett (2nd gen+) | UAC2 class compliant; works today if its Full Speed config has 48 kHz duplex                                 |
| MOTU 828 (model?)   | 828es/828x have class-compliant modes, mk3 Hybrid needs a driver; 28 channels do not fit a Full Speed packet |
| VoiceLive Rack      | 2-channel 16-bit 44.1/48 kHz, works on iPad, so class compliant and almost certainly UAC1                    |
| Rig Kontrol 3       | Proprietary NI driver, not class compliant                                                                   |
| GR-55               | Roland driver for audio, 44.1 kHz; refused by the 48 kHz rule                                                |

## JOYO MOMIX CAB

- Hardware: JieLi AC6926A4 SoC (512 KiB flash), RS721 input buffer, 1/4" guitar in, 3.5 mm
  headphone out, 48 kHz 16-bit, bus powered 5 V 50 mA. Direct monitoring is done in the DSP
  firmware and cannot be removed in hardware.
- Rob Murru's custom firmware removes the always-on monitor, leaving a plain USB audio
  interface. Caveats: a software rate adapter in `ladc.c` skips one 32-sample DMA block every
  10,000 ISR calls (about 6.7 s) to absorb roughly 100 ppm crystal drift against the host, so
  capture has a periodic 0.67 ms discontinuity by construction; only `DAC_SAMPLE_CON = 0` was
  stable; buttons unmapped; bMaxPower hex-patched to 100 mA.
- Flashing: JL USB Updater dongle (or Arduino emulation of it) to enter UBOOT mode, then the
  repo's Windows `download.bat` (`isd_download.exe` with `0x077A.key`), which needs the "free
  key patch" from the USMI forum. kagaimiq's `jl-uboot-tool` (Python) can read and write flash
  once the chip is in UBOOT mode. Back up the stock flash first; the repo carries a stock dump.

### USB descriptors (extracted from the firmware images, same USB library in stock and custom)

- Device: VID 0xE5B7, PID 0x0811, USB 1.10 (Full Speed), bcdDevice 1.00, EP0 64 bytes.
- Audio function, UAC1 (bcdADC 1.00): interface 1 AudioControl; streaming interfaces 2 and 3.
- Playback: input terminal 1 (USB streaming, 2 ch) -> feature unit 9 (mute master, volume L/R)
  -> output terminal 3 (speaker). Interface 2 alt 1: PCM 2 ch, 2-byte subframe, 16 bit, one
  discrete rate 48000. Endpoint 0x02 OUT, isochronous adaptive, 192 bytes, 1 ms. CS endpoint
  attributes 0 (no sampling-frequency control). No feedback endpoint.
- Capture: input terminal 2 (microphone, 1 ch) -> feature unit 10 (mute and volume, master)
  -> output terminal 4 (USB streaming). Interface 3 alt 1: PCM 2 ch, 16 bit, 48000. Endpoint
  0x83 IN, isochronous adaptive, 192 bytes, 1 ms. CS endpoint attributes 0.
- The configuration header is built at run time by the closed library; the fixture below
  assumes 3 interfaces, total 193 bytes, 100 mA, interfaces numbered from 1. Confirm with a
  live dump from a Mac or from the pedal's maintenance log on the devkit before trusting it.
- Fit with the pedal: 48 frames per packet both ways, 1 ms, same device clock, 48 kHz only.
  Needs generic UAC1 discovery; must not send the sampling-frequency request; playback at the
  USB frame cadence; capture uses channel 0 of the stereo 16-bit stream.

Fixture (now `tests/usb/momix-cab-custom-fw-configuration.bin`; config header synthesized,
function bytes verbatim from `flash/sdk.app` 0x021aa0 to 0x021b58 of the custom firmware
repository):

```text
09 02 c1 00 03 01 00 80 32 09 04 01 00 00 01 01 00 00 0a 24 01 00 01 47 00 02 02 03 0c 24
02 01 01 01 00 02 03 00 00 00 0a 24 06 09 01 01 01 02 02 00 09 24 03 03 01 03 00 09 00 0c
24 02 02 01 02 00 01 01 00 00 00 09 24 06 0a 02 01 03 00 00 09 24 03 04 01 01 00 0a 00 09
04 02 00 00 01 02 00 00 09 04 02 01 01 01 02 00 00 07 24 01 01 01 01 00 0b 24 02 01 02 02
10 01 80 bb 00 09 05 02 09 c0 00 01 00 00 07 25 01 00 00 00 00 09 04 03 00 00 01 02 00 00
09 04 03 01 01 01 02 00 00 07 24 01 04 01 01 00 0b 24 02 01 02 02 10 01 80 bb 00 09 05 83
09 c0 00 01 00 00 07 25 01 00 00 00 00
```

## Design notes (proposed, 2026-10-08)

- Generic UAC1 discovery, descriptor-driven like the UAC2 path, is the right shape. Flash is
  not the constraint; the pedal's scarce resources are internal SRAM and IRAM on the audio hot
  path, and discovery runs once on the USB setup task (6 KiB stack) with small vectors that
  land in PSRAM under the sdkconfig heap policy. The iRig HD 2 profile is a historical
  exception ("predates UAC2 support"), not a pattern to extend.
- Device quirks, when a device needs one, go in a small table in code keyed by VID:PID, as
  `snd-usb-audio` does, not in a config file on the SD card. SD mounting is deliberately
  deferred until after the NAM banks are placed, to keep driver allocations out of internal
  SRAM, and USB discovery should not depend on it. Quirks are behaviours, which belong next to
  the code they alter and under the host tests.
- Keep the HD 2 profile until its descriptors are captured and the generic path is shown to
  handle it; we cannot test that device ourselves.
- Streaming: a generic UAC1 device should run through the same codec objects, ring targets and
  pacing as UAC2, with the class version only choosing the control-request dialect. The
  existing `Uac1` branches encode HD 2 specifics (3-byte capture, 4-byte playback frames).
- Tests (done): `tests/usb/uac1_test.cpp` runs against both MOMIX fixtures from `scripts/test.sh`.
- Conventions when this goes upstream: CONTRIBUTING.md (`npm run check`, `npm test`, warning-free
  firmware build, clang-format 22.1.8, a hardware-testing statement in the PR), the CLA bot
  comment, and fold these notes into the canonical places: README board table,
  `docs/USB_AUDIO.md` (Discovery, Tested interfaces, Known limits) and
  `skills/building-pedal-firmware/SKILL.md`. Until then everything stays in `docs/lavfin/`.

### MOMIX drift: what a proper fix looks like

- Correction to an earlier guess: `DAC_SAMPLE_CON` in the JieLi SDK is only the DAC DMA block
  size exponent (`DAC_SAMPLE_POINT = 32 << DAC_SAMPLE_CON`), not a sync mode.
- The host cannot fix the drift: the IN endpoint is fixed at 48 frames per packet, so no rate
  information crosses the bus, and the device clock is free-running.
- The SDK has the building blocks for a device-side fix: a USB SOF interrupt
  (`IRQ_USB_SOF_IDX`), the closed library's own sync layer (`usb_mic_syn_init`,
  `usb_spk_syn_init`, `ADC_USB_SYN_OPS` and `DAC_USB_SYN_OPS` with `set_frameSize`, in
  `lib_usb_syn.a`), and a sample-rate converter (`src_api.h`). A glitch-free version would
  steer an SRC ratio in the mic path from SOF count versus ADC sample count instead of
  dropping a block every 6.7 s. That belongs in Rob Murru's repository, not here.

## Per-device timing: a standard method (proposed)

The host cannot resample. Both cores run near their budget, so there will never be an
asynchronous sample-rate converter in the pedal; 48 kHz and one shared clock stay hard rules.
What the host can do is choose how playback is paced, measure, and document. The method:

1. **One pacing ladder, fed from descriptors.** Feedback endpoint when the device has one;
   otherwise capture-paced playback for an asynchronous OUT endpoint on a shared clock (the
   XTONE case); otherwise the USB frame cadence of 48 frames per ms (adaptive and synchronous
   endpoints, and the iRig HD 2). This exists for UAC2 in `usb_audio.cpp`; generic UAC1 must
   feed the same ladder from its own descriptors rather than add a path.
2. **Read the clock from the heartbeat.** When IN packets vary in size, `cap=+N` per 5 s
   gives the device clock against the S3: `N - 240000` frames per 5 s, so `+240024` is
   +100 ppm. Harmless when playback is capture-paced or feedback-driven. For a fixed-size IN
   endpoint such as the MOMIX, N is always exactly 240000 and the device's internal drop or
   duplicate is invisible to the host. Document this interpretation in `docs/USB_AUDIO.md`.
3. **Loopback glitch test** (to add, as an `AUDIO TRY` option): play a tone, loop the
   interface's output back to its input with a cable, and count discontinuities in the
   captured sine with the two-sample recurrence `x[n] = 2 cos(w) x[n-1] - x[n-2]`. Report
   glitches per minute and the implied drift (a 32-sample drop every T seconds is
   `32 / (48000 T)` ppm). It is the only host-side way to see device-internal drop or
   duplicate, and it needs no gear beyond a TRS cable. Runs with the DSP bypassed.
4. **Quirk table, code, keyed by VID:PID**, with timing fields only when a device proves it
   needs them: pacing override and ring depth. Nothing else is in the host's power.
5. **Device-side contract for hackable interfaces**, documented: expose the real clock
   (variable-size IN packets or a feedback endpoint), or lock the audio clock to USB SOF;
   48 kHz; 1 ms packets of at least 48 frames; one clock for both directions. Verify with
   steps 2 and 3.

Upstream shape: three separate PRs. Generic UAC1 with fixtures and tests; the clocking
section, heartbeat interpretation and loopback test; the LAFVIN target. Each one only after
it works well on the hardware here.

## Milestone 1 status (2026-10-09, late)

- `boards/esp32-s3-lafvin-multimedia-2.8.json` is the composed definition: headless, 16 MB,
  octal PSRAM, console on UART 0, I2C on 2/1, SDMMC on 39/38/40, BOOT as the launcher button,
  and the PCM5100 declared as an ES8311 on its real I2S pins as a stopgap so Geastack's audio
  binding compiles (its codec init fails over I2C at run time and the speaker stays hidden;
  MCLK, DIN and the amplifier-enable pin the CLI insists on go to GPIO 3, 46 and 15, which
  nothing uses). `package.json` carries its 16 MB partition table and
  `GEA_BOARD_HAS_POWER: 0`, as it now does for the devkit.
- It builds against ESP-IDF 6.0.2: 1,949,024 bytes, 54% of the 4 MB OTA slot free. The alias is
  machine-local in `.gea/boards.json`:

  ```json
  "lafvin": {
    "target": "esp32-s3-lafvin-multimedia-2.8",
    "targetDefinition": "../boards/esp32-s3-lafvin-multimedia-2.8.json",
    "adapter": "esp32-idf",
    "appPlatform": "esp32",
    "flashSize": "16MB"
  }
  ```

  Add `transports.usbSerial.serial` (from `npx gea boards discover`) before flashing, and
  `IDF_TOOLS_PATH=/Volumes/idf6/.espressif-idf6` in the shell. `npx gea build --board lafvin`
  writes `build/pedalboard.bin`; `npx gea flash --board lafvin --monitor` provisions over the
  CH343 port.

- `src/native/storage/sd_models.cpp` now reads the slot pins from every board's `board.h` and
  declines to mount when a board declares none, instead of special-casing two targets and
  hard-coding the AMOLED's pins. Verified by building the AMOLED target (same 3,068,336-byte
  image) and the LAFVIN target.
- Not yet verified on hardware: boot, the stopgap audio failing gracefully, SD, USB host with
  the MOMIX. The dev board must be out of the carrier, or pins 19, 20 and 0 lifted, before any
  USB test.

## The MOMIX CAB, stock firmware, dumped live (2026-10-09)

`momix-cab-stock-fw-usb-dump.txt` is the parse; the raw 215-byte configuration is
`tests/usb/momix-cab-stock-fw-configuration.bin` and `npm test` runs UAC1 discovery against it.
What the factory firmware presents, against Rob's custom firmware:

|          | Stock                                                                     | Rob's firmware                           |
| -------- | ------------------------------------------------------------------------- | ---------------------------------------- |
| Layout   | IAD, control on interface 0, streams on 1 and 2                           | no IAD, control on 1, streams on 2 and 3 |
| Playback | stereo 16-bit 48 kHz, adaptive, EP 0x03, 192 bytes                        | same, EP 0x02                            |
| Capture  | **mono** 16-bit 48 kHz, **asynchronous**, EP 0x83, 96 bytes               | stereo, adaptive, 192 bytes              |
| Units    | selector units 7 and 8 between the feature units and the output terminals | none                                     |
| Power    | 50 mA declared                                                            | 100 mA                                   |

So the stock device already works with the pedal's generic UAC1 path: discovery adopts it (the
UAC2 module declines it, the fallback takes it), the capture is channel 0 through the generic
frame loop, and `fullSpeed48k` accepts 96-byte mono packets. Nothing in the descriptors
explains the always-on monitor; that is firmware behaviour, which is what Rob's build changes.
His firmware also makes the capture stereo, which the pedal does not need. The custom-firmware
fixture remains a synthesized header around bytes from his image; the stock one is real.

## Planned: flashing the MOMIX from the pedal (decided 2026-10-09)

The pedal should be able to put Rob Murru's firmware onto a stock MOMIX CAB by itself, from
maintenance mode, with the interface plugged into the pedal's own host port. Design sketch:

1. **Prerequisite:** the manual route must have worked first on at least one unit: a USB-C
   breakout, the MOMIX's D+ and D- on two GPIOs of any spare dev board for the key wiggle, VBUS
   and ground from the Mac so the chip never loses power, then `jl-uboot-tool` on the Mac to
   read the backup and write Rob's `flash/jl_692x.bin`. The pedal version only reproduces what
   that proved.
2. **Entering UBOOT:** the JieLi updater dongle is a bit-bang of the 16-bit key 0x16EF on D+ and
   D- (clock on one, data on the other) until the chip acknowledges by pulling both low for one
   to two milliseconds; see kagaimiq's `how-to-enter-uboot.md` and Christian Kramer's AC690X
   repository for the timing. On the pedal this means detaching the USB PHY, driving GPIO 19
   and 20 as plain outputs for the wiggle, then handing them back and installing the host stack.
   Maintenance boots do not install the USB host today, so this runs as its own bounded boot,
   the way `AUDIO TRY` does.
3. **Talking UBOOT:** the chip enumerates as a mass-storage-style device (product strings
   `UBOOT`, `UDISK` or `DEVICE` in kagaimiq's tool). The protocol is bulk-only transport: a
   command block out, a data phase, a status block in, with the vendor SCSI opcodes
   `jl-uboot-tool` sends. A few hundred lines on top of the pedal's existing host stack; no
   general SCSI or filesystem code.
4. **Safety:** read all 512 KiB to the SD card first, refuse to proceed if the read fails or
   the SD card is absent, write, read back and compare, and say plainly in the command's reply
   what is about to be written into whose hardware. Test on two units before anyone else runs
   it.
5. **The image:** from the SD card (`/sdcard/momix/`), never embedded in the pedal's flash. It
   contains JieLi's closed SDK libraries and Rob's repository has no license; fetch it at setup
   time with his blessing, as the existing capture fetcher does for models.
6. **Command:** a maintenance-mode `MOMIX FLASH` next to `AUDIO TRY` in `remote_service.cpp`,
   with `MOMIX BACKUP` as the read-only half for people who only want the dump.

## Build environment on this Mac (2026-10-09)

- Intel Mac; Homebrew in `/usr/local`, MacPorts in `/opt/local` (the user is moving to
  MacPorts; `/opt/homebrew` is a symlink to `/opt/local`). Python for host scripts is the
  python.org 3.14 venv at `~/.venv` (pyusb, pyserial). `DYLD_FALLBACK_LIBRARY_PATH`, if set,
  must repeat dyld's defaults (`~/lib:/usr/local/lib:/usr/lib`) or Homebrew libraries vanish.
- ESP-IDF 6.0.2 lives on the APFS sparse bundle `/Volumes/idf6`: tree at
  `/Volumes/idf6/esp/esp-idf` (also reachable as `~/esp/esp-idf`), tools root
  `/Volumes/idf6/.espressif-idf6`. The CLI finds the tree by itself but needs
  `IDF_TOOLS_PATH=/Volumes/idf6/.espressif-idf6` exported; `~/.zshrc` sets it. `install.sh
esp32s3` leaves out `riscv32-esp-elf-gdb`, which `idf_tools.py export` demands, so
  `idf_tools.py install riscv32-esp-elf-gdb` is part of setup. Keep IDF trees and `.gea/build`
  off the boot volume; it filled up once. The repos themselves are moving to another APFS
  loopback volume; a symlink at `~/coyopedal` keeps paths stable.
- Format tooling: `npm run format:setup` installs clang-format 22.1.8, ruff and cmake-format
  into `build/format-tools`; `npm run format:check` fails loudly if that venv is missing.
- Machine-local aliases in the gitignored `.gea/boards.json`: `amoled` (build only),
  `s3-devkit` (build only, currently fails upstream), `lafvin` (needs `transports.usbSerial.serial`
  from `npx gea boards discover` before flashing). `npx gea flash --board lafvin --monitor`
  provisions over the CH343 port; a board already in maintenance mode updates over Wi-Fi with
  `tools/esp32/amoled_remote.py ota`.

## Upstream Geastack blockers found on the way (2026-10-09)

1. `GEA_BOARD_HAS_POWER` redefined on composed boards: the generated `board.h` defines it 0
   while the app's root defines pass 1 for the AMOLED default, and `-Werror` stops the build.
   Worked around in `package.json` with a per-target override (a one-line PR for upstream's
   own `s3-devkit`, which has not built since the internal speaker landed).
2. The ES8311 binding is compiled for every audio-capable app whether or not the board has a
   codec, and the composer never emits a has-audio variable. A codec-less composed board cannot
   build. Fix: the CLI's `custom-target.mjs` emits `GEA_CUSTOM_TARGET_HAS_AUDIO`, and the 2.06
   target's CMake falls back to `audio_runtime_stub.cpp` with `GEA_BOARD_HAS_SPEAKER=0` when a
   custom target has no audio chip. Our stopgap (PCM5100 declared as ES8311) goes away then.
3. Already known: no SPI panel driver (ST7789/ILI9341) in the composable base, no FT6336
   touch adapter, no codec-less I2S DAC driver.

## iRig HD 2 profile retirement (plan)

No public descriptor dump of the HD 2 (1963:0033) exists and we have no unit. Plan: a quirk
table keyed by VID:PID whose HD 2 entry reproduces the profile's choices exactly (capture
interface 2 alt 2, playback interface 1 alt 1, rate request sent unconditionally and allowed to
fail, playback trimmed to the startup fill), then delete the fixed-profile code.
Descriptor-level equivalence needs a dump from an HD 2 owner as a fixture; streaming equivalence
needs the device, which upstream has. Sequence agreed 2026-10-09: before any PR, write to the
author about the work, ask for a descriptor dump of his HD 2 (the branch's own maintenance log
dumps it, or `lsusb -v`), and ask whether he wants to test, with a plain account of the
streaming and latency state: the generic path uses the UAC2 ring targets and frame-cadence
pacing where the profile trimmed to the startup fill. For that test the fork needs a switch that
routes the HD 2 through the generic path instead of the VID:PID profile. Offer the PR only if
that comes back good.

## Sources

Kit and vendor code:

- https://lafvin-esp32-s3-multimedia-kit.readthedocs.io/en/latest/ (tutorials; SD, touch,
  battery, camera, I2S pages under `Arduino/`)
- https://github.com/lafvintech/LAFVIN-ESP32-S3-Multimedia-Kit (vendor examples and libraries;
  `lib/TFT_eSPI.zip` holds the display `User_Setup.h`)
- https://www.amazon.com/LAFVIN-Multimedia-Development-Capacitive-Bluetooth/dp/B0H41YD3NN
- https://lafvintech.com/pages/tutorials3

Geastack:

- https://www.npmjs.com/package/@geastack/targets (README: app-local custom targets, chips)
- https://www.npmjs.com/package/@geastack/chips (`catalog.json`)
- https://www.npmjs.com/package/@geastack/cli (`src/boards/custom-target.mjs`,
  `docs/NATIVE-BUILD-CONFIG.md`)

USB and interfaces:

- https://industrialmonitordirect.com/blogs/knowledgebase/resolving-usb-speed-mis-detection-full-vs-high-speed-on-linux
- https://www.embeddedrelated.com/showthread/comp.arch.embedded/213-1.php
- https://userguides.focusrite.com/hc/en-gb/articles/23031328747026-Scarlett-2i2-3rd-Gen-specifications
- https://www.zzounds.com/item--MOT828ES and
  https://vintageking.com/motu-828-mk3-hybrid-firewire-usb-2-0-audio-interface
- https://www.soundonsound.com/reviews/tc-helicon-voicelive-rack and
  https://musictribecommunity.powerappsportals.com/en-us/knowledgebase/article/2018
- https://craiganderton.org/how-to-use-nis-rig-kontrol-3-in-windows-10/
- https://www.manualslib.com/manual/933289/Roland-Gr-55.html?page=66 and
  https://support.roland.com/hc/en-us/articles/115005257466-FA-06-FA-07-FA-08-USB-Driver-Mode

JOYO MOMIX CAB:

- https://www.joyoaudio.co.uk/joyo-momix-cab-guitar-audio-interface-pocket-internal-recording-sound-card
- https://robmurru.wordpress.com/2026/01/24/joyo-momix-cab-reverse-engineering-part-1/
- https://robmurru.wordpress.com/2026/02/24/joyo-momix-cab-reverse-engineering-part-2/
- https://robmurru.wordpress.com/2026/05/31/joyo-momix-cab-reverse-engineering-part-3/
- https://github.com/robmurru/Joyo-MOMIX-CAB-Custom-Firmware (and issues/1)
- https://github.com/kagaimiq/jl-uboot-tool and
  https://github.com/kagaimiq/jl-uboot-tool/blob/main/docs/how-to-enter-uboot.md
- https://kagaimiq.github.io/jielie/chips/chip-marks.html
- https://github.com/christian-kramer/JieLi-AC690X-Familiarization
- https://usmi.mybb.ru/viewtopic.php?id=4 (key patch)
- https://sonicmetric.com/joyo-momix-cab-guitar-audio-interface-review-the-ultimate-mobile-solution-for-guitarists-and-streamers/
- https://www.aliexpress.com/s/wiki-ssr/article/joyo-momix-cab-usb-audio-interface
