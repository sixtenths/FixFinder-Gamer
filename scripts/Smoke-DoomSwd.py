"""Exercise the linked Cortex-M4 game runtime in Unicorn with a synthetic tick."""

import argparse
from pathlib import Path
from struct import pack, unpack

from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc, UcError
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP


def smoke_test(path, frame_output):
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for address, size in (
        (0x08000000, 3 << 20), (0x10000000, 64 << 10), (0x20000000, 192 << 10),
        (0x40023000, 0x1000), (0xE000E000, 0x1000),
    ):
        cpu.mem_map(address, size)

    with open(path, "rb") as stream:
        elf = ELFFile(stream)
        symbols = {
            symbol.name: symbol["st_value"]
            for symbol in elf.get_section_by_name(".symtab").iter_symbols()
        }
        for segment in elf.iter_segments(type="PT_LOAD"):
            if segment["p_filesz"]:
                cpu.mem_write(segment["p_vaddr"], segment.data())

    def read_word(name, fmt="<I"):
        return unpack(fmt, cpu.mem_read(symbols[name], 4))[0]

    def position():
        return (read_word("doom_player_x", "<i"), read_word("doom_player_y", "<i"))

    blocks = ticks = 0
    starting_position = None
    starting_hash = 0
    first_level_frame = 0
    expected_rows = []
    verified_rows = []

    def capture_frame(uc, _address, _size, _data):
        if read_word("doom_level_frames") != 29:
            return
        indexed = uc.mem_read(uc.reg_read(UC_ARM_REG_R0), 240 * 160)
        rgb = uc.mem_read(uc.reg_read(UC_ARM_REG_R1), 256 * 3)
        colors = [
            (r // 8) * 2048 + (g // 4) * 32 + b // 8
            for r, g, b in zip(rgb[::3], rgb[1::3], rgb[2::3])
        ]
        expected_rows.extend(
            tuple(colors[indexed[(y * 2 // 3) * 240 + x * 3 // 4]] for x in range(320))
            for y in range(240)
        )

    def check_row(uc, _address, _size, _data):
        if read_word("doom_level_frames") != 29:
            return
        row = uc.reg_read(UC_ARM_REG_R0)
        pixels = unpack("<320H", uc.mem_read(uc.reg_read(UC_ARM_REG_R1), 640))
        if row != len(verified_rows) or row >= 240 or pixels != expected_rows[row]:
            raise SystemExit(f"Unexpected RGB565 data at LCD row {row}")
        verified_rows.append(pixels)

    def tick(uc, _address, _size, _data):
        nonlocal blocks, ticks, starting_position, starting_hash, first_level_frame
        blocks += 1
        if blocks % 512:
            return
        ticks += 1
        uc.mem_write(symbols["milliseconds"], pack("<I", ticks))
        if uc.mem_read(symbols["doom_error"], 1)[0]:
            uc.emu_stop()
            return

        frames = read_word("doom_level_frames")
        if frames >= 10 and starting_position is None:
            starting_position = position()
            starting_hash = read_word("doom_frame_hash")
            first_level_frame = frames
            uc.mem_write(symbols["doom_test_forward"], pack("<I", 1))
        if starting_position is not None and frames >= first_level_frame + 20:
            uc.emu_stop()

    cpu.hook_add(UC_HOOK_BLOCK, tick)
    for symbol, callback in (("I_FinishUpdate_e32", capture_frame), ("doom_lcd_write_row", check_row)):
        address = symbols[symbol] & ~1
        cpu.hook_add(UC_HOOK_CODE, callback, begin=address, end=address)
    cpu.reg_write(UC_ARM_REG_SP, symbols["_sp"])
    try:
        # Start after the real USB initialization. The emulator supplies memory
        # and a synthetic clock, but has no GD32 peripheral models.
        cpu.emu_start(symbols["fixfinder_doom_run"] | 1, 0, timeout=20_000_000)
    except UcError as error:
        raise SystemExit(f"CPU error at PC 0x{cpu.reg_read(UC_ARM_REG_PC):08x}: {error}") from error
    error = bytes(cpu.mem_read(symbols["doom_error"], 160)).split(b"\0", 1)[0]
    if error:
        raise SystemExit(f"DOOM reported an error: {error.decode('ascii', errors='replace')}")

    level_frames = read_word("doom_level_frames")
    end_hash = read_word("doom_frame_hash")
    if starting_position is None or level_frames < first_level_frame + 20:
        raise SystemExit(f"Timed out after {level_frames} E1M1 frames")
    if starting_position == position() or starting_hash == end_hash:
        raise SystemExit("Forward input did not change both player position and frame hash")
    if len(verified_rows) != 240:
        raise SystemExit(f"Expected 240 RGB565 rows, captured {len(verified_rows)}")

    print(f"E1M1 rendered {level_frames} frames with {ticks} synthetic ticks")
    print(f"Forward input moved the player from {starting_position} to {position()}")
    print(f"Frame hash changed from {starting_hash:08x} to {end_hash:08x}")
    print("Verified all 76,800 RGB565 pixels presented for the 320x240 landscape frame")
    if frame_output:
        rgb = bytearray()
        for row in verified_rows:
            for pixel in row:
                rgb.extend(((pixel >> 11) * 255 // 31, ((pixel >> 5) & 63) * 255 // 63,
                            (pixel & 31) * 255 // 31))
        frame_output.write_bytes(b"P6\n320 240\n255\n" + rgb)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", help="ELF produced by Build-DoomSwd.ps1")
    parser.add_argument("--frame-output", type=Path, help="Write the verified RGB565 frame as a PPM")
    args = parser.parse_args()
    smoke_test(args.elf, args.frame_output)
