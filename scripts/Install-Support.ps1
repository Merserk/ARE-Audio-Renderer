# Shared by install/uninstall. Compatible with Windows PowerShell 5.1.
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
        if ($helperVersion -lt [version]'3.1.1.0' -and $item.CommandLine -notmatch '\s--mpc-integration$') { continue }
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
