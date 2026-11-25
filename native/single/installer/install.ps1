# Takoyo v2.2.1 "Octo" Installer
# Self-extracting PowerShell installer

$ErrorActionPreference = 'Stop'
$installDir = "$env:LOCALAPPDATA\Takoyo"
$exeName = "Takoyo.exe"
$version = "2.2.1"
$buildId = 1795577389

Write-Host "╔═══════════════════════════════════════════════╗"
Write-Host "║  Takoyo v$version (Octo) Installer        ║"
Write-Host "║  Build $buildId                              ║"
Write-Host "╚═══════════════════════════════════════════════╝"
Write-Host ""

# Check for existing installation
if (Test-Path "$installDir\$exeName") {
    $existing = Get-Item "$installDir\$exeName"
    Write-Host "Existing install found: $($existing.LastWriteTime) ($($existing.Length/1MB) MB)"
    $confirm = Read-Host "Overwrite? [Y/n]"
    if ($confirm -ine 'y' -and $confirm -ine 'yes' -and $confirm -ine '') {
        Write-Host "Installation cancelled."
        exit 0
    }
}

# Create install directory
New-Item -ItemType Directory -Force -Path $installDir | Out-Null

# Extract embedded executable
$scriptDir = $PSScriptRoot
$sourceExe = "$scriptDir\$exeName"
if (-not (Test-Path $sourceExe)) {
    Write-Error "Embedded executable not found: $sourceExe"
    exit 1
}

Write-Host "Installing to $installDir..."
Copy-Item -LiteralPath $sourceExe -Destination "$installDir\$exeName" -Force

# Create shortcuts
$shell = New-Object -ComObject WScript.Shell
$startMenu = [Environment]::GetFolderPath('StartMenu')
$shortcut = $shell.CreateShortcut("$startMenu\Programs\Takoyo.lnk")
$shortcut.TargetPath = "$installDir\$exeName"
$shortcut.WorkingDirectory = $installDir
$shortcut.Description = "Takoyo AI Agent v$version (Octo)"
$shortcut.Save()

$desktop = [Environment]::GetFolderPath('Desktop')
$shortcut2 = $shell.CreateShortcut("$desktop\Takoyo.lnk")
$shortcut2.TargetPath = "$installDir\$exeName"
$shortcut2.WorkingDirectory = $installDir
$shortcut2.Save()

# Register uninstaller
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Takoyo"
New-Item -Path $uninstallKey -Force | Out-Null
Set-ItemProperty -Path $uninstallKey -Name "DisplayName" -Value "Takoyo v$version (Octo)"
Set-ItemProperty -Path $uninstallKey -Name "DisplayVersion" -Value $version
Set-ItemProperty -Path $uninstallKey -Name "Publisher" -Value "Takoyo"
Set-ItemProperty -Path $uninstallKey -Name "InstallLocation" -Value $installDir
Set-ItemProperty -Path $uninstallKey -Name "UninstallString" -Value "`"$installDir\$exeName`" --uninstall"
Set-ItemProperty -Path $uninstallKey -Name "QuietUninstallString" -Value "`"$installDir\$exeName`" --uninstall --quiet"
Set-ItemProperty -Path $uninstallKey -Name "NoModify" -Value 1
Set-ItemProperty -Path $uninstallKey -Name "NoRepair" -Value 1

Write-Host ""
Write-Host "✅ Installation complete!"
Write-Host "   Start Menu: Programs -> Takoyo"
Write-Host "   Desktop: Takoyo shortcut"
Write-Host ""
Write-Host "Run Takoyo from Start Menu or: $installDir\$exeName"
Read-Host "Press Enter to exit"
