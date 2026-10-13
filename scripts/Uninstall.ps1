$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot 'ARE-Audio-Renderer-Control.exe'
$dll = Join-Path $PSScriptRoot 'ARE-Audio-Renderer.dll'
if (!(Test-Path -LiteralPath $tool) -or !(Test-Path -LiteralPath $dll)) { throw 'Run this script from the installed or extracted release folder.' }
$machineStream = [IO.File]::OpenRead($dll)
try {
    $reader = [IO.BinaryReader]::new($machineStream)
    $machineStream.Position = 0x3c
    $machineStream.Position = $reader.ReadInt32() + 4
    $architecture = switch ($reader.ReadUInt16()) { 0x8664 { 'x64' }; 0x014c { 'x86' }; default { throw 'Unsupported DLL architecture.' } }
} finally { $machineStream.Dispose() }
$event = $null
try {
    $event = [Threading.EventWaitHandle]::OpenExisting("Local\ARE.AudioRenderer.MPC.Integration.Stop.$architecture")
    $event.Set() | Out-Null
} catch [Threading.WaitHandleCannotBeOpenedException] { }
finally { if ($event) { $event.Dispose() } }
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if (Test-Path -LiteralPath $runKey) {
    Remove-ItemProperty -LiteralPath $runKey -Name "ARE Audio Renderer MPC-HC $architecture" -ErrorAction SilentlyContinue
}
& $tool --uninstall $dll
if ($LASTEXITCODE -ne 0) { throw 'Unregistration failed.' }
Write-Host 'Renderer unregistered. Close MPC-HC before removing this folder. Saved device preferences were retained.'
