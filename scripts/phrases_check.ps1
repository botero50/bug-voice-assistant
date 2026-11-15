<#
Sends every phrase of scripts\phrases.txt to the real board (as if it had heard it) and lists the ones the board did NOT answer by itself.
That finds the ways of asking that the skills do not understand yet.

  scripts\phrases_check.ps1 -Port COM5                 all of them (about 10 minutes, the board speaks every answer)
  scripts\phrases_check.ps1 -Port COM5 -Skill clima    only that skill's lines
  scripts\phrases_check.ps1 -Port COM5 -Quiet          mute the speaker meanwhile (restores the volume at the end)

A phrase counts as understood when the log says "answering on the board". "asking the PC" means the board did not understand it.
#>
param([string]$Port = 'COM5', [string]$Skill = '', [string]$File = '', [switch]$Quiet, [string]$Report = '')
if (-not $File) { $File = Join-Path $PSScriptRoot 'phrases.txt' }
$rows = Get-Content $File -Encoding UTF8 | Where-Object { $_ -match '\|' -and $_ -notmatch '^\s*#' } | ForEach-Object {
    $parts = $_ -split '\|', 2
    [pscustomobject]@{ Skill = $parts[0].Trim(); Phrase = $parts[1].Trim() }
}
if ($Skill) { $rows = $rows | Where-Object { $_.Skill -eq $Skill } }

$p = New-Object IO.Ports.SerialPort $Port, 115200
$p.Encoding = [Text.Encoding]::UTF8; $p.ReadBufferSize = 262144; $p.DtrEnable = $false; $p.RtsEnable = $false
try { $p.Open() } catch { Write-Output "Cannot open $Port. Close other programs that use it."; exit 1 }
function Read-Log($seconds, $until = '') {
    $end = (Get-Date).AddSeconds($seconds); $sb = ''
    while ((Get-Date) -lt $end) {
        if ($p.BytesToRead -gt 0) { $sb += $p.ReadExisting() }
        if ($until -and $sb -match $until) { Start-Sleep -Milliseconds 250; $sb += $p.ReadExisting(); break }
        Start-Sleep -Milliseconds 40
    }
    return ($sb -replace '\x1b\[[0-9;]*m', '')
}
[void](Read-Log 12 'Room noise level')
if ($Quiet) { $p.WriteLine('BUGCFG {"volume":0}'); Start-Sleep -Milliseconds 500 }

$missed = @(); $ok = 0; $n = 0
foreach ($row in $rows) {
    $n++
    $line = $null
    foreach ($attempt in 1, 2) {
        Start-Sleep -Milliseconds 1500
        [void]$p.ReadExisting()
        $p.WriteLine('BUGCFG {"hear":"' + $row.Phrase + '"}')
        $log = Read-Log 45 'Test phrase done'
        $line = ($log -split "`n" | Where-Object { $_ -match 'ASSISTANT: Heard ' } | Select-Object -Last 1)
        if ($line) { break }
    }
    $answered = $line -and ($line -match 'answering on the board|repeating the last answer|Follow-up')
    if ($answered) { $ok++ } else {
        $heard = if ($line) { ($line.Trim() -replace '^.*ASSISTANT: ', '') } else { 'nothing' }
        $missed += [pscustomobject]@{ Skill = $row.Skill; Phrase = $row.Phrase; Got = $(if ($heard -match 'asking the PC') { 'PC' } else { $heard }) }
        Write-Output ("MISS  {0,-10} {1}" -f $row.Skill, $row.Phrase)
    }
}
if ($Quiet) { $p.WriteLine('BUGCFG {"volume":-1}') }
$p.Close()
Write-Output "`n$ok of $n phrases answered by the board; $($missed.Count) missed."
if ($Report) { $missed | ForEach-Object { "$($_.Skill) | $($_.Phrase) | $($_.Got)" } | Set-Content $Report -Encoding UTF8 }
exit ([int]($missed.Count -gt 0))
