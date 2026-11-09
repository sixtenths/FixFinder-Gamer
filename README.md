# FixFinder DOOM

This project targets the experimental AutoZone FixFinder 5034VSM. V8 USBHS HID is the verified firmware baseline. See the [status log](docs/STATUS.md) and [LCD bring-up notes](docs/LCD.md).

## Rebuild V8 on Windows

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-V8.ps1
```

The script uses Arm GNU Toolchain 15.2.Rel1 from the existing FixFinder installation. Pass `-ToolchainBin` to select another installation of that release. It compiles from source into a fresh directory under `build/`, emits an ELF, BIN and link map, and requires the BIN to match the [recovery checksum](USB-Build-V8/SHA256SUMS). The original [V8 binary](USB-Build-V8/FixFinder-USB-V8.bin) and ELF are archived with the source and original objects.

## DOOM runtime diagnostic

Use GBADoom revision `25deefdb44427e068735d3ef2af1e9a44d5cac85`. Reduce a local shareware IWAD to E1M1 with the [Prusa reducer](https://github.com/NilsN3DP/Prusa-Core-One._But-Can-it-run-Doom/blob/1b791ccd23d4c95d4eb4a0991ae0192f22730739/tools/reduce_doom.py), then run `GbaWadUtil -in <reduced.wad> -out <converted.wad>` from the pinned GBADoom checkout's `GbaWadUtil` directory. The converted WAD stays under ignored `build/`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-DoomSwd.ps1 -EngineSource <checkout> -ConvertedWad <converted.wad>
python scripts/Smoke-DoomSwd.py <output-elf>
```

The smoke test uses Python with `unicorn` and `pyelftools`. It enters the linked game runtime after USB setup, supplies a synthetic clock, and exercises E1M1 with simulated movement, firing and release. It also checks all 76,800 RGB565 pixels emitted for a complete 320 x 240 frame. Add `--frame-output <preview.ppm>` to capture that frame.

The linked ELF preserves the V8 USB startup path and keeps counters, an RGB565 frame hash, player coordinates and the last engine error readable over SWD. The display path scales the 240 x 160 indexed frame in TCM to a logical landscape frame through a 640-byte row buffer in AHB SRAM. The SWD diagnostic consumes rows in software. The physical LCD backend will consume the same rows once the panel bus and orientation are verified.

The diagnostic takes pressed actions from the `doom_test_buttons` word at the address reported by the current ELF. The [action bits](firmware/doom-swd/input.h) map to the engine's directional controls, Fire, Use, Menu and Automap. The runtime sends key events on press and release so multiple actions can remain held together. Physical button wiring remains to be verified.

Add `-CompactReciprocal` to build the smaller diagnostic variant. It replaces the engine's 1 MiB reciprocal lookup table with an equivalent calculation. The regular build is the variant tested on the scanner. The compact build has passed emulation and still needs target timing verification.

### Three-map diagnostic

To retain E1M1 through E1M3 from a local DOOM 1.9 shareware IWAD, use the following sequence from the repository root. Run the converter from its own directory with absolute input and output paths, as for the E1M1 build.

```powershell
python scripts/Prepare-SharewareWad.py <doom1.wad> build/shareware-three.wad --maps 3
# Convert build/shareware-three.wad to build/shareware-three-gba.wad with GbaWadUtil.
python scripts/Prepare-SharewareWad.py build/shareware-three-gba.wad build/shareware-three-packed.wad --deduplicate-converted
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-DoomSwd.ps1 -EngineSource <checkout> -ConvertedWad build/shareware-three-packed.wad -CompactReciprocal
python scripts/Smoke-DoomSwd.py <output-elf> --check-progression
```

The progression check triggers each normal level exit directly and uses simulated Fire presses to advance the intermissions. The three-map image leaves about 13 KiB of internal flash and was tested in emulation. It omits audio, demos and the remaining map data, including the E1M3 secret exit's destination. The nine-map packed WAD alone is larger than the MCU's internal flash.

## Raspberry Pi

The deployment script defaults to uploading and checking the archived V8 image in a private staging directory on `zac@pi`. It requires a trusted host key and batch-mode SSH authentication. `-Inspect` separately reads the CPUID, MCU UID and first two vector words over SWD. The expected V8 vectors are `20020000 08001ca1`. SWD access requires non-interactive sudo on the Pi.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Deploy-V8.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Deploy-V8.ps1 -Inspect
```

Record the full UID in address order after physically identifying the SWD target as the experimental board. For V8 recovery, pass `-Restore -ExpectedUid '<word1> <word2> <word3>'`. The script checks the image hash, CPUID and UID and uses a verified sector download at `0x08000000`. It accepts only the archived V8 binary. Connect the Pi to the experimental scanner only, using SWDIO, SWCLK and ground. Power the scanner separately over USB.
