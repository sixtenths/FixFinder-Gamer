"""Windows HID enumeration and timed report transfers using system DLLs."""

import ctypes as c
from ctypes import wintypes as w


def api(dll, name, result, *args):
    function = getattr(dll, name)
    function.restype = result
    function.argtypes = args
    return function


class Overlapped(c.Structure):
    _fields_ = [
        ("internal", c.c_size_t), ("internal_high", c.c_size_t),
        ("offset", w.DWORD), ("offset_high", w.DWORD), ("event", w.HANDLE),
    ]


kernel = c.WinDLL("kernel32", use_last_error=True)
hid = c.WinDLL("hid", use_last_error=True)
cfg = c.WinDLL("cfgmgr32")
pointer = c.c_void_p
pdword = c.POINTER(w.DWORD)
poverlapped = c.POINTER(Overlapped)
open_file = api(kernel, "CreateFileW", w.HANDLE, w.LPCWSTR, w.DWORD, w.DWORD,
                pointer, w.DWORD, w.DWORD, w.HANDLE)
close_handle = api(kernel, "CloseHandle", w.BOOL, w.HANDLE)
create_event = api(kernel, "CreateEventW", w.HANDLE, pointer, w.BOOL, w.BOOL, w.LPCWSTR)
reset_event = api(kernel, "ResetEvent", w.BOOL, w.HANDLE)
read_file = api(kernel, "ReadFile", w.BOOL, w.HANDLE, pointer, w.DWORD, pdword, poverlapped)
write_file = api(kernel, "WriteFile", w.BOOL, w.HANDLE, pointer, w.DWORD, pdword, poverlapped)
wait = api(kernel, "WaitForSingleObject", w.DWORD, w.HANDLE, w.DWORD)
cancel_io = api(kernel, "CancelIoEx", w.BOOL, w.HANDLE, poverlapped)
io_result = api(kernel, "GetOverlappedResult", w.BOOL, w.HANDLE, poverlapped, pdword, w.BOOL)
get_serial = api(hid, "HidD_GetSerialNumberString", w.BYTE, w.HANDLE, pointer, w.ULONG)
set_input_buffers = api(hid, "HidD_SetNumInputBuffers", w.BYTE, w.HANDLE, w.ULONG)
get_guid = api(hid, "HidD_GetHidGuid", None, pointer)
list_size = api(cfg, "CM_Get_Device_Interface_List_SizeW", w.DWORD, pdword,
                pointer, w.LPCWSTR, w.ULONG)
list_paths = api(cfg, "CM_Get_Device_Interface_ListW", w.DWORD, pointer,
                 w.LPCWSTR, w.LPWSTR, w.ULONG, w.ULONG)


def interfaces(vid, pids):
    guid = (w.DWORD * 4)()
    get_guid(c.byref(guid))
    size = w.DWORD()
    while True:
        result = list_size(c.byref(size), c.byref(guid), None, 0)
        if result:
            raise OSError(f"HID enumeration size failed ({result:#x}).")
        paths = c.create_unicode_buffer(size.value)
        result = list_paths(c.byref(guid), None, paths, size, 0)
        if result != 0x1A:  # CR_BUFFER_SMALL when interfaces change during enumeration.
            break
    if result:
        raise OSError(f"HID enumeration failed ({result:#x}).")
    prefixes = {f"#vid_{vid:04x}&pid_{pid:04x}": pid for pid in pids}
    for path in paths[:].split("\0"):
        pid = next((pid for prefix, pid in prefixes.items() if prefix in path.lower()), None)
        if pid is None:
            continue
        handle = open_file(path, 0, 3, None, 3, 0, None)
        if handle == pointer(-1).value:
            continue
        try:
            value = c.create_unicode_buffer(128)
            if get_serial(handle, value, c.sizeof(value)) and value.value:
                yield pid, value.value.upper(), path
        finally:
            close_handle(handle)


def handles(vid, pid, serial):
    for _, actual_serial, path in interfaces(vid, (pid,)):
        if actual_serial == serial:
            yield path


class Device:
    def __init__(self, path, output_size=0):
        self.output_size = output_size
        self.handle = open_file(path, 0xC0000000, 3, None, 3, 0x40000000, None)
        if self.handle == pointer(-1).value:
            raise c.WinError(c.get_last_error())
        self.event = create_event(None, True, False, None)
        if not self.event:
            error = c.get_last_error()
            close_handle(self.handle)
            raise c.WinError(error)

    def close(self):
        close_handle(self.event)
        close_handle(self.handle)

    def set_input_buffers(self, count):
        if not set_input_buffers(self.handle, count):
            raise c.WinError(c.get_last_error())

    def transfer(self, operation, buffer, timeout):
        if not reset_event(self.event):
            raise c.WinError(c.get_last_error())
        status = Overlapped(event=self.event)
        done = w.DWORD()
        if not operation(self.handle, buffer, len(buffer), c.byref(done), c.byref(status)):
            error = c.get_last_error()
            if error != 997:  # ERROR_IO_PENDING
                raise c.WinError(error)
            try:
                if wait(self.event, timeout) != 0:
                    raise TimeoutError("Timed out waiting for a USB report.")
            except BaseException:
                cancel_io(self.handle, c.byref(status))
                io_result(self.handle, c.byref(status), c.byref(done), True)
                raise
            if not io_result(self.handle, c.byref(status), c.byref(done), False):
                raise c.WinError(c.get_last_error())
        return done.value

    def write(self, data):
        if self.output_size:
            data = data.ljust(self.output_size, b"\0")
        return self.transfer(write_file, c.create_string_buffer(data, len(data)), 3000)

    def read(self, length, timeout):
        buffer = c.create_string_buffer(length)
        count = self.transfer(read_file, buffer, timeout)
        return buffer.raw[:count]
