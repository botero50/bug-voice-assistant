<#
Builds the firmware and flashes the board, then shows the first seconds of its log.

  scripts\flash.ps1 -Port COM5            build, flash, show the log
  scripts\flash.ps1 -NoFlash              only build (checks that it compiles)
  scripts\flash.ps1 -Port COM5 -LogSeconds 30

Needs the tools from  scripts\Instalar.bat -Firmware.  Close anything that has the port open (a serial monitor, the installer page).
#>
param([string]$Port = 'COM5', [switch]$NoFlash, [int]$LogSeconds = 12)

# ESP-IDF rejects project paths containing spaces, so build through a junction.
$Root = Split-Path -Parent $PSScriptRoot
$link = "$env:USERPROFILE\esp\bug-fw"
if (-not (Test-Path $link)) {
    New-Item -ItemType Directory -Force (Split-Path $link) | Out-Null
    New-Item -ItemType Junction -Path $link -Target "$Root\firmware" | Out-Null
}
$export = "$env:USERPROFILE\esp\esp-idf\export.ps1"
if (-not (Test-Path $export)) { Write-Output 'ESP-IDF is not installed: run scripts\Instalar.bat -Firmware first.'; exit 1 }
. $export *> $null
Set-Location $link
if (-not (Test-Path 'build\CMakeCache.txt')) {
    idf.py set-target esp32s3 *>&1 | Select-Object -Last 5
}
$out = idf.py build *>&1 | ForEach-Object { "$_" }
$out | Select-String -Pattern 'error:|undefined reference|binary size|FAILED|ninja: build stopped|Project build complete' | ForEach-Object { $_.Line } | Select-Object -First 40
if ($LASTEXITCODE -ne 0) { Write-Output "BUILD FAILED ($LASTEXITCODE)"; $out | Select-Object -Last 25; exit 1 }
if ($NoFlash) { exit 0 }

$fl = idf.py -p $Port flash *>&1 | ForEach-Object { "$_" }
$fl | Select-String -Pattern 'Chip is|Hash of data verified|Hard resetting|error|Error|Failed|failed' | ForEach-Object { $_.Line } | Select-Object -Unique -First 15
if ($LASTEXITCODE -ne 0) { Write-Output "FLASH FAILED ($LASTEXITCODE)"; $fl | Select-Object -Last 15; exit 1 }

# The first seconds of the log after the reboot (warnings, errors and the assistant's own lines)
Start-Sleep -Milliseconds 300
$p = New-Object IO.Ports.SerialPort $Port, 115200
$p.ReadBufferSize = 262144; $p.DtrEnable = $false; $p.RtsEnable = $false
$p.Open()
$sb = New-Object Text.StringBuilder
$p.RtsEnable = $true; Start-Sleep -Milliseconds 120; $p.RtsEnable = $false
$end = (Get-Date).AddSeconds($LogSeconds)
while ((Get-Date) -lt $end) { try { if ($p.BytesToRead -gt 0) { [void]$sb.Append($p.ReadExisting()) } } catch {}; Start-Sleep -Milliseconds 30 }
$p.Close()
$t = $sb.ToString() -replace '\x1b\[[0-9;]*m', ''
Write-Output "log bytes: $($t.Length)"
($t -split "`n") | Select-String -Pattern 'rst:|App version|ASSISTANT|VOICE_CLIENT|NETWORK|E \(|W \(|Guru|panic|abort|Backtrace|assert' | ForEach-Object { $_.Line }
