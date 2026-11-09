"""Build the FixFinder bonus game pack from the CC0 CHIP-8 Archive."""
import argparse
import json
import struct
import zlib
from pathlib import Path

# CHIP-8 key numbers in FixFinder Back, Down, Enter, Erase order.
GAMES = (
    ("dinorun", "DINO RUN", (5, 5, 5, 5)),
    ("flightrunner", "FLIGHT RUNNER", (7, 8, 9, 5)),
    ("down8", "DOWN 8", (7, 5, 9, 5)),
    ("danm8ku", "DANM8KU", (7, 8, 9, 5)),
    ("tick", "TICK", (10, 10, 10, 10)),
    ("caveexplorer", "CAVE EXPLORER", (7, 4, 9, 6)),
)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    info = json.loads((args.archive / "programs.json").read_text(encoding="utf-8"))
    toc = bytearray(struct.pack("<8sII", b"FFARCADE", 1, len(GAMES)))
    data = bytearray()
    for slug, name, keys in GAMES:
        rom = (args.archive / "roms" / f"{slug}.ch8").read_bytes()
        opts = info[slug]["options"]
        flags = sum(bool(opts.get(key)) << i for i, key in enumerate((
            "shiftQuirks", "loadStoreQuirks", "jumpQuirks",
            "logicQuirks", "clipQuirks", "vBlankQuirks")))
        offset = 16 + 36 * len(GAMES) + len(data)
        if not 2 <= len(rom) <= 3584:
            raise ValueError(f"{slug}: unsupported ROM size")
        toc += struct.pack("<16sIIIHBB4B", name.encode("ascii"), offset, len(rom),
                           zlib.crc32(rom), int(opts["tickrate"]), flags, 0, *keys)
        data += rom
        print(f"{name}: {len(rom)} bytes, {', '.join(info[slug]['authors'])}")
    args.output.write_bytes(toc + data)

if __name__ == "__main__":
    main()
