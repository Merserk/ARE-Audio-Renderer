$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Install-Support.ps1')
. (Join-Path $PSScriptRoot 'MpcHc-Compatibility.ps1')
Assert-AreMpcClosed 'uninstalling'
$tool = Join-Path $PSScriptRoot 'ARE-Audio-Renderer-Control.exe'
$dll = Join-Path $PSScriptRoot 'ARE-Audio-Renderer.dll'
if (!(Test-Path -LiteralPath $tool) -or !(Test-Path -LiteralPath $dll)) { throw 'Run this script from the installed or extracted release folder.' }
$architecture = Get-AreArchitecture $dll
Stop-AreIntegration $architecture
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if (Test-Path -LiteralPath $runKey) {
    Remove-ItemProperty -LiteralPath $runKey -Name "ARE Audio Renderer MPC-HC $architecture" -ErrorAction SilentlyContinue
}
& $tool --uninstall $dll
if ($LASTEXITCODE -ne 0) { throw 'Unregistration failed.' }
$otherView = if ($architecture -eq 'x64') { [Microsoft.Win32.RegistryView]::Registry32 } else { [Microsoft.Win32.RegistryView]::Registry64 }
$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,$otherView)
$otherInstalled = $false
try {
    $other = $base.OpenSubKey('Software\Classes\CLSID\{8A793BA1-CF7E-4F24-939F-288D13CA5787}\InprocServer32')
    if ($other) { try { $otherInstalled = [bool]$other.GetValue('') } finally { $other.Dispose() } }
} finally { $base.Dispose() }
if (!$otherInstalled) { Restore-AreMpcCompatibility }
Write-Host 'Renderer unregistered and integration startup removed. Open players/settings may retain the old DLL until they close. Reinstalling is safe; saved device preferences are retained.'
