"""Send an active Windows display to the FixFinder over USB."""

import argparse
from io import BytesIO
from itertools import groupby
import os
from queue import Empty, SimpleQueue
import struct
import threading
import time
import webbrowser

import fixfinder_hid_windows as hid
from fixfinder_desktop_windows import Capture, HEIGHT, WIDTH, TileCompressor, dpi_awareness, match_size, monitors
from fixfinder_image import DESKTOP_PID, VID

REPORT_ID, REPORT_SIZE = 0x17, 64
START, TILE, DATA, STOP, PING, BATCH, MOTION = range(1, 8)
BATCH_ID, BATCH_SIZE = 0x18, 4096
MOTION_BATCH, MOTION_PAYLOAD = 0x4000, 14400
JPEG_BATCH, JPEG_CAPACITY = 0x8000, 24 * 1024
TILE_W, TILE_H = 40, 20
TILE_COLS, TILE_ROWS = WIDTH // TILE_W, HEIGHT // TILE_H
TILE_BYTES = 2 * TILE_W * TILE_H
RAW, LZNT1, PATCH = 0, 5, 6
DRIVER_URL = "https://github.com/VirtualDrivers/Virtual-Display-Driver/releases"


def tiles(frame):
    for top in range(0, HEIGHT, TILE_H):
        for left in range(0, WIDTH, TILE_W):
            yield b"".join(frame[(y * WIDTH + left) * 2:(y * WIDTH + left + TILE_W) * 2]
                           for y in range(top, top + TILE_H))


def encode(raw, compact=False, compressor=None):
    packed = compressor(raw) if compressor else None
    kind, payload = (LZNT1, packed) if packed else (RAW, raw)
    if len(payload) <= 8:
        return kind, payload
    pixels = struct.unpack(f"<{len(raw) // 2}H", raw)
    runs = bytearray()
    for color, group in groupby(pixels):
        count = sum(1 for _ in group)
        while count > 255:
            runs.extend(struct.pack("<BH", 255, color))
            count -= 255
        runs.extend(struct.pack("<BH", count, color))
        if len(runs) >= len(payload):
            break
    if len(runs) < len(payload):
        kind, payload = 1, runs

    palette = list(dict.fromkeys(pixels))
    if len(palette) <= 16:
        indexed, bits = 2, 4
        if compact and len(palette) <= 2:
            indexed, bits = 3, 1
        elif compact and len(palette) <= 4:
            indexed, bits = 4, 2
        if 1 + 2 * len(palette) + (len(pixels) * bits + 7) // 8 < len(payload):
            by_color = {color: index for index, color in enumerate(palette)}
            indices = [by_color[pixel] for pixel in pixels]
            per_byte = 8 // bits
            indices.extend([0] * (-len(indices) % per_byte))
            groups = zip(*(indices[i::per_byte] for i in range(per_byte)))
            packed = bytes(sum(index << (i * bits) for i, index in enumerate(group)) for group in groups)
            return indexed, struct.pack(f"<B{len(palette)}H", len(palette), *palette) + packed
    return kind, payload


def changed_rect(raw, previous):
    """Crop a tile to the bounds of the changed RGB565 pixels."""
    if previous is None:
        return None
    stride = 2 * TILE_W
    top, bottom = 0, TILE_H
    while raw[top * stride:(top + 1) * stride] == previous[top * stride:(top + 1) * stride]:
        top += 1
        if top == TILE_H:
            return None
    while raw[(bottom - 1) * stride:bottom * stride] == previous[(bottom - 1) * stride:bottom * stride]:
        bottom -= 1
    left, right = stride, 0
    for row in range(top * stride, bottom * stride, stride):
        if raw[row:row + stride] == previous[row:row + stride]:
            continue
        x = 0
        while x < left and raw[row + x:row + x + 2] == previous[row + x:row + x + 2]:
            x += 2
        left = min(x, left)
        x = stride
        while x > right and raw[row + x - 2:row + x] == previous[row + x - 2:row + x]:
            x -= 2
        right = max(x, right)
        if not left and right == stride:
            break
    if (right - left) * (bottom - top) >= TILE_BYTES * 3 // 4:
        return None
    patch = b"".join(raw[row + left:row + right] for row in range(top * stride, bottom * stride, stride))
    return bytes((left // 2, top, (right - left) // 2, bottom - top)), patch


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
        self.batched = False
        self.rectangles = False
        self.jpeg = False
        self.sharp_video = False
        self.motion = False
        self.motion_token = None
        self.compressor = None

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
                raise OSError("The FixFinder rejected a desktop update.")
            return reply
        raise TimeoutError("The FixFinder did not acknowledge the desktop update.")

    def start(self):
        self.send(START, b"\x01")
        try:
            reply = self.receive(START, 1)
        except TimeoutError as error:
            raise TimeoutError("Open the game selector on the FixFinder and close the USB viewer, then retry.") from error
        self.started = True
        self.batched = reply[5] == 1
        self.rectangles = self.batched and reply[6] == 1
        self.jpeg = self.batched and reply[7] == 1
        self.sharp_video = self.jpeg and reply[8] == 1
        self.motion = self.jpeg and reply[9] == 1
        if self.rectangles:
            self.compressor = TileCompressor(TILE_BYTES)

    def tile(self, index, raw):
        encoding, payload = encode(raw)
        self.token = (self.token + 1) & 0xffff
        self.send(TILE, struct.pack("<HBBH", self.token, index, encoding, len(payload)))
        for offset in range(0, len(payload), REPORT_SIZE - 7):
            part = payload[offset:offset + REPORT_SIZE - 7]
            self.send(DATA, struct.pack("<HHB", self.token, offset, len(part)) + part)
        self.receive(TILE, self.token)

    def update(self, changes, stop, previous=None):
        if not self.batched:
            for index, raw in changes:
                if stop.is_set():
                    return False
                self.tile(index, raw)
            return True

        batch = bytearray(5)
        for index, raw in changes:
            if stop.is_set():
                return False
            patch = changed_rect(raw, previous[index]) if self.rectangles and previous else None
            if patch:
                bounds, region = patch
                encoding, payload = encode(region, compact=True, compressor=self.compressor)
                encoding, payload = PATCH, bounds + bytes((encoding,)) + payload
            else:
                encoding, payload = encode(raw, compact=True, compressor=self.compressor)
            if len(batch) + 4 + len(payload) > BATCH_SIZE:
                if self.rectangles and encoding == RAW:
                    # Use the end of this transfer for whole rows of a raw tile.
                    row_bytes = 2 * TILE_W
                    rows = (BATCH_SIZE - len(batch) - 4 - 5) // row_bytes
                    if rows >= 2:
                        cut = rows * row_bytes
                        first = bytes((0, 0, TILE_W, rows, RAW)) + raw[:cut]
                        batch.extend(struct.pack("<BBH", index, PATCH, len(first)))
                        batch.extend(first)
                        encoding = PATCH
                        payload = bytes((0, rows, TILE_W, TILE_H - rows, RAW)) + raw[cut:]
                self.send_batch(batch)
                batch = bytearray(5)
            batch.extend(struct.pack("<BBH", index, encoding, len(payload)))
            batch.extend(payload)
        if len(batch) > 5 and not stop.is_set():
            self.send_batch(batch)
        return not stop.is_set()

    def send_batch(self, batch, flags=0):
        self.flush_motion()
        self.token = (self.token + 1) & 0xffff
        struct.pack_into("<BHH", batch, 0, BATCH_ID, self.token, len(batch) | flags)
        if self.rectangles and not flags and len(batch) < REPORT_SIZE:
            self.send(BATCH, batch[1:])
        else:
            self.device.write_feature(bytes(batch).ljust(BATCH_SIZE, b"\0"))
        self.receive(BATCH, self.token)

    def video_frame(self, jpeg, stop):
        chunk_size = BATCH_SIZE - 9
        for offset in range(0, len(jpeg), chunk_size):
            if stop.is_set():
                return False
            batch = bytearray(9)
            struct.pack_into("<HH", batch, 5, offset, len(jpeg))
            batch.extend(jpeg[offset:offset + chunk_size])
            self.send_batch(batch, JPEG_BATCH)
        return True

    def motion_frame(self, data):
        if len(data) != MOTION_PAYLOAD:
            raise ValueError("Unexpected motion frame size.")
        self.token = (self.token + 1) & 0xffff
        chunk_size = BATCH_SIZE - 7
        for offset in range(0, len(data), chunk_size):
            part = data[offset:offset + chunk_size]
            report = struct.pack("<BHHH", BATCH_ID, self.token, (len(part) + 7) | MOTION_BATCH, offset) + part
            self.device.write_feature(report.ljust(BATCH_SIZE, b"\0"))
        self.flush_motion()
        self.motion_token = self.token

    def flush_motion(self):
        if self.motion_token is not None:
            token, self.motion_token = self.motion_token, None
            self.receive(MOTION, token)

    def ping(self):
        self.flush_motion()
        self.send(PING)
        self.receive(PING)

    def close(self):
        try:
            self.flush_motion()
        except OSError:
            pass
        try:
            # Also clears a pending START when the scanner was in a game.
            self.send(STOP)
            if self.started:
                self.receive(STOP)
        except OSError:
            pass
        finally:
            self.device.close()


class VideoEncoder:
    def __init__(self, sharp=True):
        try:
            from PIL import Image, ImageFilter
        except ImportError as error:
            raise OSError("Video mode needs Pillow. Install it with python -m pip install Pillow.") from error
        self.image = Image
        self.size = (240, 180) if sharp else (WIDTH // 2, HEIGHT // 2)
        self.sharpen = ImageFilter.UnsharpMask(radius=.65, percent=100, threshold=3) if sharp else None

    def __call__(self, frame):
        image = self.image.frombytes("RGB", (WIDTH, HEIGHT), frame, "raw", "BGR;16")
        image = image.resize(self.size, self.image.Resampling.LANCZOS if self.sharpen else self.image.Resampling.BOX)
        if self.sharpen:
            image = image.filter(self.sharpen)
        for quality in (72, 55, 40, 25):
            with BytesIO() as buffer:
                image.save(buffer, "JPEG", quality=quality, subsampling=2)
                data = buffer.getvalue()
            if len(data) <= 2 * (BATCH_SIZE - 9):
                break
        return data if len(data) <= JPEG_CAPACITY else None


class MotionEncoder:
    """Keep each source pixel's position with four local colors per 8 by 12 block."""
    def __init__(self):
        import numpy as np
        from PIL import Image
        self.np, self.image = np, Image

    def __call__(self, frame):
        np = self.np
        image = self.image.frombytes("RGB", (WIDTH, HEIGHT), frame, "raw", "BGR;16")
        image = image.resize((240, 180), self.image.Resampling.LANCZOS)
        blocks = np.asarray(image).reshape(15, 12, 30, 8, 3)
        blocks = blocks.transpose(0, 2, 1, 3, 4).reshape(-1, 96, 3).astype(np.float32)
        luma = blocks @ np.array((.299, .587, .114), dtype=np.float32)
        low, high = luma.min(axis=1), luma.max(axis=1)
        positions = np.array((0, 1 / 3, 2 / 3, 1), dtype=np.float32)
        target = low[:, None] + (high - low)[:, None] * positions
        anchors = np.abs(luma[:, :, None] - target[:, None, :]).argmin(axis=1)
        palette = np.take_along_axis(blocks, anchors[:, :, None], axis=1)
        delta = blocks[:, :, None, :] - palette[:, None, :, :]
        nearest = np.einsum("...c,...c->...", delta, delta).argmin(axis=2)
        for color in range(4):
            matches = nearest == color
            count = matches.sum(axis=1)[:, None]
            total = np.einsum("bp,bpc->bc", matches, blocks)
            np.divide(total, count, out=palette[:, color], where=count != 0)
        red, green, blue = palette.round().astype(np.uint16).transpose(2, 0, 1)
        rgb565 = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        packed = nearest.astype(np.uint8).reshape(-1, 24, 4)
        indices = packed[:, :, 0] | (packed[:, :, 1] << 2) | (packed[:, :, 2] << 4) | (packed[:, :, 3] << 6)
        result = np.empty((len(blocks), 32), dtype=np.uint8)
        result[:, :8] = rgb565.astype(">u2").view(np.uint8)
        result[:, 8:] = indices
        return result.tobytes()


def stream(source, cursor, serial, stop, notify, video=False):
    link = DisplayLink(serial)
    try:
        link.start()
        if video and not link.jpeg:
            raise OSError("Update the scanner's arcade firmware to use Video mode.")
        motion = None
        if video and link.motion:
            try:
                motion = MotionEncoder()
            except ImportError:
                pass
        encoder = VideoEncoder(link.sharp_video) if video and motion is None else None
        notify(f"Connected to {link.serial}  |  Sending first frame…")
        capture = Capture(source, cursor=cursor)
        try:
            previous = [None] * (TILE_COLS * TILE_ROWS)
            last_transfer = last_check = time.monotonic()
            updated = 0
            video_active = False
            last_motion = 0.0
            while not stop.is_set():
                now = time.monotonic()
                if now - last_check >= 1:
                    if source not in monitors():
                        raise OSError("The source display changed. Refresh the list and select it again.")
                    rate = updated / (now - last_check)
                    size = ("Native 320 × 240" if (source.width, source.height) == (WIDTH, HEIGHT)
                            else f"{source.width} × {source.height} → {WIDTH} × {HEIGHT}")
                    notify(f"{size}  |  {rate:.1f} FPS updated" if updated else f"{size}  |  No changes")
                    updated = 0
                    last_check = now
                rgb = capture.frame()
                frame = list(tiles(rgb))
                changes = [(i, raw) for i, raw in enumerate(frame) if raw != previous[i]]
                if changes:
                    if len(changes) >= 24:
                        last_motion = now
                    moving = now - last_motion < .3
                    jpeg = encoder(rgb) if encoder and moving else None
                    if motion and moving:
                        link.motion_frame(motion(rgb))
                        video_active = True
                    elif jpeg is not None:
                        if not link.video_frame(jpeg, stop):
                            break
                        video_active = True
                    else:
                        if video_active:
                            changes = list(enumerate(frame))
                        if not link.update(changes, stop, None if video_active else previous):
                            break
                        video_active = False
                    previous = frame
                    updated += 1
                    last_transfer = time.monotonic()
                elif video_active and now - last_transfer >= .3:
                    # Refresh every pixel after motion stops to restore lossless text.
                    if not link.update(list(enumerate(frame)), stop):
                        break
                    video_active = False
                    last_transfer = time.monotonic()
                if time.monotonic() - last_transfer >= .75:
                    link.ping()
                    last_transfer = time.monotonic()
                # Windows Python uses a high-resolution waitable timer for sleep.
                delay = 1 / 60 - (time.monotonic() - now)
                if delay > 0:
                    time.sleep(delay)
        finally:
            capture.close()
    finally:
        link.close()


def show_app(serial, connect_virtual=False, video=False):
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
                    style="Option.TCheckbutton").grid(sticky="w", pady=(0, 2))
    smooth = tk.BooleanVar(value=video)
    video_toggle = ttk.Checkbutton(panel, text="Video mode (smoother motion)", variable=smooth,
                                   style="Option.TCheckbutton")
    video_toggle.grid(sticky="w", pady=(0, 11))
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
                                      if item.virtual and not item.primary), 0)
                picker.current(preferred)
            else:
                picker.set("")
        except OSError as error:
            status.set(str(error))

    def set_native():
        if picker.current() < 0:
            status.set("Select a secondary virtual display first.")
            return
        try:
            width, height = match_size(source_list[picker.current()])
            refresh()
            status.set("Native 320 × 240 mode ready." if (width, height) == (WIDTH, HEIGHT)
                       else "640 × 480 mode ready. The scanner uses exact 2:1 scaling.")
        except OSError as error:
            status.set(str(error))

    def connect():
        nonlocal worker
        if picker.current() < 0:
            status.set("Select a Windows display first.")
            return
        source, show_cursor, use_video = source_list[picker.current()], cursor.get(), smooth.get()
        stop.clear()
        picker.configure(state="disabled")
        connect_button.configure(state="disabled")
        refresh_button.configure(state="disabled")
        native_button.configure(state="disabled")
        video_toggle.configure(state="disabled")
        stop_button.configure(state="normal")
        status.set("Connecting to the FixFinder…")

        def run():
            try:
                stream(source, show_cursor, serial, stop, lambda message: mailbox.put((False, message)),
                       video=use_video)
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
                    native_button.configure(state="normal")
                    video_toggle.configure(state="normal")
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
    native_button = ttk.Button(setup, text="Match FixFinder size", command=set_native)
    native_button.pack(side="left")
    ttk.Button(setup, text="Windows display settings",
               command=lambda: os.startfile("ms-settings:display")).pack(side="left", padx=(7, 0))
    ttk.Button(setup, text="Virtual monitor driver",
               command=lambda: webbrowser.open(DRIVER_URL)).pack(side="left", padx=(7, 0))
    refresh()
    if connect_virtual:
        candidates = [i for i, item in enumerate(source_list) if item.virtual and not item.primary]
        if len(candidates) == 1:
            picker.current(candidates[0])
            root.after_idle(connect)
        else:
            status.set("Select the virtual display and click Connect.")
    root.protocol("WM_DELETE_WINDOW", close)
    root.after(100, poll)
    root.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", type=str.upper, help="USB serial when more than one scanner is connected")
    parser.add_argument("--list-displays", action="store_true", help="list active Windows display sources")
    parser.add_argument("--connect-virtual", action="store_true",
                        help="connect on launch when exactly one secondary virtual display is active")
    parser.add_argument("--video", action="store_true", help="start with Video mode selected")
    args = parser.parse_args()
    dpi_awareness()
    if args.list_displays:
        for source in monitors():
            print(source)
    else:
        show_app(args.serial, args.connect_virtual, args.video)


if __name__ == "__main__":
    main()
