# FixFinder Gamer

Turn a compatible AutoZone FixFinder 5034VSM into a handheld arcade. Play the complete DOOM shareware episode on the scanner’s built-in screen and buttons, or stream the display and play from a Windows PC over USB.

**[Install and build](docs/SETUP.md)** · **[Games and controls](docs/PLAY.md)** · **[Developer guide](docs/DEVELOPMENT.md)** · **[Recovery](docs/RECOVERY.md)** · **[Changelog](CHANGELOG.md)**

<table>
  <tr>
    <td rowspan="2" align="center"><img src="docs/photos/fixfinder-doom-gameplay.jpg" width="320" alt="A FixFinder scanner running DOOM on its built-in LCD"></td>
    <td align="center"><img src="docs/screenshots/arcade-selector.png" width="320" alt="HackerZone game selector with DOOM selected"><br>HackerZone Arcade selector</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/screenshots/doom-e1m2.png" width="320" alt="DOOM E1M2 Nuclear Plant on the USB viewer"><br>Nuclear Plant, captured over USB</td>
  </tr>
</table>

## What runs on it

| Game or feature | What you get |
| --- | --- |
| **DOOM** | All nine maps of *Knee-Deep in the Dead*, including the secret level and episode finale. The beeper plays distinct cues for weapons, doors, pickups, and damage. |
| **Built-in arcade** | Snake, Maze Chase, Pong, Breakout, Neon Circuit, and Dino Run. The native racer and runner render at 320 × 240. |
| **Original Pac-Man** | Optional Midway arcade version built from your local ROMs. |
| **eMMC bonus pack** | Six optional CHIP-8 games, including Dino Run Classic, Flight Runner, and Cave Explorer. |
| **USB viewer** | A 320 × 240 Windows preview with keyboard controls. Use the physical controls or the keyboard while the scanner is connected. |
| **USB game loader** | First-time SWD installation, then USB installs of compatible game images with on-device verification. |

## Get started

1. Confirm the scanner is a USB-C **5034VSM** with a **GD32F427ZK** and PCB marking `PCB_2L_5034_REVC_TYPEC_20200319`. This is the combination verified on hardware. [Board and display notes](docs/LCD.md)
2. Start with the [installation guide](docs/SETUP.md). You’ll need a Windows PC, a local DOOM 1.9 shareware IWAD, and the pinned build tools. If you are starting from factory firmware, follow the [backup and SWD procedure](docs/RECOVERY.md#install-the-resident-loader-once-over-swd).
3. Build the game image and install it through the resident loader. The complete episode uses a matching map pack in eMMC. The included eMMC writer is locked to the project’s test unit, so other boards need a verified way to place the pack on their own eMMC. The one-map USB build can run without eMMC provisioning. [Choose a build](docs/SETUP.md#choose-a-build)
4. Pick a game with **Erase** or **Down** and launch it with **Enter**. Hold **Down + Back** to return to the selector. See the [controls and Windows viewer commands](docs/PLAY.md).

## Explore the project

| Guide | What it covers |
| --- | --- |
| [Installation](docs/SETUP.md) | Toolchain, loader and game builds, eMMC provisioning, and USB troubleshooting |
| [Play and stream](docs/PLAY.md) | Scanner and keyboard controls, LED behavior, and viewer options |
| [Development](docs/DEVELOPMENT.md) | Source layout, custom games, and tests |
| [Recovery](docs/RECOVERY.md) | Factory backup, first-time SWD, V8 recovery, and Pi preview |
| [Hardware notes](docs/STATUS.md) | LCD bring-up, board measurements, and hardware verification |
| [Archive](archive) | Earlier USB builds and display experiments |

The game WAD, arcade ROM files, eMMC packs, and generated game images are built locally. See the setup guide for asset sources and the repository’s component notices for attribution.
