param([switch]$NoSettings)
$ErrorActionPreference = 'Stop'
$dll = Join-Path $PSScriptRoot 'ARE-Audio-Renderer.dll'
$tool = Join-Path $PSScriptRoot 'ARE-Audio-Renderer-Control.exe'
if (!(Test-Path -LiteralPath $dll) -or !(Test-Path -LiteralPath $tool)) { throw 'Run this script from an extracted release folder.' }
$architecture = if ($PSScriptRoot -match '(?:^|[\\/])x86(?:[\\/]|$)') { 'x86' } else { 'x64' }
# Read the PE machine rather than relying on the extracted folder name.
$stream = [IO.File]::OpenRead($dll)
try {
    $reader = [IO.BinaryReader]::new($stream)
    $stream.Position = 0x3c
    $peOffset = $reader.ReadInt32()
    $stream.Position = $peOffset + 4
    $machine = $reader.ReadUInt16()
    $architecture = switch ($machine) { 0x8664 { 'x64' }; 0x014c { 'x86' }; default { throw 'Unsupported DLL architecture.' } }
} finally { $stream.Dispose() }
$version = (Get-Item -LiteralPath $dll).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid renderer version.' }
# A running player can keep the previous DLL loaded until it exits.
$target = Join-Path $env:LOCALAPPDATA "ARE Audio Renderer\$architecture\$version"
New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach ($name in @('ARE-Audio-Renderer.dll', 'ARE-Audio-Renderer-Settings.exe', 'ARE-Audio-Renderer-Control.exe', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'Uninstall.ps1', 'Uninstall.bat')) {
    $sourceFile = Join-Path $PSScriptRoot $name
    if (Test-Path -LiteralPath $sourceFile) { Copy-Item -LiteralPath $sourceFile -Destination (Join-Path $target $name) -Force }
}
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'licenses')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination $target -Recurse -Force
}
$installedTool = Join-Path $target 'ARE-Audio-Renderer-Control.exe'
& $installedTool --install (Join-Path $target 'ARE-Audio-Renderer.dll')
if ($LASTEXITCODE -ne 0) { throw 'Renderer registration failed.' }
& $installedTool --verify-installed
if ($LASTEXITCODE -ne 0) { throw 'Renderer verification failed.' }
Write-Host "Installed $architecture renderer. Restart MPC-HC, then select ARE Audio Renderer in Options > Playback > Output > Audio Renderer."
if (!$NoSettings) { & (Join-Path $target 'ARE-Audio-Renderer-Settings.exe') }
