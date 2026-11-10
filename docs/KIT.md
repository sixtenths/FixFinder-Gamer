# FixFinder Gamer starter kit

This kit targets the USB-C AutoZone FixFinder 5034VSM with a GD32F427ZK and PCB marking `PCB_2L_5034_REVC_TYPEC_20200319`. Match the board before flashing. Installing the loader replaces the factory boot path, and the project has no verified full factory restore. [Identify your scanner](SETUP.md#identify-your-scanner) · [J5 photos and backup steps](RECOVERY.md#check-the-scanner-over-swd)

## Download compiled firmware

| File | Purpose |
| --- | --- |
| [FixFinder-Arcade-Flash.bin](../downloads/FixFinder-Arcade-Flash.bin) | One SWD image at `0x08000000` with the bootloader, game verification record, board drivers, Dino Run, and Neon Circuit. |
| [FixFinder-USB-Loader.bin](../downloads/FixFinder-USB-Loader.bin) | The resident loader by itself for first-time SWD install at `0x08000000`. |
| [FixFinder-Arcade.bin](../downloads/FixFinder-Arcade.bin) | Dino Run and Neon Circuit as a game image for the USB installer. |
| [SHA256SUMS](../downloads/SHA256SUMS) | Verify the downloads before flashing. |

The drivers for the LCD, backlight, controls, LEDs, beeper, and USB are compiled into the firmware. The one-file image boots to the arcade and can accept future games over USB. The separate game image requires the resident loader. If you have the kit ZIP, look in its `firmware-bin` folder for the same choices and the Pixel Catcher example.

Building your own games needs the Arm GNU 15.2.Rel1 compiler for `arm-none-eabi`. Put it in `%LOCALAPPDATA%\FixFinder\ArmGCC\bin` or pass its `bin` directory with `-ToolchainBin`. [Arm GNU Toolchain](https://developer.arm.com/tools-and-software/gnu-toolchain#Downloads)

## Install and play

1. For a stock scanner, complete the [read-only checks and backup](RECOVERY.md#check-the-scanner-over-swd). If the checks pass and you’re dedicating the unit to games, use the [one-file SWD install](RECOVERY.md#install-the-combined-arcade-image-over-swd) or [install just the resident loader](RECOVERY.md#install-the-resident-loader-once-over-swd).
2. With the loader installed, plug the scanner into the Windows PC with a USB data cable. Open PowerShell in the project folder. Set `$serial` to the part of the matching USB instance ID after the last backslash. If you flashed the combined image, you can open the viewer immediately. If you used the separate loader, install the arcade over USB first. In the kit ZIP, use `firmware-bin/FixFinder-Arcade.bin` in the install command.

```powershell
Get-PnpDevice -PresentOnly |
    Where-Object InstanceId -Match '^USB\\VID_28E9&PID_028[BCD]\\' |
    Select-Object Status,InstanceId

$serial = 'YOUR_SCANNER_SERIAL'
python scripts/Install-UsbGame.py --serial $serial --wait 45 downloads/FixFinder-Arcade.bin
python scripts/View-FixFinderUsb.py --serial $serial
```

The game slot holds one image at a time. Close the viewer before installing a different BIN. Use **Erase** and **Down** to browse and **Enter** to launch. Hold **Down + Back** to return to the menu. Pixel Catcher uses **Back** and **Enter** for left and right, **Erase** for up, and **Down** for down. The viewer maps the arrow keys to those four buttons. [Controls and viewer](PLAY.md)

## Build Doom with one command

With Git, Python 3, and the compiler installed, run this in the kit folder. The first run fetches the pinned GBADoom checkout, verifies the original shareware data, and compiles the first level and arcade into one USB game. It prints the installable BIN path. You can supply an exact DOOM 1.9 shareware IWAD with `-Wad`.

```powershell
$doom = ./scripts/Build-FixFinderDoom.ps1
python scripts/Install-UsbGame.py --serial $serial --wait 45 $doom
```

The build fetches the shareware assets from the pinned upstream checkout. The one-map build runs without eMMC setup. Use `$game, $pack = ./scripts/Build-FixFinderDoom.ps1 -Episode` to build the nine-map BIN and its matching eMMC pack locally. The episode build needs a board-specific way to provision that pack. [Pinned GBADoom revision](https://github.com/doomhack/GBADoom/tree/25deefdb44427e068735d3ef2af1e9a44d5cac85) · [Episode storage setup](SETUP.md#provision-the-map-pack)

To create one SWD file with the loader and your local Doom build, combine the two BINs. The packer adds the size and CRC record the loader checks on boot. You can substitute any compatible game BIN for `$doom`.

```powershell
python scripts/fixfinder_image.py downloads/FixFinder-USB-Loader.bin $doom FixFinder-Doom-E1M1-Flash.bin
```

## Make your own game

Start from [Pixel Catcher](../examples/pixel-catcher/main.c). The SDK starts USB, the LCD, and the button inputs, then calls `fixfinder_game_main()`. Define a palette, draw palette indices into `fixfinder_screen`, and call `fixfinder_present()` to show the frame. Use `fixfinder_keys(fixfinder_millis())` to read held buttons, including keyboard input from the viewer. The header also exposes the LEDs and beeper. [SDK header](../firmware/game-sdk/fixfinder.h)

```powershell
$build = ./scripts/Build-UsbLoader.ps1 -GameSdk -GameSource examples/pixel-catcher/main.c
python scripts/Install-UsbGame.py --serial $serial --wait 45 (Join-Path $build 'FixFinder-Game.bin')
```

The firmware runs native Cortex-M4 code. To bring over an existing game, port its input, drawing, timing, and any storage or sound calls to this board and build a new BIN. [Developer guide](DEVELOPMENT.md#build-a-custom-game-for-the-usb-loader)
