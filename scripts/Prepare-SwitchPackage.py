"""Prepare one relocated FixFinder game for the local eMMC switcher."""

import argparse
from pathlib import Path
import struct
import zlib

from fixfinder_image import vectors_ok

APP_BASE = 0x08010200
APP_LIMIT = 0x082C0000
MAGIC = 0x31534746
GAMES = {"arcade": 1, "sonic": 2}


def package(game, data):
    data = data + b"\xff" * (-len(data) % 4)
    if not 8 <= len(data) <= APP_LIMIT - APP_BASE or not vectors_ok(data, APP_BASE, 0x2002FFF8):
        raise ValueError("Expected an image built with -SwitchApp for the local application slot.")
    return struct.pack("<4I", MAGIC, GAMES[game], len(data), zlib.crc32(data)) + data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", choices=GAMES)
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        args.output.write_bytes(package(args.game, args.image.read_bytes()))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
