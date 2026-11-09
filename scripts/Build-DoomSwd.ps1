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
    [string]$ProvisionLibrary
)

$ErrorActionPreference = 'Stop'
if ($BoardSource -and $FixFinderLcd) { throw 'Select one display backend.' }
if ($Arcade -and (!$UsbGame -or !$FixFinderLcd)) { throw '-Arcade requires -FixFinderLcd and -UsbGame.' }
if ($PacmanRom -and !$Arcade) { throw '-PacmanRom requires -Arcade.' }
if ($StorageProbe -and !$Arcade) { throw '-StorageProbe requires -Arcade.' }
if ($EmmcLibrary -and !$Arcade) { throw '-EmmcLibrary requires -Arcade.' }
if ($PacmanFromEmmc -and !$EmmcLibrary) { throw '-PacmanFromEmmc requires -EmmcLibrary.' }
if ($ProvisionLibrary -and !$EmmcLibrary) { throw '-ProvisionLibrary requires -EmmcLibrary.' }
if ($ProvisionLibrary) { $ProvisionLibrary = (Resolve-Path -LiteralPath $ProvisionLibrary).Path }
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
for ($i = 0; $i -lt $lumpCount; $i++) {
    $entry = 12 + 16 * $i
    if ([BitConverter]::ToInt32($wadBytes, $entry) -lt 0) {
        $name = [Text.Encoding]::ASCII.GetString($wadBytes, $entry + 8, 8).TrimEnd([char]0)
        if ($name -cnotin 'THINGS', 'SIDEDEFS', 'SSECTORS', 'SECTORS') {
            throw "Packed data is only supported for setup lumps. Unexpected lump $name."
        }
        $packedSetup = $true
    }
}

function Invoke-Gcc([string[]]$Arguments) {
    & (Join-Path $toolchain 'arm-none-eabi-gcc.exe') @arch @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "arm-none-eabi-gcc exited with code $LASTEXITCODE."
    }
}

function Replace-EngineSource([string]$Name, [string]$Pattern, [string]$Replacement, [int]$Count = 1) {
    $original = Get-Content -LiteralPath (Join-Path $source "source/$Name.c") -Raw
    $matches = [regex]::Matches($original, $Pattern)
    if ($matches.Count -ne $Count) { throw "Unexpected source layout in $Name.c." }
    $modified = [regex]::Replace($original, $Pattern, $Replacement)
    $copy = Join-Path $build "$Name.c"
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
    Replace-EngineSource w_wad '(?s)int_64_t nameint = 0;.*?if\(nameint == nameint2\)' @'
char key[8] = {0};
        strncpy(key, name, sizeof(key));
        for (int i = header->numlumps - 1; i >= 0; i--)
        {
            if (memcmp(key, fileinfo[i].name, sizeof(key)) == 0)
'@
    Replace-EngineSource z_zone 'maxHeapSize = \(256 \* 1024\);' 'maxHeapSize = (144 * 1024);'
    $replacements = @('d_main.o', 'w_wad.o', 'z_zone.o')
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
        $boardObjects += $arcadeObject, $racerObject
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
