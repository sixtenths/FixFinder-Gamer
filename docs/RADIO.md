# FixFinder Wi-Fi loader bring-up

The experimental 5034VSM has an ESP32-WROOM-32D at BT2. The radio and GD32F427 have separate firmware. The scanner's USB game installer writes the GD32 game slot, so radio provisioning needs separate serial access. [Espressif module datasheet](https://documentation.espressif.com/esp32-wroom-32d_esp32-wroom-32u_datasheet_en.html)

## Board evidence

| Radio signal | Module contact | Status |
| --- | --- | --- |
| UART0 TXD | 35 | Direct receive-only capture produced 14 bytes but no recognizable boot log |
| UART0 RXD | 34 | Board route to the GD32 remains unverified |
| GPIO0 boot strap | 25 | Low during radio reset requests the ESP32 ROM downloader |
| Chip enable | 3 | Board-level control remains unverified |

TP46 yielded zero serial bytes at USB power-up. TP47 yielded one `ff` byte. Neither establishes the UART route. A factory GPIO snapshot configured GD32 PA0/PA1 for UART3 and PA9/PA10 for USART0. Both are candidates. ESP32 ROM programming needs a stable 3.3 V serial connection with a common ground. [Espressif boot mode guide](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)

The Pi 400's runtime `uart3` overlay exposed GPIO4 TX (header pin 7) and GPIO5 RX (header pin 29) as `/dev/ttyAMA3`. Loose jumpers could not secure the boot strap or a bidirectional connection, so ROM sync and the flash backup remain pending. The overlay was removed after the captures.

## Staged software

`scripts/Build-FixFinderRadio.ps1` builds the radio sketch with Arduino CLI and the Espressif Arduino core 3.0.7. It generates a random Wi-Fi password in its ignored build directory. `scripts/Build-UsbLoader.ps1 -RadioUart UART3` and `-RadioUart USART0` build two candidate GD32 loaders. Install only the variant with a verified bidirectional serial link.

The staged design starts Wi-Fi update mode when Enter is held at power-up. The radio opens a password-protected `FixFinder-XXXXXX` access point and forwards game reports to the GD32 over UART. The GD32 verifies the same game layout, offsets, vectors, and CRC used by USB. The installer checks the scanner serial from the GD32 before sending the erase command. Booting normally keeps the existing USB update mode.

After radio provisioning and hardware verification, join the access point and run the existing installer with `--wifi 192.168.4.1`. The current scanner firmware supports USB installation. Wi-Fi upload has been exercised on a PC test server, and both device firmware builds compile. The radio's factory flash and the GD32 bootloader should be backed up and verified before deploying the paired builds.
