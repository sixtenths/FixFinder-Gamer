[CmdletBinding()]
param([string]$ArduinoCli = 'arduino-cli')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root "build/radio-$([guid]::NewGuid())"
$sketch = Join-Path $work 'fixfinder-radio'
New-Item -ItemType Directory -Path $sketch -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $root 'firmware/fixfinder-radio/fixfinder-radio.ino') $sketch
foreach ($header in @('protocol.h', 'radio_frame.h')) {
    Copy-Item -LiteralPath (Join-Path $root "firmware/usb-loader/$header") $sketch
}

$bytes = [byte[]]::new(12)
$random = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $random.GetBytes($bytes) } finally { $random.Dispose() }
$password = [BitConverter]::ToString($bytes).Replace('-', '').ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $work 'wifi-password.txt') -Value $password -Encoding ascii
Set-Content -LiteralPath (Join-Path $sketch 'radio_config.h') -Value (
    '#define FIXFINDER_AP_PASSWORD "' + $password + '"') -Encoding ascii

Push-Location $work
try {
    & $ArduinoCli compile --fqbn esp32:esp32:esp32 --output-dir $work $sketch | Out-Host
    if ($LASTEXITCODE) { throw "Arduino compile exited with code $LASTEXITCODE." }
} finally {
    Pop-Location
}
Write-Output $work
