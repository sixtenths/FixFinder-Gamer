"""Compile and test the FixFinder renderer against the original SM64 port's graphics API."""
import argparse
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

REVISION = "2b17d081c9798b31b91dc71f37994b0da28cffc9"


def execute(elf_path, function):
    from elftools.elf.elffile import ELFFile
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
    from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP

    with elf_path.open("rb") as source:
        elf = ELFFile(source)
        cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        cpu.mem_map(0x08000000, 0x300000)
        cpu.mem_map(0x20000000, 0x30000)
        for segment in elf.iter_segments():
            if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]:
                cpu.mem_write(segment["p_vaddr"], segment.data())
        stop = 0x2002d000
        cpu.reg_write(UC_ARM_REG_SP, 0x2002fff8)
        cpu.reg_write(UC_ARM_REG_LR, stop | 1)
        cpu.emu_start(elf.header.e_entry | 1, stop, count=120000000)
        result = cpu.reg_read(UC_ARM_REG_R0)
        if cpu.reg_read(UC_ARM_REG_PC) != stop or result != 0:
            raise ValueError(f"{function} failed, result={result}.")
        print(f"{function} passed in Cortex-M4 emulation.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="checkout of sm64-port/sm64-port")
    default = Path(os.environ.get("LOCALAPPDATA", ".")) / "FixFinder/ArmGCC/bin/arm-none-eabi-gcc.exe"
    parser.add_argument("--gcc", type=Path, default=default)
    args = parser.parse_args()
    source = args.source.resolve()
    gcc = args.gcc.resolve()
    revision = subprocess.check_output(["git", "-C", source, "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        parser.error(f"Use sm64-port revision {REVISION}.")

    root = Path(__file__).resolve().parents[1]
    gfx = source / "src/pc/gfx"
    flags = [
        "-mcpu=cortex-m4", "-mthumb", "-Os", "-ffunction-sections", "-fdata-sections",
        "-DTARGET_PC", "-DNDEBUG", "-DF3DEX_GBI_2",
        f"-I{gfx}", f"-I{source / 'include'}", f"-I{source / 'src'}",
        "-nostartfiles", "--specs=nosys.specs", "-T", "firmware/doom-swd/firmware.ld",
        "-Wl,--defsym,__fixfinder_app_base=0x08008000",
        "-Wl,--defsym,__fixfinder_minimum_heap=0", "-Wl,--gc-sections",
    ]
    with TemporaryDirectory(prefix="fixfinder-sm64-") as temp:
        work = Path(temp)
        # Give the renderer a smaller batch buffer and remove host profiling.
        translator = (gfx / "gfx_pc.c").read_text()
        translator = translator.replace("#define MAX_BUFFERED 256", "#define MAX_BUFFERED 8", 1)
        start = translator.index("static unsigned long get_time(void)")
        end = translator.index("static void gfx_flush", start)
        translator_path = work / "gfx_pc.c"
        translator_path.write_text(translator[:start] + "static unsigned long get_time(void) { return 0; }\n\n" +
                                   translator[end:])
        for function, test, extra in (
            ("sm64_backend_test", "sm64_renderer_smoke.c", []),
            ("sm64_display_list_test", "sm64_display_list_smoke.c", [translator_path]),
        ):
            elf = work / f"{function}.elf"
            result = subprocess.run([
                gcc, *flags, f"-Wl,-e,{function}", "firmware/sm64/renderer.c",
                root / "tests" / test, gfx / "gfx_cc.c", *extra, "-lm", "-o", elf,
            ], cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if result.returncode:
                parser.exit(1, result.stdout)
            execute(elf, function)


if __name__ == "__main__":
    main()
