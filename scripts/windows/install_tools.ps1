# Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

function Prepend-ToPath {
    param([string]$NewPath)
    $CurrentPath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    if ($CurrentPath -notlike "*$NewPath*") {
        Write-Host "Prepending $NewPath to the TOP of System PATH..."
        $UpdatedPath = "$NewPath;" + $CurrentPath
        [Environment]::SetEnvironmentVariable("Path", $UpdatedPath, "Machine")
        $env:Path = "$NewPath;" + $env:Path
    }
}

# 1. Install Winget packages
Write-Host "--- Installing CLI tools via Winget ---"
$apps = @("JernejSimoncic.Wget", "Python.Python.3.12", "Git.Git", "MSYS2.MSYS2")
foreach ($app in $apps) {
    winget install --id $app -e --silent --accept-package-agreements --accept-source-agreements
}

# 2. Kill PowerShell's wget alias
if (Get-Alias wget -ErrorAction SilentlyContinue) {
    Remove-Item alias:wget -Force
}

# 3. ARM Toolchain Download & Interactive Install
Write-Host "--- Downloading ARM Toolchain ---"
$msi = "$env:TEMP\arm-toolchain.msi"
curl.exe -L `
  "https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-mingw-w64-i686-arm-none-eabi.msi" `
  -o $msi
Start-Process msiexec.exe -Wait -NoNewWindow -ArgumentList @(
  "/i"
  "`"$msi`""
  "EULA=1"
  "/quiet"
)

# 4. PATH Configuration (Prepending to top for 'make' and 'arm-none-eabi')
Write-Host "--- Updating System PATH ---"
Prepend-ToPath "C:\msys64\ucrt64\bin"
Prepend-ToPath "C:\Program Files (x86)\Arm\GNU Toolchain mingw-w64-i686-arm-none-eabi\bin"
# Standard Appends
$otherPaths = @("C:\Program Files\Git\bin", "C:\msys64\usr\bin", "C:\msys64\mingw64\bin")
foreach ($p in $otherPaths) {
    $current = [Environment]::GetEnvironmentVariable("Path", "Machine")
    if ($current -notlike "*$p*") {
        [Environment]::SetEnvironmentVariable("Path", $current + ";" + $p, "Machine")
    }
}

# 5. MSYS2 Toolchain Setup (Direct Pacman call)
Write-Host "--- Updating MSYS2 Packages ---"
$pacmanExe = "C:\msys64\usr\bin\pacman.exe"

if (Test-Path $pacmanExe) {
    # Update package database and core system
    Write-Host "Refreshing package databases..."
    & $pacmanExe -Sy --noconfirm

    # Install the toolchain directly
    Write-Host "Installing toolchain packages..."
    $packages = @(
        "mingw-w64-ucrt-x86_64-SDL2",
        "mingw-w64-ucrt-x86_64-gcc",
        "mingw-w64-ucrt-x86_64-make",
        "mingw-w64-ucrt-x86_64-binutils"
    )

    # Joining array into a space-separated string for pacman
    & $pacmanExe -S --noconfirm $packages
} else {
    Write-Error "Pacman not found at $pacmanExe. Is MSYS2 installed?"
}

# 6. Create Aliases
Write-Host "--- Creating Command Aliases ---"
# Make alias
if (Test-Path "C:\msys64\ucrt64\bin\mingw32-make.exe") {
    $makeLink = "C:\msys64\ucrt64\bin\make.exe"
    if (-not (Test-Path $makeLink)) { New-Item -ItemType SymbolicLink -Path $makeLink -Target "C:\msys64\ucrt64\bin\mingw32-make.exe" }
}
# Python3 alias
$pyBase = "C:\Program Files\Python312"
if (Test-Path "$pyBase\python.exe") {
    if (-not (Test-Path "$pyBase\python3.exe")) { New-Item -ItemType SymbolicLink -Path "$pyBase\python3.exe" -Target "$pyBase\python.exe" }
}

Write-Host "--- Setup Complete. Restart your Terminal for PATH changes to take effect. ---"
