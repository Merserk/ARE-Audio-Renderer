# Shared by install/uninstall. Compatible with Windows PowerShell 5.1.
function Get-AreIntegrationTaskName([string]$Architecture) {
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    return "ARE Audio Renderer MPC-HC $Architecture $sid"
}

function Remove-AreIntegrationTask([string]$Architecture) {
    $scheduler = New-Object -ComObject Schedule.Service
    $scheduler.Connect()
    $folder = $scheduler.GetFolder('\')
    $name = Get-AreIntegrationTaskName $Architecture
    $task = $null
    try { $task = $folder.GetTask($name) }
    catch { if ($_.Exception.HResult -ne -2147024894) { throw }; return } # file not found
    # Disable first so a recovery trigger cannot race uninstall/reinstall.
    $task.Enabled = $false
    $folder.DeleteTask($name, 0)
}

function Register-AreIntegrationTask([string]$Architecture, [string]$HelperPath) {
    $scheduler = New-Object -ComObject Schedule.Service
    $scheduler.Connect()
    $task = $scheduler.NewTask(0)
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $task.RegistrationInfo.Description = 'Keep the ARE settings gear available after player updates and recover if the settings helper exits.'
    $task.Principal.UserId = $sid
    $task.Principal.LogonType = 3 # interactive token; no stored password or elevation
    $task.Principal.RunLevel = 0
    $task.Settings.MultipleInstances = 2 # ignore new while already running
    $task.Settings.DisallowStartIfOnBatteries = $false
    $task.Settings.StopIfGoingOnBatteries = $false
    $task.Settings.ExecutionTimeLimit = 'PT0S'
    $task.Settings.StartWhenAvailable = $true
    $task.Settings.AllowDemandStart = $true
    $task.Settings.RestartInterval = 'PT1M'
    $task.Settings.RestartCount = 3
    $task.Settings.Hidden = $false # keep the recovery task visible/manageable
    $logon = $task.Triggers.Create(9)
    $logon.UserId = $sid
    # Run entries only start at sign-in and provide no recovery when an updater
    # or another process closes the helper. Retry without depending on any
    # player path, version, resource ID or executable modification.
    $retry = $task.Triggers.Create(1)
    $retry.StartBoundary = (Get-Date).AddMinutes(1).ToString('yyyy-MM-ddTHH:mm:ss')
    $retry.Repetition.Interval = 'PT1M'
    $action = $task.Actions.Create(0)
    $action.Path = $HelperPath
    $action.Arguments = '--mpc-integration'
    $action.WorkingDirectory = Split-Path -Parent $HelperPath
    $scheduler.GetFolder('\').RegisterTaskDefinition((Get-AreIntegrationTaskName $Architecture), $task, 6, $sid, $null, 3) | Out-Null
}

function Get-AreArchitecture([string]$DllPath) {
    $stream = [IO.File]::OpenRead($DllPath)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw 'Invalid renderer DLL.' }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 6) { throw 'Invalid renderer DLL.' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw 'Invalid renderer DLL.' }
        switch ($reader.ReadUInt16()) { 0x8664 { 'x64' }; 0x014c { 'x86' }; default { throw 'Unsupported renderer architecture.' } }
    } finally { $stream.Dispose() }
}

function Stop-AreIntegration([string]$Architecture) {
    # Capture the actual helper processes before signalling. The mutex may be
    # released just before Windows finishes unmapping the DLL/executable.
    $architectureRoot = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA "ARE Audio Renderer\$Architecture")).TrimEnd('\') + '\'
    $processes = @(Get-CimInstance Win32_Process -Filter "Name = 'ARE-Audio-Renderer-Settings.exe'" | Where-Object {
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($architectureRoot, [StringComparison]::OrdinalIgnoreCase) -and
        $_.CommandLine -match '(?:^|\s)"?--mpc-integration"?(?:\s|$)'
    })
    $stopEvent = $null
    $signalled = $false
    try {
        $stopEvent = [Threading.EventWaitHandle]::OpenExisting("Local\ARE.AudioRenderer.MPC.Integration.Stop.$Architecture")
        $signalled = $stopEvent.Set()
    } catch [Threading.WaitHandleCannotBeOpenedException] { }
    finally { if ($stopEvent) { $stopEvent.Dispose() } }
    # The accidentally opened 3.1.0 settings window has no stop event. Its
    # loaded files are handled by hash reuse/new payload directories instead.
    if (!$signalled) { return }
    foreach ($item in $processes) {
        # The known 3.1.0 parser bug can leave a hidden configuration process
        # whose MainWindowHandle is also zero. It does not own the helper event.
        $helperVersion = [version](Get-Item -LiteralPath $item.ExecutablePath).VersionInfo.ProductVersion
        if ($helperVersion -eq [version]'3.1.0.0' -and $item.CommandLine -notmatch '\s--mpc-integration$') { continue }
        $process = $null
        try { $process = [Diagnostics.Process]::GetProcessById([int]$item.ProcessId) }
        catch [ArgumentException] { continue }
        try {
            if ($process.MainWindowHandle -ne [IntPtr]::Zero) { continue }
            if (!$process.WaitForExit(10000)) {
                # 3.1.0 compared WinMain's raw command string exactly. A trailing
                # space accidentally opened a settings dialog without a stop event.
                # Leave that dialog open; the new installation uses another path.
                Write-Host "An older ARE process is still open (PID $($item.ProcessId)). Close it when convenient."
            }
        } finally { if ($process) { $process.Dispose() } }
    }
}

function Get-AreFileHash([string]$Path) {
    # Use .NET directly: Get-FileHash may not be available in the Windows
    # PowerShell host inherited by a CMD launcher from newer PowerShell.
    $stream = [IO.File]::OpenRead($Path)
    $digest = [Security.Cryptography.SHA256]::Create()
    try { return -join ($digest.ComputeHash($stream) | ForEach-Object { $_.ToString('x2') }) }
    finally { $digest.Dispose(); $stream.Dispose() }
}

function Test-AreSameFile([string]$Source, [string]$Destination) {
    if (!(Test-Path -LiteralPath $Destination -PathType Leaf)) { return $false }
    if ((Get-Item -LiteralPath $Source).Length -ne (Get-Item -LiteralPath $Destination).Length) { return $false }
    return (Get-AreFileHash $Source) -eq (Get-AreFileHash $Destination)
}

function Copy-AreFile([string]$Source, [string]$Destination) {
    # Identical DLLs and executables are often still mapped by a player or an
    # open settings dialog. Reinstalling them requires no write at all.
    if (Test-AreSameFile $Source $Destination) { return }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}
