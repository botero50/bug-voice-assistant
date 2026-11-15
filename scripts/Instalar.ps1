<#
Sets up everything Bug needs on a new Windows PC. Safe to run again: each step checks what is already there.

  scripts\Instalar.bat                  server + free chat model + desktop shortcut   (the normal case)
  scripts\Instalar.bat -Firmware        also the tools to build and flash the board (ESP-IDF 5.3.2)
  scripts\Instalar.bat -ClaudeHooks     also let the board approve/reject Claude Code permissions
  scripts\Instalar.bat -Train           also the PyTorch environment to train the wake word / commands
  scripts\Instalar.bat -SmartHome            also the smart-home broker (Mosquitto, with a generated password kept in server\mqtt_pass.txt)
  scripts\Instalar.bat -All             all of the above
  -Model qwen3:8b                       the Ollama model for free conversation (any model Ollama has)
  -SkipOllama / -SkipServer             leave that part out
  -NoAutostart                          do not make the Bug server start with Windows (it does by default)
  -RemoveAutostart                      take the automatic start away and nothing else

It never asks for, stores or prints a password. Wi-Fi, Gmail and tokens are typed by you in the board installer page
or in the terminal prompt of server\tools\pair_board.py (see README.md).
#>
param(
    [switch]$Firmware, [switch]$ClaudeHooks, [switch]$Train, [switch]$SmartHome, [switch]$All,
    [switch]$SkipOllama, [switch]$SkipServer, [switch]$NoShortcut, [switch]$NoAutostart, [switch]$RemoveAutostart,
    [string]$Model = 'qwen3:8b'
)
$ErrorActionPreference = 'Stop'
if ($All) { $Firmware = $true; $ClaudeHooks = $true; $Train = $true; $SmartHome = $true }
$Root = Split-Path -Parent $PSScriptRoot
$Venv = Join-Path $env:USERPROFILE 'bug-venv'
$TrainVenv = Join-Path $env:USERPROFILE 'bug-train-venv'
$IdfDir = Join-Path $env:USERPROFILE 'esp\esp-idf'
$IdfTag = 'v5.3.2'

function Step($text) { Write-Host "`n== $text" -ForegroundColor Cyan }
function Ok($text) { Write-Host "   ok: $text" -ForegroundColor Green }
function Have($cmd) { return [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }
function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}
function Need-Winget($id, $name) {
    if (-not (Have 'winget')) { throw "$name is missing and winget is not available. Install $name by hand and run this again." }
    Write-Host "   installing $name with winget..."
    winget install -e --id $id --accept-package-agreements --accept-source-agreements | Out-Null
    Refresh-Path
}
# The command (and its arguments) that runs Python 3.12, or $null
function Find-Python312 {
    $options = @(@('py', '-3.12'), @('python'))
    foreach ($option in $options) {
        if (Have $option[0]) {
            $extra = @($option | Select-Object -Skip 1)
            $version = (& $option[0] @extra --version 2>&1) -join ' '
            if ($version -match 'Python 3\.12') { return $option }
        }
    }
    return $null
}
function Run-Python($arguments) {
    $extra = @($script:py | Select-Object -Skip 1)
    & $script:py[0] @extra @arguments
}

# ---------------------------------------------------------------- Python 3.12
Step 'Python 3.12'
$py = Find-Python312
if (-not $py) { Need-Winget 'Python.Python.3.12' 'Python 3.12'; $py = Find-Python312 }
if (-not $py) { throw 'Python 3.12 was installed but this window cannot see it. Open a new terminal and run this again.' }
Ok ((Run-Python @('--version')) -join ' ')

# ---------------------------------------------------------------- Server
if (-not $SkipServer) {
    Step "Server environment ($Venv)"
    if (-not (Test-Path "$Venv\Scripts\python.exe")) { Run-Python @('-m', 'venv', $Venv) }
    & "$Venv\Scripts\python.exe" -m pip install --upgrade pip | Out-Null
    & "$Venv\Scripts\pip.exe" install -q -r "$Root\server\requirements.txt"
    Ok 'Python packages installed (speech recognition, voices, Flask, ...)'

    Step 'Local voice (Piper, works offline)'
    $voices = "$Root\server\voices"
    New-Item -ItemType Directory -Force $voices | Out-Null
    $base = 'https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_MX/claude/high'
    foreach ($file in 'es_MX-claude-high.onnx', 'es_MX-claude-high.onnx.json') {
        if (-not (Test-Path "$voices\$file")) { Write-Host "   downloading $file ..."; Invoke-WebRequest "$base/$file" -OutFile "$voices\$file" }
    }
    Ok 'Piper voice present'

    Step 'Windows Firewall (so the board can reach the server)'
    $rule = Get-NetFirewallRule -DisplayName 'Bug server' -ErrorAction SilentlyContinue
    if ($rule) { Ok 'rule already there' }
    else {
        $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            New-NetFirewallRule -DisplayName 'Bug server' -Direction Inbound -Protocol TCP -LocalPort 5000 -Profile Private -Action Allow | Out-Null
            Ok 'allowed TCP 5000 on private networks'
        } else {
            Write-Host '   not an administrator: when Windows asks the first time the server starts, choose "Private networks".' -ForegroundColor Yellow
        }
    }
}

# ---------------------------------------------------------------- Ollama
if (-not $SkipOllama) {
    Step "Free chat model (Ollama, $Model)"
    if (-not (Have 'ollama')) { Need-Winget 'Ollama.Ollama' 'Ollama' }
    $up = $true
    try { Invoke-RestMethod 'http://127.0.0.1:11434/api/version' -TimeoutSec 3 | Out-Null } catch { $up = $false }
    if (-not $up) { Start-Process ollama -ArgumentList 'serve' -WindowStyle Minimized; Start-Sleep 5 }
    $have = (& ollama list) -join "`n"
    if ($have -match [regex]::Escape($Model)) { Ok "$Model already downloaded" }
    else { Write-Host "   downloading $Model (several GB) ..."; & ollama pull $Model; Ok "$Model downloaded" }
}

# ---------------------------------------------------------------- Board tools
if ($Firmware) {
    Step 'Board tools (ESP-IDF 5.3.2)'
    if (-not (Have 'git')) { Need-Winget 'Git.Git' 'Git' }
    if (-not (Test-Path "$IdfDir\export.ps1")) {
        New-Item -ItemType Directory -Force (Split-Path $IdfDir) | Out-Null
        git clone -b $IdfTag --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git $IdfDir
    }
    & "$IdfDir\install.ps1" esp32s3
    Ok 'ESP-IDF ready'
    # ESP-IDF refuses project paths with spaces: build through a junction
    $link = Join-Path $env:USERPROFILE 'esp\bug-fw'
    if (-not (Test-Path $link)) { New-Item -ItemType Junction -Path $link -Target "$Root\firmware" | Out-Null }
    Ok "build folder $link"
    $local = "$Root\firmware\main\bug_config.local.h"
    if (-not (Test-Path $local) -and (Test-Path "$local.example")) {
        Copy-Item "$local.example" $local
        Write-Host '   created firmware\main\bug_config.local.h: open it and put your Wi-Fi and server address (it is ignored by Git).' -ForegroundColor Yellow
    }
    Write-Host '   build and flash with: scripts\flash.ps1 -Port COM5   (find the port in Device Manager)'
}

# ---------------------------------------------------------------- Claude Code hooks
if ($ClaudeHooks) {
    Step 'Claude Code <-> board (hooks)'
    & "$Venv\Scripts\python.exe" "$Root\server\tools\install_claude_hooks.py" --apply
    Write-Host '   pair the board once with: python server\tools\pair_board.py COM5   (the token is created for you; Wi-Fi password is typed in that prompt)'
}

# ---------------------------------------------------------------- Smart home (MQTT broker)
if ($SmartHome) {
    Step 'Smart home broker (Mosquitto)'
    $mosq = "$env:ProgramFiles\mosquitto"
    if (-not (Test-Path "$mosq\mosquitto.exe")) { Need-Winget 'EclipseFoundation.Mosquitto' 'Mosquitto' }
    if (-not (Test-Path "$mosq\mosquitto.exe")) { throw 'Mosquitto was installed but not found in Program Files. Install it by hand and run this again.' }
    $passFile = "$Root\server\mqtt_pass.txt"
    if (-not (Test-Path $passFile)) {
        $bytes = New-Object byte[] 18
        [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
        [IO.File]::WriteAllText($passFile, ([Convert]::ToBase64String($bytes) -replace '[+/=]', 'x'))
    }
    & "$mosq\mosquitto_passwd.exe" -b -c "$mosq\bug_passwd" bug (Get-Content $passFile -Raw).Trim()
    $conf = @"
listener 1883
allow_anonymous false
password_file $mosq\bug_passwd
"@
    [IO.File]::WriteAllText("$mosq\bug.conf", $conf)
    $ip = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like '192.168.*' -or $_.IPAddress -like '10.*' } | Select-Object -First 1).IPAddress
    [IO.File]::WriteAllText("$Root\server\mqtt_url.txt", "mqtt://bug:$((Get-Content $passFile -Raw).Trim())@${ip}:1883")
    Ok 'broker configured with a login "bug" and a random password (server\mqtt_pass.txt, never committed)'
    if (-not (Get-NetFirewallRule -DisplayName 'Bug MQTT' -ErrorAction SilentlyContinue)) {
        $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            New-NetFirewallRule -DisplayName 'Bug MQTT' -Direction Inbound -Protocol TCP -LocalPort 1883 -Profile Private -Action Allow | Out-Null
        } else { Write-Host '   not an administrator: allow TCP 1883 on private networks in Windows Firewall by hand.' -ForegroundColor Yellow }
    }
    Write-Host '   start it with:  & "$env:ProgramFiles\mosquitto\mosquitto.exe" -c "$env:ProgramFiles\mosquitto\bug.conf"'
    Write-Host '   give the board the address (the password never shows on screen):  python server\tools\pair_board.py COM5 --mqtt'
    Write-Host '   no devices yet? try the pretend home instead:  python server\tools\sim_home.py'
}

# ---------------------------------------------------------------- Training environment
if ($Train) {
    Step "Training environment ($TrainVenv)"
    if (-not (Test-Path "$TrainVenv\Scripts\python.exe")) { Run-Python @('-m', 'venv', $TrainVenv) }
    & "$TrainVenv\Scripts\pip.exe" install torch --index-url https://download.pytorch.org/whl/cu124
    & "$TrainVenv\Scripts\pip.exe" install numpy scipy soundfile datasets
    Ok 'PyTorch with CUDA ready (SETUP.md section 7c has the training steps)'
}

# ---------------------------------------------------------------- Shortcut
if (-not $NoShortcut -and -not $SkipServer) {
    Step 'Desktop shortcut'
    $desktop = [Environment]::GetFolderPath('Desktop')
    $shell = New-Object -ComObject WScript.Shell
    $lnk = $shell.CreateShortcut("$desktop\Iniciar Bug.lnk")
    $lnk.TargetPath = "$Root\Iniciar Bug.bat"
    $lnk.WorkingDirectory = $Root
    $lnk.Save()
    Ok "$desktop\Iniciar Bug.lnk"
}

# ---------------------------------------------------------------- Start with Windows
$startupLink = Join-Path ([Environment]::GetFolderPath('Startup')) 'Iniciar Bug.lnk'
if ($RemoveAutostart) {
    Step 'Start with Windows'
    if (Test-Path $startupLink) { Remove-Item $startupLink -Force; Ok 'automatic start removed' } else { Ok 'it was not set' }
}
elseif (-not $NoAutostart -and -not $SkipServer) {
    Step 'Start the Bug server with Windows'
    $shell = New-Object -ComObject WScript.Shell
    $lnk = $shell.CreateShortcut($startupLink)
    $lnk.TargetPath = "$Root\Iniciar Bug.bat"
    $lnk.WorkingDirectory = $Root
    $lnk.WindowStyle = 7   # minimized: it runs in the taskbar, close it to stop the server
    $lnk.Description = 'Starts the Bug server when you sign in'
    $lnk.Save()
    Ok "$startupLink (remove it with: scripts\Instalar.bat -RemoveAutostart)"
}
if (-not $SkipOllama -and -not $RemoveAutostart) {
    # Ollama's own installer makes it start with Windows; say so if that did not happen
    $run = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -ErrorAction SilentlyContinue
    $ollamaStartup = Join-Path ([Environment]::GetFolderPath('Startup')) 'Ollama.lnk'
    if (($run -and $run.PSObject.Properties.Name -contains 'Ollama') -or (Test-Path $ollamaStartup)) { Ok 'Ollama starts with Windows' }
    else { Write-Host '   Ollama does not start with Windows by itself; Iniciar Bug.bat starts it when the server starts.' -ForegroundColor Yellow }
    # A real question through the same door the Bug server uses, so a broken model shows up now and not at the first free question
    Step 'Checking the free chat model answers'
    try {
        $body = @{ model = $Model; stream = $false; think = $false; options = @{ num_predict = 12 }; messages = @(@{ role = 'user'; content = 'Responde solo: listo' }) } | ConvertTo-Json -Depth 5
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $answer = Invoke-RestMethod 'http://127.0.0.1:11434/api/chat' -Method Post -Body $body -ContentType 'application/json' -TimeoutSec 180
        Ok ("$Model answered in {0:N1} s: {1}" -f $watch.Elapsed.TotalSeconds, ($answer.message.content -replace '\s+', ' ').Trim())
    } catch { Write-Host "   the model did not answer ($($_.Exception.Message)). Free chat will say 'No te entendi' until it does." -ForegroundColor Yellow }
}

Write-Host "`nDone. Start the server with 'Iniciar Bug' and read README.md for the board and your accounts." -ForegroundColor Green
