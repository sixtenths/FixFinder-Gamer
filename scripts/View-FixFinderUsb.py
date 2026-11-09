"""View DOOM frames from a FixFinder connected over USB to Windows."""

import argparse
import threading
import time
import zlib

import fixfinder_hid_windows as hid


WIDTH, HEIGHT = 240, 160
FAST_WIDTH, FAST_HEIGHT = 120, 80
PALETTE_SIZE = 256 * 3
TRANSFER_SIZE = PALETTE_SIZE + FAST_WIDTH * FAST_HEIGHT
REPORT_SIZE = 64
PAYLOAD_SIZE = REPORT_SIZE - 3


def connect(serial):
    paths = list(hid.handles(0x28E9, 0x028D, serial))
    if len(paths) != 1:
        raise OSError(f"Waiting for one USB video game with serial {serial}.")
    device = hid.Device(paths[0])
    try:
        device.set_input_buffers(256)
    except OSError:
        device.close()
        raise
    return device


def read_transfer(device, command):
    tagged = command != 0xC3
    size = TRANSFER_SIZE + tagged
    device.write(bytes((0x14, command)))
    data = bytearray()
    deadline = time.monotonic() + 4
    while len(data) < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Timed out waiting for a complete USB frame.")
        report = device.read(REPORT_SIZE, max(1, int(remaining * 1000)))
        if len(report) != REPORT_SIZE or report[0] != 0x42:
            raise ValueError("Unexpected USB video report.")
        offset = int.from_bytes(report[1:3], "little")
        if offset == 0:
            data.clear()
            if tagged and report[3] != command:
                continue
        elif not data:
            # A previous viewer may have left part of a frame in flight.
            continue
        if offset != len(data):
            raise ValueError("USB frame missed a packet.")
        data.extend(report[3:3 + min(PAYLOAD_SIZE, size - offset)])
    return bytes(data[1:] if tagged else data)


def read_frame(device, fast):
    rgb = bytearray()
    for command in ((0xC3,) if fast else range(0xC4, 0xC8)):
        part = read_transfer(device, command)
        palette = [part[i:i + 3] for i in range(0, PALETTE_SIZE, 3)]
        rgb.extend(b"".join(palette[i] for i in part[PALETTE_SIZE:]))
    return rgb


def view(serial, fast):
    import tkinter as tk

    root = tk.Tk()
    root.title("FixFinder DOOM USB Viewer")
    placeholder = tk.PhotoImage(width=480, height=320)
    picture = tk.Label(root, background="#181818", image=placeholder)
    picture.pack(fill="both", expand=True)
    status = tk.StringVar(value="Connecting…")
    tk.Label(root, textvariable=status).pack(fill="x")
    stopped = threading.Event()
    latest = ("Connecting…", None)
    shown = None
    width, height = (FAST_WIDTH, FAST_HEIGHT) if fast else (WIDTH, HEIGHT)
    header = f"P6\n{width} {height}\n255\n".encode()

    def receive():
        nonlocal latest
        while not stopped.is_set():
            device = None
            try:
                device = connect(serial)
                while not stopped.is_set():
                    start = time.monotonic()
                    image = header + read_frame(device, fast)
                    fps = 1 / (time.monotonic() - start)
                    latest = (f"USB • {width} × {height} • {fps:.1f} fps", image)
            except (OSError, ValueError) as error:
                latest = (str(error), None)
                stopped.wait(0.4)
            finally:
                if device is not None:
                    device.close()

    def refresh():
        nonlocal shown
        current = latest
        if current is not shown:
            message, data = current
            status.set(message)
            if data is not None:
                picture.image = tk.PhotoImage(data=data, format="PPM").zoom(4 if fast else 2)
                picture.configure(image=picture.image, width=480, height=320)
            shown = current
        root.after(30, refresh)

    def close():
        stopped.set()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", close)
    threading.Thread(target=receive, daemon=True).start()
    refresh()
    root.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, type=str.upper)
    parser.add_argument("--frames", type=int, help="Read this many frames without opening a window.")
    parser.add_argument("--fast", action="store_true", help="Use the faster 120 × 80 preview.")
    args = parser.parse_args()
    if args.frames is None:
        view(args.serial, args.fast)
        return
    if args.frames < 1:
        parser.error("--frames must be positive")
    device = connect(args.serial)
    try:
        start = time.monotonic()
        for _ in range(args.frames):
            frame = read_frame(device, args.fast)
            print(f"USB frame {len(frame)} bytes, CRC-32 {zlib.crc32(frame):08x}")
        print(f"{args.frames / (time.monotonic() - start):.1f} frames/s")
    finally:
        device.close()


if __name__ == "__main__":
    main()
