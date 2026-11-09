"""Pack a resident loader and USB game as one FixFinder SWD flash image."""

import argparse
from pathlib import Path
import struct
import zlib

FLASH_BASE = 0x08000000
GAME_MANIFEST = 0x08004000
GAME_BASE = 0x08008000
GAME_CAPACITY = 0x08300000 - GAME_BASE
MANIFEST_MAGIC = 0x31474646
VID = 0x28E9
LOADER_PID = 0x028B


def vectors_ok(image, base, stack_top):
    stack, reset = struct.unpack_from("<II", image)
    return (
        0x20000000 < stack <= stack_top and stack % 8 == 0
        and reset & 1 and base + 8 <= (reset & ~1) < base + len(image)
    )


def validate_game(data):
    if not 8 <= len(data) <= GAME_CAPACITY:
        raise ValueError(f"Game must be 8 to {GAME_CAPACITY:,} bytes.")
    if not vectors_ok(data, GAME_BASE, 0x2002FFF8):
        raise ValueError("The BIN is not linked for the FixFinder USB game slot at 0x08008000.")
    return data + b"\xff" * (-len(data) % 4)


def combine_image(loader, game):
    manifest_offset = GAME_MANIFEST - FLASH_BASE
    if not 8 <= len(loader) <= manifest_offset or not vectors_ok(loader, FLASH_BASE, 0x20030000):
        raise ValueError("Expected the resident loader linked at 0x08000000 within sector 0.")
    loader_id = struct.pack("<HH", VID, LOADER_PID)
    if not any(loader[i:i + 2] == b"\x12\x01" and loader[i + 8:i + 12] == loader_id
               for i in range(len(loader) - 17)):
        raise ValueError("Expected the FixFinder 028B USB loader, not the archived V8 image.")

    game = validate_game(game)
    manifest = struct.pack("<III", MANIFEST_MAGIC, len(game), zlib.crc32(game))
    return (
        loader.ljust(manifest_offset, b"\xff")
        + manifest.ljust(GAME_BASE - GAME_MANIFEST, b"\xff")
        + game
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("loader", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.resolve() in (args.loader.resolve(), args.game.resolve()):
        parser.error("Use a separate output path for the combined image.")
    try:
        image = combine_image(args.loader.read_bytes(), args.game.read_bytes())
        args.output.write_bytes(image)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(f"{args.output} ({len(image):,} bytes, SWD base 0x{FLASH_BASE:08X})")


if __name__ == "__main__":
    main()
