# Runs the whole voice test: makes the fixtures (songs, books, calendar), restarts the server on them,
# runs test_client.py, then restarts the server normally (your own music, books and calendar).
# Usage: powershell -File run_tests.ps1
$server = $PSScriptRoot
$python = "$env:USERPROFILE\bug-venv\Scripts\python.exe"
$log = Join-Path $env:TEMP "bug_server_test.log"

function Stop-Server {
    Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
        Where-Object { $_.CommandLine -like '*bug_server.py*' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
    Start-Sleep 2
}

function Start-Server {
    $env:PYTHONUNBUFFERED = '1'; $env:HF_HUB_DISABLE_SYMLINKS_WARNING = '1'
    Start-Process -FilePath $python -ArgumentList "bug_server.py" -WorkingDirectory $server -WindowStyle Hidden `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep 3
        try { Invoke-RestMethod http://127.0.0.1:5000/health -TimeoutSec 2 | Out-Null; return } catch {}
    }
    throw "The server did not start"
}

Set-Location $server
$fixtures = & $python make_test_data.py
Stop-Server
foreach ($line in $fixtures) { $name, $value = $line -split '=', 2; Set-Item -Path "Env:$name" -Value $value }
Start-Server
& $python test_client.py
$code = $LASTEXITCODE
Write-Output "TESTS_DONE exit=$code"
Stop-Server
foreach ($line in $fixtures) { $name = ($line -split '=', 2)[0]; Remove-Item -Path "Env:$name" }
Start-Server
exit $code
