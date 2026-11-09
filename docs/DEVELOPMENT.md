# Developer guide

Start with the [installation guide](SETUP.md) for the supported hardware and toolchain. The [bench notes](STATUS.md) record what was measured on the scanner.

## Repository map

| Path | Contents |
| --- | --- |
| [firmware/doom-swd](../firmware/doom-swd) | DOOM runtime, arcade selector, games, eMMC and USB video |
| [firmware/usb-loader](../firmware/usb-loader) | Resident USB loader, boot art and the sample game |
| [firmware/lcd](../firmware/lcd) | Panel drivers, controls, LEDs and beeper |
| [scripts](../scripts) | Windows builds, host utilities and asset preparation |
| [tests](../tests) | Host regressions and the simulated LCD bus |
| [USB-Build-V8](../USB-Build-V8) | Verified experimental recovery image and shared USB startup |
| [archive](../archive) | Earlier USB and RGB experiments |

## Build a custom game for the USB loader

The [Pixel Catcher example](../examples/pixel-catcher/main.c) is a complete game. Its entry point is `fixfinder_game_main()`. The [game SDK header](../firmware/game-sdk/fixfinder.h) exposes the 240 × 160 indexed framebuffer, palette, millisecond clock, and board input and output functions. The SDK scales frames to the 320 × 240 LCD and serves the same frames to the USB viewer.

```powershell
$demo = ./scripts/Build-UsbLoader.ps1 -GameSdk -GameSource examples/pixel-catcher/main.c
python scripts/Install-UsbGame.py --serial $serial (Join-Path $demo 'FixFinder-Game.bin')
```

Copy the example to a new directory and pass its source path to `-GameSource`. For a game split across C files, pass a PowerShell array of source paths. Keep headers with the source and reference them from your C files. See the [kit quick start](KIT.md) for controls and deployment.

The loader accepts a raw Cortex-M4 Thumb image linked at `0x08008000` and checks its vectors and CRC. The slot holds up to 3,040 KiB. The split-episode build reserves its last 256 KiB for the active DOOM map. For a low-level starting point that owns its own rendering loop, build the [display demo](../firmware/usb-loader/sample_game.c) with `-GameSource` and implement `fixfinder_game_run()`.

## Run checks

The host checks use synthetic game data and need only Python.

```powershell
python -B -m unittest discover -s tests
```

With the ARM toolchain installed, install `unicorn` and `pyelftools` into your Python environment and run the LCD protocol check. It exercises the legacy HX8347 diagnostic with a mock bus.

```powershell
python -B scripts/Check-LcdProtocol.py
```

## DOOM runtime diagnostic

Use GBADoom revision `25deefdb44427e068735d3ef2af1e9a44d5cac85`. Reduce a local shareware IWAD to E1M1 with `python scripts/Prepare-SharewareWad.py <doom1.wad> <reduced.wad> --maps 1`, then run `GbaWadUtil -in <reduced.wad> -out <converted.wad>` from the pinned GBADoom checkout's `GbaWadUtil` directory. The converted WAD stays under ignored `build/`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-DoomSwd.ps1 -EngineSource <checkout> -ConvertedWad <converted.wad>
python scripts/Smoke-DoomSwd.py <output-elf>
```

The smoke test uses Python with `unicorn` and `pyelftools`. It enters the linked game runtime after USB setup, supplies a synthetic clock, and exercises E1M1 with simulated movement, firing and release. It also checks all 76,800 RGB565 pixels emitted for a complete 320 x 240 frame. Add `--frame-output <preview.ppm>` to capture that frame.

The linked ELF preserves the V8 USB startup path and keeps counters, an RGB565 frame hash, player coordinates and the last engine error readable over SWD. The display path scales the 240 x 160 indexed frame in TCM to a logical landscape frame through a 640-byte row buffer in AHB SRAM. The default SWD diagnostic consumes rows in software.

The diagnostic takes pressed actions from the `doom_test_buttons` word at the address reported by the current ELF. The [action bits](../firmware/doom-swd/input.h) map to movement, turning, strafing, Fire, Use, Menu and Automap. The runtime sends key events on press and release so multiple actions can remain held together. Hold Use with Strafe Left or Right to cycle weapons. Add `--check-weapon-cycle` to verify both chords in emulation.

Add `-CompactReciprocal` to build the smaller diagnostic variant. It replaces the engine's 1 MiB reciprocal lookup table with an equivalent calculation. The compact variant is used by the USB arcade build verified on scanner 1.

`-BoardSource` enables the game build that sends each frame to an initialized landscape HX8347 transport and reads debounced buttons from the same board implementation used by V9. The board must supply measured GPIO mappings, confirm the controller and provide the `read_buttons` callback in [the board interface](../firmware/lcd/v9_board.h). The build emits a separate `FixFinder-Doom-Board.elf`. This path has been exercised with a mock board in Cortex-M4 emulation. See the [LCD bring-up notes](LCD.md) for the outstanding hardware checks.

For the experimental 5034VSM with the verified `00 e3 00 00` display response, use `-FixFinderLcd` instead of `-BoardSource`. This builds `FixFinder-Doom-LCD.elf` with the observed 8-bit EXMC wiring, DCS commands, PF7 backlight PWM, and the on-board controls. The SWD input mailbox remains available for diagnostics.

## Flash-only multi-map experiments

To retain E1M1 through E1M3 from a local DOOM 1.9 shareware IWAD, use the following sequence from the repository root. Run the converter from its own directory with absolute input and output paths, as for the E1M1 build.

```powershell
python scripts/Prepare-SharewareWad.py <doom1.wad> build/shareware-three.wad --maps 3
# Convert build/shareware-three.wad to build/shareware-three-gba.wad with GbaWadUtil.
python scripts/Prepare-SharewareWad.py build/shareware-three-gba.wad build/shareware-three-packed.wad --deduplicate-converted --compact-patches
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-DoomSwd.ps1 -EngineSource <checkout> -ConvertedWad build/shareware-three-packed.wad -CompactReciprocal
python scripts/Smoke-DoomSwd.py <output-elf> --check-progression
```

The progression check triggers each normal level exit directly and uses simulated Fire presses to advance the intermissions. The optional patch compaction shares identical columns within each converted graphic while preserving its dimensions, column order and pixels. The compact three-map board build with the mock driver leaves about 100 KiB of internal flash and was tested in emulation.

For a four-map build, use `--maps 4`, add `--compress-setup` during deduplication, and pass `--last-map 4` to the progression check. This uses a FixFinder-specific WAD format for compressed setup data. The build detects it and decodes one temporary map lump at a time into a 21 KiB TCM buffer. Long-lived map data still stays directly accessible in flash. The four-map mock-board image leaves 5,392 bytes of internal flash. Both builds omit audio, demos and the remaining map data, including the E1M3 secret exit's destination. The nine-map packed WAD alone is larger than the MCU's internal flash.
