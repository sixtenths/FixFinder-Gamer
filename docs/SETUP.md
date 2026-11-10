# Build and install

This guide targets the USB-C AutoZone FixFinder **5034VSM** with a **GD32F427ZK** and PCB marking **`PCB_2L_5034_REVC_TYPEC_20200319`**. [Board photos and LCD notes](LCD.md)

The downloadable kit includes a prebuilt loader, Dino Run, Neon Circuit, and a starter game. It also includes a single command to fetch the pinned DOOM inputs and build the first map for USB install. The full build steps are below. [Kit quick start](KIT.md)

## Identify your scanner

AutoZone uses the orange Fix Finder for its in-store scan service. Innova’s public catalog and update list provide no retail cross-reference for the `5034VSM`. Innova’s Windows updater 1.3.9 lists it under USB product IDs `064A` and `064B`. The project observed stock USB ID `1720:064A` on a physical scanner. [AutoZone Fix Finder service](https://www.autozone.com/lp/fix-finder) · [Innova update list](https://innova-electronics.helpscoutdocs.com/article/2692-innova-obd2-scan-tool-updates) · [Bench notes](LCD.md)

The retail **FixAssist 3100RS** and **CarScan Inspector 5310** are candidates for a future port. Innova’s updater groups revisions of those two retail models under `03EF` and `03E5`, separate from the Fix Finder IDs. Their main MCU, display, and pin wiring would need to be checked on an actual board before either could run this firmware. [Innova 3100RS](https://www.innova.com/products/fixassist-3100rs) · [Innova 5310](https://www.innova.com/products/carscan-inspector-5310)

On Windows, plug in a stock scanner with a USB data cable and run this in PowerShell. It prints the Innova USB vendor and product IDs without the device serial number.

```powershell
Get-PnpDevice -PresentOnly | ForEach-Object {
    if ($_.InstanceId -match '^USB\\(VID_1720&PID_[0-9A-F]{4})') { $Matches[1] }
} | Sort-Object -Unique
```

If you’re checking a used unit before buying it for this project, ask for the model label and, if available, the USB ID or board photos. Before flashing, confirm the exact PCB revision and MCU. Only the board listed above has run the arcade on hardware. [PCB marking photo](photos/286.jpg) · [MCU photo](photos/265.jpg)

## Start with one scanner

You don’t need a second scanner. The second unit in the [hardware notes](LCD.md) was used to learn how the factory display works. The wiring, loader, and game builds below target the scanner you already have.

| If this describes your scanner | What to do |
| --- | --- |
| You still use it for vehicle diagnostics, or need to be able to return it to stock. | Keep it stock. The arcade replaces the factory boot path, has no dual boot, and this project has no verified procedure for restoring all factory functionality. You can still read the guide, browse the captures, and build on your PC. |
| It’s a factory unit you’re willing to dedicate to games. | Match the board markings and run the [read-only checks](RECOVERY.md#check-the-scanner-over-swd). Save and verify the unit’s own factory flash before writing anything. Stop if reads are blocked, the dump fails verification, or the debug connection is unstable. Even a verified main-flash dump doesn’t guarantee a return to stock. |
| It already runs the HackerZone USB loader. | Skip SWD for ordinary game installs. Use [the USB installer](#install-or-replace-the-game-over-usb) or go straight to [the controls and viewer](PLAY.md). |

### What you gain and what you give up

You get Dino Run, Neon Circuit, Pong, Snake, Breakout, and Maze Chase on the original LCD and buttons, with LED and beeper feedback. The DOOM build adds E1M1. The Windows viewer streams at the panel’s 320 × 240 resolution and lets you play with a keyboard. After the first SWD install, you can build and swap compatible game images over USB without reopening the case.

You give up the scanner’s normal automotive functions while the arcade is installed. A failed flash may leave the scanner unable to boot and require another SWD session. If factory recovery matters, the practical cost could be a replacement diagnostic tool. Later DOOM maps require a matching eMMC pack. The included writer is locked to the development unit.

### What you need and what it costs

| Item | Cost and purpose |
| --- | --- |
| One compatible scanner | **$0 if you already own it.** If it’s your only scan tool, price out how you’d replace that function before converting it. |
| Windows PC and USB-C data cable | Reuse what you have. The PC builds games and runs the USB installer and viewer. The cable also powers the scanner on the bench. |
| Raspberry Pi with a populated GPIO header, power supply, and boot media | Borrow or reuse one for the first install and any SWD recovery. As a price reference, the project bench used a Pi 400; [Raspberry Pi lists it at US$60 for the unit](https://www.raspberrypi.com/news/more-memory-driven-price-rises/), before accessories, tax, and shipping. |
| Three jumper leads and a screwdriver that fits your case | Reuse or buy them if needed. The [J5 photos](RECOVERY.md#find-the-j5-holes) show the no-solder setup. The jumpers must stay secure without being held during a write. |
| Game data and tools | The build tools and DOOM 1.9 shareware are free. The kit can fetch the pinned shareware inputs, or you can supply a matching `doom1.wad`. |
| Bench time | Set aside at least an hour for a first attempt: open the case, set up the tools, make two flash reads, and test USB. Setup or debug problems can take longer. If SWD is unreliable, stop before flashing. |

With the tools and cables on hand or borrowed, the conversion can cost **$0 out of pocket**. You can check the board and prepare the software before buying debug hardware.

## Choose a build

| Build | DOOM maps | Storage setup |
| --- | --- | --- |
| **One-map arcade (start here)** | Hangar, E1M1 | Runs without eMMC provisioning. |
| Complete shareware arcade | E1M1–E1M8 plus E1M9 | Needs the matching `0:/FFDOOM1.PAK` on a verified FAT32 eMMC volume. The included seed installer only provisions the project’s development scanner. |

Both builds use the same USB loader and support the native arcade games. On the one-map build, stop at the Hangar results screen or hold **Down + Back** to leave. Continuing to E1M2 requires the complete episode build.

## Prepare the toolchain and WAD

Use a Windows PC with Git, Python 3, and [Arm GNU Toolchain 15.2.Rel1](https://developer.arm.com/tools-and-software/gnu-toolchain#Downloads) for the `arm-none-eabi` target. The PowerShell scripts expect the toolchain in `%LOCALAPPDATA%\FixFinder\ArmGCC\bin`. Pass `-ToolchainBin` with your compiler’s `bin` folder if it is installed elsewhere. Run the commands from the repository root in a PowerShell session that allows local build scripts. You can build the loader and game BIN with the scanner unplugged.

```powershell
git clone https://github.com/sixtenths/FixFinder-Gamer.git
Set-Location FixFinder-Gamer
```

Set `$wad` to a local DOOM 1.9 shareware `doom1.wad`. Keep game data and generated game images in ignored `build/` or outside the repository. The reducer checks the exact IWAD hash. The converter and engine must come from the same pinned GBADoom checkout. [Pinned GBADoom source and tools](https://github.com/doomhack/GBADoom/tree/25deefdb44427e068735d3ef2af1e9a44d5cac85)

```powershell
$wad = 'C:\path\to\doom1.wad'
$mapCount = 1  # Use 9 only for the complete episode with a matching eMMC pack.
New-Item -ItemType Directory -Force build | Out-Null
$engine = 'build/GBADoom-engine'
if (-not (Test-Path -LiteralPath $engine)) {
    git clone https://github.com/doomhack/GBADoom.git $engine
    if ($LASTEXITCODE) { throw 'GBADoom clone failed' }
}
git -C $engine checkout 25deefdb44427e068735d3ef2af1e9a44d5cac85
if ($LASTEXITCODE) { throw 'Could not check out the pinned engine' }
$converter = (Resolve-Path "$engine/GbaWadUtil/GbaWadUtil.exe").Path
$reduced = Join-Path (Get-Location) "build/doom-$mapCount-source.wad"
$converted = Join-Path (Get-Location) "build/doom-$mapCount-converted.wad"

python scripts/Prepare-SharewareWad.py $wad $reduced --maps $mapCount
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

$arcade = @{
    EngineSource = $engine
    FixFinderLcd = $true
    CompactReciprocal = $true
    UsbGame = $true
    Arcade = $true
}
```

## Build the resident loader

Skip this step if the supported USB game loader is already installed.

```powershell
$loaderDir = ./scripts/Build-UsbLoader.ps1
$loaderBin = Join-Path $loaderDir 'FixFinder-USB-Loader.bin'
Get-FileHash -LiteralPath $loaderBin -Algorithm SHA256
```

The loader BIN is for the [guarded SWD procedure](RECOVERY.md#install-the-resident-loader-once-over-swd), after the read-only checks and backup. It goes at `0x08000000`. The game BIN uses USB and a separate address, `0x08008000`.

## Build the one-map arcade

Use `$mapCount = 1` in the WAD preparation step.

```powershell
./scripts/Build-DoomSwd.ps1 @arcade -ConvertedWad $converted
```

The script prints the ELF path. Use the neighboring `FixFinder-Doom-LCD-USB.bin` for the [USB installer](#install-or-replace-the-game-over-usb).

## Build the complete shareware episode

Use `$mapCount = 9` in the WAD preparation step. These commands produce the shared graphics WAD, matching map pack, and episode game BIN.

```powershell
python scripts/Prepare-SharewareWad.py $converted build/episode-dedup.wad --deduplicate-converted --compact-patches
if ($LASTEXITCODE) { throw 'Episode compaction failed' }
python scripts/Prepare-SharewareWad.py build/episode-dedup.wad build/episode-assets.wad --split-episode-maps build/episode-maps.pak
if ($LASTEXITCODE) { throw 'Episode split failed' }
./scripts/Build-DoomSwd.ps1 @arcade -EmmcLibrary -ConvertedWad build/episode-assets.wad
```

### Provision the map pack

The firmware opens `0:/FFDOOM1.PAK` and checks its header against the graphics WAD. The pack stays on eMMC across game installs. Each map is verified and staged in the last 256 KiB of MCU flash before play. A matching map already in that slot can be reused.

For the project's test scanner, build this temporary seed image and install its BIN first. Allow the seed to finish writing before replacing it with the episode BIN. The write path is locked to the test scanner’s MCU and eMMC identities. On another board, use a verified file provisioning method for that specific unit. The stock USB interface does not expose the volume as a Windows disk.

```powershell
python -c "from pathlib import Path; from struct import pack; Path('build/episode-seed.wad').write_bytes(pack('<4sII',b'IWAD',0,12))"
./scripts/Build-DoomSwd.ps1 @arcade -EmmcLibrary -ConvertedWad build/episode-seed.wad -ProvisionDoomMaps build/episode-maps.pak
```

The seed writes a temporary file, verifies the bytes, and renames the file to `FFDOOM1.PAK`. Leave USB power connected until the selector appears. For a definitive seed check on the bench, read the seed ELF’s `doom_map_install_status` over SWD. The value `2` confirms the write and verification. After installing the episode BIN, launch DOOM. If the selector reports **DOOM map pack missing**, check that the matching pack was provisioned. Subsequent firmware builds using that pack need only the episode BIN.

## Install or replace the game over USB

Once the resident loader is installed, leave the case closed and connect the scanner using a USB data cable. Close the viewer before installing. From the project folder, run the installer with either an application BIN or a combined flash BIN made by the FixFinder packer.

```powershell
python scripts/Install-UsbGame.py --wait 45 downloads/FixFinder-Arcade-Flash.bin
```

The installer detects a single compatible scanner, enters its loader, flashes the application, checks its CRC on the scanner, and starts it. For a combined image, it validates and extracts the application. The resident recovery loader stays installed. A first-time conversion from stock or replacing the resident loader itself requires SWD.

If you have several converted scanners connected, use `--serial` to select one. To find your scanner’s HID serial in PowerShell, run the command below. It is the last part of the instance ID. The loader uses product ID `028B`, older games use `028C`, games with USB video use `028D`, and desktop-enabled arcade builds use `028E`.

```powershell
Get-PnpDevice -PresentOnly |
    Where-Object InstanceId -Match '^USB\\VID_28E9&PID_028[BCDE]\\' |
    Select-Object Status,InstanceId

$serial = 'YOUR_SCANNER_SERIAL'
$game = 'C:\path\to\FixFinder-Doom-LCD-USB.bin'
python scripts/Install-UsbGame.py --serial $serial --wait 45 $game
```

Wait for CRC verification and game launch. The slot holds one installable image, which can contain multiple games. If the loader does not appear, start the installer and reconnect the USB cable while it waits.

## Add original arcade Pac-Man

Use your local MAME `pacman.zip` to build the original Midway arcade game alongside the rest of the lineup. For split sets, supply both `pacman.zip` and `puckman.zip` as an array to `-PacmanRom`. The packer checks the original ROM checksums.

```powershell
$game = ./scripts/Build-FixFinderDoom.ps1 -PacmanRom 'C:\path\to\pacman.zip'
python scripts/Install-UsbGame.py --wait 45 $game
```

If a verified `PACMAN.ROM` is already on your scanner’s FAT32 eMMC, use `-PacmanFromEmmc` instead. The project scanner uses `-Episode -PacmanFromEmmc` so both the DOOM map pack and Pac-Man ROM stay on eMMC during firmware updates.

## Enable save files

The project scanner uses this build to keep a suspended round for each arcade title and eight DOOM checkpoints. It uses the previously provisioned episode map pack and Pac-Man ROM.

```powershell
$game, $pack = ./scripts/Build-FixFinderDoom.ps1 -Episode -PacmanFromEmmc -SaveFiles
python scripts/Install-UsbGame.py --wait 45 $game
```

Omit `-Episode` for the one-map build. Use `./scripts/Build-UsbLoader.ps1 -ArcadeOnly -SaveFiles` to build the arcade without DOOM. Save files use alternating copies on the mounted FAT32 volume, with a checksum checked on read. The current eMMC write guard enables writes only for the development scanner's verified MCU and storage identities. Other units need a separately verified storage configuration. [Save controls](PLAY.md#save-and-resume)

## Check eMMC

With the viewer closed, `python scripts/Read-FixFinderStorage.py --serial $serial` reports the eMMC capacity on builds with the storage probe enabled.

## Troubleshooting

| What you see | What to check |
| --- | --- |
| No USB device | Use a known data cable and a direct PC port. The loader and game USB IDs only appear after the custom firmware is installed. |
| Installer cannot find the loader | Start the installer, then reconnect scanner USB while it waits. |
| BIN is rejected | Use the game BIN built with `-UsbGame`. A FixFinder combined flash BIN also works. Standalone loader BINs and SWD diagnostics require SWD. |
| Upload was interrupted | Reconnect USB and run the installer again. The loader starts the game after verification. |
| DOOM map pack missing | Check the pack filename and use the pack built alongside the episode’s graphics WAD. |
| No SWD response | Check scanner USB power, J5 orientation, shared ground, and the Pi GPIO-chip number. Start at 50 kHz. |

[Play and stream](PLAY.md) · [Recovery and bench tools](RECOVERY.md) · [Experimental radio notes](RADIO.md)
