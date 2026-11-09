# Recovery and bench tools

## Install the resident loader once over SWD

If this is still a factory scanner, obtain and verify a restorable backup before writing anything. If SWD reads are blocked by read protection or the backup cannot be verified, stop. Changing protection can erase the factory firmware. This project’s V8 image is a recovery baseline for the experimental build, and restoring V8 will not bring back the original automotive firmware. Keep an intact factory scanner for diagnostics.

Power both devices off to connect the three leads. Find **J5**, the row of six holes along the board edge beside the Kingston chip. Start at **hole 1**, the square pad, and count toward the opposite end. Power the scanner separately through its USB data cable.

### Find the J5 holes

[![Bare J5 holes on the component side, left of the Kingston chip. Hole 6 is at the top and the square pad for hole 1 is at the bottom.](photos/265.jpg)](photos/265.jpg)

In the bare board photo, J5 is on the left. From top to bottom, the holes are **6 (ground), 5, 4, 3 (SWCLK), 2 (SWDIO), 1 (square pad)**.

[![J5 with the three SWD leads attached along the lower board edge. The square pad for hole 1 is on the right and is empty.](photos/278.jpg)](photos/278.jpg)

In the wired photo, J5 runs left to right along the bottom edge. The yellow lead is on **6 (ground)**, orange is on **3 (SWCLK)**, and teal is on **2 (SWDIO)**. The empty square pad at the right is **1**. Follow the hole numbers if your leads use different colors.

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

Build the resident loader using the [installation guide](SETUP.md#build-the-resident-loader). Copy `$loaderBin` to your Pi using SCP or another file transfer and compare its SHA-256 with the Windows result. From the folder containing the copied file, recheck the UID and program the loader. Replace the three placeholders with the words you recorded.

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

## Verify or restore the V8 recovery baseline

```powershell
./scripts/Build-V8.ps1
```

The build must match the [archived V8 SHA-256](../USB-Build-V8/SHA256SUMS). V8 is the project's experimental USB baseline. To use the guarded Pi helper, review its SSH user and probe-rs path in [Deploy-V8.ps1](../scripts/Deploy-V8.ps1), then use `-Inspect` and, only for the experimental board, `-Restore -ExpectedUid 'WORD1 WORD2 WORD3'`. It verifies the archived image, core ID and board UID. A factory restoration requires your own verified factory backup.

## Preview through the Pi

Leave the Pi debug leads connected and use the ELF for the exact game build installed on the scanner. Run `arm-none-eabi-nm -S FixFinder-Doom-LCD-USB.elf` and find the addresses of `doom_frame` and `palette`. The frame is 38,400 bytes and the palette is 512 bytes.

Copy `scripts/Preview-FixFinder.py` to the Pi. Run the command below with the Pi's LAN address and the two addresses from your ELF. Substitute your Pi address and the addresses from the matching ELF. Open the private URL printed by the script from your PC on the same network. Stop the preview with Ctrl+C before using the debugger for flashing.

```bash
python3 Preview-FixFinder.py --bind <PI_LAN_IP> --frame <FRAME_ADDRESS> --palette <PALETTE_ADDRESS>
```

The browser refreshes every few seconds. It reads the game framebuffer while DOOM runs, so a capture can include parts of successive frames. The loader splash appears before the game framebuffer is available.
