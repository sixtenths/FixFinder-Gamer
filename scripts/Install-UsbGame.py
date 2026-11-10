"""Flash FixFinder application firmware over USB."""

import argparse
from pathlib import Path
import socket
import struct
import sys
import time
import zlib
from fixfinder_image import GAME_BASE, GAME_CAPACITY, LOADER_PID, VID, split_usb_image

GAME_PID = 0x028C
VIDEO_GAME_PID = 0x028D
REPORT_ID = 0x41
REPORT_SIZE = 64
CHUNK = 56
INFO, BEGIN, WRITE, COMMIT, RUN = range(1, 6)
WIFI_PORT = 5034


class WifiDevice:
    def __init__(self, host, wait):
        deadline = time.monotonic() + wait
        while True:
            try:
                self.stream = socket.create_connection((host, WIFI_PORT), timeout=2)
                break
            except OSError as error:
                if time.monotonic() >= deadline:
                    raise OSError(f"Could not reach the FixFinder radio at {host}:{WIFI_PORT}.") from error
                time.sleep(0.2)
        self.stream.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def write(self, data):
        self.stream.settimeout(10)
        self.stream.sendall(data)
        return len(data)

    def read(self, length, timeout):
        self.stream.settimeout(timeout / 1000)
        data = bytearray()
        while len(data) < length:
            chunk = self.stream.recv(length - len(data))
            if not chunk:
                raise OSError("The radio closed the connection before the loader replied.")
            data.extend(chunk)
        return data

    def close(self):
        self.stream.close()


def connect(hid, serial, wait):
    deadline = time.monotonic() + wait
    requested_reset = False
    while True:
        devices = list(hid.interfaces(VID, (LOADER_PID, VIDEO_GAME_PID, GAME_PID)))
        if serial is None:
            serials = {actual_serial for _, actual_serial, _ in devices}
            if len(serials) > 1:
                raise ValueError("More than one FixFinder is connected. Unplug the others or pass --serial.")
            serial = next(iter(serials), None)
        paths = {}
        for pid, actual_serial, path in devices:
            if actual_serial != serial:
                continue
            if pid in paths:
                raise ValueError(f"More than one matching USB interface for serial {serial}.")
            paths[pid] = path
        path = paths.get(LOADER_PID)
        if path is not None:
            try:
                return hid.Device(path), serial
            except OSError:
                pass
        if not requested_reset:
            path = paths.get(VIDEO_GAME_PID) or paths.get(GAME_PID)
            if path is not None:
                try:
                    device = hid.Device(path)
                except OSError:
                    pass
                else:
                    try:
                        device.write(bytes([0x14, 0xB7]))
                    except OSError:
                        # A reset can disconnect the device before the write returns.
                        pass
                    finally:
                        requested_reset = True
                        device.close()
        if time.monotonic() >= deadline:
            raise ValueError("Loader not found. Start this command, then reconnect scanner USB.")
        time.sleep(0.15)


def exchange(device, command, argument=0, payload=b""):
    request = struct.pack("<BBBBI", REPORT_ID, command, 0, 0, argument) + payload
    if device.write(request.ljust(REPORT_SIZE, b"\0")) != REPORT_SIZE:
        raise OSError("Incomplete loader report write.")
    answer = bytes(device.read(REPORT_SIZE, 10000))
    if len(answer) != REPORT_SIZE or answer[:2] != bytes([REPORT_ID, command]):
        raise OSError("Missing or unexpected loader reply.")
    if answer[3] != 1:
        raise ValueError(f"Unsupported USB loader protocol version {answer[3]}.")
    if answer[2]:
        messages = {1: "invalid request or offset", 2: "flash erase or write failed",
                    3: "game verification failed or no valid game is installed"}
        raise ValueError(f"Loader rejected command {command}: "
                         f"{messages.get(answer[2], answer[2])}.")
    return answer


def install(device, data):
    checksum = zlib.crc32(data)
    exchange(device, BEGIN, len(data), struct.pack("<I", checksum))
    next_progress = 0
    for offset in range(0, len(data), CHUNK):
        block = data[offset:offset + CHUNK]
        reply = exchange(device, WRITE, offset, block)
        written, = struct.unpack_from("<I", reply, 4)
        if written != offset + len(block):
            raise OSError(f"Expected offset {offset + len(block)}, received {written}.")
        if written >= next_progress or written == len(data):
            print(f"\rUploading {written:,}/{len(data):,} bytes", end="", flush=True)
            next_progress = written + 65536
    print()
    exchange(device, COMMIT)
    print(f"Verified on the scanner (CRC-32 {checksum:08X}).")


def check_layout(info):
    if struct.unpack_from("<II", info, 8) != (GAME_BASE, GAME_CAPACITY):
        raise ValueError("Loader reports an unexpected flash layout.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", nargs="?", type=Path, help="application BIN or combined FixFinder flash BIN")
    parser.add_argument("--serial", type=str.upper, help="scanner USB serial when several are connected")
    parser.add_argument("--wait", type=float, default=30, help="seconds to wait for the loader")
    parser.add_argument("--wifi", metavar="HOST", help="use an equipped scanner's Wi-Fi loader")
    parser.add_argument("--run", action="store_true", help="start the already installed game")
    args = parser.parse_args()
    if args.run and args.game:
        parser.error("--run cannot be combined with a game BIN")

    if args.wifi and not args.serial:
        parser.error("--wifi requires --serial.")

    try:
        data, combined = split_usb_image(args.game.read_bytes()) if args.game else (None, False)
        if combined:
            print("Combined image verified. Installing its application and keeping the resident loader.")
        target = f"FixFinder {args.serial}" if args.serial else "one USB FixFinder"
        print(f"Waiting for {target}…", flush=True)
        if args.wifi:
            device = WifiDevice(args.wifi, args.wait)
        else:
            if sys.platform != "win32":
                raise ValueError("USB mode uses the Windows HID driver.")
            import fixfinder_hid_windows as hid

            device, args.serial = connect(hid, args.serial, args.wait)
        try:
            info = exchange(device, INFO)
            check_layout(info)
            if args.wifi:
                uid0, uid1, uid2 = struct.unpack_from("<III", info, 20)
                serial = f"{(uid0 + uid2) & 0xffffffff:08X}{uid1 >> 16:04X}"
                if serial != args.serial:
                    raise ValueError(f"Radio reports scanner {serial}, expected {args.serial}.")
            connection = "Wi-Fi" if args.wifi else "USB"
            print(f"{connection} loader connected to {args.serial}. Game installed: {'yes' if info[16] else 'no'}.")
            if data is not None:
                install(device, data)
            if data is not None or args.run:
                exchange(device, RUN)
                print("Starting the installed game.")
        finally:
            device.close()
    except (OSError, ValueError) as error:
        parser.exit(1, f"\n{error}\n")


if __name__ == "__main__":
    main()
