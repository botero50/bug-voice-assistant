<#
Writes the board's log lines about what it hears and decides into a file, for a few minutes, while you speak to it.

  scripts\log_board.ps1 -Port COM5 -Out wake.log -Minutes 15
  (open the file afterwards: "Wake check" lines show the classifier, the greeting check and the decision for each sound)
#>
param([string]$Port = 'COM5', [string]$Out = 'board_log.txt', [int]$Minutes = 15,
      [string]$Pattern = 'Wake check|Wake word|Greeting|Speech started|Too short|Recorded|Heard|Speaker|COMMAND|FINDER|E \(')
Set-Content -Path $Out -Value "capture started $(Get-Date -Format s)" -Encoding utf8
$p = New-Object IO.Ports.SerialPort $Port, 115200
$p.Encoding = [Text.Encoding]::UTF8; $p.DtrEnable = $false; $p.RtsEnable = $false; $p.ReadBufferSize = 262144
try { $p.Open() } catch { Add-Content -Path $Out -Value "cannot open $Port : $($_.Exception.Message)" -Encoding utf8; exit 1 }
$end = (Get-Date).AddMinutes($Minutes)
$pending = ''
while ((Get-Date) -lt $end) {
    if ($p.BytesToRead -gt 0) {
        $pending += ($p.ReadExisting() -replace '\x1b\[[0-9;]*m', '')
        $lines = $pending -split "`n"
        $pending = $lines[-1]
        foreach ($line in $lines[0..($lines.Count - 2)]) {
            if ($line -match $Pattern) { Add-Content -Path $Out -Value $line.Trim() -Encoding utf8 }
        }
    }
    Start-Sleep -Milliseconds 100
}
$p.Close()
