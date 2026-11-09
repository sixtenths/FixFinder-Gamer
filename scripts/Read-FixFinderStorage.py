"""Read the FixFinder storage diagnostic over the game USB interface."""
import argparse
import importlib.util
import struct
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--dump", type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location(
        "viewer", Path(__file__).with_name("View-FixFinderUsb.py"))
    viewer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(viewer)
    device = viewer.connect(args.serial.upper())
    try:
        reply = viewer.read_transfer(device, 0xC8)
    finally:
        device.close()
    data = bytes(reply[768:768 + 2120])
    magic, stage, status, ocr, *words = struct.unpack_from("<16I", data)
    if magic != 0x454D4D43 or not stage & 0x80000000:
        raise SystemExit("No completed eMMC probe reported.")
    cid = bytes.fromhex("".join(f"{v:08x}" for v in words[:4]))
    capacity, rev, partition, count = words[8:]
    print(f"Stage {stage & 0x7fffffff}; SDIO status 0x{status:08x}; OCR 0x{ocr:08x}")
    print(f"CID {cid.hex()}; product {cid[3:9]!r}")
    print(f"Sectors {capacity:,}; bytes {capacity * 512:,}; EXT_CSD revision {rev}; "
          f"partition config 0x{partition:02x}; transfer words {count}")
    library_status, games = struct.unpack_from("<II", data, 2112)
    if library_status:
        print(f"FAT32 game pack: status 0x{library_status:03x}, {games} titles")
    for index, name in enumerate(("EXT_CSD", "LBA 0", "LBA 1", "Last LBA")):
        sector = data[64 + index*512:64 + (index+1)*512]
        print(f"{name:8}: {sector[:32].hex(' ')} … {sector[-16:].hex(' ')} "
              f"({sum(byte != 0 for byte in sector)} nonzero bytes)")
    if args.dump:
        args.dump.write_bytes(data)
    if (stage & 0x7fffffff) != 11:
        raise SystemExit(2)

if __name__ == "__main__":
    main()
