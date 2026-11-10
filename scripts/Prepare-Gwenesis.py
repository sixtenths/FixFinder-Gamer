"""Stage the pinned Gwenesis core and a word-swapped 512 KiB Sonic cartridge."""
import argparse
from pathlib import Path
import re
import shutil
from struct import pack

SOURCES = (
    "bus/gwenesis_bus.c", "cpus/M68K/m68kcpu.c", "cpus/Z80/Z80.c",
    "io/gwenesis_io.c", "sound/gwenesis_sn76489.c", "sound/ym2612.c",
    "sound/z80inst.c", "vdp/gwenesis_vdp_gfx.c", "vdp/gwenesis_vdp_mem.c",
)


def replace(text, before, after):
    if text.count(before) != 1:
        raise ValueError(f"Unexpected Gwenesis source near {before[:80]!r}")
    return text.replace(before, after)


def cartridge(data):
    if len(data) != 512 * 1024 or b"SONIC THE HEDGEHOG" not in b" ".join(data[0x120:0x180].split()):
        raise ValueError("Expected a raw 512 KiB Genesis Sonic the Hedgehog cartridge.")
    stack, reset = (int.from_bytes(data[i:i + 4], "big") for i in (0, 4))
    if not 0xff0000 <= stack <= 0x1000000 or not 0x200 <= reset < len(data) or reset & 1:
        raise ValueError("Invalid Genesis reset vectors.")
    return b"".join(pack("<H", (data[i] << 8) | data[i + 1]) for i in range(0, len(data), 2))


def patch_bus(path):
    text = path.read_text()
    text = replace(text, "unsigned char ROM_DATA[MAX_ROM_SIZE]; // 68K Main Program (uncompressed)",
                   "/* Cartridge data is word-swapped by the host and linked in flash. */")
    start = text.index("    memset(ROM_DATA, 0, MAX_ROM_SIZE);")
    end = text.index("    set_region();", start)
    text = text[:start] + """    (void)buffer;
    (void)size;
    z80_set_memory(ZRAM);
    z80_pulse_reset();
""" + text[end:]
    text = replace(text, "#include <assert.h>", "#define printf(...) ((void)0)")
    text = replace(text, "    assert(0);", "    /* Unmapped sound-bus address. */")
    path.write_text(text)


def patch_fetch(path):
    text = path.read_text()
    text = replace(text, "extern unsigned char ROM_DATA[];", "extern const unsigned char ROM_DATA[];")
    start = text.index("#define FETCH8ROM")
    end = text.index("\n#if GNW_TARGET_MARIO", start)
    text = text[:start] + """/* Sonic's 512 KiB cartridge mirrors through the ROM address space. */
#define FETCH8ROM(A) (ROM_DATA[((A) ^ 1) & 0x7ffffu])
#define FETCH16ROM(A) (*(const unsigned short *)&ROM_DATA[(A) & 0x7fffeu])
#define FETCH32ROM(A) ((unsigned int)FETCH16ROM(A) << 16 | FETCH16ROM((A) + 2))
""" + text[end:]
    path.write_text(text)


def compact_dispatch(root):
    handlers = re.findall(r"\bm68k_op_\w+", (root / "m68ki_instruction_jump_table_full.h").read_text())
    cycles = re.findall(r"\b\d+\*7", (root / "m68ki_cycles_full.h").read_text())
    if len(handlers) != 65536 or len(cycles) != len(handlers):
        raise ValueError("Unexpected Gwenesis opcode tables.")
    instructions = list(zip(handlers, cycles))
    pages = {}
    offsets = []
    for first in range(0, len(instructions), 16):
        page = tuple(instructions[first:first + 16])
        offsets.append(pages.setdefault(page, len(pages) * 16))

    # All firmware code lives in 0x08xxxxxx. Keep the original Thumb address
    # and guest cycle count in one word, then share identical opcode pages.
    with (root / "fixfinder_dispatch.h").open("w") as table:
        table.write("""#define DISPATCH(handler, cycles) \\
    ((uintptr_t)(handler) + (((uint32_t)(cycles) - 0x08u) << 24))
static const uint16_t opcode_pages[] = {
""")
        for first in range(0, len(offsets), 16):
            table.write("    " + ", ".join(map(str, offsets[first:first + 16])) + ",\n")
        table.write("};\nstatic const uint32_t opcode_entries[] = {\n")
        for page in pages:
            for handler, ticks in page:
                table.write(f"    DISPATCH({handler}, {ticks}),\n")
        table.write("""};
#undef DISPATCH

static inline uint32_t decode_opcode(unsigned int opcode)
{
    return opcode_entries[opcode_pages[opcode >> 4] + (opcode & 15u)];
}

static inline unsigned int opcode_cycles(unsigned int opcode)
{
    return decode_opcode(opcode) >> 24;
}

static inline void execute_opcode(uint32_t entry)
{
    void (*handler)(void) = (void (*)(void))(uintptr_t)(0x08000000u | (entry & 0x00ffffffu));
    handler();
}
""")


def patch_68k(root, rom):
    compact_dispatch(root)
    header = root / "m68kcpu.h"
    text = replace(header.read_text(), "#define CYC_INSTRUCTION   m68ki_cycles",
                   "static inline unsigned int opcode_cycles(unsigned int opcode);")
    header.write_text(re.sub(r"CYC_INSTRUCTION\[(\w+)\]", r"opcode_cycles(\1)", text))
    ops = root / "m68kops.h"
    text = replace(ops.read_text(), """  #ifndef TABLES_FULL
    #include "m68ki_instruction_jump_table.h"
  #else
    #include "m68ki_instruction_jump_table_full.h"
  #endif""", '  #include "fixfinder_dispatch.h"')
    ops.write_text(text)
    path = root / "m68kcpu.c"
    text = replace(path.read_text(), """#ifndef BUILD_TABLES
  #ifndef TABLES_FULL
    #include "m68ki_cycles.h"
  #else
    #include "m68ki_cycles_full.h"
  #endif
#endif""", "#include <stdint.h>\n#define TABLES_FULL")
    text = replace(text, """    m68ki_instruction_jump_table[REG_IR]();
    USE_CYCLES(CYC_INSTRUCTION[REG_IR]);""", """    unsigned int opcode = REG_IR;
    uint32_t entry = decode_opcode(opcode);
    execute_opcode(entry);
    /* A VDP write can execute another instruction before returning. */
    USE_CYCLES(REG_IR == opcode ? entry >> 24 : opcode_cycles(REG_IR));""")
    text = text.replace("m68ki_instruction_jump_table[REG_IR]();", "execute_opcode(decode_opcode(REG_IR));")
    text = re.sub(r"CYC_INSTRUCTION\[(\w+)\]", r"opcode_cycles(\1)", text)
    # Sonic's WaitForVBlank enables interrupts, polls a byte in work RAM,
    # then returns when the VBlank interrupt handler clears it.
    wait_loop = bytes.fromhex("46fc 2300 4a38 f62a 66fa 4e75")
    if rom.count(wait_loop) == 1 and rom.index(wait_loop) % 2 == 0:
        poll = rom.index(wait_loop) + 4
        text = replace(text, "  /* Save end cycles count for when CPU is stopped */", f"""  /* The VBlank flag cannot change within this CPU timeslice.
     Skip complete polling loops, leaving at least one to set the flags. */
  const unsigned int loop_cycles = (12 + 10) * MUL;
  if (!FLAG_T1 && m68k.cycles < cycles && cycles - m68k.cycles > loop_cycles &&
      (REG_PC == 0x{poll:x} || (REG_PC == 0x{poll + 4:x} && COND_NE())) &&
      FETCH8RAM(0xfff62a)) {{
    unsigned int loops = (cycles - m68k.cycles - 1) / loop_cycles - 1;
    USE_CYCLES(loops * loop_cycles);
  }}

  /* Save end cycles count for when CPU is stopped */""")
    text += "\nvoid fixfinder_genesis_rebase(unsigned int cycles) { m68k.cycles -= cycles; }\n"
    path.write_text(text)


def patch_z80_fetch(path):
    text = replace(path.read_text(), "#define OpZ80(A) RdZ80(A)", """extern unsigned char *Z80_RAM;
static inline byte read_z80(word address)
{
    if (address < 0x4000) return Z80_RAM[address & 0x1fff];
    return RdZ80(address);
}
#define RdZ80(A) read_z80(A)
#define OpZ80(A) read_z80(A)""")
    path.write_text(text)


def patch_z80(path):
    text = path.read_text()
    text = replace(text, "    rem = ExecZ80(&cpu, current_timeslice / Z80_FREQ_DIVISOR);",
                   """    int budget = current_timeslice / Z80_FREQ_DIVISOR;
    /* Sonic's DAC driver waits at $0035 for the 68K to queue a sample.
       Recognize LD A,(HL); OR A; JP P,$0035 in the uploaded driver.
       The 68K is paused during this call, so the mailbox is stable. */
    enum { wait_pc = 0x35, mailbox = 0x1fff, loop_cycles = 7 + 4 + 10 };
    unsigned int pc = cpu.PC.W;
    if (budget > loop_cycles && pc >= wait_pc && pc <= wait_pc + 2 &&
        !(cpu.IFF & (IFF_1 | IFF_EI | IFF_HALT)) && cpu.IRequest != INT_NMI &&
        cpu.HL.W == mailbox && !(Z80_RAM[mailbox] & 0x80) &&
        (pc != wait_pc + 1 || !(cpu.AF.B.h & 0x80)) &&
        (pc != wait_pc + 2 || !(cpu.AF.B.l & S_FLAG)) &&
        Z80_RAM[wait_pc] == 0x7e && Z80_RAM[wait_pc + 1] == 0xb7 &&
        Z80_RAM[wait_pc + 2] == 0xf2 && Z80_RAM[wait_pc + 3] == wait_pc &&
        Z80_RAM[wait_pc + 4] == 0) {
      /* Keep a full loop for the interpreter to update A and flags. */
      budget = (budget - 1) % loop_cycles + loop_cycles + 1;
    }
    rem = ExecZ80(&cpu, budget);""")
    path.write_text(text)


def patch_video(path):
    text = path.read_text()
    start = text.index("void gwenesis_vdp_render_line(int line)")
    prefix, render = text[:start], text[start:]
    prefix += "extern unsigned char fixfinder_genesis_line[320];\n\n"
    render = replace(render, """  if (BITS(gwenesis_vdp_regs[12], 1, 2) != 0)
    return;

  if (line >= (REG1_PAL ? 240 : 224))
    return;""", """  if (BITS(gwenesis_vdp_regs[12], 1, 2) != 0 || line >= (REG1_PAL ? 240 : 224)) {
    memset(fixfinder_genesis_line, 192, 320);
    return;
  }""")
    begin = render.index("#if GNW_TARGET_MARIO")
    end = render.index("  uint8_t *pb", begin)
    render = render[:begin] + """  if (REG0_DISABLE_DISPLAY || !REG1_DISP_ENABLED) {
    memset(fixfinder_genesis_line, REG0_DISABLE_DISPLAY ? 192 : 0, 320);
    return;
  }

""" + render[end:]
    begin = render.index("#if GNW_TARGET_MARIO")
    end = render.index("\n#endif", begin)
    render = render[:begin] + """  unsigned int width = screen_width;
  unsigned int left = (320 - width) / 2;
  memset(fixfinder_genesis_line, 192, 320);
  uint8_t *output = fixfinder_genesis_line + left;
  for (unsigned int x = 0; x < width; ++x) {
    uint8_t plane = pb[x];
    uint8_t sprite = ps[x];
    uint8_t pixel = plane & 0x3f;
    if (MODE_SHI && (plane & 0xc0) < (sprite & 0xc0)) {
      switch (sprite & 0x3f) {
      case 0x3e: pixel += 128; break;
      case 0x3f: pixel += 64; break;
      default: pixel = sprite & 0x3f; break;
      }
    }
    output[x] = pixel;
  }""" + render[end + len("\n#endif"):]
    path.write_text(prefix + render)


def patch_audio(path):
    text = path.read_text()
    lo = text.index("  //printf(\"YM2612 init tables")
    hi = text.index("  /* build DETUNE table */", lo)
    # This frontend retains guest-visible FM timers and the detune table.
    # The unused synthesis path and its lookup tables are discarded at link.
    text = text[:lo] + "  int d, i;\n" + text[hi:]
    text = replace(text,
                   "    YM2612Update(gwenesis_ym2612_buffer + ym2612_prev_index, ym2612_index-ym2612_prev_index);",
                   """    int samples = ym2612_index - ym2612_prev_index;
    /* This board has a beeper. Keep guest-visible timer status without
       mixing PCM samples that the frontend cannot play. */
    for (int sample = 0; sample < samples; ++sample) {
      INTERNAL_TIMER_A();
    }
    INTERNAL_TIMER_B(samples);""")
    path.write_text(text)


def patch_psg(path):
    text = path.read_text()
    text = replace(text,
                   "    gwenesis_SN76489_Update(gwenesis_sn76489_buffer + sn76489_prev_index, sn76489_index-sn76489_prev_index);",
                   "    /* Advance PSG timing without producing unused PCM. */")
    text += """
unsigned int fixfinder_genesis_psg_hz(void)
{
    unsigned int period = 0;
    unsigned int volume = 15;
    for (unsigned int channel = 0; channel < 3; ++channel) {
        unsigned int level = gwenesis_SN76489.Registers[channel*2+1] & 15;
        unsigned int pitch = gwenesis_SN76489.Registers[channel*2] & 1023;
        if (pitch > 20 && level < volume) {
            volume = level;
            period = pitch;
        }
    }
    return period ? 3579545U / (32 * period) : 0;
}
"""
    path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("source", "rom", "output"):
        parser.add_argument(arg, type=Path)
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    payload = cartridge(rom)
    root = args.output.resolve()
    shutil.copytree(args.source / "src", root, ignore=shutil.ignore_patterns("*.o"))
    patch_bus(root / "bus/gwenesis_bus.c")
    patch_fetch(root / "cpus/M68K/m68k.h")
    patch_68k(root / "cpus/M68K", rom)
    patch_z80_fetch(root / "cpus/Z80/Z80.c")
    patch_z80(root / "sound/z80inst.c")
    patch_video(root / "vdp/gwenesis_vdp_gfx.c")
    patch_audio(root / "sound/ym2612.c")
    patch_psg(root / "sound/gwenesis_sn76489.c")
    swapped = root / "cartridge.bin"
    swapped.write_bytes(payload)
    assembly = root / "cartridge.S"
    assembly.write_text(f'.section .rodata.cartridge,"a",%progbits\n.balign 4\n'
                        f'.global ROM_DATA\nROM_DATA:\n.incbin "{swapped.as_posix()}"\n')
    for path in SOURCES:
        source = root / path
        source.write_text('#pragma GCC optimize("O3")\n' + source.read_text())
        print(source)
    print(assembly)


if __name__ == "__main__":
    main()
