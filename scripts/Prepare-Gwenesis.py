"""Stage the pinned Gwenesis core and a word-swapped 512 KiB Sonic cartridge."""
import argparse
from pathlib import Path
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


def patch_video(path):
    text = path.read_text()
    start = text.index("void gwenesis_vdp_render_line(int line)")
    prefix, render = text[:start], text[start:]
    prefix += "extern unsigned char fixfinder_genesis_line[320];\n\n"
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
    payload = cartridge(args.rom.read_bytes())
    root = args.output.resolve()
    shutil.copytree(args.source / "src", root, ignore=shutil.ignore_patterns("*.o"))
    patch_bus(root / "bus/gwenesis_bus.c")
    patch_fetch(root / "cpus/M68K/m68k.h")
    patch_video(root / "vdp/gwenesis_vdp_gfx.c")
    patch_audio(root / "sound/ym2612.c")
    patch_psg(root / "sound/gwenesis_sn76489.c")
    with (root / "cpus/M68K/m68kcpu.c").open("a") as cpu:
        cpu.write("\nvoid fixfinder_genesis_rebase(unsigned int cycles) { m68k.cycles -= cycles; }\n")
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
