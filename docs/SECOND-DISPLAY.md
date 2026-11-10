# Use the FixFinder as a second display

The Windows app sends an active Windows display to the scanner at its native 320 × 240 resolution over a USB data cable. The arcade selector stays installed. Open it before connecting the app, then click **Disconnect** or press **Back** on the scanner to return to the games.

## Set up the Windows display

1. For an extended desktop, enable an existing virtual monitor or install a signed Windows virtual display driver. Driver setup requires Windows administrator access. The app’s **Virtual monitor driver** button opens the upstream release page. Download and extract Virtual Driver Control, open it, and install its display driver. [Upstream installation instructions](https://github.com/VirtualDrivers/Virtual-Display-Driver#-installation)
2. In **Settings → System → Display**, select **Extend these displays**. Choose a small 4:3 resolution for the virtual monitor, such as 800 × 600, if offered. Use **Identify** to match its number to the app’s display list. [Microsoft’s multiple-monitor instructions](https://support.microsoft.com/en-gb/windows/hardware/display-graphics/how-to-use-multiple-monitors-in-windows)
3. If you want to mirror an existing monitor, select that active monitor in the app. This works with the Windows HID driver already used by the arcade viewer.

## Connect the scanner

1. Install a desktop-enabled arcade build over USB. The current [built-in arcade download](../downloads/FixFinder-Arcade.bin) includes desktop mode. Build a local [DOOM and Pac-Man arcade image](SETUP.md#add-original-arcade-pac-man) if you use those games. On a converted scanner, use the existing [USB installer](SETUP.md#install-or-replace-the-game-over-usb) to install the appropriate image.
2. Open [FixFinder-SecondDisplay.pyw](../scripts/FixFinder-SecondDisplay.pyw) on Windows with Python and Tkinter. If Windows does not associate `.pyw` with Python, run `python scripts/FixFinder-SecondDisplay.pyw` from the repository.
3. Leave the scanner on its game selector and close the USB game viewer. In the app, choose the Windows display and click **Connect**. Drag a window to that desktop or use **Windows + Shift + Left/Right Arrow** to move it between displays.

The app sends only tiles whose pixels changed. The small panel works best with large text, timers, status widgets, and simple dashboards. Full-screen motion requires more USB bandwidth than a static desktop.
