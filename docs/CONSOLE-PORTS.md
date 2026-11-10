# Experimental console ports

## Sonic the Hedgehog

The FixFinder runs the original 512 KiB Genesis cartridge through a Cortex-M4 build of Gwenesis. The cartridge is word-swapped into read-only flash and the VDP sends indexed scanlines to the LCD. The USB viewer supports its native and preview modes. The beeper can reproduce one PSG tone at a time. FM output and emulator save states are outside the current build.

The build packs the 68000 opcode tables into faster internal flash and skips complete iterations of recognized idle loops while preserving guest cycles. LCD transfers overlap emulation, and USB capture runs independently of the physical LCD refresh. Gameplay and LCD refresh have separate rates, with a physical update every third emulated frame.

On the test scanner, the Green Hill Zone timer advanced 15 seconds in a 15-second wall-clock sample, both between captures and during continuous fast USB preview.

Supply a raw Sonic the Hedgehog cartridge BIN. The hardware test used the REV01 image built by Sonic Retro's disassembly and verified against its reference hash. The build downloads the pinned Gwenesis source if needed and keeps all generated data under `build/`. [Sonic Retro disassembly](https://github.com/sonicretro/s1disasm) · [Gwenesis source and license](https://github.com/bzhxx/gwenesis/tree/168e46661a086a0b793a9ee127a206604ebe7df1)

```powershell
$sonic = ./scripts/Build-FixFinderSonic.ps1 -Rom 'C:\path\to\sonic.bin'
python scripts/Install-UsbGame.py --wait 45 $sonic
```

Sonic uses the full application slot. To switch back, install your arcade image with the same USB command. Arcade saves and the DOOM map pack stay on eMMC.

| Action | Scanner | USB viewer |
| --- | --- | --- |
| Start or pause | Back + Enter | Enter |
| Move left or right | Back or Enter | Left or Right |
| Jump | Erase | Space |
| Crouch | Down | Down |

## Super Mario 64 renderer

This work targets the original Super Mario 64 source port. The software backend takes the upstream Fast3D translator's triangle batches and renders into a 128 × 96 RGB565 buffer with a 16-bit depth buffer. It supports perspective-correct interpolation, point-sampled textures, depth, scissor, the upstream color combiner, alpha, and fog. Texture and shader storage are bounded. The complete game still needs its engine, level and asset memory, controller mapping, and LCD presentation integrated.

The renderer and test fixture use no cartridge assets. The tests run a color, depth, scissor, and texture fixture and a separate Fast3D display list through the upstream translator. The smoke build reduces the translator's batch size and supplies a minimal window interface. It does not compile the game loop. [Upstream Super Mario 64 port](https://github.com/sm64-port/sm64-port/tree/2b17d081c9798b31b91dc71f37994b0da28cffc9)

```powershell
git clone https://github.com/sm64-port/sm64-port.git build/sm64-port
git -C build/sm64-port checkout 2b17d081c9798b31b91dc71f37994b0da28cffc9
python -m pip install pyelftools unicorn
python scripts/Check-Sm64Renderer.py build/sm64-port
```
