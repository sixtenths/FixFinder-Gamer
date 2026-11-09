"""View DOOM frames from a FixFinder connected over USB to Windows."""

import argparse
import threading
import time
import zlib

import fixfinder_hid_windows as hid


WIDTH, HEIGHT = 240, 160
LCD_WIDTH, LCD_HEIGHT = 320, 240
FAST_WIDTH, FAST_HEIGHT = 120, 80
PALETTE_SIZE = 256 * 3
TRANSFER_SIZE = PALETTE_SIZE + FAST_WIDTH * FAST_HEIGHT
REPORT_SIZE = 64
PAYLOAD_SIZE = REPORT_SIZE - 3
PACKED_BANDS = range(0xD1, 0xD9)

ARCADE_KEYS = {
    "left": 1, "a": 1, "down": 2, "s": 2, "right": 4, "d": 4,
    "return": 4, "up": 8, "w": 8,
}
DOOM_KEYS = {
    "up": 1, "w": 1, "down": 2, "s": 2, "left": 4, "a": 4,
    "right": 8, "d": 8, "space": 16, "e": 32, "return": 32,
    "escape": 64, "tab": 128, "q": 256, "c": 512,
}


def keyboard_mask(held):
    arcade = doom = 0
    for key in held:
        arcade = ARCADE_KEYS.get(key, arcade)
        doom |= DOOM_KEYS.get(key, 0)
    if "backspace" in held:
        return 16
    if "r" in held:
        arcade = 8 | 4  # Pac-Man's coin and restart chord.
    elif "p" in held:
        arcade = 4 | 1  # Pac-Man's pause chord.
    return arcade | (doom << 5)


def send_keys(device, mask):
    commands = (0x20 | (mask & 31), 0x40 | ((mask >> 5) & 31), 0x60 | (mask >> 10)) if mask else (0x1F,)
    for command in commands:
        device.write(bytes((0x14, command)))


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


def read_transfer(device, command, timeout=4, service=None):
    tagged = command != 0xC3
    size = TRANSFER_SIZE + tagged
    device.write(bytes((0x14, command)))
    data = bytearray()
    deadline = time.monotonic() + timeout
    while len(data) < size:
        if service is not None:
            service()
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Timed out waiting for a complete USB frame.")
        wait = max(1, int(remaining * 1000))
        try:
            report = device.read(REPORT_SIZE, min(wait, 20) if service is not None else wait)
        except TimeoutError:
            if service is None:
                raise
            continue
        if len(report) != REPORT_SIZE or report[0] != 0x42:
            raise ValueError("Unexpected USB video report.")
        offset = int.from_bytes(report[1:3], "little")
        if offset == 0:
            data.clear()
            if tagged and report[3] != command:
                continue
            if command in PACKED_BANDS:
                length = int.from_bytes(report[4:6], "little")
                if not 1 <= length <= FAST_WIDTH * FAST_HEIGHT:
                    raise ValueError("Invalid packed USB band length.")
                size = 3 + PALETTE_SIZE + length
        elif not data:
            # A previous viewer may have left part of a frame in flight.
            continue
        if offset != len(data):
            raise ValueError("USB frame missed a packet.")
        data.extend(report[3:3 + min(PAYLOAD_SIZE, size - offset)])
    return bytes(data[1:] if tagged else data)


def unpack_pixels(payload):
    if len(payload) == FAST_WIDTH * FAST_HEIGHT:
        return payload
    pixels = bytearray()
    cursor = 0
    while cursor < len(payload):
        control = payload[cursor]
        cursor += 1
        count = (control & 127) + 1
        size = 1 if control & 128 else count
        if cursor + size > len(payload) or len(pixels) + count > FAST_WIDTH * FAST_HEIGHT:
            raise ValueError("Invalid USB pixel run.")
        pixels.extend(payload[cursor:cursor + size] * (count if control & 128 else 1))
        cursor += size
    if len(pixels) != FAST_WIDTH * FAST_HEIGHT:
        raise ValueError("Incomplete USB pixel band.")
    return pixels


def read_frame(device, fast, native=False, probe=False, service=None, packed=False):
    rgb = bytearray()
    commands = PACKED_BANDS if packed else range(0xC9, 0xD1) if native else ((0xC3,) if fast else range(0xC4, 0xC8))
    for index, command in enumerate(commands):
        part = read_transfer(device, command, timeout=1 if probe and index == 0 else 4, service=service)
        if packed:
            part = part[2:]
        palette = [part[i:i + 3] for i in range(0, PALETTE_SIZE, 3)]
        pixels = part[PALETTE_SIZE:]
        rgb.extend(b"".join(palette[i] for i in (unpack_pixels(pixels) if packed else pixels)))
    return rgb


def frames(device, fast, source, service=None):
    if not (fast or source):
        for packed in (True, False):
            try:
                first = read_frame(device, False, native=True, probe=True, service=service, packed=packed)
            except TimeoutError:
                continue
            yield LCD_WIDTH, LCD_HEIGHT, first
            while True:
                yield LCD_WIDTH, LCD_HEIGHT, read_frame(device, False, native=True, service=service, packed=packed)
    while True:
        if fast:
            yield FAST_WIDTH, FAST_HEIGHT, read_frame(device, True, service=service)
        else:
            yield WIDTH, HEIGHT, read_frame(device, False, service=service)


def view(serial, fast, source, scale):
    import tkinter as tk

    root = tk.Tk()
    root.title("FixFinder USB Viewer")
    placeholder = tk.PhotoImage(width=LCD_WIDTH * scale, height=LCD_HEIGHT * scale)
    picture = tk.Label(root, background="#181818", image=placeholder)
    picture.pack(fill="both", expand=True)
    status = tk.StringVar(value="Connecting…")
    tk.Label(root, textvariable=status).pack(fill="x")
    tk.Label(root, text="Arrows / WASD  ·  Enter to launch  ·  Hold Backspace for selector").pack(fill="x")
    stopped = threading.Event()
    latest = ("Connecting…", None)
    shown = None
    held = {}
    desired = 0

    def key(event, pressed):
        nonlocal desired
        name = event.keysym.lower()
        if name not in DOOM_KEYS and name not in ("r", "p", "backspace"):
            return None
        if pressed:
            held[name] = None
        else:
            held.pop(name, None)
        desired = keyboard_mask(held)
        return "break"

    def clear_keys(_event=None):
        nonlocal desired
        held.clear()
        desired = 0

    def receive():
        nonlocal latest
        while not stopped.is_set():
            device = None
            try:
                device = connect(serial)
                sent = None
                heartbeat = 0

                def service_input():
                    nonlocal sent, heartbeat
                    closing = stopped.is_set()
                    mask = 0 if closing else desired
                    now = time.monotonic()
                    if mask != sent or (mask and now >= heartbeat):
                        send_keys(device, mask)
                        sent = mask
                        heartbeat = now + 0.075
                    if closing:
                        raise InterruptedError()

                start = time.monotonic()
                for width, height, frame in frames(device, fast, source, service=service_input):
                    if stopped.is_set():
                        break
                    now = time.monotonic()
                    fps = 1 / (now - start)
                    start = now
                    mode = "LCD" if width == LCD_WIDTH else "Preview"
                    image = f"P6\n{width} {height}\n255\n".encode() + frame
                    latest = (f"USB • {mode} {width} × {height} • {fps:.1f} fps", image)
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
                picture.image = tk.PhotoImage(data=data, format="PPM").zoom(scale)
                picture.configure(image=picture.image)
            shown = current
        root.after(30, refresh)

    def close():
        clear_keys()
        stopped.set()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", close)
    root.bind("<KeyPress>", lambda event: key(event, True))
    root.bind("<KeyRelease>", lambda event: key(event, False))
    root.bind("<FocusOut>", clear_keys)
    picture.bind("<Button-1>", lambda _event: root.focus_set())
    worker = threading.Thread(target=receive, daemon=True)
    worker.start()
    refresh()
    root.mainloop()
    worker.join(timeout=0.3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, type=str.upper)
    parser.add_argument("--frames", type=int, help="Read this many frames without opening a window.")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--fast", action="store_true", help="Use the faster 120 × 80 preview.")
    modes.add_argument("--source", action="store_true", help="Use the 240 × 160 game preview.")
    parser.add_argument("--scale", type=int, choices=range(1, 7), help="Integer window zoom (default: 2).")
    args = parser.parse_args()
    if args.frames is None:
        view(args.serial, args.fast, args.source, args.scale or (4 if args.fast else 2))
        return
    if args.frames < 1:
        parser.error("--frames must be positive")
    device = connect(args.serial)
    try:
        start = time.monotonic()
        for index, (width, height, frame) in enumerate(frames(device, args.fast, args.source), 1):
            print(f"USB frame {width} × {height}, {len(frame)} bytes, CRC-32 {zlib.crc32(frame):08x}")
            if index == args.frames:
                break
        print(f"{args.frames / (time.monotonic() - start):.1f} frames/s")
    finally:
        device.close()


if __name__ == "__main__":
    main()
