# LAFVIN ESP32-S3 Multimedia Kit and JOYO MOMIX CAB: working notes

Running notes from the board investigation started on 2026-10-08. Nothing here is
upstream-ready; it is what we learned, where it came from, and what we decided, so it is not
lost. Photos of the actual board revision are beside this file (`IMG_4706.jpeg` is the carrier
pinout table, `IMG_4707.jpeg` the board layout).

## Status

- The kit can host the pedal, but only after a small reversible hardware mod (below), because
  the carrier board uses the ESP32-S3's native USB pins for the display and a battery divider.
- First firmware milestone is a headless composed target. The screen, touch and speaker need
  upstream Geastack work before they can be used.
- The audio interface will be a JOYO MOMIX CAB running Rob Murru's custom firmware. It is a
  UAC1 device, so the pedal needs generic UAC1 discovery first; its descriptors are fully known
  (below) and a fixture can be built from them.

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
reference board (alias `amoled`), which we do not have. Our test vehicle is the headless
`s3-devkit` target instead:

- **Development vehicle: an ESP32-S3-WROOM-1 N16R8 dev board** as `s3-devkit`
  (`esp32-s3-devkit-n16r8`, a supported and measured configuration). Either one of the spare
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

### Repo changes for milestone 1 (headless)

- Composed definition: extends esp32-s3, 16MB, octal PSRAM 80 MHz, console uart 0 at 115200,
  SDMMC clk 39 cmd 38 data0 40, launcher button GPIO 0 active low, no display, no touch, no
  power, no audio chip for now.
- `package.json`: `partitionsByTarget` entry with the 16 MB layout (two 4 MB OTA slots, as the
  other 16 MB boards), `boards.<id>` with `GEA_BOARD_HAS_POWER: 0`. Later, for the display:
  native 240 x 320, landscape 320 x 240, `cssDevicePixelRatio` 1 (1.5 would leave 160 logical
  pixels of height against the UI's 205 minimum).
- `src/native/storage/sd_models.cpp`: also honor `GEA_BOARD_HAS_MICROSD` (the composer's
  macro) so the board's SD pins are used instead of the AMOLED's hard-coded 2/1/3.
- `.gea/boards.json` alias (machine-local), README and skill updates.

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

Fixture candidate (config header synthesized, function bytes verbatim from
`flash/sdk.app` 0x021aa0 to 0x021b58 of the custom firmware repository):

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
- Tests: add the MOMIX fixture and a `uac1_test.cpp` under `tests/usb/`, wired into
  `scripts/test.sh`, before any device work.
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

## When the MOMIX is back

1. Stock firmware first, on the Mac: `python3 docs/lavfin/dump_usb_config.py --out
docs/lavfin/momix-cab-stock-configuration.bin` (needs `brew install libusb` and
   `python3 -m pip install --user pyusb`). Compare with the synthesized fixture: interface
   numbering, total length, bMaxPower (stock is expected to say 400 mA).
2. Plug it into the devkit pedal in audio mode, then read the maintenance log: discovery should
   fail with a UAC2 error and dump the raw bytes. Confirm they match the Mac dump.
3. Back up the flash with jl-uboot-tool before flashing Rob's build, then dump again.
4. Drift experiment on the device: compare the noise-burst period with capture only against
   full duplex with the OUT stream running. A change means the DAC adapts to the OUT stream
   and the ADC shares its clock, which would make the pedal's fixed cadence lock the device.
5. After generic UAC1 lands: `AUDIO TRY` with the MOMIX, then the loopback test.

## Before the next session

- Pick up the MOMIX. Dump the stock descriptors on the Mac with the script (needs libusb
  and pyusb). Keep the stock dump beside the custom-firmware fixture.
- Choose the devkit vehicle: the kit's dev board pulled from its socket, or a spare N16R8.
  Check which USB-C port is the native OTG one and whether it can supply 5 V out (IN-OUT
  jumper); otherwise plan on a Y-cable or powered hub.
- Wi-Fi credentials for the maintenance API: `npm run remote:configure -- --ssid YOUR_WIFI`
  writes the gitignored `src/native/services/remote_config.h`. The headless devkit has no
  other way to be configured.
- Toolchain: the Geastack CLI runs through `npx gea` from `node_modules`; ESP-IDF 5.5.4 is
  under `~/.espressif/v5.5.4`. Run `npx gea doctor` and `npm run format:setup`, and put
  clang-format 22.1.8 on the PATH for the C++ format check.
- MOMIX flashing plan: dongle or Arduino emulation, Windows for `isd_download.exe`, USMI key
  patch, stock flash backup first.
- Decisions for the carrier mod: lift pin 0 so BOOT stays the footswitch, which jumper
  targets to use for MOSI and D/C, whether the camera stays plugged in.

## Next steps

1. Dump the MOMIX's live descriptors (on the Mac, then on the devkit pedal via the maintenance
   log) and compare with the fixture above.
2. Implement generic UAC1 discovery in the pedal with that fixture; test on the devkit pedal.
3. Hardware mod on the kit; register the alias; build the headless composed target; verify
   the headless build still compiles with the internal-speaker code.
4. Upstream Geastack: SPI ST7789/ILI9341 panel for composed targets, FT6336 adapter, codec-less
   I2S DAC driver.

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
