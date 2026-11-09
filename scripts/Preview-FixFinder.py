"""Show the running game's indexed framebuffer over a Raspberry Pi SWD connection."""

import argparse
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import secrets
import struct
import subprocess
import zlib


WIDTH, HEIGHT = 240, 160
TARGET = ["--chip", "GD32F427ZK", "--protocol", "swd", "--probe",
          "0:0:gpiochip0,swclk=25,swdio=24", "--speed", "50"]

PAGE = b"""<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>FixFinder live preview</title>
<style>
body { background: #181818; color: #eee; font: 16px system-ui; text-align: center; }
img { width: min(96vw, 720px); aspect-ratio: 3/2; image-rendering: pixelated; }
</style>
<h1>FixFinder live preview</h1>
<img alt="Live DOOM framebuffer">
<p role="status">Reading the scanner over SWD...</p>
<script>
const frame = document.querySelector('img');
const status = document.querySelector('p');
async function refresh() {
  try {
    const result = await fetch(location.pathname + '/frame.png', { cache: 'no-store' });
    if (!result.ok) throw Error(await result.text());
    const previous = frame.src;
    frame.src = URL.createObjectURL(await result.blob());
    if (previous.startsWith('blob:')) URL.revokeObjectURL(previous);
    status.textContent = 'Live SWD preview';
  } catch (error) {
    status.textContent = error.message;
  }
  setTimeout(refresh, 400);
}
refresh();
</script></html>
"""


def read_words(probe, address, count):
    result = subprocess.run(
        ["sudo", "-n", str(probe), "read", "b32", hex(address), str(count),
         "--format", "simple-hex", *TARGET],
        capture_output=True, text=True, timeout=15, check=True,
    )
    words = result.stdout.split()
    if len(words) != count:
        raise ValueError("The debugger returned an incomplete frame.")
    return struct.pack(f"<{count}I", *(int(word, 16) for word in words))


def encode_frame(pixels, rgb565):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))

    palette = bytearray()
    for (color,) in struct.iter_unpack("<H", rgb565):
        palette.extend(((color >> 11) * 255 // 31,
                        ((color >> 5) & 63) * 255 // 63,
                        (color & 31) * 255 // 31))
    rows = b"".join(b"\0" + pixels[y:y + WIDTH] for y in range(0, len(pixels), WIDTH))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 3, 0, 0, 0))
            + chunk(b"PLTE", palette)
            + chunk(b"IDAT", zlib.compress(rows, 1))
            + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", required=True, help="Pi LAN address")
    parser.add_argument("--frame", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--palette", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--probe-rs", type=Path, default=Path.home() / ".cargo/bin/probe-rs")
    args = parser.parse_args()
    path = "/" + secrets.token_urlsafe(24)

    class Viewer(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def do_GET(self):
            if self.path == path:
                status, content_type, data = 200, "text/html; charset=utf-8", PAGE
            elif self.path == path + "/frame.png":
                try:
                    pixels = read_words(args.probe_rs, args.frame, WIDTH * HEIGHT // 4)
                    palette = read_words(args.probe_rs, args.palette, 128)
                    status, content_type, data = 200, "image/png", encode_frame(pixels, palette)
                except (OSError, subprocess.SubprocessError, ValueError):
                    status, content_type = 503, "text/plain; charset=utf-8"
                    data = b"SWD read failed. Check the powered scanner and the Pi leads."
            else:
                status, content_type, data = 404, "text/plain", b"Not found"
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Referrer-Policy", "no-referrer")
            self.end_headers()
            self.wfile.write(data)

    with HTTPServer((args.bind, args.port), Viewer) as server:
        print(f"http://{args.bind}:{args.port}{path}", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
