[CmdletBinding()]
param(
    [string]$ToolchainBin = "$env:LOCALAPPDATA/FixFinder/ArmGCC/bin",
    [string[]]$GameSource,
    [string[]]$GameInclude,
    [ValidateRange(0, 148)]
    [int]$GameHeapKiB = 148,
    [switch]$GameSdk,
    [switch]$ArcadeOnly,
    [switch]$SaveFiles,
    [ValidateSet('UART3', 'USART0')]
    [string]$RadioUart,
    [switch]$RamTest
)

$ErrorActionPreference = 'Stop'
$buildGame = $GameSource -or $ArcadeOnly
if ($ArcadeOnly -and ($GameSource -or $GameSdk)) { throw '-ArcadeOnly selects its own game sources.' }
if ($SaveFiles -and !$ArcadeOnly) { throw '-SaveFiles requires -ArcadeOnly.' }
if (($GameInclude -or $GameHeapKiB -ne 148) -and !$GameSource) {
    throw 'Custom include paths and heap limits require -GameSource.'
}
if ($GameInclude) { $GameInclude = @($GameInclude | ForEach-Object { (Resolve-Path -LiteralPath $_).Path }) }
if ($GameSdk -and !$GameSource) { throw '-GameSdk requires -GameSource.' }
if ($RamTest -and $buildGame) { throw '-RamTest applies to the resident loader.' }
if ($RadioUart -and $buildGame) { throw '-RadioUart applies to the resident loader.' }
$toolchain = (Resolve-Path -LiteralPath $ToolchainBin).Path
if ($GameSource) { $GameSource = @($GameSource | ForEach-Object { (Resolve-Path -LiteralPath $_).Path }) }
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root "build/usb-loader-$([guid]::NewGuid())"
$arch = @('-mcpu=cortex-m4', '-mthumb')

function Invoke-Gcc([string[]]$Arguments) {
    & (Join-Path $toolchain 'arm-none-eabi-gcc.exe') @arch @Arguments | Out-Host
    if ($LASTEXITCODE) { throw "arm-none-eabi-gcc exited with code $LASTEXITCODE" }
}

function Compile([string]$Source, [string]$Name, [string[]]$Defines) {
    $object = Join-Path $work "$Name.o"
    Invoke-Gcc ($compile + $Defines + @('-c', $Source, '-o', $object))
    $object
}

function Export-Image([string]$Name, [string]$Linker, [string[]]$Layout, [string[]]$Objects) {
    $elf = Join-Path $work "$Name.elf"
    Invoke-Gcc ($Layout + @('-nostartfiles', '-T', $Linker,
        '-Wl,--gc-sections', '-o', $elf) + $Objects + '-lm')
    & (Join-Path $toolchain 'arm-none-eabi-objcopy.exe') -O binary $elf (Join-Path $work "$Name.bin")
    if ($LASTEXITCODE) { throw "Could not export $Name.bin." }
}

$v8 = & "$PSScriptRoot/Build-V8.ps1" -ToolchainBin $toolchain -PassThru -LoaderObjects:(!$buildGame)
New-Item -ItemType Directory -Path $work | Out-Null
Push-Location $root
try {
    $sdk = 'GD32F4xx_Firmware_Library/Firmware'
    $peripheral = "$sdk/GD32F4xx_standard_peripheral"
    $usb = "$sdk/GD32F4xx_usb_library"
    $compile = @('-Os', '-ffunction-sections', '-fdata-sections', '-DGD32F427',
        '-DUSE_USB_HS', '-DUSE_EMBEDDED_PHY', '-DHXTAL_VALUE=8000000U',
        '-IFixFinder-USB-Test/inc', "-I$sdk/CMSIS", "-I$sdk/CMSIS/GD/GD32F4xx/Include",
        "-I$peripheral/Include", "-I$usb/driver/Include", "-I$usb/device/core/Include",
        "-I$usb/device/class/hid/Include", "-I$usb/ustd/common", "-I$usb/ustd/class/hid")
    $common = @(Get-ChildItem -LiteralPath $v8 -Filter 'part_*.o' |
        Where-Object Name -notin @('part_1.o', 'part_22.o') | ForEach-Object FullName)

    if ($buildGame) {
        $defines = @('-DFIXFINDER_USB_GAME')
        if ($GameSdk -or $ArcadeOnly) {
            $defines += '-DFIXFINDER_USB_VIDEO', '-Ifirmware/game-sdk'
        }
        if ($ArcadeOnly) { $defines += '-DFIXFINDER_ARCADE_ONLY' }
        if ($SaveFiles) {
            $defines += '-DFIXFINDER_SAVE_FILES', '-DFIXFINDER_EMMC_LIBRARY', '-DFIXFINDER_STORAGE_PROBE'
        }
        $objects = $common + @(
            (Compile 'USB-Build-V8/app.c' 'game-entry' $defines),
            (Compile 'USB-Build-V8/custom_hid_core.c' 'game-hid' $defines))
        if ($GameSdk -or $ArcadeOnly) {
            foreach ($unit in @('firmware/game-sdk/fixfinder.c', 'firmware/lcd/fixfinder_io.c',
                'firmware/lcd/doom_dcs.c', 'firmware/doom-swd/display.c',
                'firmware/doom-swd/usb_video.c', 'firmware/doom-swd/usb_input.c')) {
                $objects += Compile $unit ([IO.Path]::GetFileNameWithoutExtension($unit)) $defines
            }
        }
        if ($SaveFiles) {
            foreach ($unit in @('emmc_probe', 'game_library', 'save_files', 'storage/ff')) {
                $objects += Compile "firmware/doom-swd/$unit.c" ([IO.Path]::GetFileName($unit)) $defines
            }
        }
        if ($ArcadeOnly) {
            $objects += Compile 'firmware/doom-swd/arcade.c' 'arcade' $defines
            foreach ($game in @('racer', 'dino')) {
                $objects += Compile "firmware/doom-swd/$game.c" $game ($defines + '-O2')
            }
        }
        $gameDefines = $defines + @($GameInclude | ForEach-Object { "-I$_" })
        for ($i = 0; $i -lt $GameSource.Count; ++$i) {
            $objects += Compile $GameSource[$i] "game-$i" $gameDefines
        }
        $layout = @('-Wl,--defsym,__fixfinder_app_base=0x08008000')
        if ($GameInclude) { $layout += '--specs=nosys.specs' }
        if ($GameHeapKiB -ne 148) {
            $layout += "-Wl,--defsym,__fixfinder_minimum_heap=$($GameHeapKiB * 1024)"
        }
        $name = if ($ArcadeOnly) { 'FixFinder-Arcade' } else { 'FixFinder-Game' }
        Export-Image $name 'firmware/doom-swd/firmware.ld' $layout $objects
    } else {
        $defines = @('-DFIXFINDER_USB_LOADER', '-Oz', '-flto')
        $layout = @('-Oz', '-flto')
        $name = 'FixFinder-USB-Loader'
        if ($RamTest) {
            $defines += '-DFIXFINDER_LOADER_RAM_TEST'
            $layout += '-Wl,--defsym,__ram_test=1'
            $name += '-RAM'
        }
        if ($RadioUart) {
            $defines += '-DFIXFINDER_RADIO', "-DFIXFINDER_RADIO_$RadioUart"
            $name += "-$RadioUart"
        }
        $objects = $common + @(
            (Compile 'firmware/usb-loader/loader.c' 'loader' $defines),
            (Compile 'firmware/usb-loader/screen.c' 'loader-screen' $defines),
            (Compile 'USB-Build-V8/custom_hid_core.c' 'loader-hid' $defines),
            (Compile "$peripheral/Source/gd32f4xx_fmc.c" 'fmc' $defines))
        if ($RadioUart) {
            $objects += Compile 'firmware/usb-loader/radio_uart.c' 'radio-uart' $defines
        }
        Export-Image $name 'firmware/usb-loader/loader.ld' $layout $objects
    }
    Write-Output $work
} finally {
    Pop-Location
}
