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
DESKTOP_PID = 0x028E


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


def validate_loader(loader):
    if not 8 <= len(loader) <= GAME_MANIFEST - FLASH_BASE or not vectors_ok(loader, FLASH_BASE, 0x20030000):
        raise ValueError("Expected the resident loader linked at 0x08000000 within sector 0.")
    loader_id = struct.pack("<HH", VID, LOADER_PID)
    if not any(loader[i:i + 2] == b"\x12\x01" and loader[i + 8:i + 12] == loader_id
               for i in range(len(loader) - 17)):
        raise ValueError("Expected the FixFinder 028B USB loader, not the archived V8 image.")

def split_usb_image(data):
    """Return the application payload and whether it came from a combined image."""
    manifest_offset = GAME_MANIFEST - FLASH_BASE
    game_offset = GAME_BASE - FLASH_BASE
    if (len(data) >= 8 and vectors_ok(data, GAME_BASE, 0x2002FFF8)
            or len(data) < game_offset
            or struct.unpack_from("<I", data, manifest_offset)[0] != MANIFEST_MAGIC):
        return validate_game(data), False

    validate_loader(data[:manifest_offset])
    size, crc = struct.unpack_from("<II", data, manifest_offset + 4)
    game = data[game_offset:]
    if (len(game) != size or size % 4 or zlib.crc32(game) != crc
            or data[manifest_offset + 12:game_offset] != b"\xff" * (game_offset - manifest_offset - 12)):
        raise ValueError("Combined flash image has an invalid game size, CRC, or manifest padding.")
    return validate_game(game), True


def combine_image(loader, game):
    validate_loader(loader)
    manifest_offset = GAME_MANIFEST - FLASH_BASE
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
