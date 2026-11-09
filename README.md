# FixFinder DOOM

This project targets the experimental AutoZone FixFinder 5034VSM. V8 USBHS HID is the verified firmware baseline. See the [status log](docs/STATUS.md) and [LCD bring-up notes](docs/LCD.md).

## Rebuild V8 on Windows

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build-V8.ps1
```

The script uses Arm GNU Toolchain 15.2.Rel1 from the existing FixFinder installation. Pass `-ToolchainBin` to select another installation of that release. It compiles from source into a fresh directory under `build/`, emits an ELF, BIN and link map, and requires the BIN to match the [recovery checksum](USB-Build-V8/SHA256SUMS). The original [V8 binary](USB-Build-V8/FixFinder-USB-V8.bin) and ELF are archived with the source and original objects.

## Raspberry Pi

The deployment script defaults to uploading and checking the archived V8 image in a private staging directory on `zac@pi`. It requires a trusted host key and batch-mode SSH authentication. `-Inspect` separately reads the CPUID, MCU UID and first two vector words over SWD. The expected V8 vectors are `20020000 08001ca1`. SWD access requires non-interactive sudo on the Pi.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Deploy-V8.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Deploy-V8.ps1 -Inspect
```

Record the full UID in address order after physically identifying the SWD target as the experimental board. For V8 recovery, pass `-Restore -ExpectedUid '<word1> <word2> <word3>'`. The script checks the image hash, CPUID and UID and uses a verified sector download at `0x08000000`. It accepts only the archived V8 binary. Connect the Pi to the experimental scanner only, using SWDIO, SWCLK and ground. Power the scanner separately over USB.
