# Play and stream

After boot, the HackerZone Arcade build opens the game selector. **Erase** goes to the previous game, **Down** goes to the next game, and **Enter** launches it. Over USB, use **Up / Down** to browse and **Enter** to launch. See the save controls below for returning to the selector.

## Save and resume

On save-enabled firmware, press **Down + Back** during an arcade round to save it and return to the selector. In the USB viewer, press **Backspace**. Wait for **Progress saved**. Select the game and press **Enter** to resume. Press **Back** on the scanner or **Left Arrow** in the viewer to start a new run without using the saved checkpoint. Each arcade game keeps one suspended round. A new suspend becomes that game's current checkpoint. After a round ends, hold **Down + Back** or **Backspace** to return. Builds without saves use the same hold to exit a round.

In DOOM, tap **Down + Back** or press **Esc**, select **Save Game**, then choose one of the eight slots. Open **Load Game** and choose the slot to continue. The GBADoom checkpoint restores the saved map, weapons and ammo and restarts at the map entrance. It does not record exact position or changes to the level.

The save files live on FAT32 eMMC and stay there during USB firmware updates. The one-map and full episode builds have separate DOOM slot files. [Build with save files](SETUP.md#enable-save-files)

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
| Midway Pac-Man | Steer with Erase, Down, Back, and Enter. Press Enter + Back to pause or resume. Press Erase + Enter to start another game. In the USB viewer, use the direction keys, P to pause, and R to restart. Requires the local Pac-Man ROM build. |
| Maze Chase | Use the same directions to collect dots and dodge ghosts. Included in builds without Pac-Man. |
| Snake | Use Erase, Down, Back, and Enter to steer up, down, left, and right. |
| Pong | Move your paddle with Erase and Down. |
| Breakout | Move the paddle with Back and Enter. Clear the bricks before losing three balls. |
| Neon Circuit | Steer with Back / Enter, boost with Erase, and brake with Down. From the USB viewer, use Left / Right or A / D to steer, Up, W, or Space to boost, and Down / S to brake. Pass the traffic and reach the finish before the timer runs out. |
| Dino Run | Tap any scanner button or press **Space** in the viewer to jump. Clear the cacti as the runner picks up speed. |

The LEDs are off in the selector and yellow while a game loads. During play they show the following states.

| Game | Green | Yellow | Red |
| --- | --- | --- | --- |
| DOOM | Over 50 health | 26–50 health | 25 health or less |
| Midway Pac-Man | Three or more lives | Two lives, starting, or paused | Last life, just lost a life, or game over |
| Snake | Playing or won | Just ate food | Game over |
| Maze Chase | Three lives | Two lives or powered up | Last life or game over |
| Pong | Tied, ahead, or won | Opponent is ahead | Lost the match |
| Breakout | Three balls or won | Two balls | Last ball or game over |
| Neon Circuit | Racing or finished | Off road or under seven seconds left | Collision or time expired |
| Dino Run | Running | Loading | Game over |

## Preview the game on a PC

Install the latest DOOM build with `-UsbGame` and keep the scanner connected to Windows through USB. Run the viewer with Python and Tkinter, using [your scanner’s HID serial](SETUP.md#install-or-replace-the-game-over-usb). The default mode streams the full 320 × 240 LCD raster with its RGB palette and compresses suitable strips without losing pixels. Use `--scale 3` for a larger window with sharp pixel edges, `--source` for the lighter 240 × 160 game preview, or `--fast` for the 120 × 80 preview. Older firmware uses its supported uncompressed mode. The USB stream is assembled from strips, so movement can show a seam between strips.

```powershell
python -B scripts/View-FixFinderUsb.py --serial $serial
```

Click the viewer window to give it keyboard focus. In the selector, use **Up / Down** or **W / S** to browse and **Enter** to launch. In arcade games, use the arrow keys or **WASD**. Hold **Backspace** for one second to return to the selector.
