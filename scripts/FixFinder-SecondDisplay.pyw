"""Send an active Windows display to the FixFinder over USB."""

import argparse
from itertools import groupby
import os
from queue import Empty, SimpleQueue
import struct
import threading
import time
import webbrowser

import fixfinder_hid_windows as hid
from fixfinder_desktop_windows import Capture, HEIGHT, WIDTH, dpi_awareness, monitors
from fixfinder_image import DESKTOP_PID, VID

REPORT_ID, REPORT_SIZE = 0x17, 64
START, TILE, DATA, STOP, PING = range(1, 6)
TILE_W, TILE_H = 40, 20
TILE_COLS, TILE_ROWS = WIDTH // TILE_W, HEIGHT // TILE_H
DRIVER_URL = "https://github.com/VirtualDrivers/Virtual-Display-Driver/releases"


def tiles(frame):
    for top in range(0, HEIGHT, TILE_H):
        for left in range(0, WIDTH, TILE_W):
            yield b"".join(frame[(y * WIDTH + left) * 2:(y * WIDTH + left + TILE_W) * 2]
                           for y in range(top, top + TILE_H))


def encode(raw):
    pixels = struct.unpack("<800H", raw)
    runs = bytearray()
    for color, group in groupby(pixels):
        count = sum(1 for _ in group)
        while count > 255:
            runs.extend(struct.pack("<BH", 255, color))
            count -= 255
        runs.extend(struct.pack("<BH", count, color))

    palette = list(dict.fromkeys(pixels))
    choices = [(0, raw), (1, runs)]
    if len(palette) <= 16:
        by_color = {color: index for index, color in enumerate(palette)}
        indices = [by_color[pixel] for pixel in pixels]
        packed = bytes(a | b << 4 for a, b in zip(indices[::2], indices[1::2]))
        choices.append((2, struct.pack(f"<B{len(palette)}H", len(palette), *palette) + packed))
    return min(choices, key=lambda item: len(item[1]))


class DisplayLink:
    def __init__(self, serial=None):
        paths = [(actual, path) for _, actual, path in hid.interfaces(VID, (DESKTOP_PID,))
                 if serial is None or actual == serial]
        if len(paths) != 1:
            raise OSError("Connect one FixFinder with desktop-enabled arcade firmware over USB.")
        self.serial, path = paths[0]
        self.device = hid.Device(path, output_size=REPORT_SIZE)
        self.token = 0
        self.started = False

    def send(self, operation, payload=b""):
        if self.device.write(bytes((REPORT_ID, operation)) + payload) != REPORT_SIZE:
            raise OSError("The scanner did not receive a complete USB report.")

    def receive(self, operation, token=0):
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            left = max(1, int((deadline - time.monotonic()) * 1000))
            reply = self.device.read(REPORT_SIZE, left)
            if len(reply) != REPORT_SIZE or reply[:2] != bytes((REPORT_ID, operation)):
                continue
            if struct.unpack_from("<H", reply, 3)[0] != token:
                continue
            if reply[2]:
                raise OSError("The FixFinder rejected a desktop tile.")
            return
        raise TimeoutError("The FixFinder did not acknowledge the desktop update.")

    def start(self):
        self.send(START, b"\x01")
        try:
            self.receive(START, 1)
        except TimeoutError as error:
            raise TimeoutError("Open the game selector on the FixFinder and close the USB viewer, then retry.") from error
        self.started = True

    def tile(self, index, raw):
        encoding, payload = encode(raw)
        self.token = (self.token + 1) & 0xffff
        self.send(TILE, struct.pack("<HBBH", self.token, index, encoding, len(payload)))
        for offset in range(0, len(payload), REPORT_SIZE - 7):
            part = payload[offset:offset + REPORT_SIZE - 7]
            self.send(DATA, struct.pack("<HHB", self.token, offset, len(part)) + part)
        self.receive(TILE, self.token)

    def ping(self):
        self.send(PING)
        self.receive(PING)

    def close(self):
        try:
            # Also clears a pending START when the scanner was in a game.
            self.send(STOP)
            if self.started:
                self.receive(STOP)
        except OSError:
            pass
        finally:
            self.device.close()


def stream(source, cursor, serial, stop, notify):
    link = DisplayLink(serial)
    try:
        link.start()
        notify(f"Connected to {link.serial}  |  Sending first frame…")
        capture = Capture(source, cursor=cursor)
        try:
            previous = [None] * (TILE_COLS * TILE_ROWS)
            last_transfer = last_check = time.monotonic()
            changed = 0
            while not stop.is_set():
                now = time.monotonic()
                if now - last_check >= 1:
                    if source not in monitors():
                        raise OSError("The source display changed. Refresh the list and select it again.")
                    notify(f"Connected to {link.serial}  |  {changed} tiles updated")
                    last_check = now
                for index, raw in enumerate(tiles(capture.frame())):
                    if stop.is_set():
                        break
                    if raw != previous[index]:
                        link.tile(index, raw)
                        previous[index] = raw
                        changed += 1
                        last_transfer = time.monotonic()
                if time.monotonic() - last_transfer >= .75:
                    link.ping()
                    last_transfer = time.monotonic()
                stop.wait(.05)
        finally:
            capture.close()
    finally:
        link.close()


def show_app(serial):
    import tkinter as tk
    from tkinter import ttk

    root = tk.Tk()
    root.title("FixFinder Second Display")
    root.resizable(False, False)
    root.configure(bg="#121820")
    theme = ttk.Style(root)
    theme.theme_use("clam")
    theme.configure("Panel.TFrame", background="#121820")
    theme.configure("Text.TLabel", background="#121820", foreground="#e7edf3")
    theme.configure("Title.TLabel", background="#121820", foreground="#ffffff", font=("Segoe UI", 19, "bold"))
    theme.configure("Status.TLabel", background="#202a35", foreground="#b9e7db", padding=(10, 8))
    theme.configure("Option.TCheckbutton", background="#121820", foreground="#e7edf3")
    theme.map("Option.TCheckbutton", background=[("active", "#121820")])
    theme.configure("Connect.TButton", foreground="#ffffff", background="#a44210", padding=(14, 8))
    theme.map("Connect.TButton", background=[("active", "#bd541b"), ("disabled", "#393e47")])
    theme.configure("TButton", padding=(9, 5))

    panel = ttk.Frame(root, padding=22, style="Panel.TFrame")
    panel.grid()
    ttk.Label(panel, text="FixFinder Second Display", style="Title.TLabel").grid(sticky="w")
    ttk.Label(panel, text="Send a Windows display to the scanner over USB.",
              style="Text.TLabel").grid(sticky="w", pady=(2, 16))
    ttk.Label(panel, text="Source display", style="Text.TLabel").grid(sticky="w")
    picker = ttk.Combobox(panel, width=57, state="readonly")
    picker.grid(sticky="ew", pady=(4, 9))
    cursor = tk.BooleanVar(value=True)
    ttk.Checkbutton(panel, text="Show mouse pointer", variable=cursor,
                    style="Option.TCheckbutton").grid(sticky="w", pady=(0, 11))
    status = tk.StringVar(value="Open the game selector on the FixFinder.")
    ttk.Label(panel, textvariable=status, style="Status.TLabel", wraplength=410,
              justify="left").grid(sticky="ew", pady=(0, 12))
    controls = ttk.Frame(panel, style="Panel.TFrame")
    controls.grid(sticky="ew")
    source_list = []
    mailbox = SimpleQueue()
    worker = None
    stop = threading.Event()
    closing = False

    def refresh():
        try:
            prior = source_list[picker.current()].device if picker.current() >= 0 else None
            source_list[:] = monitors()
            picker["values"] = [
                f"{item.device.rsplit(chr(92), 1)[-1]}   {item.width} × {item.height}   {item.label}"
                for item in source_list
            ]
            if source_list:
                preferred = next((i for i, item in enumerate(source_list) if item.device == prior), None)
                if preferred is None:
                    preferred = next((i for i, item in enumerate(source_list)
                                      if "virtual" in item.label.lower()), 0)
                picker.current(preferred)
            else:
                picker.set("")
        except OSError as error:
            status.set(str(error))

    def connect():
        nonlocal worker
        if picker.current() < 0:
            status.set("Select a Windows display first.")
            return
        source, show_cursor = source_list[picker.current()], cursor.get()
        stop.clear()
        picker.configure(state="disabled")
        connect_button.configure(state="disabled")
        refresh_button.configure(state="disabled")
        stop_button.configure(state="normal")
        status.set("Connecting to the FixFinder…")

        def run():
            try:
                stream(source, show_cursor, serial, stop, lambda message: mailbox.put((False, message)))
            except (OSError, ValueError) as error:
                mailbox.put((True, str(error)))
            else:
                mailbox.put((True, "Disconnected. The FixFinder is back in the arcade."))

        worker = threading.Thread(target=run, daemon=True)
        worker.start()

    def disconnect():
        stop.set()
        stop_button.configure(state="disabled")
        status.set("Disconnecting…")

    def close():
        nonlocal closing
        closing = True
        stop.set()
        if worker is None:
            root.destroy()

    def poll():
        nonlocal worker
        try:
            while True:
                done, message = mailbox.get_nowait()
                status.set(message)
                if done:
                    worker = None
                    if closing:
                        root.destroy()
                        return
                    picker.configure(state="readonly")
                    connect_button.configure(state="normal")
                    refresh_button.configure(state="normal")
                    stop_button.configure(state="disabled")
        except Empty:
            pass
        root.after(100, poll)

    connect_button = ttk.Button(controls, text="Connect", style="Connect.TButton", command=connect)
    connect_button.pack(side="left")
    stop_button = ttk.Button(controls, text="Disconnect", state="disabled", command=disconnect)
    stop_button.pack(side="left", padx=(7, 0))
    refresh_button = ttk.Button(controls, text="Refresh displays", command=refresh)
    refresh_button.pack(side="left", padx=(7, 0))
    setup = ttk.Frame(panel, style="Panel.TFrame")
    setup.grid(sticky="w", pady=(11, 0))
    ttk.Button(setup, text="Windows display settings",
               command=lambda: os.startfile("ms-settings:display")).pack(side="left")
    ttk.Button(setup, text="Virtual monitor driver",
               command=lambda: webbrowser.open(DRIVER_URL)).pack(side="left", padx=(7, 0))
    refresh()
    root.protocol("WM_DELETE_WINDOW", close)
    root.after(100, poll)
    root.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", type=str.upper, help="USB serial when more than one scanner is connected")
    parser.add_argument("--list-displays", action="store_true", help="list active Windows display sources")
    args = parser.parse_args()
    dpi_awareness()
    if args.list_displays:
        for source in monitors():
            print(source)
    else:
        show_app(args.serial)


if __name__ == "__main__":
    main()
