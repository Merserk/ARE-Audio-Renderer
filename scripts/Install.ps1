param([switch]$NoSettings, [switch]$NoMpcIntegration, [switch]$NoMpcConfiguration, [string]$MpcHcPath = '')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Install-Support.ps1')
. (Join-Path $PSScriptRoot 'MpcHc-Compatibility.ps1')
$binaryNames = @('ARE-Audio-Renderer.dll', 'ARE-Audio-Renderer-Settings.exe', 'ARE-Audio-Renderer-Control.exe')
foreach ($name in $binaryNames) {
    if (!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $name) -PathType Leaf)) { throw 'Run this script from a complete extracted release folder.' }
}
$dll = Join-Path $PSScriptRoot $binaryNames[0]
$architecture = Get-AreArchitecture $dll
$version = (Get-Item -LiteralPath $dll).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid renderer version.' }
if (!$NoMpcConfiguration) { Assert-AreMpcClosed }

function Test-Payload([string]$Folder) {
    foreach ($name in $binaryNames) {
        if (!(Test-AreSameFile (Join-Path $PSScriptRoot $name) (Join-Path $Folder $name))) { return $false }
    }
    return $true
}

# A player/property page can retain a DLL after unregistration. Reuse identical
# binaries; stage different bytes elsewhere instead of overwriting loaded images.
$versionRoot = Join-Path $env:LOCALAPPDATA "ARE Audio Renderer\$architecture\$version"
$target = $versionRoot
if ((Test-Path -LiteralPath $versionRoot) -and !(Test-Payload $versionRoot)) {
    $hashes = @($binaryNames | ForEach-Object { Get-AreFileHash (Join-Path $PSScriptRoot $_) }) -join ''
    $digest = [Security.Cryptography.SHA256]::Create()
    try { $payloadId = -join ($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes($hashes)) | ForEach-Object { $_.ToString('x2') }) }
    finally { $digest.Dispose() }
    $target = Join-Path $versionRoot ('build-' + $payloadId.Substring(0,16))
    if ((Test-Path -LiteralPath $target) -and !(Test-Payload $target)) {
        $target = Join-Path $versionRoot ('build-' + $payloadId.Substring(0,16) + '-' + [guid]::NewGuid().ToString('N'))
    }
}
Stop-AreIntegration $architecture
New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach ($name in @($binaryNames) + @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'Uninstall.ps1', 'Uninstall.bat', 'Install-Support.ps1', 'MpcHc-Compatibility.ps1')) {
    $sourceFile = Join-Path $PSScriptRoot $name
    if (Test-Path -LiteralPath $sourceFile) { Copy-AreFile $sourceFile (Join-Path $target $name) }
}
$sourceLicenses = Join-Path $PSScriptRoot 'licenses'
if (Test-Path -LiteralPath $sourceLicenses) {
    $targetLicenses = Join-Path $target 'licenses'
    New-Item -ItemType Directory -Force -Path $targetLicenses | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $sourceLicenses -File) { Copy-AreFile $file.FullName (Join-Path $targetLicenses $file.Name) }
}
$installedTool = Join-Path $target 'ARE-Audio-Renderer-Control.exe'
& $installedTool --install (Join-Path $target 'ARE-Audio-Renderer.dll')
if ($LASTEXITCODE -ne 0) { throw 'Renderer registration failed.' }
& $installedTool --verify-installed
if ($LASTEXITCODE -ne 0) { throw 'Renderer verification failed.' }
if (!$NoMpcConfiguration) { Set-AreMpcCompatibility $MpcHcPath }
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runName = "ARE Audio Renderer MPC-HC $architecture"
if ($NoMpcIntegration) {
    if (Test-Path -LiteralPath $runKey) { Remove-ItemProperty -LiteralPath $runKey -Name $runName -ErrorAction SilentlyContinue }
} else {
    if (!(Test-Path -LiteralPath $runKey)) { New-Item -Path $runKey -Force | Out-Null }
    $installedHelper = Join-Path $target 'ARE-Audio-Renderer-Settings.exe'
    New-ItemProperty -LiteralPath $runKey -Name $runName -Value ('"' + $installedHelper + '" --mpc-integration') -PropertyType String -Force | Out-Null
    $watcher = Start-Process -FilePath $installedHelper -WorkingDirectory $target -ArgumentList '--mpc-integration' -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 250
    if ($watcher.HasExited) { throw 'MPC-HC settings integration could not start.' }
}
Write-Host "Installed $architecture renderer at $target. Restart MPC-HC to load this version."
if (!$NoSettings) { Start-Process -FilePath (Join-Path $target 'ARE-Audio-Renderer-Settings.exe') -WorkingDirectory $target -Wait }
