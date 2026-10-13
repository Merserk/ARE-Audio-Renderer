param([ValidateSet('x64','x86')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
# Enable the installed helper instead of replacing MPC-HC with a 2.8.2 build.
$view = if ($Architecture -eq 'x64') { [Microsoft.Win32.RegistryView]::Registry64 } else { [Microsoft.Win32.RegistryView]::Registry32 }
$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, $view)
try {
    $key = $base.OpenSubKey('Software\Classes\CLSID\{8A793BA1-CF7E-4F24-939F-288D13CA5787}\InprocServer32')
    if (!$key) { throw 'Install the matching ARE Audio Renderer 3.1.1 package first.' }
    try { $dll = [string]$key.GetValue('') } finally { $key.Dispose() }
} finally { $base.Dispose() }
$helper = Join-Path (Split-Path -Parent $dll) 'ARE-Audio-Renderer-Settings.exe'
if (!(Test-Path -LiteralPath $helper) -or [version](Get-Item -LiteralPath $helper).VersionInfo.ProductVersion -lt [version]'3.1.1.0') {
    throw 'Install ARE Audio Renderer 3.1.1 to enable version-independent settings integration.'
}
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if (!(Test-Path -LiteralPath $runKey)) { New-Item -Path $runKey -Force | Out-Null }
New-ItemProperty -LiteralPath $runKey -Name "ARE Audio Renderer MPC-HC $Architecture" -Value ('"' + $helper + '" --mpc-integration') -PropertyType String -Force | Out-Null
Start-Process -FilePath $helper -ArgumentList '--mpc-integration' -WindowStyle Hidden
Write-Host 'MPC-HC settings helper enabled. The player executable was not changed.'
