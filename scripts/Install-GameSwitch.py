"""Provision both games on the scanner and install the local game switcher once."""

import argparse
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import time
import zlib

from fixfinder_image import validate_game

ROOT = Path(__file__).resolve().parent
SEED_STATUS = 0x53454544
SEED_ERROR = 0x53450000
SWITCH_MAGIC = 0x31534746


def files(directory):
    bundles = []
    for game, kind in (("arcade", 1), ("sonic", 2)):
        package = (directory / f"{game}.pkg").read_bytes()
        magic, actual_kind, length, crc = struct.unpack_from("<4I", package)
        if (magic != SWITCH_MAGIC or actual_kind != kind or length != len(package) - 16
                or zlib.crc32(package[16:]) != crc):
            raise ValueError(f"Invalid local package for {game}.")
        seed = directory / f"{game}-seed.bin"
        data = seed.read_bytes()
        validate_game(data)
        if package not in data:
            raise ValueError(f"The seed does not contain the verified {game} package.")
        bundles.append((game, seed, crc))
    supervisor = directory / "FixFinder-Switcher-USB.bin"
    if len(validate_game(supervisor.read_bytes())) > 0x8000:
        raise ValueError("The switcher exceeds its protected flash region.")
    return bundles, supervisor


def install(path, serial):
    subprocess.run([sys.executable, "-B", str(ROOT / "Install-UsbGame.py"),
                    "--serial", serial, "--wait", "45", str(path)], check=True)


def wait_for_seed(viewer, serial, expected):
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        try:
            device = viewer.connect(serial)
            try:
                packet = viewer.read_transfer(device, 0xC8, timeout=3)
            finally:
                device.close()
        except OSError:
            time.sleep(.3)
            continue
        status, crc = struct.unpack_from("<II", packet, 768 + 2112)
        if status & 0xFFFF0000 == SEED_ERROR and status != SEED_STATUS:
            step = {1: "validate the image or storage guard", 2: "mount FAT32",
                    3: "open the temporary file", 4: "write or close the file",
                    5: "verify the file", 6: "publish the file"}.get(status & 0xffff, "finish the copy")
            raise OSError(f"The scanner could not {step}.")
        if status == SEED_STATUS:
            if crc != expected:
                raise OSError("The scanner could not write and verify the game on eMMC.")
            return
    raise TimeoutError("Timed out waiting for the scanner to verify its eMMC copy.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="output directory from Build-FixFinderGameSwitch.ps1")
    parser.add_argument("--serial", required=True, type=str.upper)
    args = parser.parse_args()
    try:
        bundles, supervisor = files(args.directory)
        spec = importlib.util.spec_from_file_location("viewer", ROOT / "View-FixFinderUsb.py")
        viewer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(viewer)
        for game, seed, crc in bundles:
            print(f"Provisioning {game} on the scanner's eMMC…", flush=True)
            install(seed, args.serial)
            wait_for_seed(viewer, args.serial, crc)
            print(f"Verified {game} package on eMMC (CRC-32 {crc:08X}).", flush=True)
        print("Installing the permanent local switcher…", flush=True)
        install(supervisor, args.serial)
    except (OSError, ValueError, struct.error, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
