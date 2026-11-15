<#
Saves (and brings back) the private files of Bug that are NOT in Git, so formatting the PC does not lose your setup.

  scripts\Respaldo.bat                          makes  Desktop\Bug-respaldo-<date>.zip
  scripts\Respaldo.bat -WithData                also your voice recordings (server\wake_data, can be large)
  scripts\Respaldo.bat -Restore <file.zip>      puts the files back into the repository folders

The zip holds secrets (tokens, e-mail app password, calendar links): keep it somewhere private, never upload it.
The board has its own copy of its settings on its microSD card (bugcfg.js), so the board needs nothing from here.
#>
param([switch]$WithData, [string]$Restore = '', [string]$Out = '')
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot

# Files that only exist on this PC (see .gitignore)
$files = @(
    'server\dev_token.txt', 'server\pc_token.txt', 'server\dev_relay.json', 'server\dev_claude.json',
    'server\mail_accounts.json', 'server\calendar_sources.txt', 'server\user_settings.json', 'server\reading_state.json',
    'server\timers.json', 'server\mqtt_pass.txt', 'server\mqtt_url.txt', 'firmware\main\bug_config.local.h'
)

if ($Restore) {
    if (-not (Test-Path $Restore)) { throw "File not found: $Restore" }
    $temp = Join-Path $env:TEMP ('bug-restore-' + [guid]::NewGuid().ToString('N'))
    Expand-Archive -LiteralPath $Restore -DestinationPath $temp
    $count = 0
    foreach ($item in Get-ChildItem $temp -Recurse -File) {
        $relative = $item.FullName.Substring($temp.Length + 1)
        $target = Join-Path $Root $relative
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item $item.FullName $target -Force
        $count++
    }
    Remove-Item $temp -Recurse -Force
    Write-Host "Restored $count files into $Root. Start the server with 'Iniciar Bug.bat'; if the board does not answer, pair it again: python server\tools\pair_board.py COM5" -ForegroundColor Green
    exit 0
}

$stage = Join-Path $env:TEMP ('bug-backup-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $stage | Out-Null
$count = 0
foreach ($relative in $files) {
    $source = Join-Path $Root $relative
    if (Test-Path $source) {
        $target = Join-Path $stage $relative
        New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
        Copy-Item $source $target
        $count++
    }
}
if ($WithData -and (Test-Path "$Root\server\wake_data")) {
    Copy-Item "$Root\server\wake_data" "$stage\server\wake_data" -Recurse
    Write-Host 'Included server\wake_data (voice recordings).'
}
if (-not $Out) { $Out = Join-Path ([Environment]::GetFolderPath('Desktop')) ("Bug-respaldo-" + (Get-Date -Format 'yyyyMMdd') + '.zip') }
if (Test-Path $Out) { Remove-Item $Out -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $Out
Remove-Item $stage -Recurse -Force
Write-Host "Saved $count private files in $Out" -ForegroundColor Green
Write-Host 'It contains secrets: keep it private. To restore on a new PC: scripts\Respaldo.bat -Restore <that file>'
