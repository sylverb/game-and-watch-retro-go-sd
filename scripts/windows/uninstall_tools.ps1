# Requires -RunAsAdministrator

function Remove-FromPath {
    param([string]$PathToRemove)
    $CurrentPath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    if ($CurrentPath -like "*$PathToRemove*") {
        Write-Host "Removing $PathToRemove from System PATH..."
        $NewPath = ($CurrentPath -split ';' | Where-Object { $_ -ne $PathToRemove }) -join ';'
        [Environment]::SetEnvironmentVariable("Path", $NewPath, "Machine")
    }
}

# 1. Uninstall Winget packages
$apps = @(
    "JernejSimoncic.Wget",
    "Python.Python.3.12",
    "Git.Git",
    "MSYS2.MSYS2"
)

foreach ($app in $apps) {
    Write-Host "Uninstalling $app..."
    winget uninstall --id $app -e --silent
}

# 2. Uninstall ARM Toolchain (via Product Code or Name search)
Write-Host "Uninstalling ARM Toolchain..."
$armApp = Get-WmiObject -Class Win32_Product | Where-Object { $_.Name -match "Arm GNU Toolchain" }
if ($armApp) {
    $armApp.Uninstall()
}

# 3. Clean up PATH
Remove-FromPath "C:\Program Files\Git\bin"
Remove-FromPath "C:\msys64\usr\bin"
Remove-FromPath "C:\msys64\mingw64\bin"
Remove-FromPath "C:\msys64\ucrt64\bin"

# 4. Optional: Remove MSYS2 directory leftovers
if (Test-Path "C:\msys64") {
    Write-Host "Removing residual MSYS2 files..."
    Remove-Item "C:\msys64" -Recurse -Force
}

Write-Host "Uninstallation complete."
