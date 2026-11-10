# Play and stream

After boot, the HackerZone Arcade build opens the game selector. **Erase** goes to the previous game, **Down** goes to the next game, and **Enter** launches it. Hold **Down + Back** for about one second to return to the selector. Over USB, use **Up / Down**, **Enter**, and hold **Backspace** to return.

## DOOM controls

| Action | Scanner | PC viewer |
| --- | --- | --- |
| Walk forward / backward | Erase / Down | W / S or Up / Down |
| Turn left / right | Back / Enter | A / D or Left / Right |
| Fire | Back + Enter | Space |
| Open a door or use a switch | Erase + Down | E or Enter |
| Strafe left / right | — | Q / C |
| Open or close menu | Tap Down + Back | Esc |
| Automap | Down + Enter | Tab |
| Return to selector | Hold Down + Back | Hold Backspace |

Hold Erase with Back or Enter to walk forward while turning. In the game menu, Erase and Down move the selection, Back and Enter adjust settings, and Erase + Down confirms. In the automap, Erase + Down toggles follow mode. DOOM starts in Hangar. Advance the results screen with Use to continue. The complete shareware build loads each next map from eMMC, which adds a brief pause.

The green, yellow and red LEDs track health. The built-in beeper plays different pitch patterns for weapon fire, doors, pickups and damage.

| Loading DOOM | Secret level E1M9 |
| --- | --- |
| ![HackerZone loading screen with the DOOM logo](screenshots/doom-loading.png) | ![E1M9 Military Base captured over USB](screenshots/doom-e1m9.png) |

## Other games

| Game | Controls |
| --- | --- |
| Neon Circuit | Steer with Back / Enter, boost with Erase, and brake with Down. From the USB viewer, use Left / Right or A / D to steer, Up, W, or Space to boost, and Down / S to brake. Pass the traffic and reach the finish before the timer runs out. |
| Dino Run | Tap any scanner button or press **Space** in the viewer to jump. Clear the cacti as the runner picks up speed. |

The LEDs are off in the selector and yellow while a game loads. During play they show the following states.

| Game | Green | Yellow | Red |
| --- | --- | --- | --- |
| DOOM | Over 50 health | 26–50 health | 25 health or less |
| Neon Circuit | Racing or finished | Off road or under seven seconds left | Collision or time expired |
| Dino Run | Running | Loading | Game over |

## Preview the game on a PC

Install the latest DOOM build with `-UsbGame` and keep the scanner connected to Windows through USB. Run the viewer with Python and Tkinter, using [your scanner’s HID serial](SETUP.md#install-or-replace-the-game-over-usb). The default mode streams the full 320 × 240 LCD raster with its RGB palette and compresses suitable strips without losing pixels. Use `--scale 3` for a larger window with sharp pixel edges, `--source` for the lighter 240 × 160 game preview, or `--fast` for the 120 × 80 preview. Older firmware uses its supported uncompressed mode. The USB stream is assembled from strips, so movement can show a seam between strips.

```powershell
python -B scripts/View-FixFinderUsb.py --serial $serial
```

Click the viewer window to give it keyboard focus. In the selector, use **Up / Down** or **W / S** to browse and **Enter** to launch. In arcade games, use the arrow keys or **WASD**. Hold **Backspace** for one second to return to the selector.
