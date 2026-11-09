"""Verify and combine local Midway Pac-Man ROM archives for the FixFinder build."""
import argparse
from pathlib import Path
from zipfile import ZipFile

# MAME src/mame/pacman/pacman.cpp ROM_START(pacman), in board address order.
CHIPS = (
    ("pacman.6e", 4096, 0xC1E6AB10),
    ("pacman.6f", 4096, 0x1A6FB2D4),
    ("pacman.6h", 4096, 0xBCDD1BEB),
    ("pacman.6j", 4096, 0x817D94E3),
    ("pacman.5e", 4096, 0x0C944964),
    ("pacman.5f", 4096, 0x958FEDF9),
    ("82s123.7f", 32, 0x2FC650BD),
    ("82s126.4a", 256, 0x3EB3A8E4),
)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sound-only", action="store_true", help="Extract the waveform PROM.")
    parser.add_argument("output", type=Path)
    parser.add_argument("archives", type=Path, nargs="+")
    args = parser.parse_args()
    chips = (("82s126.1m", 256, 0xA9CC86BF),) if args.sound_only else CHIPS
    found = {}
    signatures = {(length, crc): name for name, length, crc in chips}
    for path in args.archives:
        with ZipFile(path) as archive:
            for member in archive.infolist():
                name = signatures.get((member.file_size, member.CRC))
                if name and name not in found:
                    found[name] = archive.read(member)
    missing = [name for name, _, _ in chips if name not in found]
    if missing:
        parser.error("missing matching chips: " + ", ".join(missing) +
                     "; a split set may also need puckman.zip")
    args.output.write_bytes(b"".join(found[name] for name, _, _ in chips))
    print(f"Verified {len(chips)} Midway Pac-Man chip(s) against the MAME manifest.")

if __name__ == "__main__":
    main()
