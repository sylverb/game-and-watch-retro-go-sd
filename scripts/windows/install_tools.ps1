# Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

# -- Helpers -------------------------------------------------------------------

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

function Get-PythonVersion {
    foreach ($cmd in @("python", "python3")) {
        $exe = Get-Command $cmd -ErrorAction SilentlyContinue
        if (-not $exe) { continue }
        try {
            $out = & $exe.Source -c "import sys; print(sys.version_info.major, sys.version_info.minor)" 2>$null
            $parts = $out.Trim() -split ' '
            return @([int]$parts[0], [int]$parts[1])
        } catch {}
    }
    return $null
}

function Get-RegistryApp {
    param([string]$NamePattern)
    $regPaths = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    return Get-ItemProperty $regPaths -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -match $NamePattern } |
        Select-Object -First 1
}

function Install-Python312 {
    Write-Host "  Installing Python 3.12 via Winget..."
    winget install --id Python.Python.3.12 -e --silent --accept-package-agreements --accept-source-agreements
    $pyBase = "C:\Program Files\Python312"
    if ((Test-Path "$pyBase\python.exe") -and -not (Test-Path "$pyBase\python3.exe")) {
        Write-Host "  Creating python3 symlink..."
        New-Item -ItemType SymbolicLink -Path "$pyBase\python3.exe" -Target "$pyBase\python.exe" | Out-Null
    }
}

$requirements = Join-Path $PSScriptRoot "..\..\requirements.txt"
if (-not (Test-Path $requirements)) {
    Write-Error "requirements.txt not found at $requirements"
    exit 1
}
python -m pip install -r $requirements

# -- Step 1: Build environment choice ------------------------------------------

Write-Host ""
Write-Host "=== Build Environment ===" -ForegroundColor Cyan
Write-Host "How do you build this project?"
Write-Host "  [1] WSL  - Python and pip only (needed for gnwmanager on Windows)"
Write-Host "  [2] MSYS2 - Full toolchain install"
Write-Host ""

do {
    $choice = Read-Host "Enter 1 or 2"
} while ($choice -notin @('1', '2'))

# -- Step 2: Python ------------------------------------------------------------

Write-Host ""
Write-Host "=== Python ===" -ForegroundColor Cyan

$pyVer = Get-PythonVersion
if ($pyVer -and ($pyVer[0] -gt 3 -or ($pyVer[0] -eq 3 -and $pyVer[1] -ge 10))) {
    Write-Host "  Python $($pyVer[0]).$($pyVer[1]) found -- skipping."
} elseif ($pyVer) {
    Write-Host "  Python $($pyVer[0]).$($pyVer[1]) found, but 3.10 or later is required."
    if (Confirm-Action "  Install Python 3.12 alongside it?") {
        Install-Python312
    } else {
        Write-Warning "Python 3.10+ is required for gnwmanager. Continuing anyway."
    }
} else {
    Write-Host "  Python not found."
    Install-Python312
}

# -- WSL path: done after Python -----------------------------------------------

if ($choice -eq '1') {
    Write-Host ""
    Write-Host "WSL selected. To verify pip is available, run: python -m ensurepip --upgrade" -ForegroundColor Green
    Write-Host "Done. Restart your terminal for any PATH changes to take effect."
    exit 0
}

# -- Step 3: Git ---------------------------------------------------------------

Write-Host ""
Write-Host "=== Git ===" -ForegroundColor Cyan

if (Get-Command git -ErrorAction SilentlyContinue) {
    Write-Host "  Git already installed -- skipping."
} else {
    Write-Host "  Installing Git..."
    winget install --id Git.Git -e --silent --accept-package-agreements --accept-source-agreements
    Append-ToPath "C:\Program Files\Git\bin"
}

# -- Step 4: ARM GNU Toolchain -------------------------------------------------

Write-Host ""
Write-Host "=== ARM GNU Toolchain ===" -ForegroundColor Cyan

$targetArmVersion = "15.2"
$armApp = Get-RegistryApp "Arm GNU Toolchain"

if ($armApp -and $armApp.DisplayVersion -like "$targetArmVersion*") {
    Write-Host "  ARM GNU Toolchain $($armApp.DisplayVersion) already installed -- skipping."
} else {
    if ($armApp) {
        Write-Host "  Found ARM GNU Toolchain $($armApp.DisplayVersion), expected $targetArmVersion.x."
    } else {
        Write-Host "  ARM GNU Toolchain not found."
    }
    Write-Host "  Downloading toolchain (this may take a while)..."
    $msi = "$env:TEMP\arm-toolchain.msi"
    curl.exe -L "https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-mingw-w64-i686-arm-none-eabi.msi" -o $msi
    Write-Host "  Running installer..."
    Start-Process msiexec.exe -Wait -NoNewWindow -ArgumentList "/i", "`"$msi`"", "EULA=1", "/quiet"
    Prepend-ToPath "C:\Program Files (x86)\Arm\GNU Toolchain mingw-w64-i686-arm-none-eabi\bin"
}

# -- Step 5: MSYS2 + build dependencies ----------------------------------------

Write-Host ""
Write-Host "=== MSYS2 ===" -ForegroundColor Cyan

$pacmanExe = "C:\msys64\usr\bin\pacman.exe"

if (Test-Path $pacmanExe) {
    Write-Host "  MSYS2 already installed -- checking build dependencies..."
} else {
    Write-Host "  Installing MSYS2..."
    winget install --id MSYS2.MSYS2 -e --silent --accept-package-agreements --accept-source-agreements
    Prepend-ToPath "C:\msys64\ucrt64\bin"
    Append-ToPath  "C:\msys64\usr\bin"
    Append-ToPath  "C:\msys64\mingw64\bin"
    if (-not (Test-Path $pacmanExe)) {
        Write-Error "pacman not found at $pacmanExe -- MSYS2 may not have installed correctly."
        exit 1
    }
    Write-Host "  Refreshing package databases..."
    & $pacmanExe -Sy --noconfirm
}

Write-Host "  Installing/updating build packages..."
& $pacmanExe -S --noconfirm @(
    "mingw-w64-ucrt-x86_64-SDL2"
    "mingw-w64-ucrt-x86_64-gcc"
    "mingw-w64-ucrt-x86_64-make"
    "mingw-w64-ucrt-x86_64-binutils"
)

$makeTarget = "C:\msys64\ucrt64\bin\mingw32-make.exe"
$makeLink   = "C:\msys64\ucrt64\bin\make.exe"
if ((Test-Path $makeTarget) -and -not (Test-Path $makeLink)) {
    Write-Host "  Creating make symlink..."
    New-Item -ItemType SymbolicLink -Path $makeLink -Target $makeTarget | Out-Null
}

Write-Host ""
Write-Host "Setup complete. Restart your terminal for PATH changes to take effect." -ForegroundColor Green
