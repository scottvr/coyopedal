# LAFVIN kit and MOMIX: sprint and backlog

The working list for this fork's board-and-interface work. `NOTES.md` is the knowledge log
(what we learned and why); this file is what we do next. Branch: `lavfin-kit-notes`.

## Cadence

- A sprint is one hardware session. Before it: read "Status" in `NOTES.md`, then "Current
  sprint" here. After it: move finished items to "Done", write what was learned into
  `NOTES.md`, pull the next items up from the backlog, commit.
- Host-side work (tests, parsers, docs, upstream branches) can happen any time; hardware items
  wait for the bench.
- Work that lives outside this repo (the Geastack forks) is a **track** in the table below:
  one row per upstream change, with its fork branch, what it has run on, and its PR. That
  table is the only place that state is kept.
- A task handed to a subagent gets a **brief** first: goal, the files it may touch, how it is
  tested, and what "done" looks like. The brief lives under the track or sprint item it serves;
  the agent reads `NOTES.md` for background and writes findings back there.
- Build: `IDF_TOOLS_PATH=/Volumes/idf6/.espressif-idf6 npx gea build --board lafvin`. Flash a
  built image without rebuilding: `npx gea flash --board lafvin --no-build --monitor`. Never
  run two builds against one build directory; a flash reads from it too. Run `npm test` and
  `npm run check` after source changes.

## Current sprint: the carrier's display, and the Wi-Fi tail

Goal: the pedal draws on the kit's 2.8" panel and takes touch, through composed-target
support contributed upstream; meanwhile the maintenance link works on the main network.

Wi-Fi tail (one bench visit):

1. Main network with the WPA3 build: `npm run remote:configure -- --ssid ATT33mamnd --force`,
   build, flash `--no-build`, hold BOOT 1.5 s. Expect `wifi:connected with` within a second of
   `assoc -> run`. Then `amoled_remote.py discover`, `status`, one `ota` round trip.
2. `effects-profile` over the API: per-effect cost on this board (both cores sit at 87%).
3. When the 1/4" to 1/8" cable arrives: loopback glitch count and the drift experiment
   (NOTES: "MOMIX drift", "Per-device timing"). Keep the input below full scale.

Display (tracks B and C below, then the bench):

4. Hardware mod, in this order: lift dev-board pins 0, 19, 20; jumper the panel's MOSI and DC
   to two camera pins with the ribbon unplugged (GPIO 16 and 17, say). Keep GPIO 3 and 46 for
   the ES8311 stopgap until track A lands. The carrier routes no backlight or reset GPIO to the
   panel: BL is always on, RST follows EN.
5. Point `package.json` at the fork builds of `@geastack/cli`, `targets` and `chips`, add
   `chips.display` (spi, st7789, 240x320, BGR, start at 40 MHz) and `chips.touch` (ft6336, no
   INT, no reset) to `boards/esp32-s3-lafvin-multimedia-2.8.json`, set the board's
   `cssDevicePixelRatio` to 1 (320x240 logical; the UI needs 205 rows).
6. First light on the panel: orientation, colour order, inversion, then touch. Raise the SPI
   clock only after it is right at 40 MHz.
7. Write results into `NOTES.md`; PRs go up only after the panel has run.

## Upstream tracks

Repositories: github.com/geastack/cli, geastack/targets, geastack/core (packages/chips). All
GPL-3.0, one author, releases most days, no CONTRIBUTING file. State as of 2026-10-12.

| Track | Change                                                                                                                                                                                                                                         | Repos               | Fork branch | Ran on hardware | PR  |
| ----- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------- | ----------- | --------------- | --- |
| A     | Gate the ES8311 binding on a composed board's audio chip (`GEA_CUSTOM_TARGET_HAS_AUDIO`, fall back to `audio_runtime_stub.cpp`); one-line `GEA_BOARD_HAS_POWER` for `s3-devkit`                                                                | cli, targets        | not started | no              |     |
| B     | SPI panel for composed targets: composer accepts `interface: "spi"` (st7789 first), emits the ST7789 display struct and defines; base CMake includes the sticks3 pipeline; pipeline gains colour order, inversion and optional backlight/reset | cli, targets, chips | not started | no              |     |
| C     | FT6336 touch adapter in the chips catalog; confirm the touch gate in the base CMake admits it (the binding already reads `board::touch` and polls without INT)                                                                                 | chips, targets      | not started | no              |     |
| D     | Codec-less I2S DAC binding for the PCM5100 (I2S half of the ES8311 binding without the codec-dev calls); only if the kit's speakers become the monitor path                                                                                    | targets, chips      | not started | no              |     |
| E     | Ask whether the LAFVIN target belongs in `@geastack/targets` once B and C are in                                                                                                                                                               | targets             |             |                 |     |

Briefs for B and C are written when the forks exist; the design is in `NOTES.md`, "Display
plan".

## Backlog

### Pedal firmware

- UAC1 streaming fixes that the loopback test exposes (expected: rate adaptation against the
  async capture, see "MOMIX drift" and "Per-device timing" in NOTES).
- Loopback glitch counter as an `AUDIO TRY` option (NOTES: "Per-device timing", step 3). Can
  be written and host-tested before the cable arrives.
- Quirk table keyed by VID:PID; iRig HD 2 profile retirement (plan in NOTES; needs the author's
  HD 2 dump and a switch to route the HD 2 through the generic path).
- `MOMIX BACKUP` / `MOMIX FLASH` maintenance commands (design in NOTES: "Planned: flashing the
  MOMIX from the pedal"). Prerequisite: the manual breakout + jl-uboot-tool route proven once.
- Hide the speaker control when the board's codec init fails (verify the binding already does).
- Remove the ES8311 stopgap from the LAFVIN definition once track A lands.
- Runtime Wi-Fi provisioning over Improv Serial (NOTES: "Wi-Fi findings"), own branch:
  credentials to NVS, compiled header as fallback, the API token stays compiled until the
  device can generate and reveal one.

### Hardware

- MOMIX manual flash: USB-C breakout, D+/D- on two GPIOs of a spare dev board for the 0x16EF
  key wiggle, VBUS and ground from the Mac, backup with jl-uboot-tool, then Rob's
  `flash/jl_692x.bin`. Only needed if the MOMIX headphone jack becomes the pedal's output.
- Choose the pedal's audio output on the kit: the PCM5100 module (track D) or an external DAC.
- Decide whether the camera stays once two of its pins carry the panel.

### Upstream coyopedal (after hardware results)

- Outreach to the author (sequence in NOTES: "iRig HD 2 profile retirement"), then the UAC1 PR
  with a hardware-testing statement, README board table, `docs/USB_AUDIO.md`, skill updates.
- WPA3 commit `e6dc162` goes in the same conversation; its message is written to be the PR text.

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
- 2026-10-10: first boot on the kit's dev board over the CH343 port (USB serial registered on
  the `lafvin` alias); the stock MOMIX enumerates, UAC2 declines, UAC1 adopts; the live
  configuration descriptor is byte-identical to the fixture (215 bytes); streaming clean at
  48 kHz (cap = play every 5 s window, 0 errors, 0 missed blocks, both cores at 87% of the
  1333 us block). Sprint "first light" items 1 to 6 closed; latency judged by ear.
- 2026-10-11: Wi-Fi diagnosed: reason 15 on the WPA2/WPA3 transition router with a correct
  passphrase; the pedal's PMF flag is a no-op on IDF 6 and the Gea base turns SAE off. WPA3
  enabled in the app's sdkconfig (`e6dc162`), +47 KB. Factory asset packers made idempotent so a
  no-op build no longer relinks (`29ee4f9`). The gateway's guest network refuses the pedal at
  authentication (reason 2) under both WPA2-only and transition settings; abandoned.
- 2026-10-12: upstream Geastack surveyed for the display path (NOTES: "Display plan"); tracks
  A to E defined.
