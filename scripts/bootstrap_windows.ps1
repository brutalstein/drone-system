$ErrorActionPreference = "Stop"

Write-Host "Checking WSL..."
wsl --status | Out-Null
if ($LASTEXITCODE -ne 0) {
  Write-Host "Installing WSL and Ubuntu 24.04. A reboot may be required."
  wsl --install -d Ubuntu-24.04
  exit 0
}

$distros = (wsl -l -q) -join "
"
if ($distros -notmatch "Ubuntu-24.04") {
  wsl --install -d Ubuntu-24.04
}

Write-Host "WSL baseline ready."
Write-Host "Open Ubuntu-24.04, clone the repo, then run ./scripts/bootstrap_wsl.sh"
