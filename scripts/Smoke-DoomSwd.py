"""Exercise the linked Cortex-M4 game runtime in Unicorn with a synthetic tick."""

import argparse
from pathlib import Path
from struct import pack, unpack

from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc, UcError
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP

FORWARD = 1 << 0
FIRE = 1 << 4
USE = 1 << 5
STRAFE_LEFT = 1 << 8
STRAFE_RIGHT = 1 << 9
FIST = 0
PISTOL = 1


def smoke_test(path, frame_output, check_progression, check_weapon_cycle):
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
    starting_ammo = 0
    ammo_after_release = None
    first_level_frame = 0
    expected_rows = []
    verified_rows = []
    active_map = 0
    next_map = 0
    map_start_frame = 0
    intermission_start = None
    weapon_cycle_start = None

    def level_setup(uc, _address, _size, _data):
        nonlocal active_map, map_start_frame
        if uc.reg_read(UC_ARM_REG_R0) == 1:
            active_map = uc.reg_read(UC_ARM_REG_R1)
            map_start_frame = read_word("doom_level_frames")
            if next_map and active_map == next_map:
                uc.mem_write(symbols["doom_test_buttons"], pack("<I", 0))

    def start_intermission(_uc, _address, _size, _data):
        nonlocal intermission_start
        intermission_start = read_word("doom_frames")

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
        nonlocal starting_ammo, ammo_after_release
        blocks += 1
        if blocks % 512:
            return
        ticks += 1
        uc.mem_write(symbols["milliseconds"], pack("<I", ticks))
        if uc.mem_read(symbols["doom_error"], 1)[0]:
            uc.emu_stop()
            return
        if next_map:
            if active_map == next_map:
                if read_word("doom_level_frames") - map_start_frame >= 10:
                    uc.emu_stop()
            elif intermission_start is not None:
                elapsed = read_word("doom_frames") - intermission_start
                pressed = FIRE if elapsed % 8 >= 4 else 0
                uc.mem_write(symbols["doom_test_buttons"], pack("<I", pressed))
            return

        frames = read_word("doom_level_frames")
        if weapon_cycle_start is not None:
            elapsed = frames - weapon_cycle_start
            if elapsed >= 2:
                uc.mem_write(symbols["doom_test_buttons"], pack("<I", 0))
            if elapsed >= 16:
                uc.emu_stop()
            return
        if frames >= 10 and starting_position is None:
            starting_position = position()
            starting_hash = read_word("doom_frame_hash")
            starting_ammo = read_word("doom_player_bullets")
            first_level_frame = frames
            uc.mem_write(symbols["doom_test_buttons"], pack("<I", FORWARD | FIRE))
        if frames == 16:
            uc.mem_write(symbols["doom_test_buttons"], pack("<I", FORWARD))
        if frames == 23:
            ammo_after_release = read_word("doom_player_bullets")
        if starting_position is not None and frames >= first_level_frame + 20:
            uc.emu_stop()

    cpu.hook_add(UC_HOOK_BLOCK, tick)
    hooks = [("I_FinishUpdate_e32", capture_frame), ("doom_lcd_write_row", check_row)]
    if check_progression:
        # Install before Unicorn translates either engine function.
        hooks.extend((("P_SetupLevel", level_setup), ("WI_Start", start_intermission)))
    for symbol, callback in hooks:
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
    if ammo_after_release is None or not 0 < ammo_after_release < starting_ammo:
        raise SystemExit("Fire input did not consume any bullets")
    if read_word("doom_player_bullets") != ammo_after_release:
        raise SystemExit("Ammunition kept decreasing after releasing Fire")
    if len(verified_rows) != 240:
        raise SystemExit(f"Expected 240 RGB565 rows, captured {len(verified_rows)}")

    print(f"E1M1 rendered {level_frames} frames with {ticks} synthetic ticks")
    print(f"Forward input moved the player from {starting_position} to {position()}")
    print(f"Fire consumed {starting_ammo - ammo_after_release} bullets and stopped on release")
    print(f"Frame hash changed from {starting_hash:08x} to {end_hash:08x}")
    print("Verified all 76,800 RGB565 pixels presented for the 320x240 landscape frame")
    if frame_output:
        rgb = bytearray()
        for row in verified_rows:
            for pixel in row:
                rgb.extend(((pixel >> 11) * 255 // 31, ((pixel >> 5) & 63) * 255 // 63,
                            (pixel & 31) * 255 // 31))
        frame_output.write_bytes(b"P6\n320 240\n255\n" + rgb)

    if check_weapon_cycle:
        if read_word("doom_player_weapon") != PISTOL:
            raise SystemExit("Expected the player to start with the pistol")
        for action, weapon, description in (
            (STRAFE_LEFT, FIST, "Use + Strafe Left switched from pistol to fist"),
            (STRAFE_RIGHT, PISTOL, "Use + Strafe Right switched from fist to pistol"),
        ):
            weapon_cycle_start = read_word("doom_level_frames")
            cpu.mem_write(symbols["doom_test_buttons"], pack("<I", USE | action))
            cpu.emu_start(cpu.reg_read(UC_ARM_REG_PC) | 1, 0, timeout=20_000_000)
            if (read_word("doom_level_frames") < weapon_cycle_start + 16 or
                    read_word("doom_player_weapon") != weapon):
                raise SystemExit(f"Weapon-cycle check failed for action mask {action:#x}")
            print(description)
        weapon_cycle_start = None

    if check_progression:
        for next_map in (2, 3):
            intermission_start = None
            cpu.mem_write(symbols["doom_test_buttons"], pack("<I", 0))
            # Trigger the engine's normal exit path without walking the level.
            saved = cpu.context_save()
            stop = 0x2002D000
            cpu.reg_write(UC_ARM_REG_LR, stop | 1)
            cpu.emu_start(symbols["G_ExitLevel"] | 1, stop, timeout=100_000)
            if cpu.reg_read(UC_ARM_REG_PC) != stop:
                raise SystemExit("G_ExitLevel did not return")
            cpu.context_restore(saved)
            cpu.emu_start(cpu.reg_read(UC_ARM_REG_PC) | 1, 0, timeout=20_000_000)
            error = bytes(cpu.mem_read(symbols["doom_error"], 160)).split(b"\0", 1)[0]
            if error:
                raise SystemExit(f"DOOM reported an error: {error.decode('ascii', errors='replace')}")
            rendered = read_word("doom_level_frames") - map_start_frame
            if intermission_start is None or active_map != next_map or rendered < 10:
                raise SystemExit(f"Expected E1M{next_map} after intermission, saw map {active_map} "
                                 f"with {rendered} rendered frames")
            print(f"Intermission completed and E1M{next_map} rendered {rendered} frames")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", help="ELF produced by Build-DoomSwd.ps1")
    parser.add_argument("--frame-output", type=Path, help="Write the verified RGB565 frame as a PPM")
    parser.add_argument("--check-progression", action="store_true",
                        help="Check intermission and level transitions in an E1M1–E1M3 build")
    parser.add_argument("--check-weapon-cycle", action="store_true",
                        help="Check the in-game weapon-cycle chord in a current build")
    args = parser.parse_args()
    smoke_test(args.elf, args.frame_output, args.check_progression, args.check_weapon_cycle)
