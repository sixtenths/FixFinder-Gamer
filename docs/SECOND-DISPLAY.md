# Use the FixFinder as a second display

The Windows app sends an active Windows display to the scanner at its native 320 × 240 resolution over a USB data cable. The arcade selector stays installed. Open it before connecting the app, then click **Disconnect** or press **Back** on the scanner to return to the games.

## Set up the Windows display

1. For an extended desktop, enable an existing virtual monitor or install a signed Windows virtual display driver. Driver setup requires Windows administrator access. The app’s **Virtual monitor driver** button opens the upstream release page. Download and extract Virtual Driver Control, open it, and install its display driver. [Upstream installation instructions](https://github.com/VirtualDrivers/Virtual-Display-Driver#-installation)
2. In **Settings → System → Display**, select **Extend these displays**. Use **Identify** to match the virtual monitor to the app’s display list. [Microsoft’s multiple-monitor instructions](https://support.microsoft.com/en-gb/windows/hardware/display-graphics/how-to-use-multiple-monitors-in-windows)
3. If you want to mirror an existing monitor, select that active monitor in the app. This works with the Windows HID driver already used by the arcade viewer.

### Match the FixFinder’s size

Add **320 × 240 at 60 Hz** and **640 × 480 at 60 Hz** in Virtual Driver Control’s **Displays** settings. If you edit `vdd_settings.xml` directly, add these entries inside its existing `<resolutions>` element and reload the driver. [Virtual Driver Control](https://github.com/VirtualDrivers/Virtual-Driver-Control/blob/main/VirtualDriverControl/README.md) · [Example driver configuration](https://github.com/VirtualDrivers/Virtual-Display-Driver/blob/master/Virtual%20Display%20Driver%20%28HDR%29/vdd_settings.xml)

```xml
<resolution>
  <width>320</width><height>240</height><refresh_rate>60</refresh_rate>
</resolution>
<resolution>
  <width>640</width><height>480</height><refresh_rate>60</refresh_rate>
</resolution>
```

In FixFinder Second Display, click **Refresh displays**, select the virtual monitor, and click **Match FixFinder size**. The app tries a 320 × 240 Windows desktop first and copies it pixel for pixel. If Windows or the driver rejects that mode, it tries 640 × 480 and scales it down by exactly 2× to fill the panel.

## Connect the scanner

1. Install a desktop-enabled arcade build over USB. The current [built-in arcade download](../downloads/FixFinder-Arcade.bin) includes desktop mode. Build a local [DOOM and Pac-Man arcade image](SETUP.md#add-original-arcade-pac-man) if you use those games. On a converted scanner, use the existing [USB installer](SETUP.md#install-or-replace-the-game-over-usb) to install the appropriate image.
2. Open [FixFinder-SecondDisplay.pyw](../scripts/FixFinder-SecondDisplay.pyw) on Windows with Python and Tkinter. If Windows does not associate `.pyw` with Python, run `python scripts/FixFinder-SecondDisplay.pyw` from the repository.
3. Leave the scanner on its game selector and close the USB game viewer. In the app, choose the Windows display and click **Connect**. Drag a window to that desktop or use **Windows + Shift + Left/Right Arrow** to move it between displays.

The app shows the rate of delivered updates. The latest firmware sends rectangles around changed pixels and compresses them losslessly at native resolution. Very small updates use short USB reports. The app also works with earlier desktop-enabled firmware at its original transfer speed. Full-screen motion with little repeated detail needs more USB bandwidth than a moving cursor or a dashboard.

If exactly one secondary virtual monitor is active, launch the app with `--connect-virtual` to connect to it automatically.

## Smoother video

Install Pillow and NumPy with `python -m pip install Pillow numpy`, then select **Video mode (smoother motion)** before connecting. With the updated firmware, the app sends 240 × 180 frames with four local colors per 8 × 12 block. It can upload a frame while the scanner draws the preceding one. The lower color precision can show banding in moving gradients. The app restores the regular 320 × 240 RGB565 image when motion settles. Windows keeps the selected desktop size. Use `--video` to start with this option selected.

The app uses JPEG for motion on earlier firmware or if NumPy is unavailable. The firmware uses TJpgDec R0.03 with RGB565 output and fast Huffman decoding enabled. [TJpgDec project and license](https://elm-chan.org/fsw/tjpgd/) · [Pinned source mirror](https://github.com/lvgl/lv_lib_split_jpg/tree/c292bfd196fdae251069efb5f1c2f0c1040d764f)
