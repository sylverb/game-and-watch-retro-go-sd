# Requires -RunAsAdministrator
$ErrorActionPreference = "SilentlyContinue"  # Winget/WMI failures are non-fatal

# -- Helpers --------------------------------------------------------------------

function Confirm-Action {
    param([string]$Prompt, [bool]$Default = $true)
    $hint = if ($Default) { "[Y/n]" } else { "[y/N]" }
    $answer = Read-Host "$Prompt $hint"
    if ($answer -eq '') { return $Default }
    return $answer -match '^[Yy]'
}

function Remove-FromPath {
    param([string]$PathToRemove)
    $current = [Environment]::GetEnvironmentVariable("Path", "Machine")
    if ($current -like "*$PathToRemove*") {
        $updated = ($current -split ';' | Where-Object { $_ -ne $PathToRemove }) -join ';'
        [Environment]::SetEnvironmentVariable("Path", $updated, "Machine")
        Write-Host "  Removed: $PathToRemove"
    }
}

# -- Winget packages ------------------------------------------------------------

Write-Host ""
Write-Host "=== Winget Packages ===" -ForegroundColor Cyan

$wingetApps = @(
    @{ Id = "Python.Python.3.12"; Name = "Python 3.12" }
    @{ Id = "Git.Git";            Name = "Git" }
    @{ Id = "MSYS2.MSYS2";        Name = "MSYS2" }
)

foreach ($app in $wingetApps) {
    if (Confirm-Action "Uninstall $($app.Name)?") {
        winget uninstall --id $app.Id -e --silent
    }
}

# -- ARM GNU Toolchain ----------------------------------------------------------

Write-Host ""
Write-Host "=== ARM GNU Toolchain ===" -ForegroundColor Cyan

if (Confirm-Action "Uninstall ARM GNU Toolchain?") {
    $armApp = Get-WmiObject -Class Win32_Product | Where-Object { $_.Name -match "Arm GNU Toolchain" }
    if ($armApp) {
        $armApp.Uninstall() | Out-Null
        Write-Host "  ARM Toolchain removed."
    } else {
        Write-Host "  Not found - skipping."
    }
}

# -- PATH entries ---------------------------------------------------------------

Write-Host ""
Write-Host "=== System PATH Entries ===" -ForegroundColor Cyan

$pathEntries = @(
    "C:\Program Files\Git\bin"
    "C:\msys64\usr\bin"
    "C:\msys64\mingw64\bin"
    "C:\msys64\ucrt64\bin"
    "C:\Program Files (x86)\Arm\GNU Toolchain mingw-w64-i686-arm-none-eabi\bin"
)

if (Confirm-Action "Remove toolchain entries from System PATH?") {
    foreach ($p in $pathEntries) { Remove-FromPath $p }
}

# -- MSYS2 directory ------------------------------------------------------------

Write-Host ""
Write-Host "=== MSYS2 Directory ===" -ForegroundColor Cyan

if (Test-Path "C:\msys64") {
    if (Confirm-Action "Delete residual MSYS2 files at C:\msys64? (This cannot be undone)") {
        Remove-Item "C:\msys64" -Recurse -Force
        Write-Host "  C:\msys64 removed."
    }
} else {
    Write-Host "  C:\msys64 not found - skipping."
}

Write-Host ""
Write-Host "Uninstallation complete." -ForegroundColor Green
