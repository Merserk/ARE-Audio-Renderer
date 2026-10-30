param([Parameter(Mandatory)][string]$Fixture, [Parameter(Mandatory)][string]$Dll, [Parameter(Mandatory)][string]$Helper)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\scripts\Install-Support.ps1')
$architecture = Get-AreArchitecture $Dll
$scheduler = New-Object -ComObject Schedule.Service
$scheduler.Connect()
$task = $null
try { $task = $scheduler.GetFolder('\').GetTask((Get-AreIntegrationTaskName $architecture)) }
catch { if ($_.Exception.HResult -ne -2147024894) { throw } }
$enabled = $task -and $task.Enabled
$installedRoot = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA "ARE Audio Renderer\$architecture")).TrimEnd('\') + '\'
$previous = @(Get-CimInstance Win32_Process -Filter "Name = 'ARE-Audio-Renderer-Settings.exe'" | Where-Object {
    $_.ExecutablePath -and $_.ExecutablePath.StartsWith($installedRoot, [StringComparison]::OrdinalIgnoreCase) -and
    $_.CommandLine -match '(?:^|\s)"?--mpc-integration"?(?:\s|$)'
})
$result = 1
try {
    if ($task) { $task.Enabled = $false }
    Stop-AreIntegration $architecture
    & $Fixture $Dll $Helper
    $result = $LASTEXITCODE
} finally {
    if ($task) { $task.Enabled = [bool]$enabled }
    foreach ($item in $previous) {
        Start-Process -FilePath $item.ExecutablePath -ArgumentList '--mpc-integration' -WindowStyle Hidden | Out-Null
    }
}
exit $result
