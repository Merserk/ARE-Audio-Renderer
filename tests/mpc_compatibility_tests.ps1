param([string]$SourceDirectory = (Split-Path -Parent $PSScriptRoot), [string]$TestDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'out\compatibility-tests'))
$ErrorActionPreference = 'Stop'
. (Join-Path $SourceDirectory 'scripts\MpcHc-Compatibility.ps1')
function Require([bool]$Value,[string]$Reason) { if (!$Value) { throw $Reason } }
$root = Join-Path ([IO.Path]::GetFullPath($TestDirectory)) ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$key = 'HKCU:\Software\ARE-MPC-Compatibility-Test-' + [guid]::NewGuid().ToString('N')
try {
    New-Item -Path $key -Force | Out-Null
    New-ItemProperty -LiteralPath $key -Name AudioRendererType -Value 'Old renderer' -PropertyType String | Out-Null
    New-ItemProperty -LiteralPath $key -Name EnableAudioSwitcher -Value 1 -PropertyType DWord | Out-Null
    $profile = [pscustomobject]@{ Type='registry'; Path=$key }
    $backup = Join-Path $root 'registry-backup.json'
    Set-AreMpcCompatibility -Profiles @($profile) -BackupPath $backup
    Require ((Get-ItemProperty -LiteralPath $key).AudioRendererType -eq $script:AreMpcMoniker) 'ARE was not selected'
    Require ((Get-ItemProperty -LiteralPath $key).EnableAudioSwitcher -eq 0) 'Blocking audio switcher was not disabled'
    New-ItemProperty -LiteralPath $key -Name Volume -Value 77 -PropertyType DWord | Out-Null
    Set-AreMpcCompatibility -Profiles @($profile) -BackupPath $backup
    Restore-AreMpcCompatibility -BackupPath $backup
    $settings = Get-ItemProperty -LiteralPath $key
    Require ($settings.AudioRendererType -eq 'Old renderer' -and $settings.EnableAudioSwitcher -eq 1 -and $settings.Volume -eq 77) 'Reinstall lost original settings or uninstall overwrote unrelated preferences'
    Set-AreMpcCompatibility -Profiles @($profile) -BackupPath $backup
    Set-ItemProperty -LiteralPath $key -Name AudioRendererType -Value 'New user choice'
    Restore-AreMpcCompatibility -BackupPath $backup
    Require ((Get-ItemProperty -LiteralPath $key).AudioRendererType -eq 'New user choice') 'Uninstall overwrote a later user choice'
    foreach ($encoding in @([Text.Encoding]::Unicode, [Text.UTF8Encoding]::new($true), [Text.UTF8Encoding]::new($false))) {
        $path = Join-Path $root ('mpc-' + [guid]::NewGuid().ToString('N') + '.ini')
        $text = "[Other]`r`nName=Unicode " + [char]0x03a9 + "`r`n[Settings]`r`nVolume=77`r`nAudioRendererType=Old renderer`r`n[Last]`r`nKeep=1`r`n"
        [IO.File]::WriteAllText($path,$text,$encoding)
        $iniBackup = $path + '.json'
        Set-AreMpcCompatibility -Profiles @([pscustomobject]@{ Type='ini'; Path=$path }) -BackupPath $iniBackup
        $modified = Read-AreIni $path
        Require ((Get-AreIniSetting $modified.Text 'EnableAudioSwitcher') -eq '0') 'Portable audio switcher remained enabled'
        Require ($modified.Text.Contains('Name=Unicode ' + [char]0x03a9)) 'Portable Unicode settings changed'
        Restore-AreMpcCompatibility -BackupPath $iniBackup
        Require ([IO.File]::ReadAllText($path) -eq $text) 'Portable settings were not restored exactly'
    }
    Write-Output 'MPC-HC compatibility: registry/portable profiles, direct audio route, Unicode preservation, repeated install, uninstall restoration and later user choices passed.'
} finally {
    # A dedicated single registry leaf created by this test; never a user profile.
    if (Test-Path -LiteralPath $key) { Remove-Item -LiteralPath $key }
}
