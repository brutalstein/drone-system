[CmdletBinding()]
param(
    [ValidateRange(1,64)][int]$Drones = 3,
    [switch]$Doctor,
    [switch]$SkipTests,
    [switch]$NoGrok,
    [switch]$NoGazebo,
    [switch]$Stop,
    [string]$Distro = "Ubuntu-24.04"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Runtime = Join-Path $Root "runtime"
New-Item -ItemType Directory -Path $Runtime -Force | Out-Null

function Write-Brand {
    Clear-Host
    Write-Host ""
    Write-Host "  ╔══════════════════════════════════════════════════════╗" -ForegroundColor DarkCyan
    Write-Host "  ║            AERION  •  DRONE FLEET OS               ║" -ForegroundColor Cyan
    Write-Host "  ║      Intelligent Windows / WSL Orchestrator         ║" -ForegroundColor Magenta
    Write-Host "  ╚══════════════════════════════════════════════════════╝" -ForegroundColor DarkCyan
    Write-Host ""
}
function Step([string]$Text) { Write-Host "  ◆ $Text" -ForegroundColor Cyan }
function Good([string]$Text) { Write-Host "  ✓ $Text" -ForegroundColor Green }
function Warn([string]$Text) { Write-Host "  ! $Text" -ForegroundColor Yellow }
function Fail([string]$Text) { Write-Host "  ✗ $Text" -ForegroundColor Red }
function Is-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($id)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Invoke-WSL([string]$Command, [switch]$AllowFailure) {
    & wsl.exe -d $Distro -- bash -lc $Command
    $code = $LASTEXITCODE
    if ($code -ne 0 -and -not $AllowFailure) { throw "WSL command failed with exit code $code" }
    return $code
}
function Add-WSLEnv([string]$Entry) {
    $parts = @()
    if ($env:WSLENV) { $parts = @($env:WSLENV.Split(":") | Where-Object { $_ }) }
    if ($parts -notcontains $Entry) { $parts += $Entry }
    $env:WSLENV = $parts -join ":"
}
function Ensure-AdminRelaunch {
    if (Is-Admin) { return }
    Warn "Administrator permission is required because a Windows/WSL prerequisite is missing."
    $args = @("-NoProfile","-ExecutionPolicy","Bypass","-File",$PSCommandPath,"-Drones",$Drones,"-Distro",$Distro)
    if ($Doctor) { $args += "-Doctor" }
    if ($SkipTests) { $args += "-SkipTests" }
    if ($NoGrok) { $args += "-NoGrok" }
    if ($NoGazebo) { $args += "-NoGazebo" }
    if ($Stop) { $args += "-Stop" }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $args
    exit 0
}

Write-Brand
Step "Building a private local inventory of this PC"
$os = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors
$gpus = Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,AdapterRAM
$disks = Get-CimInstance Win32_LogicalDisk -Filter "DriveType=3" | Select-Object DeviceID,Size,FreeSpace
$apps = @()
$roots = @(
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*",
    "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*",
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*"
)
foreach ($path in $roots) {
    $apps += Get-ItemProperty $path -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName } | Select-Object DisplayName,DisplayVersion,Publisher,InstallLocation
}
$apps = $apps | Sort-Object DisplayName -Unique
$tools = [ordered]@{}
foreach ($name in @("git","cmake","python","pwsh","winget","wsl")) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    $tools[$name] = if ($cmd) { $cmd.Source } else { $null }
}
$report = [ordered]@{
    generated_at = (Get-Date).ToUniversalTime().ToString("o")
    computer_name = $env:COMPUTERNAME
    os = [ordered]@{ caption=$os.Caption; version=$os.Version; build=$os.BuildNumber; architecture=$os.OSArchitecture }
    cpu = $cpu
    memory_gb = [math]::Round($os.TotalVisibleMemorySize / 1MB,2)
    gpu = $gpus
    disks = $disks
    tools = $tools
    installed_applications = $apps
}
$report | ConvertTo-Json -Depth 7 | Set-Content -Path (Join-Path $Runtime "system-report-windows.json") -Encoding UTF8
Good "Windows inventory saved locally; it is never uploaded by the launcher."

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    if ($Doctor) { Fail "WSL is not installed."; exit 10 }
    Ensure-AdminRelaunch
    Step "Installing WSL platform"
    wsl.exe --install --no-distribution
    Warn "A reboot may be required. Run this same script again after Windows restarts."
    exit 3010
}

Step "Inspecting WSL and Ubuntu distribution"
& wsl.exe --status | Out-Null
if ($LASTEXITCODE -ne 0) {
    if ($Doctor) { Fail "WSL is present but not operational."; exit 10 }
    Ensure-AdminRelaunch
    wsl.exe --install --no-distribution
}

$distros = @((& wsl.exe -l -q 2>$null) | ForEach-Object { ($_ -replace [char]0,"").Trim() } | Where-Object { $_ })
if ($distros -notcontains $Distro) {
    if ($Doctor) { Fail "$Distro is not installed."; exit 10 }
    Ensure-AdminRelaunch
    Step "Installing only the missing $Distro distribution"
    wsl.exe --install -d $Distro --no-launch
    if ($LASTEXITCODE -ne 0) { throw "Ubuntu installation failed." }
    Warn "If Ubuntu requests a Linux username/password on first launch, complete that one-time step and run this script again."
}
Good "WSL distribution: $Distro"

$repoWsl = (& wsl.exe -d $Distro -- wslpath -a "$Root").Trim()
if (-not $repoWsl) { throw "Could not translate the repository path into WSL." }

$env:DRONE_SOURCE_ROOT = $repoWsl
Add-WSLEnv "DRONE_SOURCE_ROOT/u"
if ($env:XAI_API_KEY) {
    Add-WSLEnv "XAI_API_KEY/u"
    if ($env:DRONE_GROK_MODEL) { Add-WSLEnv "DRONE_GROK_MODEL/u" }
    Good "xAI key detected and passed ephemerally to WSL; the key value is not written to reports."
} elseif (-not $NoGrok) {
    Warn "XAI_API_KEY is not set. Core fleet system will run; Grok advisor will stay disabled."
}

if ($Stop) {
    Step "Stopping the running drone stack"
    Invoke-WSL 'if [ -x "$HOME/.local/share/drone-system/repo/scripts/stop_stack.sh" ]; then "$HOME/.local/share/drone-system/repo/scripts/stop_stack.sh"; else tmux kill-session -t drone-system 2>/dev/null || true; fi' -AllowFailure | Out-Null
    Good "Stop command complete."
    exit 0
}

Step "Synchronizing source into the Linux filesystem for reliable ROS builds"
$sync = @'
set -e
SRC="$DRONE_SOURCE_ROOT"
DST="$HOME/.local/share/drone-system/repo"
if [ -x "$DST/scripts/stop_stack.sh" ]; then "$DST/scripts/stop_stack.sh" >/dev/null 2>&1 || true; fi
NEXT="$DST.next"
rm -rf "$NEXT"
mkdir -p "$NEXT"
(cd "$SRC" && tar --exclude=.git --exclude=runtime --exclude=ros2_ws/build --exclude=ros2_ws/install --exclude=ros2_ws/log -cf - .) | (cd "$NEXT" && tar -xf -)
if [ -d "$DST/ros2_ws/build" ]; then mkdir -p "$NEXT/ros2_ws"; mv "$DST/ros2_ws/build" "$NEXT/ros2_ws/build"; fi
if [ -d "$DST/ros2_ws/install" ]; then mkdir -p "$NEXT/ros2_ws"; mv "$DST/ros2_ws/install" "$NEXT/ros2_ws/install"; fi
if [ -d "$DST/ros2_ws/log" ]; then mkdir -p "$NEXT/ros2_ws"; mv "$DST/ros2_ws/log" "$NEXT/ros2_ws/log"; fi
if [ -d "$DST/runtime" ]; then mv "$DST/runtime" "$NEXT/runtime"; fi
rm -rf "$DST"
mv "$NEXT" "$DST"
chmod +x "$DST"/scripts/*.sh
'@
Invoke-WSL $sync | Out-Null
Good "Linux worktree synchronized; previous build cache preserved."

$bootstrapFlags = @()
if ($Doctor) { $bootstrapFlags += "--doctor" }
if ($SkipTests) { $bootstrapFlags += "--skip-tests" }
Step $(if ($Doctor) { "Running full dependency/system doctor" } else { "Checking dependencies, building only when source changed, and validating tests" })
$bootstrapCmd = 'cd "$HOME/.local/share/drone-system/repo" && ./scripts/smart_bootstrap.sh ' + ($bootstrapFlags -join " ")
$bootstrapCode = Invoke-WSL $bootstrapCmd -AllowFailure
if ($bootstrapCode -ne 0) {
    if ($Doctor) { Fail "Doctor found missing prerequisites. Re-run without -Doctor to repair automatically."; exit $bootstrapCode }
    throw "Bootstrap/build validation failed."
}
Good "Environment validation complete."

$linuxReport = & wsl.exe -d $Distro -- bash -lc 'cat "$HOME/.local/share/drone-system/repo/runtime/system-report-wsl.json" 2>/dev/null || true'
if ($linuxReport) { $linuxReport | Set-Content -Path (Join-Path $Runtime "system-report-wsl.json") -Encoding UTF8 }

if ($Doctor) {
    Good "Doctor completed. No launch requested."
    Write-Host "  Reports: $Runtime" -ForegroundColor DarkGray
    exit 0
}

$launchFlags = @()
if ($NoGrok) { $launchFlags += "--no-grok" }
if ($NoGazebo) { $launchFlags += "--no-gazebo" }
Step "Launching the complete fleet stack"
$launchCmd = 'cd "$HOME/.local/share/drone-system/repo" && ./scripts/launch_stack.sh ' + $Drones + " " + ($launchFlags -join " ")
Invoke-WSL $launchCmd | Out-Null

Good "Drone System is running."
Write-Host ""
Write-Host "  Drones       : $Drones" -ForegroundColor White
Write-Host "  UI           : AERION premium fleet console via WSLg" -ForegroundColor White
Write-Host "  Gazebo       : $(if ($NoGazebo) { 'disabled' } else { 'enabled' })" -ForegroundColor White
Write-Host "  Grok         : $(if ($NoGrok -or -not $env:XAI_API_KEY) { 'disabled / no key' } else { 'enabled' })" -ForegroundColor White
Write-Host "  Windows scan : $Runtime\system-report-windows.json" -ForegroundColor DarkGray
Write-Host "  Linux scan   : $Runtime\system-report-wsl.json" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  Stop later   : .\START-DRONE-SYSTEM.ps1 -Stop" -ForegroundColor Cyan
Write-Host "  Health only  : .\START-DRONE-SYSTEM.ps1 -Doctor" -ForegroundColor Cyan
Write-Host ""
