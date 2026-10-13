$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Install-Support.ps1')
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
Write-Host 'Renderer unregistered and integration startup removed. Open players/settings may retain the old DLL until they close. Reinstalling is safe; saved device preferences are retained.'
