# Stock MPC-HC rejects unconnected third-party renderers in its internal Audio
# Switcher. Connect the decoder directly to ARE; never alter the player binary.
$script:AreMpcMoniker = '@device:sw:{E0F158E1-CB04-11D0-BD4E-00A0C911CE86}\{8A793BA1-CF7E-4F24-939F-288D13CA5787}'

function Read-AreIni([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $encoding = [Text.UTF8Encoding]::new($false)
    $offset = 0
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254) { $encoding = [Text.Encoding]::Unicode; $offset = 2 }
    elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255) { $encoding = [Text.Encoding]::BigEndianUnicode; $offset = 2 }
    elseif ($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) { $encoding = [Text.UTF8Encoding]::new($true); $offset = 3 }
    [pscustomobject]@{ Text = $encoding.GetString($bytes, $offset, $bytes.Length - $offset); Encoding = $encoding }
}
function Get-AreIniSetting([string]$Text, [string]$Name) {
    $section = [regex]::Match($Text, '(?ms)^\[Settings\][ \t]*\r?\n(?<body>.*?)(?=^\[|\z)')
    if (!$section.Success) { return $null }
    $entry = [regex]::Match($section.Groups['body'].Value, '(?m)^[ \t]*' + [regex]::Escape($Name) + '[ \t]*=(?<value>[^\r\n]*)')
    if ($entry.Success) { return $entry.Groups['value'].Value }
    return $null
}
function Set-AreIniSetting([string]$Text, [string]$Name, $Value) {
    $newline = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $section = [regex]::Match($Text, '(?ms)^\[Settings\][ \t]*\r?\n(?<body>.*?)(?=^\[|\z)')
    if (!$section.Success) {
        if ($null -eq $Value) { return $Text }
        return $Text.TrimEnd("`r", "`n") + $newline + '[Settings]' + $newline + $Name + '=' + $Value + $newline
    }
    $body = $section.Groups['body'].Value
    $pattern = '(?m)^[ \t]*' + [regex]::Escape($Name) + '[ \t]*=[^\r\n]*(?:\r?\n|$)'
    $entry = [regex]::Match($body, $pattern)
    $replacement = if ($null -eq $Value) { '' } else { $Name + '=' + $Value + $newline }
    if ($entry.Success) { $body = $body.Remove($entry.Index,$entry.Length).Insert($entry.Index,$replacement) }
    elseif ($null -ne $Value) { $body = $body.TrimEnd("`r", "`n") + $newline + $replacement }
    $group = $section.Groups['body']
    return $Text.Remove($group.Index,$group.Length).Insert($group.Index,$body)
}
function Get-AreMpcProfiles([string]$MpcHcPath = '') {
    $paths = @($MpcHcPath)
    if ($env:ProgramFiles) { $paths += (Join-Path $env:ProgramFiles 'MPC-HC\mpc-hc64.exe'), (Join-Path $env:ProgramFiles 'MPC-HC\mpc-hc.exe') }
    if (${env:ProgramFiles(x86)}) { $paths += (Join-Path ${env:ProgramFiles(x86)} 'MPC-HC\mpc-hc.exe') }
    foreach ($hive in @('HKCU:','HKLM:')) {
        foreach ($name in @('mpc-hc.exe','mpc-hc64.exe')) {
            $appPath = "$hive\Software\Microsoft\Windows\CurrentVersion\App Paths\$name"
            if (Test-Path -LiteralPath $appPath) { $paths += (Get-Item -LiteralPath $appPath).GetValue('') }
        }
    }
    $paths += @(Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('mpc-hc.exe','mpc-hc64.exe') } | Select-Object -ExpandProperty ExecutablePath)
    foreach ($exe in @($paths | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -Unique)) {
        foreach ($ini in @([IO.Path]::ChangeExtension($exe,'.ini'), (Join-Path (Split-Path -Parent $exe) 'mpc-hc.ini')) | Select-Object -Unique) {
            if (Test-Path -LiteralPath $ini -PathType Leaf) { [pscustomobject]@{ Type = 'ini'; Path = [IO.Path]::GetFullPath($ini) } }
        }
    }
    $registry = 'HKCU:\Software\MPC-HC\MPC-HC\Settings'
    if (Test-Path -LiteralPath $registry) { [pscustomobject]@{ Type = 'registry'; Path = $registry } }
}
function Set-AreMpcCompatibility([string]$MpcHcPath = '', $Profiles = $null, [string]$BackupPath = '') {
    if ($null -eq $Profiles) {
        Assert-AreMpcClosed
        $Profiles = @(Get-AreMpcProfiles $MpcHcPath)
    }
    if (!$BackupPath) { $BackupPath = Join-Path $env:LOCALAPPDATA 'ARE Audio Renderer\MPC-HC-settings-backup.json' }
    $backup = @()
    if (Test-Path -LiteralPath $BackupPath) { $backup = @((Get-Content -LiteralPath $BackupPath -Raw | ConvertFrom-Json).Profiles) }
    foreach ($profile in @($Profiles | Sort-Object Type,Path -Unique)) {
        if ($profile.Type -eq 'registry') {
            $key = Get-Item -LiteralPath $profile.Path
            $renderer = $key.GetValue('AudioRendererType', $null)
            $switcher = $key.GetValue('EnableAudioSwitcher', $null)
        } else {
            $ini = Read-AreIni $profile.Path
            $renderer = Get-AreIniSetting $ini.Text 'AudioRendererType'
            $switcher = Get-AreIniSetting $ini.Text 'EnableAudioSwitcher'
        }
        if (!@($backup | Where-Object { $_.Type -eq $profile.Type -and $_.Path -eq $profile.Path }).Count) {
            $backup += [pscustomobject]@{ Type = $profile.Type; Path = $profile.Path; Renderer = $renderer; Switcher = $switcher }
            New-Item -ItemType Directory -Path (Split-Path -Parent $BackupPath) -Force | Out-Null
            [IO.File]::WriteAllText($BackupPath,(@{ Profiles = @($backup) } | ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
        }
        if ($profile.Type -eq 'registry') {
            New-ItemProperty -LiteralPath $profile.Path -Name AudioRendererType -Value $script:AreMpcMoniker -PropertyType String -Force | Out-Null
            New-ItemProperty -LiteralPath $profile.Path -Name EnableAudioSwitcher -Value 0 -PropertyType DWord -Force | Out-Null
        } else {
            $text = Set-AreIniSetting $ini.Text 'AudioRendererType' $script:AreMpcMoniker
            $text = Set-AreIniSetting $text 'EnableAudioSwitcher' '0'
            [IO.File]::WriteAllText($profile.Path,$text,$ini.Encoding)
        }
        Write-Host "MPC-HC configured for direct ASIO playback: $($profile.Path). Internal Audio Switcher disabled; ARE handles volume and conversion."
    }
}
function Assert-AreMpcClosed([string]$Action = 'installing') {
    $players = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -in @('mpc-hc.exe','mpc-hc64.exe') })
    if ($players.Count) { throw "Close MPC-HC before $Action, so it cannot overwrite the ASIO connection settings on exit." }
}
function Restore-AreMpcCompatibility([string]$BackupPath = '') {
    if (!$BackupPath) { $BackupPath = Join-Path $env:LOCALAPPDATA 'ARE Audio Renderer\MPC-HC-settings-backup.json' }
    if (!(Test-Path -LiteralPath $BackupPath)) { return }
    $backup = Get-Content -LiteralPath $BackupPath -Raw | ConvertFrom-Json
    foreach ($profile in $backup.Profiles) {
        if (!(Test-Path -LiteralPath $profile.Path)) { continue }
        if ($profile.Type -eq 'registry') {
            $current = Get-Item -LiteralPath $profile.Path
            if ($current.GetValue('AudioRendererType') -ne $script:AreMpcMoniker) { continue }
            $renderer = if ($profile.Renderer -eq $script:AreMpcMoniker) { '' } else { $profile.Renderer }
            if ($null -eq $renderer) { Remove-ItemProperty -LiteralPath $profile.Path -Name AudioRendererType -ErrorAction SilentlyContinue }
            else { New-ItemProperty -LiteralPath $profile.Path -Name AudioRendererType -Value $renderer -PropertyType String -Force | Out-Null }
            if ($current.GetValue('EnableAudioSwitcher') -eq 0) {
                if ($null -eq $profile.Switcher) { Remove-ItemProperty -LiteralPath $profile.Path -Name EnableAudioSwitcher -ErrorAction SilentlyContinue }
                else { New-ItemProperty -LiteralPath $profile.Path -Name EnableAudioSwitcher -Value ([int]$profile.Switcher) -PropertyType DWord -Force | Out-Null }
            }
        } else {
            $ini = Read-AreIni $profile.Path
            if ((Get-AreIniSetting $ini.Text 'AudioRendererType') -ne $script:AreMpcMoniker) { continue }
            $renderer = if ($profile.Renderer -eq $script:AreMpcMoniker) { '' } else { $profile.Renderer }
            $text = Set-AreIniSetting $ini.Text 'AudioRendererType' $renderer
            if ((Get-AreIniSetting $ini.Text 'EnableAudioSwitcher') -eq '0') { $text = Set-AreIniSetting $text 'EnableAudioSwitcher' $profile.Switcher }
            [IO.File]::WriteAllText($profile.Path,$text,$ini.Encoding)
        }
    }
    [IO.File]::WriteAllText($BackupPath,(@{ Profiles = @() } | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
}
