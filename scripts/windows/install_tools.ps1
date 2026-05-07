# Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

# -- Helpers --------------------------------------------------------------------

function Confirm-Action {
    param([string]$Prompt, [bool]$Default = $true)
    $hint = if ($Default) { "[Y/n]" } else { "[y/N]" }
    $answer = Read-Host "$Prompt $hint"
    if ($answer -eq '') { return $Default }
    return $answer -match '^[Yy]'
}

function Prepend-ToPath {
    param([string]$NewPath)
    $current = [Environment]::GetEnvironmentVariable("Path", "Machine")
    if ($current -notlike "*$NewPath*") {
        Write-Host "  Prepending to PATH: $NewPath"
        [Environment]::SetEnvironmentVariable("Path", "$NewPath;$current", "Machine")
        $env:Path = "$NewPath;$env:Path"
    }
}

function Append-ToPath {
    param([string]$NewPath)
    $current = [Environment]::GetEnvironmentVariable("Path", "Machine")
    if ($current -notlike "*$NewPath*") {
        Write-Host "  Appending to PATH: $NewPath"
        [Environment]::SetEnvironmentVariable("Path", "$current;$NewPath", "Machine")
    }
}

# -- Step 1: Core CLI tools -----------------------------------------------------

Write-Host ""
Write-Host "=== Step 1: Core CLI Tools ===" -ForegroundColor Cyan
Write-Host "Installing Python 3.12 and Git via Winget..."

foreach ($app in @("Python.Python.3.12", "Git.Git")) {
    winget install --id $app -e --silent --accept-package-agreements --accept-source-agreements
}

# python3 symlink so scripts can call 'python3'
$pyBase = "C:\Program Files\Python312"
if ((Test-Path "$pyBase\python.exe") -and -not (Test-Path "$pyBase\python3.exe")) {
    Write-Host "  Creating python3 symlink..."
    New-Item -ItemType SymbolicLink -Path "$pyBase\python3.exe" -Target "$pyBase\python.exe" | Out-Null
}

Append-ToPath "C:\Program Files\Git\bin"

# -- Step 2: ARM GNU Toolchain --------------------------------------------------

Write-Host ""
Write-Host "=== Step 2: ARM GNU Toolchain ===" -ForegroundColor Cyan
Write-Host "Downloading arm-none-eabi toolchain (this may take a while)..."

$msi = "$env:TEMP\arm-toolchain.msi"
curl.exe -L "https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-mingw-w64-i686-arm-none-eabi.msi" -o $msi

Write-Host "Running installer..."
Start-Process msiexec.exe -Wait -NoNewWindow -ArgumentList "/i", "`"$msi`"", "EULA=1", "/quiet"

Prepend-ToPath "C:\Program Files (x86)\Arm\GNU Toolchain mingw-w64-i686-arm-none-eabi\bin"

# -- Step 3: Build environment --------------------------------------------------

Write-Host ""
Write-Host "=== Step 3: Build Environment ===" -ForegroundColor Cyan
Write-Host "How do you build this project?"
Write-Host "  [1] WSL  - already configured, no further setup needed"
Write-Host "  [2] MSYS2 - install and configure the MSYS2 toolchain"
Write-Host ""

do {
    $choice = Read-Host "Enter 1 or 2"
} while ($choice -notin @('1', '2'))

if ($choice -eq '1') {
    Write-Host ""
    Write-Host "WSL selected. Skipping MSYS2 setup." -ForegroundColor Green
    Write-Host "Restart your terminal for PATH changes to take effect."
    exit 0
}

# -- Step 3b: MSYS2 setup -------------------------------------------------------

Write-Host ""
Write-Host "Installing MSYS2..."
winget install --id MSYS2.MSYS2 -e --silent --accept-package-agreements --accept-source-agreements

Prepend-ToPath "C:\msys64\ucrt64\bin"
Append-ToPath  "C:\msys64\usr\bin"
Append-ToPath  "C:\msys64\mingw64\bin"

$pacmanExe = "C:\msys64\usr\bin\pacman.exe"
if (-not (Test-Path $pacmanExe)) {
    Write-Error "pacman not found at $pacmanExe - MSYS2 may not have installed correctly."
    exit 1
}

Write-Host "Refreshing package databases..."
& $pacmanExe -Sy --noconfirm

Write-Host "Installing toolchain packages..."
& $pacmanExe -S --noconfirm @(
    "mingw-w64-ucrt-x86_64-SDL2"
    "mingw-w64-ucrt-x86_64-gcc"
    "mingw-w64-ucrt-x86_64-make"
    "mingw-w64-ucrt-x86_64-binutils"
)

# make symlink so 'make' works without the mingw32- prefix
$makeTarget = "C:\msys64\ucrt64\bin\mingw32-make.exe"
$makeLink   = "C:\msys64\ucrt64\bin\make.exe"
if ((Test-Path $makeTarget) -and -not (Test-Path $makeLink)) {
    Write-Host "  Creating make symlink..."
    New-Item -ItemType SymbolicLink -Path $makeLink -Target $makeTarget | Out-Null
}

Write-Host ""
Write-Host "Setup complete. Restart your terminal for PATH changes to take effect." -ForegroundColor Green

