"""Compile and run the HX8347 protocol check on a simulated Cortex-M4."""

import argparse
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP


def run_check(toolchain):
    root = Path(__file__).resolve().parents[1]
    build = root / "build"
    build.mkdir(exist_ok=True)
    with TemporaryDirectory(prefix="lcd-protocol-", dir=build) as scratch:
        elf_path = Path(scratch) / "test.elf"
        subprocess.run(
            [
                str(toolchain / "arm-none-eabi-gcc.exe"),
                "-mcpu=cortex-m4", "-mthumb", "-std=c11", "-Os", "-Wall", "-Wextra", "-Werror",
                "-ffreestanding", "-nostdlib", "-Wl,-Ttext,0x08000000,-Tbss,0x20000000,-e,test_lcd",
                "-Ifirmware/lcd", "-Ifirmware/doom-swd", "firmware/lcd/hx8347.c",
                "firmware/lcd/doom_hx8347.c", "firmware/lcd/v9_color_test.c",
                "firmware/doom-swd/display.c", "tests/hx8347_mock.c",
                "-o", str(elf_path),
            ],
            cwd=root,
            check=True,
        )
        cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        cpu.mem_map(0x07FFF000, 0x10000)
        cpu.mem_map(0x20000000, 0x10000)
        with elf_path.open("rb") as stream:
            elf = ELFFile(stream)
            for segment in elf.iter_segments(type="PT_LOAD"):
                if segment["p_filesz"]:
                    cpu.mem_write(segment["p_vaddr"], segment.data())
            entry = elf.header["e_entry"]
        stop = 0x0800E000
        cpu.reg_write(UC_ARM_REG_SP, 0x20010000)
        cpu.reg_write(UC_ARM_REG_LR, stop | 1)
        cpu.emu_start(entry | 1, stop, timeout=5_000_000)
        if cpu.reg_read(UC_ARM_REG_PC) != stop:
            raise SystemExit("LCD protocol check timed out")
        result = cpu.reg_read(UC_ARM_REG_R0)
        if result != 0:
            raise SystemExit(f"LCD protocol check failed at case {result}")
        print("Verified ID sampling, V9 color sequence, all 76,800 game-frame pixels, and abort cleanup.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--toolchain-bin", type=Path,
        default=Path(os.environ.get("LOCALAPPDATA", "")) / "FixFinder/ArmGCC/bin",
    )
    run_check(parser.parse_args().toolchain_bin.resolve())
