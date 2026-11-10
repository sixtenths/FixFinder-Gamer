"""Capture a Windows monitor directly into a native RGB565 bitmap."""

import ctypes as c
from ctypes import wintypes as w
from typing import NamedTuple

from fixfinder_hid_windows import api

WIDTH, HEIGHT = 320, 240
user = c.WinDLL("user32", use_last_error=True)
gdi = c.WinDLL("gdi32", use_last_error=True)
ptr = c.c_void_p
dc = w.HDC


class MonitorInfo(c.Structure):
    _fields_ = [("size", w.DWORD), ("rect", w.RECT), ("work", w.RECT),
                ("flags", w.DWORD), ("name", w.WCHAR * 32)]


class DisplayDevice(c.Structure):
    _fields_ = [("size", w.DWORD), ("name", w.WCHAR * 32), ("description", w.WCHAR * 128),
                ("flags", w.DWORD), ("id", w.WCHAR * 128), ("key", w.WCHAR * 128)]


class CursorInfo(c.Structure):
    _fields_ = [("size", w.DWORD), ("flags", w.DWORD), ("cursor", w.HANDLE), ("point", w.POINT)]


class BitmapInfo(c.Structure):
    _fields_ = [("size", w.DWORD), ("width", w.LONG), ("height", w.LONG),
                ("planes", w.WORD), ("bits", w.WORD), ("compression", w.DWORD),
                ("length", w.DWORD), ("xres", w.LONG), ("yres", w.LONG),
                ("colors", w.DWORD), ("important", w.DWORD), ("masks", w.DWORD * 3)]


callback_type = c.WINFUNCTYPE(w.BOOL, w.HANDLE, dc, c.POINTER(w.RECT), w.LPARAM)
enum_monitors = api(user, "EnumDisplayMonitors", w.BOOL, dc, ptr, callback_type, w.LPARAM)
monitor_info = api(user, "GetMonitorInfoW", w.BOOL, w.HANDLE, c.POINTER(MonitorInfo))
display_device = api(user, "EnumDisplayDevicesW", w.BOOL, w.LPCWSTR, w.DWORD,
                     c.POINTER(DisplayDevice), w.DWORD)
get_cursor = api(user, "GetCursorInfo", w.BOOL, c.POINTER(CursorInfo))
get_dc = api(user, "GetDC", dc, w.HWND)
release_dc = api(user, "ReleaseDC", c.c_int, w.HWND, dc)
create_dc = api(gdi, "CreateCompatibleDC", dc, dc)
delete_dc = api(gdi, "DeleteDC", w.BOOL, dc)
create_bitmap = api(gdi, "CreateDIBSection", w.HBITMAP, dc, c.POINTER(BitmapInfo), w.UINT,
                    c.POINTER(ptr), w.HANDLE, w.DWORD)
select_object = api(gdi, "SelectObject", w.HANDLE, dc, w.HANDLE)
delete_object = api(gdi, "DeleteObject", w.BOOL, w.HANDLE)
stretch_mode = api(gdi, "SetStretchBltMode", c.c_int, dc, c.c_int)
brush_origin = api(gdi, "SetBrushOrgEx", w.BOOL, dc, c.c_int, c.c_int, ptr)
stretch = api(gdi, "StretchBlt", w.BOOL, dc, c.c_int, c.c_int, c.c_int, c.c_int,
              dc, c.c_int, c.c_int, c.c_int, c.c_int, w.DWORD)


def dpi_awareness():
    try:
        set_context = api(user, "SetProcessDpiAwarenessContext", w.BOOL, ptr)
        if set_context(ptr(-4)):  # Per-monitor DPI awareness, including negative origins.
            return
    except AttributeError:
        pass
    user.SetProcessDPIAware()


class Monitor(NamedTuple):
    device: str
    label: str
    left: int
    top: int
    width: int
    height: int
    primary: bool


def monitors():
    adapters = {}
    index = 0
    while True:
        adapter = DisplayDevice(size=c.sizeof(DisplayDevice))
        if not display_device(None, index, c.byref(adapter), 0):
            break
        adapters[adapter.name] = adapter.description
        index += 1

    found = []

    @callback_type
    def visit(handle, _dc, _rect, _data):
        info = MonitorInfo(size=c.sizeof(MonitorInfo))
        if monitor_info(handle, c.byref(info)):
            device = DisplayDevice(size=c.sizeof(DisplayDevice))
            adapter = adapters.get(info.name, "")
            display_device(info.name, 0, c.byref(device), 0)
            label = device.description or adapter or info.name
            if "virtual" in adapter.lower() and "virtual" not in label.lower():
                label = f"{label} ({adapter})"
            r = info.rect
            found.append(Monitor(info.name, label, r.left, r.top, r.right - r.left,
                                 r.bottom - r.top, bool(info.flags & 1)))
        return True

    if not enum_monitors(None, None, visit, 0):
        raise c.WinError(c.get_last_error())
    return sorted(found, key=lambda item: (not item.primary, item.device))


class Capture:
    def __init__(self, monitor, cursor=True):
        self.monitor, self.cursor = monitor, cursor
        self.screen = self.memory = self.bitmap = self.previous = None
        self.bits = ptr()
        try:
            self.screen = get_dc(None)
            if not self.screen:
                raise c.WinError(c.get_last_error())
            self.memory = create_dc(self.screen)
            info = BitmapInfo(40, WIDTH, -HEIGHT, 1, 16, 3, WIDTH * HEIGHT * 2,
                              0, 0, 0, 0, (w.DWORD * 3)(0xf800, 0x07e0, 0x001f))
            self.bitmap = create_bitmap(self.screen, c.byref(info), 0, c.byref(self.bits), None, 0)
            if not self.memory or not self.bitmap:
                raise c.WinError(c.get_last_error())
            self.previous = select_object(self.memory, self.bitmap)
            if self.previous in (None, ptr(-1).value):
                self.previous = None
                raise c.WinError(c.get_last_error())
            stretch_mode(self.memory, 4)  # HALFTONE
            brush_origin(self.memory, 0, 0, None)
            self.pixels = (c.c_uint16 * (WIDTH * HEIGHT)).from_address(self.bits.value)
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.previous:
            select_object(self.memory, self.previous)
        if self.bitmap:
            delete_object(self.bitmap)
        if self.memory:
            delete_dc(self.memory)
        if self.screen:
            release_dc(None, self.screen)

    def frame(self):
        monitor = self.monitor
        ratio = min(WIDTH / monitor.width, HEIGHT / monitor.height)
        width, height = max(1, round(monitor.width * ratio)), max(1, round(monitor.height * ratio))
        x, y = (WIDTH - width) // 2, (HEIGHT - height) // 2
        c.memset(self.bits, 0, WIDTH * HEIGHT * 2)
        if not stretch(self.memory, x, y, width, height, self.screen, monitor.left, monitor.top,
                       monitor.width, monitor.height, 0x40cc0020):
            raise c.WinError(c.get_last_error())
        gdi.GdiFlush()
        if self.cursor:
            info = CursorInfo(size=c.sizeof(CursorInfo))
            if get_cursor(c.byref(info)) and info.flags & 1:
                mx, my = info.point.x - monitor.left, info.point.y - monitor.top
                if 0 <= mx < monitor.width and 0 <= my < monitor.height:
                    self.draw_pointer(x + mx * width // monitor.width, y + my * height // monitor.height)
        return c.string_at(self.bits, WIDTH * HEIGHT * 2)

    def draw_pointer(self, x, y):
        shape = ("#", "##", "#.#", "#..#", "#...#", "#....#", "#..####", "#.#", "##", "#")
        for dy, row in enumerate(shape[:HEIGHT - y]):
            for dx, pixel in enumerate(row[:WIDTH - x]):
                self.pixels[(y + dy) * WIDTH + x + dx] = 0 if pixel == "#" else 0xffff
