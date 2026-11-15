<#
Runs the demo phrases on the real board (without speaking them, over USB) and says which ones were answered the way the demo expects.
Run it before showing Bug to someone. The board may restart when the port opens; the script waits for it.

  scripts\demo_check.ps1 -Port COM5
  scripts\demo_check.ps1 -Port COM5 -Only 3,4      only those lines of the list

Expected route: board = answered by the board alone, pc = the PC answered, any = either is fine.
Needs: the board on USB (nothing else using the port), the server running for the "pc" lines (Iniciar Bug.bat).
#>
param([string]$Port = 'COM5', [int[]]$Only = @())

# phrase, expected route, seconds to wait, what to look for in the answer (a regex, or empty)
$demo = @(
    @('qué puedes hacer',                         'board', 10, 'Puedo'),
    @('qué hora es',                              'board', 5,  'Son las|Es la'),
    @('pon un temporizador de 20 segundos',       'board', 5,  'temporizador|segundos'),
    @('cancela los temporizadores',               'board', 5,  ''),
    @('cuánto es 15 por 12',                      'board', 5,  '180'),
    @('cuánto vale el dólar',                     'board', 10, 'dólar|pesos'),
    @('y el euro',                                'board', 10, 'euro'),
    @('repite lo último',                         'any',   10, ''),
    @('cómo está el clima',                       'board', 10, 'grados|clima'),
    @('cuéntame un chiste',                       'board', 14, ''),
    @('quién fue Simón Bolívar',                  'any',   16, ''),
    @('cómo se dice casa en inglés',              'board', 10, 'house'),
    @('dame un consejo para estudiar mejor',      'pc',    20, '')
)

$p = New-Object IO.Ports.SerialPort $Port, 115200
$p.Encoding = [Text.Encoding]::UTF8; $p.ReadBufferSize = 262144; $p.DtrEnable = $false; $p.RtsEnable = $false
try { $p.Open() } catch { Write-Output "Cannot open $Port ($($_.Exception.Message)). Close other programs that use it."; exit 1 }
# Reads the log for up to $seconds, stopping early when `$until` (a regex) shows up
function Read-Log($seconds, $until = '') {
    $end = (Get-Date).AddSeconds($seconds); $sb = ''
    while ((Get-Date) -lt $end) {
        if ($p.BytesToRead -gt 0) { $sb += $p.ReadExisting() }
        if ($until -and $sb -match $until) { Start-Sleep -Milliseconds 300; $sb += $p.ReadExisting(); break }
        Start-Sleep -Milliseconds 50
    }
    return ($sb -replace '\x1b\[[0-9;]*m', '')
}

Write-Output 'Waiting for the board to start...'
$boot = Read-Log 30 'Room noise level'

$pass = 0; $fail = 0; $i = 0
foreach ($row in $demo) {
    $i++
    if ($Only.Count -gt 0 -and $Only -notcontains $i) { continue }
    $phrase = $row[0]; $route = $row[1]; $wait = $row[2]; $look = $row[3]
    $json = '{"hear":"' + $phrase + '"}'
    # The board may be busy with its own microphone for a moment and drop the order: try once more if nothing was heard
    $line = $null
    foreach ($attempt in 1, 2) {
        Start-Sleep -Seconds 2
        [void]$p.ReadExisting()
        $p.WriteLine('BUGCFG ' + $json)
        $log = Read-Log ($wait + 25) 'Test phrase done'
        $line = ($log -split "`n" | Where-Object { $_ -match 'ASSISTANT: Heard ' } | Select-Object -Last 1)
        if ($line) { break }
    }
    $got = if (-not $line) { 'nothing' } elseif ($line -match 'answering on the board|repeating the last answer') { 'board' } elseif ($line -match 'asking the PC') { 'pc' } else { 'other' }
    $okRoute = ($route -eq 'any' -and $got -ne 'nothing') -or ($route -eq $got)
    $okText = (-not $look) -or ($line -match $look) -or ($got -eq 'pc')
    $bad = $log -split "`n" | Where-Object { $_ -match '^E \(' }
    $ok = $okRoute -and $okText -and -not $bad
    if ($ok) { $pass++ } else { $fail++ }
    $short = if ($line) { ($line.Trim() -replace '^.*ASSISTANT: ', '') } else { '' }
    if ($short.Length -gt 110) { $short = $short.Substring(0, 110) + '...' }
    Write-Output ("{0,-4} {1,2}. {2}  [expected {3}, got {4}]  {5}" -f $(if ($ok) { 'ok' } else { 'FAIL' }), $i, $phrase, $route, $got, $short)
    if ($bad) { $bad | Select-Object -First 2 | ForEach-Object { Write-Output "       $_" } }
}
$p.Close()
Write-Output "`n$pass passed, $fail failed."
exit ([int]($fail -gt 0))
