# Install and play FixFinder DOOM

This guide is for the experimental AutoZone FixFinder 5034VSM with a GD32F427ZK, a USB-C port, and PCB marking `PCB_2L_5034_REVC_TYPEC_20200319`. It assumes a Windows PC for building and USB installs. A Raspberry Pi with a 3.3 V SWD connection is needed to install the resident loader the first time. The scanner can then start and accept game updates through USB without the Pi.

## If your scanner already runs DOOM

Unplug its USB cable, wait a few seconds, then reconnect it while watching the screen. The HackerZone wordmark, “BOOTING,” and a progress bar appear during the roughly five-second startup window. The game starts automatically.

| Button or combination | Action |
| --- | --- |
| Erase / Down | Walk forward / backward |
| Back / Enter | Turn left / right |
| Back + Enter | Fire |
| Erase + Down | Use doors and switches |
| Down + Back | Menu |
| Down + Enter | Automap |

Hold Erase with Back or Enter to walk forward while turning. In the menu, use Erase and Down to move the selection, Back and Enter to adjust settings, and Erase + Down to confirm. Down + Back opens or closes the menu. In the automap, Erase + Down toggles follow mode.

The green, yellow, and red LEDs indicate health. The built-in beeper provides short effects.

## Build the loader and game

1. Clone this repository and open PowerShell at its root. Install Git, Python 3, and Arm GNU Toolchain **15.2.Rel1**. The scripts default to `%LOCALAPPDATA%\FixFinder\ArmGCC\bin`. If you installed the compiler elsewhere, pass its absolute `bin` path as `-ToolchainBin` to each build command. The GigaDevice firmware files and notices are in [GD32F4xx_Firmware_Library](../GD32F4xx_Firmware_Library).
2. Obtain a local DOOM 1.9 shareware `doom1.wad`. Use the Windows GbaWadUtil executable and its bundled runtime from the same GBADoom revision as the game engine, `25deefdb44427e068735d3ef2af1e9a44d5cac85`. Converter output from newer releases can omit assets expected by this engine. The build embeds the converted game data, so keep your WAD and resulting game binary in ignored `build/` or outside the repository.
3. Run the following from the repository root. Set `$wad` to your actual path. The converter runs from its own folder so it can find its bundled data.

```powershell
$wad = 'C:\path\to\doom1.wad'
New-Item -ItemType Directory -Force build | Out-Null
git clone https://github.com/doomhack/GBADoom.git build/GBADoom-engine
git -C build/GBADoom-engine checkout 25deefdb44427e068735d3ef2af1e9a44d5cac85
$converter = (Resolve-Path 'build/GBADoom-engine/GbaWadUtil/GbaWadUtil.exe').Path

$reduced = Join-Path (Get-Location) 'build/doom-e1m1.wad'
$converted = Join-Path (Get-Location) 'build/doom-e1m1-gba.wad'
python scripts/Prepare-SharewareWad.py $wad $reduced --maps 1
if ($LASTEXITCODE) { throw 'WAD preparation failed' }
Push-Location (Split-Path -Parent $converter)
try {
    & $converter -in $reduced -out $converted
    if ($LASTEXITCODE -or -not (Test-Path -LiteralPath $converted)) {
        throw 'GbaWadUtil did not produce a converted WAD'
    }
} finally {
    Pop-Location
}

$loaderDir = ./scripts/Build-UsbLoader.ps1
$loaderBin = Join-Path $loaderDir 'FixFinder-USB-Loader.bin'
Get-Item $loaderBin | Select-Object FullName,Length
Get-FileHash $loaderBin -Algorithm SHA256

./scripts/Build-DoomSwd.ps1 -EngineSource build/GBADoom-engine -ConvertedWad $converted `
    -FixFinderLcd -CompactReciprocal -UsbGame
```

The last command prints the path to the ELF under `build/doom-swd-…`. Use the neighboring **`FixFinder-Doom-LCD-USB.bin`** for the Windows USB installer. The loader BIN and game BIN serve different purposes. The loader occupies the first 16 KiB at `0x08000000`. The USB installer puts the game at `0x08008000` and manages its manifest at `0x08004000`.

## Install the resident loader once over SWD

If this is still a factory scanner, obtain and verify a restorable backup before writing anything. If SWD reads are blocked by read protection or the backup cannot be verified, stop. Changing protection can erase the factory firmware. This project’s V8 image is a recovery baseline for the experimental build, and restoring V8 will not bring back the original automotive firmware. Keep an intact factory scanner for diagnostics.

Power both devices off to connect the three leads. J5 hole 1 is the square pad. Count toward hole 6 at the opposite end. Power the scanner separately through its USB data cable.

| Scanner J5 | Raspberry Pi physical pin | Signal |
| --- | --- | --- |
| Hole 2 | 18 (GPIO24) | SWDIO |
| Hole 3 | 22 (GPIO25) | SWCLK |
| Hole 6 | 20 | Ground |

Leave J5 holes 1, 4, and 5 unconnected. Use probe-rs 0.32.0 on the Pi. If its GPIO chip differs from `gpiochip0`, identify the chip with `gpiodetect` and adjust the selector. The commands below read the core ID, the board UID, and the flash vectors. Run them on the Pi after connecting the experimental scanner.

```bash
ffprobe() {
  sudo "$HOME/.cargo/bin/probe-rs" "$@" --chip GD32F427ZK --protocol swd \
    --probe '0:0:gpiochip0,swclk=25,swdio=24' --speed 50
}
ffprobe read b32 0xE000ED00 1 --format simple-hex
ffprobe read b32 0x1FFF7A10 3 --format simple-hex
ffprobe read b32 0x08000000 2 --format simple-hex
```

The expected Cortex-M4 CPUID is `410fc241`. Record the three UID words for this physical scanner. To check a main-flash backup before programming, read all 3 MiB twice and compare the binary copies. This can take several minutes at 50 kHz. Keep the resulting backup and its checksum somewhere private. Main flash covers the region used by this project and is only one part of a factory recovery plan.

```bash
umask 077
backup_dir=$(mktemp -d "$HOME/fixfinder-flash.XXXXXXXX")
(
  set -e
  cd "$backup_dir"
  for copy in first second; do
    ffprobe read b32 0x08000000 786432 --format simple-hex > "$copy.words"
    python3 -c 'import array,pathlib,sys; p=pathlib.Path(sys.argv[1]); a=array.array("I",(int(w,16) for w in p.read_text().split())); assert a.itemsize == 4 and len(a) == 786432; a.byteswap() if sys.byteorder == "big" else None; p.with_suffix(".bin").write_bytes(a.tobytes())' "$copy.words"
  done
  cmp first.bin second.bin
  sha256sum first.bin > SHA256SUMS
  cat SHA256SUMS
  pwd
)
```

Copy `$loaderBin` to your Pi using SCP or another file transfer and compare its SHA-256 with the Windows result. From the folder containing the copied file, recheck the UID and program the loader. Replace the three placeholders with the words you recorded.

```bash
sha256sum FixFinder-USB-Loader.bin
expected_uid='WORD1 WORD2 WORD3'
actual_uid=$(ffprobe read b32 0x1FFF7A10 3 --format simple-hex)
if [ "$actual_uid" = "$expected_uid" ] &&
   [ "$(wc -c < FixFinder-USB-Loader.bin)" -le 16384 ]; then
  ffprobe download FixFinder-USB-Loader.bin --binary-format bin \
    --base-address 0x08000000 --verify && ffprobe reset
else
  printf '%s\n' 'Wrong UID or loader exceeds the first sector.'
fi
```

Use a sector download as shown. If the loader has no valid game yet, the display should settle on **USB LOADER READY**.

## Install or replace the game over USB

Connect the scanner to Windows with a USB data cable. Find its HID serial in PowerShell. The serial is the last part of the instance ID. The loader uses PID `028B`, older games use `028C`, and DOOM with USB video uses `028D`.

```powershell
Get-PnpDevice -PresentOnly |
    Where-Object InstanceId -Match '^USB\\VID_28E9&PID_028[BCD]\\' |
    Select-Object Status,InstanceId

$serial = 'YOUR_SCANNER_SERIAL'
$game = 'C:\path\to\FixFinder-Doom-LCD-USB.bin'
python scripts/Install-UsbGame.py --serial $serial --wait 45 $game
```

Wait for the CRC verification and game launch. The slot holds one installable image at a time, which can contain several games. The loader survives interrupted game installs and accepts a retry.

## Build and use HackerZone Arcade

Add `-Arcade` to the DOOM build command with `-FixFinderLcd -CompactReciprocal -UsbGame`, then install the resulting game BIN over USB. This bundles five games in the same flash slot. The selector opens after the resident loader, and each selection shows its own title screen. Use **Erase** and **Down** to select a game and **Enter** to launch it. Hold **Down + Back** for one second to return to the selector.

| Game | Controls |
| --- | --- |
| DOOM | The DOOM controls above apply. Hold Down + Back to leave DOOM and return to the selector. |
| Snake | Use Erase, Down, Back, and Enter to steer up, down, left, and right. |
| Maze Chase | Steer with the same four directions. Collect the dots and use the larger pellets to chase the ghosts. |
| Pong | Use Erase and Down to move your paddle. Play against the scanner. |
| Breakout | Move the paddle with Back and Enter. Clear the bricks before you lose three balls. |

The default build includes Maze Chase, a Pac-Man-style game with an original maze. To replace it with **the 1980 Midway arcade Pac-Man**, add `-PacmanRom` to the build with your local MAME ROM archives. A complete set works by itself; a split set needs the parent archive for the color PROMs:

```powershell
$pacman = @('C:\roms\pacman.zip', 'C:\roms\puckman.zip')
./scripts/Build-DoomSwd.ps1 -EngineSource build/GBADoom-engine -ConvertedWad $converted `
    -FixFinderLcd -CompactReciprocal -UsbGame -Arcade -PacmanRom $pacman
```

The build verifies the program, graphics, and color ROM checksums against [MAME's Midway manifest](https://github.com/mamedev/mame/blob/master/src/mame/pacman/pacman.cpp). ROM files stay local. Pac-Man auto-starts after its self-test. **Erase + Enter** inserts a coin and starts another game. Steer with the same four keys as Maze Chase. The scanner beeper plays the opening melody on its own and follows the loudest active voice during play. The tones are raised an octave for the beeper.

The USB game slot and the eMMC are separate. With `-Arcade -EmmcLibrary`, the game checks the board's FAT32 eMMC for `FFARCADE.PAK` and adds the pack's games to the selector. This reads the existing filesystem and uses no eMMC writes during gameplay. A missing or unreadable pack leaves the built-in games available.

Add `-PacmanFromEmmc` to load Pac-Man from `PACMAN.ROM` on that same volume. For the first install on scanner 1, combine it with `-PacmanRom $pacman` so the firmware can create and verify the file. Subsequent builds can omit `-PacmanRom` and load the existing file into RAM when Pac-Man starts.

The starter pack uses six games from the [CC0 CHIP-8 Archive](https://github.com/JohnEarnest/chip8Archive/tree/761e3ffc63f43e6a849e80714122feff2afca208), played through [C-Octo](https://github.com/JohnEarnest/c-octo/tree/102994e6166236045a067efa930a88532510e03e).

```powershell
git clone https://github.com/JohnEarnest/chip8Archive.git build/chip8Archive
git -C build/chip8Archive checkout 761e3ffc63f43e6a849e80714122feff2afca208
python scripts/Pack-FixFinderLibrary.py build/chip8Archive build/fixfinder-library.bin
```

The experimental provision build takes `-ProvisionLibrary build/fixfinder-library.bin` in addition to `-EmmcLibrary`. Its write path is locked to the measured MCU and eMMC on scanner 1. It creates a temporary FAT32 file, verifies its bytes, and renames it to `FFARCADE.PAK`. It does not replace an existing file. Leave power on during installation. For an already provisioned device, omit `-ProvisionLibrary` when rebuilding.

| Bonus game | Controls |
| --- | --- |
| Dino Run | Press any face button to jump. |
| Flight Runner and Danm8ku | Erase, Down, Back, and Enter steer up, down, left, and right. |
| Down 8 | Back and Enter move left and right; Erase jumps. |
| Tick | Press a face button to hit the beat. |
| Cave Explorer | Back and Enter move left and right; Erase acts; Down resets the level. |

Hold **Down + Back** to leave any bonus game. `python scripts/Read-FixFinderStorage.py --serial $serial` reports the eMMC capacity, the probe stage, and the number of games registered from the pack when the viewer is closed.

## Troubleshoot installation

| What you see | What to do |
| --- | --- |
| No USB device | Use a known data cable and a direct PC port. Unplug other scanners while identifying the target. |
| Game is frozen or installer cannot find the loader | Start the installer, then unplug and replug the scanner while it waits for USB. |
| BIN is rejected | Check that it came from the build with `-UsbGame` and targets the game slot. The resident loader BIN and the SWD diagnostic BIN have different addresses. |
| Upload was interrupted | Reconnect USB and run the same install command again. The game starts only after verification succeeds. |
| No SWD response | Confirm scanner USB power, shared ground, J5 hole orientation, and the Pi GPIO-chip number. Keep the initial clock at 50 kHz. |
| Splash passes before you see it | Watch the screen while reconnecting USB. A manual power cycle gives the full normal boot window. |

For hardware notes and recovery references, see [LCD bring-up](LCD.md), [the status log](STATUS.md), and [V8 recovery](../README.md#raspberry-pi).

## Preview the game on a PC

Install the latest DOOM build with `-UsbGame` and keep the scanner connected to Windows through USB. Run the viewer with Python and Tkinter, using the serial shown above. The default mode streams the full 320 × 240 LCD raster with its RGB palette. Use `--scale 3` for a larger window with sharp pixel edges, `--source` for the lighter 240 × 160 game preview, or `--fast` for the 120 × 80 preview. Older firmware falls back to 240 × 160. The USB stream is assembled from strips, so movement can show a seam between strips.

```powershell
python -B scripts/View-FixFinderUsb.py --serial $serial
```

Click the viewer window to give it keyboard focus. In the selector, use **Up / Down** or **W / S** to browse and **Enter** to launch. In arcade games, use the arrow keys or **WASD**. Press **R** to start another Pac-Man game. Hold **Backspace** for one second to return to the selector.

The eMMC bonus games accept simultaneous keys using each game's four-button mapping. **Space** also acts as **Up** in those games, so you can steer and fire in Danm8ku.

In DOOM, use **Up / Down** or **W / S** to move and **Left / Right** or **A / D** to turn. **Space** fires, **E** or **Enter** opens doors and activates switches, **Q / C** strafe, **Esc** opens the menu, and **Tab** toggles the map. Movement and action keys can be held together.

### Preview through the Pi

Leave the Pi debug leads connected and use the ELF for the exact game build installed on the scanner. Run `arm-none-eabi-nm -S FixFinder-Doom-LCD-USB.elf` and find the addresses of `doom_frame` and `palette`. The frame is 38,400 bytes and the palette is 512 bytes.

Copy `scripts/Preview-FixFinder.py` to the Pi. Run the command below with the Pi's LAN address and the two addresses from your ELF. The example addresses are for the build with the forward-and-turn controls fix. Open the private URL printed by the script from your PC on the same network. Stop the preview with Ctrl+C before using the debugger for flashing.

```bash
python3 Preview-FixFinder.py --bind 192.168.1.59 --frame 0x10000000 --palette 0x200064e4
```

The browser refreshes every few seconds. It reads the game framebuffer while DOOM runs, so a capture can include parts of successive frames. The loader splash appears before the game framebuffer is available.
