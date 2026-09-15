# CZ-101 Studio — Documentation

An emulation of the Casio CZ-101 synthesizer (1984) as a macOS application and plugin, built on the embedded MAME 0.289 from the Vintage Emulator Studio project. Personal, non-commercial use.

Period: September 2026. Platform: macOS on Apple Silicon (arm64).

## Overview

The goal was **one unit per instrument** instead of an emulator with dozens of machines: a program that runs only the CZ-101, receives MIDI, plays through the sound card and serves as a base for improving the interface and sound fidelity.

The work produced two sibling projects:

| Project | Folder | Role |
|---|---|---|
| CZ-101 Studio | `/Volumes/STORAGE/GITS/CZ101Studio` | Development version: Standalone, reads ROMs from a folder, panel artwork reloads without rebuilding. |
| CZ-101 Studio Personal | `/Volumes/STORAGE/GITS/CZ101Studio-Personal` | Playing version: ROMs, panel and scripts embedded in the binary; Standalone, AU and VST3; SysEx tone librarian; 13 RA-3 cartridges with 208 tones. |

The instrument emulation is entirely MAME's: Casio's original firmware runs on the emulated NEC uPD7810 CPU, sound comes from the emulated Casio uPD933 chip and the display is the emulated Hitachi HD44780 controller. The projects bridge MAME, macOS audio/MIDI and the user interface.

## Architecture

```
 MIDI (interface, DAW or panel keyboard)
        │
        ▼
 CZ101Processor (JUCE) ── filters clock/active sensing, pauses during librarian jobs
        │ sendMidiBytes
        ▼
 EmbeddedEmulatorEngine (MAME thread)
   ├─ cz101 driver: uPD7810 + Casio firmware, uPD933 (sound), HD44780 (LCD), RA-3
   ├─ MIDI IN port  ◄── byte queue
   ├─ MIDI OUT port ──► byte queue ──► MidiByteAssembler (host) or librarian
   ├─ audio (20 ms bursts) ──► queue ──► AudioJitterBuffer ──► sound card / DAW
   ├─ video (rendered layout) ──► MameDisplay (clickable panel)
   └─ NVRAM (internal memory) ──► file on exit / after writes / every 30 s
```

Key points:

- **MAME is not rebuilt.** The projects link the static libraries and the driver object (`cz101.o`) already compiled in the VES tree. The engine's driver list was reduced to `cz101`.
- **Embedded engine.** VES's `EmbeddedEmulatorEngine` was copied into each project and extended: MIDI output, on-demand NVRAM saving and an NVRAM save option.
- **Build.** CMake + Ninja + JUCE 8.0.13. Code that includes MAME headers is compiled in its own object library with exactly the same defines as `libemu.a`.

## Timeline

### 1. First unit: CZ-101 Studio

- New JUCE project with the VES engine, a driver list containing only `cz101` and persistent NVRAM.
- Links directly against the prebuilt MAME libraries (no MAME rebuild).
- First window with MAME's internal layout, MIDI lamp, level meter and on-screen keyboard (later removed on request).

### 2. Buffer crackle

- With a 512-sample buffer the sound crackled; at 64 it was clean. VES users reported the same symptom on the KVR Audio forum.
- Measured cause: MAME delivers audio in 50 Hz bursts (882 frames at 44.1 kHz every 20 ms) and the engine queue had no pre-roll. A 512-frame callback often found too little data and played silence.
- Fix: `AudioJitterBuffer` (see technical solutions). Measured at 512: from 4 underruns in 85 s to 0 in 90 s, 24–40 ms latency and no resampling.

### 3. macOS permissions and folders

- macOS asked for Documents access after every build (the ad-hoc signature changes) and MAME blocked reading the ROM until answered.
- JUCE's `userApplicationDataDirectory` is `~/Library` on macOS, not `Application Support`.
- Fix: data in `~/Library/Application Support/CZ101Studio`, ROMs outside Documents, and the ROM path is no longer restored from JUCE's saved state.

### 4. Panel

- Vector panel drawn by the user in Google Stitch (1600 × 500 SVG).
- `tools/make_artwork.py` renders the SVG with headless Google Chrome (3200 × 1000) and writes a MAME `default.lay` placing 51 clickable buttons, 49 keys, 35 LEDs and the emulated LCD on top.
- Refinements: overlapping text fixed, waveform icons redrawn and centered, labels moved below the blue rule, UP/DOWN words next to the pitch wheel removed (only the arrows remain) and window locked to the panel's proportions.

### 5. Personal version

- ROMs (`cz101.zip`, `hd44780.zip`), panel and MAME Lua scripts embedded with `juce_add_binary_data` and written to `~/Library/Application Support/CZ101StudioPersonal/runtime` on first run.
- Standalone, AU and VST3 formats; its own name and codes (`Wjst` / `Cz1p`).
- Instance guard: MAME is not re-entrant, so only one instance plays per process; others wait and take over when the first closes.
- Installation: AU copied to `~/Library/Audio/Plug-Ins/Components` (macOS ignores symlinked AUs); VST3 as a link to the external disk; ad-hoc signing of all bundles. The AU passes Apple's `auval`.

### 6. SysEx, MIDI OUT and internal memory

- The emulated CZ-101's MIDI output now reaches the host (it used to be discarded).
- In-plugin librarian using Casio's original handshake, because MIDI hosts drop the unterminated SysEx the protocol relies on.
- **Load .syx** and **Save .syx** buttons targeting "Sound area" or "INTERNAL 1–16".
- NVRAM written right after loading into INTERNAL, every 30 s and on exit.
- `cz101_sysex_test` tool validating everything against the real firmware.

### 7. Tone codec and .PAT sheet conversion

- `tools/czsyx.py`: decodes and encodes tones; rebuilds the 16 factory presets byte for byte from panel values.
- Several details in the public format documentation were wrong and were corrected against the firmware (table below).
- `tools/pat2syx.py`: converted 104 of 132 1980s/90s `.PAT` text sheets into `.syx`, with a report.

### 8. RA-3 cartridges

- The CZ-101 takes Casio's RA-3 RAM cartridge, holding 16 tones (CARTRIDGE 1–16). MAME emulates it as a 4 KB file.
- 13 cartridges built from the tone folders (208 tones), written by the CZ-101 firmware itself.
- **Cartridge** selector at the top of the window: swaps the cartridge while the instrument runs; the panel's CARTRIDGE button plays the selected one.
- **Load .syx** now offers CARTRIDGE 1–16, showing the tone name in each slot.

## Bugs and limitations fixed

### Compared with the CZ-101 running in Vintage Emulator Studio

| # | Problem in VES | Evidence in the VES code | Fix in CZ-101 Studio | Verification |
|---|---|---|---|---|
| 1 | Crackle with large buffers (512+) | Audio queue without pre-roll; MAME delivers 20 ms bursts | `AudioJitterBuffer` with pre-roll, adaptive margin and clock correction | 0 underruns in 90 s at 512 (was 4 in 85 s) |
| 2 | SysEx tones never reach the synth | `isSupportedMidiForPrototype` only accepts Note, CC, Program Change and Pitch Bend | All MIDI is passed to MAME except clock and active sensing | Tone sent and read back identical |
| 3 | The synth cannot answer over MIDI | Engine MIDI output port with an empty `write()` | Real output queue, delivered to the host or the librarian | 16 presets dumped through MIDI OUT |
| 4 | Internal memory lost on every restart | MAME run with `-nonvram_save` and a temporary NVRAM folder deleted at start | Persistent NVRAM, saved on exit, after internal writes and every 30 s | Tone in INTERNAL 1 preserved after restarting the engine |
| 5 | Fixed 10 s wait before audio and MIDI | `getBootElapsedMs() < 10000` | Released 2 s after the first video frame | Log: "ready" 2–3 s after start |

Also avoided in the Personal version: a boot hang when the `hd44780` device ROM is missing or when macOS blocks reading the ROM folder, since the ROMs are embedded.

### Compared with the original MAME 0.289

- **No CZ-101 driver bug was fixed.** The driver works well in MAME; what changed is the integration.
- **NVRAM protected against abnormal exits.** MAME only writes NVRAM on exit (`running_machine::nvram_save()` is private). The engine now writes it while running, with a copy of the same procedure using only the public NVRAM API.
- **Panel.** MAME's internal CZ-101 layout is schematic (rectangles and text); it was replaced with full artwork using the same input bindings.

### Platform problems solved

| Problem | Cause | Fix |
|---|---|---|
| Documents access prompt blocking boot | ROM read from a protected folder; ad-hoc signature changes every build | ROMs in Application Support / embedded |
| Data written to `~/Library/CZ101Studio` | JUCE's `userApplicationDataDirectory` is `~/Library` on macOS | Explicit `Application Support` path |
| AU invisible to hosts | The Audio Unit registrar ignores symlinked `.component` bundles | AU copied; VST3 stays a link |
| AU rejected by the registrar | Only the linker signed the binary; resources not sealed | `codesign --force --deep --sign -` on the bundles |
| External librarians fail | JUCE delivers unterminated SysEx via `handlePartialSysexMessage`, which hosts don't forward | Librarian inside the plugin |

## Technical solutions

### AudioJitterBuffer

Sits between MAME's audio queue and the sound card/DAW callback (`Source/AudioJitterBuffer.h`).

- Starts playing only when the queue holds one MAME burst + one device block + a safety margin.
- On an underrun it leaves one block silent, widens the margin (up to 80 ms) and re-primes.
- Every 4 s it checks the queue's lowest level; only when clearly off target does it nudge the playback ratio by ±500 ppm (0.9 cent) with Hermite interpolation. Otherwise samples are copied 1:1.
- The measured MAME and device clocks agree on average, so in practice the correction stays at 0 ppm.

### Panel (MAME artwork)

- `Resources/artwork-src/cz101-panel.svg`: the user's drawing, with the static LCD text and painted LEDs removed.
- `tools/make_artwork.py`: renders the SVG and writes `Resources/artwork/cz101/default.lay`, binding each control's coordinates to the driver inputs (`kc9`–`kc15`, keys `kc0`–`kc8`) and LEDs (`led_*`, `led_env*`, `led_tone*`).
- In the development project the artwork is read from the project folder: edit the SVG, run the script and press **Restart**.

### Tone librarian (SysEx)

Casio protocol (n = MIDI channel − 1; `pp` = program: 60 sound area, 20–2F INTERNAL 1–16):

```
Send request (CZ → computer)
  computer: F0 44 00 00 7n 10 pp     CZ: F0 44 00 00 7n 30
  computer: 7n 31                     CZ: <256 nibbles> F7
  computer: F7

Receive request (computer → CZ)
  computer: F0 44 00 00 7n 20 pp     CZ: F0 44 00 00 7n 30
  computer: <256 nibbles> F7          CZ: F7
```

- `Source/CzSysex.h` implements the protocol with no JUCE dependency.
- During a transfer the processor pauses host MIDI and diverts the CZ's output to the librarian (`AbstractFifo`), which runs on its own thread.
- The standard 264-byte `.syx` sent in one go is also accepted by the firmware (reply `F0 44 00 00 70 30 F7`).

### Corrections to the public tone format documentation

Reference: the "Casio CZ MIDI/SYSEX" page on youngmonkey.ca. Checked against the 16 presets read from the firmware and 100 real `.syx` files.

| Field | Documentation | Actual behaviour |
|---|---|---|
| Vibrato delay/rate/depth (3 bytes) | value, high byte, low byte | value, **low** byte, **high** byte |
| Vibrato delay 87 | `67 01 7F` | `57 01 7F` (typo) |
| DCA level | linear, `127 × level / 99` | `level + 28` (0 stays 0) |
| "Level coming down" bit (0x80 on rate) | when the level goes down | also when the step's target level is 0 |
| Detune fine | 30 → `1F` | `fine + fine ÷ 15` (30 → `20`) |
| Vibrato depth 0–31 | `v, 00, v+1` | confirmed by the firmware; some old editors wrote `v, v, 00` |

Confirmed waveform layout (2 bytes): high byte `[1st wave 3][2nd wave 3][2nd on][0]`, low byte `[6–8 selector 2][modulation 3][000]`.

### .PAT sheet conversion

- 132 text sheets typed by many authors, in at least eight layouts.
- The converter handles header rows, ASCII tables, key/value forms and stacked or side-by-side DCO/DCW/DCA blocks, with envelopes written as "99/85/s", "rate = 99, level = 21 (sus)", columns or horizontal rows.
- Each sheet is validated (0–99 ranges, up to 8 steps, sustain/end) and encoded with the verified codec.
- Result: 104 converted into `CZPATCHES/czpats/SYX convertidos`, with `RELATORIO.md`; 28 free-form sheets left for manual conversion. The originals were not modified.

### RA-3 cartridges

- **Written by the firmware.** `tools/cz101_cart_builder` boots the emulated CZ-101 with a blank cartridge, writes each tone over SysEx to CARTRIDGE n (program `40`–`4F`) and reads it back. On power-off MAME writes the cartridge RAM to the file. It then boots again from the file and reads all 16 slots once more. The cartridge's internal format is therefore exactly what a CZ-101 stores.
- **Building the set.** `tools/make_cartridges.py` splits the folders, runs the builder for each cartridge and writes a slot → name list for each one (the CZ-101 stores no tone names) plus `INDICE.md`.
- **In the app.** Cartridges are embedded and unpacked to `runtime/cartridges` only when missing, so tones written into them are never overwritten. The selected cartridge is kept in `settings.xml`, inserted at boot (`-cart`) and swapped at runtime through the engine's media change.
- **Persistence.** Swapping cartridges writes the previous one to its file (MAME unloads media before loading another). After loading a `.syx` into a CARTRIDGE slot the app re-inserts the same cartridge so the file is written immediately. A panel WRITE into the cartridge is saved when the cartridge is swapped or the app closes.

| Cartridge | Tones | Source |
|---|---|---|
| 01 CZ Pack 1 | 16 | Coffeeshopped CZ Pack 1 (first 16 in alphabetical order) |
| 02 RelaCZ | 16 | CZounds RelaCZ |
| 03–06 Italo DisCZo 1–4 | 64 | CZounds Italo DisCZo |
| 07–12 CZ Pats 1–6 | 96 | Converted `.PAT` sheets |
| 13 CZ Pats 7 | 12 | The remaining 8 `.PAT` sheets + Stiff Bass, Toad Bass, Wist Pad and Yay Afterhit from CZ Pack 1 |

## Folder layout (Personal version)

| Path | Contents |
|---|---|
| `CMakeLists.txt` | JUCE plugin (Standalone/AU/VST3), embedded data, `cz101_mame` library, test tool |
| `build.sh` | Build, ad-hoc signing and plugin installation |
| `Source/PluginProcessor.*` | Engine, MIDI, audio, librarian, NVRAM, instance guard |
| `Source/PluginEditor.*` | Clickable panel, meter, MIDI lamp, tone menus, window proportions |
| `Source/AudioJitterBuffer.h` | Audio buffer between MAME and the device |
| `Source/CzSysex.h` | Casio SysEx protocol |
| `Source/MidiByteAssembler.h` | Turns MIDI OUT bytes into messages |
| `Source/FrontendStubs.cpp` | MAME frontend stubs |
| `Engine/EmbeddedEmulatorEngine.*` | Adapted VES engine |
| `Embedded/roms/` | `cz101.zip`, `hd44780.zip` |
| `Resources/artwork-src/`, `Resources/artwork/` | Panel SVG and generated artwork |
| `Resources/plugins/` | MAME `boot.lua` and `layout` plugin |
| `tools/make_artwork.py` | Generates the panel artwork |
| `tools/czsyx.py`, `tools/czsyx_tables.py` | Tone codec |
| `tools/pat2syx.py` | `.PAT` sheet converter |
| `tools/cz101_sysex_test.cpp` | Protocol test against the firmware |
| `Embedded/cartridges/` | 13 RA-3 cartridges (`.bin`), tone lists (`.txt`) and `INDICE.md` |
| `tools/cz101_cart_builder.cpp` | Writes a cartridge through the firmware and verifies it |
| `tools/make_cartridges.py` | Builds the cartridge set from the tone folders |
| `tools/build_docs.py` | Generates the bilingual HTML page of this documentation |

## Building and using

```sh
cd /Volumes/STORAGE/GITS/CZ101Studio-Personal
./build.sh                                     # Standalone + AU + VST3, signs and installs
cmake --build build --target cz101_sysex_test  # test tool (optional)

python3 tools/make_artwork.py                  # after editing the panel SVG (then ./build.sh)
python3 tools/pat2syx.py PAT_FOLDER OUTPUT_FOLDER
python3 tools/czsyx.py dump TONE.syx

cmake --build build --target cz101_cart_builder
python3 tools/make_cartridges.py               # rebuilds the 13 cartridges (then ./build.sh)
```

Requirements: the VES tree with MAME 0.289 already built (`SUBTARGET=vesembedded`), JUCE 8.0.13, the static SDL3 from the VES build, Xcode (clang), CMake, Ninja and Google Chrome (artwork generation only).

Runtime data (Personal): `~/Library/Application Support/CZ101StudioPersonal/` (`runtime/`, `nvram/`); log at `~/Library/Logs/CZ101StudioPersonal.log`.

## Known limitations

- POWER, pitch bend and volume on the panel are artwork only (pitch bend works over MIDI).
- One CZ-101 instance per process (a MAME limitation).
- VST3: SysEx delivery depends on the DAW (Ableton Live doesn't pass SysEx to plugins). The built-in librarian doesn't depend on it.
- 28 free-form `.PAT` sheets were not converted automatically.
- MAME flags the HD44780 font ROM as an imperfect dump; it is only a warning.
- MAME delivers audio in 20 ms bursts, which sets the minimum latency (≈ 20–40 ms).

## Credits

### Emulation and third-party code

| Component | Authors | License | Use in this project |
|---|---|---|---|
| MAME 0.289 | MAMEdev and contributors | GPL-2.0-or-later (project); individual file licenses | All of the instrument emulation |
| Casio CZ-101 driver (`casio/cz101.cpp`) | Dirk Best, Devin Acker | BSD-3-Clause | Emulated machine |
| Casio uPD933 (`sound/upd933.cpp`) | Devin Acker | BSD-3-Clause | Sound chip |
| Casio RA-3 cartridge (`casio/ra3.cpp`) | Devin Acker | BSD-3-Clause | Driver device |
| Hitachi HD44780 (`video/hd44780.cpp`) | Sandro Ronco | BSD-3-Clause | LCD display |
| NEC uPD7810 (`cpu/upd7810`) | Juergen Buchmueller | BSD-3-Clause | CPU running the firmware |
| MIDI ports and images (`bus/midi`, `imagedev/midiin`) | R. Belmont, Aaron Giles | BSD-3-Clause | MIDI IN/OUT |
| Sound manager (`emu/sound.cpp`) | O. Galibert, Aaron Giles | BSD-3-Clause | Audio output |
| Internal layout `cz101.lay` | MAME contributors | CC0-1.0 | Input and LED map used as reference |
| `boot.lua` | Miodrag Milanovic | BSD-3-Clause | MAME Lua plugins |
| `layout` plugin (`init.lua`) | Carl | BSD-3-Clause | Layout scripts |
| Vintage Emulator Studio | Autodafe (github.com/antoniograzioli/VintageEmulatorStudio) | App: AGPL-3.0-only; MAME integration: BSD-3-Clause | Embedded engine, MAME patches, MAME and SDL3 builds |
| JUCE 8.0.13 | Raw Material Software (JUCE) | AGPLv3 or JUCE commercial licence | Plugin, audio, MIDI, interface |
| SDL 3 | Sam Lantinga and contributors | zlib | Dependency of MAME's OSD libraries |

### Firmware, documentation and tones

| Item | Authors | Use |
|---|---|---|
| CZ-101 firmware (ROMs) | Casio Computer Co., Ltd. | Run by the emulator; personal use, do not redistribute |
| HD44780 character ROM | Hitachi (dump from the MAME archive) | LCD font |
| "Casio CZ MIDI/SYSEX" | Text published on youngmonkey.ca (original author not named on the page) | Basis for the protocol and tone format |
| `.PAT` sheets | Miles Bader, Paul Boutin, Michael Schaeffer, Tom Benson, Wayne Wylupski, Truls Ellefsen, Robert Lansdale, Martin Sant, John Pritchard, J. Peter Alfke, Glenn Bruns, Brent Wilson, Alan Bland, Electronics & Music Maker magazine (Sep 1986) and authors identified only by e-mail | Tones converted to `.syx` |
| CZ Pack 1 | Coffeeshopped | Test `.syx` tones |
| RelaCZ, Italo DisCZo | CZounds | `.syx` tones used to validate the codec |

### Tools

- Google Stitch: used by the user to design the panel.
- Google Chrome (headless): renders the panel SVG.
- Apple Xcode / clang, `codesign` and `auval`: compiling, signing and validating the AU.
- CMake and Ninja: build system.
- Python and the Python-Markdown library: artwork, codec and converter scripts, and generating this documentation as HTML.
- Homebrew: command-line tools.
- KVR Audio forum: user reports of the VES buffer crackle.

### Authorship of this project

- Walter Junior.

## Legal notice

- Personal, non-commercial project.
- The CZ-101 ROMs are Casio's property. The Personal version embeds them in the binary and **must not be distributed**.
- Distributing the code requires complying with the licenses above (MAME's GPL-2.0-or-later, the AGPL-3.0 of VES/JUCE and the others).
- Casio, CZ-101 and other trademarks belong to their owners; this project is not affiliated with or endorsed by them.
