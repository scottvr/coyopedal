# LAFVIN kit and MOMIX: sprint and backlog

The working list for this fork's board-and-interface work. `NOTES.md` is the knowledge log
(what we learned and why); this file is what we do next. Branch: `lavfin-kit-notes`.

## Cadence

- A sprint is one hardware session. Before it: read "Status" in `NOTES.md`, then "Current
  sprint" here. After it: move finished items to "Done", write what was learned into
  `NOTES.md`, pull the next items up from the backlog, commit.
- Host-side work (tests, parsers, docs, upstream notes) can happen any time; hardware items
  wait for the bench.
- Build before flashing: `IDF_TOOLS_PATH=/Volumes/idf6/.espressif-idf6 npx gea build --board
lafvin`. Run `npm test` and `npm run check` after source changes.

## Current sprint: first light on the kit's dev board

Goal: the pedal boots on the LAFVIN dev board and streams audio with the stock MOMIX.

1. Pull the dev board from the carrier (or lift pins 0, 19, 20). Done 2026-10-10: the OTG
   port supplies 5 V out (the MOMIX powers up in it while the board is fed from the UART
   port), so no Y-cable or hub is needed. Pin labels on the dev board match the carrier socket.
2. `npx gea boards discover`, add `transports.usbSerial.serial` to the `lafvin` alias in
   `.gea/boards.json`.
3. Wi-Fi for the maintenance API: `npm run remote:configure -- --ssid YOUR_WIFI` (writes the
   gitignored `remote_config.h`).
4. `npx gea flash --board lafvin --monitor`. Watch for: boot, the stopgap ES8311 init failing
   over I2C and being reported (not aborting), "no SD slot" absent (the kit has one), the SD
   mounting, maintenance mode reachable over Wi-Fi.
5. Plug in the stock MOMIX. In the log: UAC2 declines, UAC1 adopts (control 0, capture
   interface 2 mono async 96 B, playback interface 1 stereo adaptive 192 B); compare with
   `tests/usb/momix-cab-stock-fw-configuration.bin`.
6. `AUDIO TRY` with the MOMIX, then the loopback test. Confirm the capture is the clean DI, not
   the cab-simmed monitor mix. Listen on the pedal's output, not the MOMIX headphone jack.
7. Drift experiment: noise-burst period with capture only versus full duplex with the OUT
   stream running (NOTES: "MOMIX drift").
8. Write results into `NOTES.md`; update README board table and
   `skills/building-pedal-firmware/SKILL.md` only once something has run.

## Backlog

### Pedal firmware

- UAC1 streaming fixes that the first light exposes (expected: rate adaptation against the
  async capture, see "MOMIX drift" and "Per-device timing" in NOTES).
- Quirk table keyed by VID:PID; iRig HD 2 profile retirement (plan in NOTES; needs the author's
  HD 2 dump and a switch to route the HD 2 through the generic path).
- `MOMIX BACKUP` / `MOMIX FLASH` maintenance commands (design in NOTES: "Planned: flashing the
  MOMIX from the pedal"). Prerequisite: the manual breakout + jl-uboot-tool route proven once.
- Hide the speaker control when the board's codec init fails (verify the binding already does).
- Remove the ES8311 stopgap from the LAFVIN definition once Geastack gates the binding.

### Hardware

- Carrier mod: lift pins 0, 19, 20; jumper display MOSI and D/C to free pins (3 and 46 are
  always free; 4 to 13 and 15 to 18 with the camera unplugged). Decide whether the camera
  stays.
- MOMIX manual flash: USB-C breakout, D+/D- on two GPIOs of a spare dev board for the 0x16EF
  key wiggle, VBUS and ground from the Mac, backup with jl-uboot-tool, then Rob's
  `flash/jl_692x.bin`. Only needed if the MOMIX headphone jack becomes the pedal's output.
- Choose the pedal's audio output on the kit: the PCM5100 module (needs the upstream DAC
  driver) or an external DAC.

### Upstream Geastack (write to the author first)

- One-line `GEA_BOARD_HAS_POWER` override for `s3-devkit`.
- Gate the ES8311 binding on a composed board's audio chip (`GEA_CUSTOM_TARGET_HAS_AUDIO`,
  fall back to `audio_runtime_stub.cpp`).
- SPI panel driver (ST7789/ILI9341) for composed targets, FT6336 touch adapter, codec-less I2S
  DAC driver.
- Ask whether the LAFVIN target should live in `@geastack/targets` once it works.

### Upstream coyopedal (after hardware results)

- Outreach to the author (sequence in NOTES: "iRig HD 2 profile retirement"), then the UAC1 PR
  with a hardware-testing statement, README board table, `docs/USB_AUDIO.md`, skill updates.

### Docs and tooling

- Fold `docs/lavfin/` into the canonical docs when the work goes upstream.
- Rotate the API key that was in `~/.zshrc` (user's item).

## Done

- 2026-10-08: kit evaluated; pin map; decision for the headless-first route; generic UAC1
  discovery written and host-tested against the custom-firmware fixture.
- 2026-10-09: ESP-IDF 6.0.2 on `/Volumes/idf6`; AMOLED and LAFVIN targets build (3.07 MB and
  1.95 MB); photos removed from the tree (asset packer lesson); pinout transcribed; devkit
  power override; SD mount reads board pins; MOMIX stock firmware dumped live and added as a
  fixture with a test variant; libusb discovery fixed in the dump script and `.zshrc`;
  in-pedal MOMIX flashing designed.
