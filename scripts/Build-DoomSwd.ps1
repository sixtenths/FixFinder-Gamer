[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$EngineSource,

    [Parameter(Mandatory)]
    [string]$ConvertedWad,

    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin",
    [switch]$CompactReciprocal,
    [string]$BoardSource,
    [switch]$FixFinderLcd,
    [switch]$UsbGame,
    [switch]$Arcade,
    [string[]]$PacmanRom,
    [switch]$StorageProbe,
    [switch]$EmmcLibrary,
    [switch]$PacmanFromEmmc,
    [string]$ProvisionLibrary,
    [string]$ProvisionDoomMaps
)

$ErrorActionPreference = 'Stop'
if ($BoardSource -and $FixFinderLcd) { throw 'Select one display backend.' }
if ($Arcade -and (!$UsbGame -or !$FixFinderLcd)) { throw '-Arcade requires -FixFinderLcd and -UsbGame.' }
if ($PacmanRom -and !$Arcade) { throw '-PacmanRom requires -Arcade.' }
if ($StorageProbe -and !$Arcade) { throw '-StorageProbe requires -Arcade.' }
if ($EmmcLibrary -and !$Arcade) { throw '-EmmcLibrary requires -Arcade.' }
if ($PacmanFromEmmc -and !$EmmcLibrary) { throw '-PacmanFromEmmc requires -EmmcLibrary.' }
if ($ProvisionLibrary -and !$EmmcLibrary) { throw '-ProvisionLibrary requires -EmmcLibrary.' }
if ($ProvisionDoomMaps -and !$EmmcLibrary) { throw '-ProvisionDoomMaps requires -EmmcLibrary.' }
if ($ProvisionLibrary) { $ProvisionLibrary = (Resolve-Path -LiteralPath $ProvisionLibrary).Path }
if ($ProvisionDoomMaps) { $ProvisionDoomMaps = (Resolve-Path -LiteralPath $ProvisionDoomMaps).Path }
$source = (Resolve-Path -LiteralPath $EngineSource).Path
$wad = (Resolve-Path -LiteralPath $ConvertedWad).Path
$board = if ($BoardSource) { (Resolve-Path -LiteralPath $BoardSource).Path }
$toolchain = (Resolve-Path -LiteralPath $ToolchainBin).Path
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build/doom-swd-$([guid]::NewGuid())"
$arch = @('-mcpu=cortex-m4', '-mthumb')

$wadBytes = [IO.File]::ReadAllBytes($wad)
if ($wadBytes.Length -lt 12 -or [Text.Encoding]::ASCII.GetString($wadBytes, 0, 4) -ne 'IWAD' -or
    [BitConverter]::ToUInt32($wadBytes, 8) -ne 12) {
    throw 'Convert the reduced IWAD with the pinned GBADoom GbaWadUtil before linking.'
}
$lumpCount = [BitConverter]::ToUInt32($wadBytes, 4)
if ($lumpCount -gt [Math]::Floor(($wadBytes.Length - 12) / 16)) {
    throw 'Converted IWAD directory extends past the file.'
}
$packedSetup = $false
$externalMaps = $false
for ($i = 0; $i -lt $lumpCount; $i++) {
    $entry = 12 + 16 * $i
    $position = [BitConverter]::ToInt32($wadBytes, $entry)
    if ($position -band 0x40000000) { $externalMaps = $true }
    if ($position -lt 0) {
        $name = [Text.Encoding]::ASCII.GetString($wadBytes, $entry + 8, 8).TrimEnd([char]0)
        if ($name -cnotin 'THINGS', 'SIDEDEFS', 'SSECTORS', 'SECTORS') {
            throw "Packed data is only supported for setup lumps. Unexpected lump $name."
        }
        $packedSetup = $true
    }
}
if ($externalMaps -and (!$EmmcLibrary -or $packedSetup -or $ProvisionDoomMaps)) {
    throw 'Split DOOM maps require -EmmcLibrary and a separate map provisioning build.'
}

function Invoke-Gcc([string[]]$Arguments) {
    & (Join-Path $toolchain 'arm-none-eabi-gcc.exe') @arch @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "arm-none-eabi-gcc exited with code $LASTEXITCODE."
    }
}

function Replace-EngineSource([string]$Name, [string]$Pattern, [string]$Replacement, [int]$Count = 1) {
    $copy = Join-Path $build "$Name.c"
    $inputPath = if (Test-Path -LiteralPath $copy) { $copy } else { Join-Path $source "source/$Name.c" }
    $original = Get-Content -LiteralPath $inputPath -Raw
    $matches = [regex]::Matches($original, $Pattern)
    if ($matches.Count -ne $Count) { throw "Unexpected source layout in $Name.c." }
    $modified = [regex]::Replace($original, $Pattern, $Replacement)
    Set-Content -LiteralPath $copy -Value $modified -NoNewline -Encoding ascii
    Invoke-Gcc ($engineFlags + @('-c', $copy, '-o', (Join-Path $build "$Name.o")))
}

$engine = & "$PSScriptRoot/Check-DoomEngine.ps1" -EngineSource $source -ToolchainBin $toolchain `
    -CompactReciprocal:$CompactReciprocal -PassThru
$engineIncludes = if ($CompactReciprocal) { Join-Path $engine 'include' } else { Join-Path $source 'include' }
$v8 = & "$PSScriptRoot/Build-V8.ps1" -ToolchainBin $toolchain -PassThru
New-Item -ItemType Directory -Path $build -Force | Out-Null

Push-Location $root
try {
    $engineFlags = @('-Os', '-ffunction-sections', '-fdata-sections', '-fno-common',
        '-DHAVE_CONFIG_H', '-std=gnu23', '-include', 'stdint.h', "-I$engineIncludes",
        '-Wno-incompatible-pointer-types', '-Wno-implicit-function-declaration')
    # A single-map image boots directly into E1M1. Avoid a 64-bit access to the
    # WAD directory, which the converter only aligns to four bytes.
    Replace-EngineSource d_main 'D_StartTitle\(\);\s*// start up intro loop' 'G_DeferedInitNew(sk_medium, 1, 1);'
    $wadSource = Get-Content -LiteralPath (Join-Path $source 'source/w_wad.c') -Raw
    $wadLookup = '(?s)int_64_t nameint = 0;.*?if\(nameint == nameint2\)'
    if ([regex]::Matches($wadSource, $wadLookup).Count -ne 1) { throw 'Unexpected WAD lookup layout.' }
    $wadSource = [regex]::Replace($wadSource, $wadLookup, @'
char key[8] = {0};
        strncpy(key, name, sizeof(key));
        for (int i = header->numlumps - 1; i >= 0; i--)
        {
            if (memcmp(key, fileinfo[i].name, sizeof(key)) == 0)
'@)
    if ($externalMaps) {
        $return = 'return (const void*)&doom_iwad[l->filepos];'
        if ([regex]::Matches($wadSource, [regex]::Escape($return)).Count -ne 1) {
            throw 'Unexpected WAD lump layout.'
        }
        $wadSource = $wadSource.Replace($return, @'
if ((uint32_t)l->filepos & FIXFINDER_EXTERNAL_MAP)
            return doom_staged_lump((uint32_t)l->filepos & ~FIXFINDER_EXTERNAL_MAP, l->size);
        return (const void*)&doom_iwad[l->filepos];
'@)
        $engineFlags += '-include', 'firmware/doom-swd/episode_maps.h'
    }
    $wadCopy = Join-Path $build 'w_wad.c'
    Set-Content -LiteralPath $wadCopy -Value $wadSource -NoNewline -Encoding ascii
    Invoke-Gcc ($engineFlags + @('-c', $wadCopy, '-o', (Join-Path $build 'w_wad.o')))
    $zoneKiB = if ($externalMaps) { 155 } else { 144 }
    Replace-EngineSource z_zone 'maxHeapSize = \(256 \* 1024\);' "maxHeapSize = ($zoneKiB * 1024);"
    $replacements = @('d_main.o', 'w_wad.o', 'z_zone.o')
    if ($externalMaps) {
        Replace-EngineSource p_setup 'lumpnum = W_GetNumForName\(lumpname\);' @'
lumpnum = W_GetNumForName(lumpname);
    doom_stage_map(episode, map);
'@
        Replace-EngineSource p_setup '(?s)_g->thingPool = Z_Calloc\(numthings, sizeof\(mobj_t\), PU_LEVEL, NULL\);\s*_g->thingPoolSize = numthings;\s*for\(int i = 0; i < numthings; i\+\+\)' @'
/* Match P_SpawnMapThing's skill and single-player filters. The pool
       only needs entries for things that can spawn on this run. */
    const unsigned int skill = _g->gameskill <= sk_easy ? MTF_EASY :
                               _g->gameskill >= sk_hard ? MTF_HARD : MTF_NORMAL;
    int pool_size = 0;
    for (int j = 0; j < numthings; ++j) {
        const int type = data[j].type;
        const unsigned int flags = data[j].options;
        if (type == 1 || (type > 4 && P_IsDoomnumAllowed(type) &&
                          (flags & (MTF_NOTSINGLE | skill)) == skill))
            ++pool_size;
    }
    _g->thingPool = Z_Calloc(pool_size, sizeof(mobj_t), PU_LEVEL, NULL);
    _g->thingPoolSize = pool_size;

    for (int i = 0; i < pool_size; ++i)
'@
        Replace-EngineSource p_setup '_g->linedata = Z_Calloc\(_g->numlines,sizeof\(linedata_t\),PU_LEVEL,0\);' @'
_g->linedata = doom_map_line_storage(_g->numlines * sizeof(linedata_t));
    if (!_g->linedata)
        _g->linedata = Z_Calloc(_g->numlines,sizeof(linedata_t),PU_LEVEL,0);
'@
        Replace-EngineSource p_setup 'Z_FreeTags\(PU_LEVEL, PU_PURGELEVEL-1\);' @'
Z_FreeTags(PU_LEVEL, PU_PURGELEVEL-1);
    P_ResetSecnodePool();
'@
        Replace-EngineSource p_map 'msecnode_t\* P_AddSecnode\(' @'
/* Allocate sector links in batches to avoid a zone header for each link. */
static msecnode_t *free_secnodes;

void P_ResetSecnodePool(void)
{
    free_secnodes = NULL;
}

static msecnode_t *P_NewSecnode(void)
{
    if (!free_secnodes) {
        const unsigned int count = 16;
        msecnode_t *batch = Z_Malloc(count * sizeof *batch, PU_LEVEL, NULL);
        for (unsigned int i = 0; i + 1 < count; ++i)
            batch[i].m_tnext = &batch[i + 1];
        batch[count - 1].m_tnext = NULL;
        free_secnodes = batch;
    }
    msecnode_t *node = free_secnodes;
    free_secnodes = node->m_tnext;
    return node;
}

msecnode_t* P_AddSecnode(
'@
        Replace-EngineSource p_map 'node = Z_Malloc\(sizeof\(msecnode_t\), PU_LEVEL, NULL\);' 'node = P_NewSecnode();'
        Replace-EngineSource p_map 'Z_Free\(node\);' @'
node->m_tnext = free_secnodes;
        free_secnodes = node;
'@
        # Leave more zone space for large maps. Only multi-patch wall
        # textures use this cache. Keep the same four-way lookup.
        Replace-EngineSource r_hotpath.iwram 'static byte columnCache\[128\*128\];' 'static byte columnCache[64*128];'
        Replace-EngineSource r_hotpath.iwram '#define CACHE_STRIDE \(128 / CACHE_WAYS\)' '#define CACHE_STRIDE (64 / CACHE_WAYS)'
        Replace-EngineSource r_hotpath.iwram 'while\(i < 128\);' 'while(i < 64);'
        $replacements += 'p_setup.o', 'p_map.o', 'r_hotpath.iwram.o'
    }
    if ($packedSetup) {
        $engineFlags += '-include', 'firmware/doom-swd/setup_wad.h'
        $pattern = '(?s)(static void P_Load(?:Subsectors|Sectors|Things|SideDefs2)\s*\(int lump\)\s*\{[^}]*?)W_CacheLumpNum'
        Replace-EngineSource p_setup $pattern '${1}W_LoadSetupLump' 4
        Invoke-Gcc ($engineFlags + @('-c', 'firmware/doom-swd/setup_wad.c',
            '-o', (Join-Path $build 'setup_wad.o')))
        $replacements += 'p_setup.o'
    }

    $sdk = 'GD32F4xx_Firmware_Library/Firmware'
    $usb = "$sdk/GD32F4xx_usb_library"
    $includes = @('-IFixFinder-USB-Test/inc', "-I$sdk/CMSIS", "-I$sdk/CMSIS/GD/GD32F4xx/Include",
        "-I$sdk/GD32F4xx_standard_peripheral/Include", "-I$usb/driver/Include",
        "-I$usb/device/core/Include", "-I$usb/device/class/hid/Include",
        "-I$usb/ustd/common", "-I$usb/ustd/class/hid")
    $platformFlags = @('-Os', '-ffunction-sections', '-fdata-sections', '-DGD32F427',
        '-DUSE_USB_HS', '-DUSE_EMBEDDED_PHY', '-DHXTAL_VALUE=8000000U') + $includes
    if ($UsbGame) { $platformFlags += '-DFIXFINDER_USB_GAME', '-DFIXFINDER_USB_VIDEO' }
    $boardObjects = @()
    if ($externalMaps) {
        $platformFlags += '-DFIXFINDER_EXTERNAL_DOOM_MAPS'
        $episode = Join-Path $build 'episode_maps.o'
        $fmc = Join-Path $build 'fmc.o'
        Invoke-Gcc ($platformFlags + @('-std=gnu23', "-I$engineIncludes", '-c',
            'firmware/doom-swd/episode_maps.c', '-o', $episode))
        Invoke-Gcc ($platformFlags + @('-c',
            "$sdk/GD32F4xx_standard_peripheral/Source/gd32f4xx_fmc.c", '-o', $fmc))
        $boardObjects += $episode, $fmc
    }
    if ($FixFinderLcd) {
        $platformFlags += '-DFIXFINDER_DOOM_DCS'
        $lcd = Join-Path $build 'doom_dcs.o'
        Invoke-Gcc ($platformFlags + @('-c', 'firmware/lcd/doom_dcs.c', '-o', $lcd))
        $io = Join-Path $build 'fixfinder_io.o'
        Invoke-Gcc ($platformFlags + @('-c', 'firmware/lcd/fixfinder_io.c', '-o', $io))
        $boardObjects += $lcd, $io
    }
    if ($board) {
        $platformFlags += '-DFIXFINDER_DOOM_BOARD', '-Ifirmware/lcd', '-Ifirmware/doom-swd'
        $boardSources = @('firmware/lcd/hx8347.c', 'firmware/lcd/doom_hx8347.c', $board)
        for ($i = 0; $i -lt $boardSources.Count; $i++) {
            $object = Join-Path $build "board_$i.o"
            Invoke-Gcc ($platformFlags + @('-c', $boardSources[$i], '-o', $object))
            $boardObjects += $object
        }
    }
    $app = Join-Path $build 'app.o'
    $runtime = Join-Path $build 'runtime.o'
    $display = Join-Path $build 'display.o'
    if ($Arcade) {
        $platformFlags += '-DFIXFINDER_ARCADE'
        if ($PacmanFromEmmc) {
            $platformFlags += '-DFIXFINDER_PACMAN_EMMC'
            if ($PacmanRom) { $platformFlags += '-DFIXFINDER_PACMAN_SEED' }
        }
        if ($EmmcLibrary) {
            $platformFlags += '-DFIXFINDER_EMMC_LIBRARY'
            if ($ProvisionDoomMaps) {
                $platformFlags += '-DFIXFINDER_PROVISION_DOOM_MAPS'
                $mapSource = Join-Path $build 'map-pack.S'
                $mapObject = Join-Path $build 'map-pack.o'
                $mapPath = $ProvisionDoomMaps.Replace('\', '/')
                @"
.section .rodata.fixfinder_maps,"a",%progbits
.global fixfinder_maps_start
.global fixfinder_maps_end
fixfinder_maps_start:
.incbin "$mapPath"
fixfinder_maps_end:
"@ | Set-Content -LiteralPath $mapSource -Encoding ascii
                Invoke-Gcc @('-c', $mapSource, '-o', $mapObject)
                $boardObjects += $mapObject
            }
            if ($ProvisionLibrary) {
                $platformFlags += '-DFIXFINDER_PROVISION_LIBRARY'
                $packSource = Join-Path $build 'library-pack.S'
                $packObject = Join-Path $build 'library-pack.o'
                $packPath = $ProvisionLibrary.Replace('\', '/')
                @"
.section .rodata.fixfinder_pack,"a",%progbits
.global fixfinder_pack_start
.global fixfinder_pack_end
fixfinder_pack_start:
.incbin "$packPath"
fixfinder_pack_end:
"@ | Set-Content -LiteralPath $packSource -Encoding ascii
                Invoke-Gcc @('-c', $packSource, '-o', $packObject)
                $boardObjects += $packObject
            }
            foreach ($unit in @('firmware/doom-swd/game_library.c', 'firmware/doom-swd/storage/ff.c')) {
                $object = Join-Path $build ((Split-Path -Leaf $unit) + '.o')
                Invoke-Gcc ($platformFlags + @('-c', $unit, '-o', $object))
                $boardObjects += $object
            }
        }
        if ($StorageProbe -or $EmmcLibrary) {
            $platformFlags += '-DFIXFINDER_STORAGE_PROBE'
            $emmc = Join-Path $build 'emmc-probe.o'
            Invoke-Gcc ($platformFlags + @('-c', 'firmware/doom-swd/emmc_probe.c', '-o', $emmc))
            $boardObjects += $emmc
        }
        if ($PacmanRom -or $PacmanFromEmmc) {
            $platformFlags += '-DFIXFINDER_PACMAN'
            if ($PacmanRom) {
                $packedRom = Join-Path $build 'pacman.rom'
                & python -B scripts/Pack-PacmanRom.py $packedRom @PacmanRom
                if ($LASTEXITCODE -ne 0) { throw 'Could not pack Pac-Man ROMs.' }
                $romSource = Join-Path $build 'pacman-rom.S'
                $romObject = Join-Path $build 'pacman-rom.o'
                $romPath = $packedRom.Replace('\', '/')
                @"
.section .rodata.pacman_rom,"a",%progbits
.global pacman_rom
pacman_rom:
.incbin "$romPath"
"@ | Set-Content -LiteralPath $romSource -Encoding ascii
                Invoke-Gcc @('-c', $romSource, '-o', $romObject)
                $boardObjects += $romObject
            }
            foreach ($unit in @('firmware/doom-swd/pacman.c', 'firmware/doom-swd/pacman/z80.c')) {
                $object = Join-Path $build ((Split-Path -Leaf $unit) + '.o')
                Invoke-Gcc ($platformFlags + @('-O3', '-c', $unit, '-o', $object))
                $boardObjects += $object
            }
        }
        $arcadeObject = Join-Path $build 'arcade.o'
        Invoke-Gcc ($platformFlags + @('-c', 'firmware/doom-swd/arcade.c', '-o', $arcadeObject))
        $racerObject = Join-Path $build 'racer.o'
        Invoke-Gcc ($platformFlags + @('-O2', '-c', 'firmware/doom-swd/racer.c', '-o', $racerObject))
        $dinoObject = Join-Path $build 'dino.o'
        Invoke-Gcc ($platformFlags + @('-O2', '-c', 'firmware/doom-swd/dino.c', '-o', $dinoObject))
        $boardObjects += $arcadeObject, $racerObject, $dinoObject
    }
    Invoke-Gcc ($platformFlags + @('-DFIXFINDER_DOOM_SWD', '-c', 'USB-Build-V8/app.c', '-o', $app))
    Invoke-Gcc ($platformFlags + @('-std=gnu23', "-I$engineIncludes", '-c',
        'firmware/doom-swd/runtime.c', '-o', $runtime))
    Invoke-Gcc ($platformFlags + @('-c', 'firmware/doom-swd/display.c', '-o', $display))

    [IO.File]::WriteAllBytes((Join-Path $build 'doom.wad'), $wadBytes)
    $wadSource = Join-Path $build 'wad.S'
    $relativeWad = (Resolve-Path -Relative (Join-Path $build 'doom.wad')).Replace('\', '/')
    @"
.section .rodata.doom_iwad,"a",%progbits
.balign 8
.global doom_iwad
doom_iwad:
.incbin "$relativeWad"
.set wad_bytes, . - doom_iwad
.balign 4
.global doom_iwad_len
doom_iwad_len:
.word wad_bytes
"@ | Set-Content -LiteralPath $wadSource -Encoding ascii
    $wadObject = Join-Path $build 'wad.o'
    Invoke-Gcc @('-c', $wadSource, '-o', $wadObject)

    $objects = @(Get-ChildItem -LiteralPath $v8 -Filter 'part_*.o' |
        Where-Object Name -ne 'part_1.o' | ForEach-Object FullName)
    $objects += @(Get-ChildItem -LiteralPath $engine -Filter '*.o' |
        Where-Object { $_.Name -ne 'engine.o' -and $_.Name -notin $replacements } | ForEach-Object FullName)
    $objects += $app, $runtime, $display, $wadObject
    $objects += $boardObjects
    $objects += $replacements | ForEach-Object { Join-Path $build $_ }
    if ($packedSetup) { $objects += Join-Path $build 'setup_wad.o' }
    $layout = @()
    if ($externalMaps) { $layout += '-Wl,--defsym,__fixfinder_map_slot=0x082c0000' }
    if ($UsbGame) {
        $gameHid = Join-Path $build 'game-hid.o'
        $video = Join-Path $build 'usb-video.o'
        $input = Join-Path $build 'usb-input.o'
        Invoke-Gcc ($platformFlags + @('-c',
            'USB-Build-V8/custom_hid_core.c', '-o', $gameHid))
        Invoke-Gcc ($platformFlags + @('-c', 'firmware/doom-swd/usb_video.c', '-o', $video))
        Invoke-Gcc ($platformFlags + @('-c', 'firmware/doom-swd/usb_input.c', '-o', $input))
        $objects = @($objects | Where-Object { (Split-Path -Leaf $_) -ne 'part_22.o' })
        $objects += $gameHid, $video, $input
        $layout += '-Wl,--defsym,__fixfinder_app_base=0x08008000'
    }

    $name = if ($FixFinderLcd) { 'FixFinder-Doom-LCD' } elseif ($board) { 'FixFinder-Doom-Board' } else { 'FixFinder-Doom-SWD' }
    if ($UsbGame) { $name += '-USB' }
    $elf = Join-Path $build "$name.elf"
    $map = Join-Path $build "$name.map"
    # An argument file keeps this link below the Windows command-line limit.
    $objectList = Join-Path $build 'objects.rsp'
    $objects | ForEach-Object { '"' + $_.Replace('\', '/') + '"' } |
        Set-Content -LiteralPath $objectList -Encoding ascii
    Invoke-Gcc ($layout + @('-nostartfiles', '--specs=nano.specs', '--specs=nosys.specs',
        '-T', 'firmware/doom-swd/firmware.ld', '-Wl,--gc-sections,--print-memory-usage',
        "-Wl,-Map,$map", "@$objectList", '-o', $elf))
    if ($UsbGame) {
        & (Join-Path $toolchain 'arm-none-eabi-objcopy.exe') -O binary $elf (Join-Path $build "$name.bin")
        if ($LASTEXITCODE -ne 0) { throw "Could not export $name.bin." }
    }
    if ($board -or $FixFinderLcd) {
        Write-Output "Linked board game ELF at $elf"
    } else {
        Write-Output "Linked SWD diagnostic ELF at $elf"
    }
} finally {
    Pop-Location
}
