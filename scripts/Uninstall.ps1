$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot 'ARE-Audio-Renderer-Control.exe'
$dll = Join-Path $PSScriptRoot 'ARE-Audio-Renderer.dll'
if (!(Test-Path -LiteralPath $tool) -or !(Test-Path -LiteralPath $dll)) { throw 'Run this script from the installed or extracted release folder.' }
& $tool --uninstall $dll
if ($LASTEXITCODE -ne 0) { throw 'Unregistration failed.' }
Write-Host 'Renderer unregistered. Close MPC-HC before removing this folder. Saved device preferences were retained.'
