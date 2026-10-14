param([Parameter(Mandatory)][string]$PackageDirectory, [Parameter(Mandatory)][string]$TestDirectory)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).ProviderPath
. (Join-Path $package 'Install-Support.ps1')
$architecture = Get-AreArchitecture (Join-Path $package 'ARE-Audio-Renderer.dll')
$version = (Get-Item -LiteralPath (Join-Path $package 'ARE-Audio-Renderer.dll')).VersionInfo.ProductVersion
$originalLocalAppData = $env:LOCALAPPDATA
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runName = "ARE Audio Renderer MPC-HC $architecture"
$previousStartup = $null
$view = if ($architecture -eq 'x64') { [Microsoft.Win32.RegistryView]::Registry64 } else { [Microsoft.Win32.RegistryView]::Registry32 }
function Registered-Dll {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, $view)
    try {
        $key = $base.OpenSubKey('Software\Classes\CLSID\{8A793BA1-CF7E-4F24-939F-288D13CA5787}\InprocServer32')
        if (!$key) { return '' }
        try { return [string]$key.GetValue('') } finally { $key.Dispose() }
    } finally { $base.Dispose() }
}
function Require([bool]$Value, [string]$Reason) { if (!$Value) { throw $Reason } }
function Run-Wrapper([string]$File, [string]$Arguments = '') {
    if ((Split-Path -Leaf $File) -eq 'Install.cmd') { $Arguments += ' -NoMpcConfiguration' }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $env:ComSpec
    $start.Arguments = '/d /s /c ""' + $File + '" ' + $Arguments + '"'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    try {
        $process.StandardInput.WriteLine()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        Require ($process.WaitForExit(25000)) ('Wrapper did not finish: ' + $File)
        $output = $stdout.Result + $stderr.Result
        Require ($process.ExitCode -eq 0) ($File + ' returned ' + $process.ExitCode + ': ' + $output)
    } finally { $process.Dispose() }
}
$previousDll = ''
$snapshotTaken = $false
$mutex = [Threading.Mutex]::new($false, "Local\AREAudioRenderer.Validation.$architecture")
$locked = $false
$streams = @()
try {
    $locked = $mutex.WaitOne(30000)
    Require $locked 'Another renderer validation is still running.'
    $previousDll = Registered-Dll
    $previousStartup = (Get-ItemProperty -LiteralPath $runKey -Name $runName -ErrorAction SilentlyContinue).$runName
    $snapshotTaken = $true
    Stop-AreIntegration $architecture
    $testRoot = Join-Path ([IO.Path]::GetFullPath($TestDirectory)) ($architecture + '-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    $env:LOCALAPPDATA = Join-Path $testRoot 'LocalAppData'
    $install = Join-Path $package 'Install.cmd'
    Run-Wrapper $install '-NoSettings -NoMpcIntegration'
    $originalTarget = Registered-Dll
    Require ((Split-Path -Parent $originalTarget) -eq (Join-Path $env:LOCALAPPDATA "ARE Audio Renderer\$architecture\$version")) 'Clean install used an unexpected path.'
    $originalHash = (Get-FileHash -LiteralPath $originalTarget).Hash
    $streams += [IO.File]::Open($originalTarget, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    Run-Wrapper $install '-NoSettings -NoMpcIntegration'
    Require ((Registered-Dll) -eq $originalTarget) 'Identical reinstall did not reuse its loaded DLL.'
    Run-Wrapper (Join-Path $package 'Uninstall.bat')
    Require (!(Registered-Dll)) 'Uninstall left COM registration.'
    Run-Wrapper $install '-NoSettings -NoMpcIntegration'
    Require ((Registered-Dll) -eq $originalTarget) 'Uninstall/reinstall failed while the old DLL was locked.'
    Write-Output "${architecture}: identical DLL lock and uninstall/reinstall passed."

    # A harmless PE overlay gives another valid binary with the same version.
    $variant = Join-Path $testRoot 'variant-package'
    Copy-Item -LiteralPath $package -Destination $variant -Recurse
    $variantDll = Join-Path $variant 'ARE-Audio-Renderer.dll'
    $append = [IO.File]::Open($variantDll, [IO.FileMode]::Append)
    try { $marker = [Text.Encoding]::ASCII.GetBytes('ARE same-version installer regression'); $append.Write($marker, 0, $marker.Length) }
    finally { $append.Dispose() }
    Run-Wrapper (Join-Path $variant 'Install.cmd') '-NoSettings -NoMpcIntegration'
    $variantTarget = Registered-Dll
    Require ($variantTarget -ne $originalTarget -and (Split-Path -Leaf (Split-Path -Parent $variantTarget)) -like 'build-*') 'Changed same-version binary did not get another payload directory.'
    Require ((Get-FileHash -LiteralPath $originalTarget).Hash -eq $originalHash) 'Changed install overwrote the locked original binary.'
    Require (Test-AreSameFile $variantDll $variantTarget) 'Changed payload was not installed correctly.'
    $streams += [IO.File]::Open($variantTarget, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    Run-Wrapper (Join-Path $variant 'Install.cmd') '-NoSettings -NoMpcIntegration'
    Require ((Registered-Dll) -eq $variantTarget) 'Repeated changed-payload install did not reuse identical files.'
    Write-Output "${architecture}: changed same-version binary with the old DLL locked passed."

    # Exercise Windows PowerShell 5.1 through the actual CMD wrapper, including
    # its Start-Process trailing-space command line and process shutdown wait.
    Run-Wrapper $install '-NoSettings'
    $helperRoot = (Split-Path -Parent (Registered-Dll)) + '\'
    $helpers = @(Get-CimInstance Win32_Process -Filter "Name = 'ARE-Audio-Renderer-Settings.exe'" | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($helperRoot, [StringComparison]::OrdinalIgnoreCase) })
    Require ($helpers.Count -eq 1) 'CMD installation did not launch one helper.'
    $oldHelperId = [int]$helpers[0].ProcessId
    $helper = [Diagnostics.Process]::GetProcessById($oldHelperId)
    try { Require ($helper.MainWindowHandle -eq [IntPtr]::Zero) 'The helper argument opened a settings dialog.' }
    finally { $helper.Dispose() }
    $integrationMutex = [Threading.Mutex]::OpenExisting("Local\ARE.AudioRenderer.MPC.Integration.$architecture")
    $integrationMutex.Dispose()
    Run-Wrapper $install '-NoSettings'
    Require (!(Get-Process -Id $oldHelperId -ErrorAction SilentlyContinue)) 'Reinstall returned before the old helper process exited.'
    Run-Wrapper (Join-Path (Split-Path -Parent (Registered-Dll)) 'Uninstall.bat')
    Require (!(Registered-Dll)) 'Installed-folder uninstall left COM registration.'
    Require (!((Get-ItemProperty -LiteralPath $runKey -Name $runName -ErrorAction SilentlyContinue).$runName)) 'Uninstall left a startup entry.'
    $helpers = @(Get-CimInstance Win32_Process -Filter "Name = 'ARE-Audio-Renderer-Settings.exe'" | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($helperRoot, [StringComparison]::OrdinalIgnoreCase) })
    Require ($helpers.Count -eq 0) 'Uninstall left the hidden helper running.'
    Run-Wrapper $install '-NoSettings -NoMpcIntegration'
    Require (Test-AreSameFile (Join-Path $package 'ARE-Audio-Renderer.dll') (Registered-Dll)) 'Final reinstall failed.'
    Write-Output "${architecture}: CMD flag forwarding, hidden helper startup, real process exit and installed-folder uninstall passed."
} finally {
    foreach ($stream in $streams) { $stream.Dispose() }
    if ($env:LOCALAPPDATA -ne $originalLocalAppData) {
        Stop-AreIntegration $architecture
        & (Join-Path $package 'ARE-Audio-Renderer-Control.exe') --uninstall (Join-Path $package 'ARE-Audio-Renderer.dll') | Out-Null
    }
    $env:LOCALAPPDATA = $originalLocalAppData
    if ($previousDll) {
        & (Join-Path (Split-Path -Parent $previousDll) 'ARE-Audio-Renderer-Control.exe') --install $previousDll | Out-Null
        Require ($LASTEXITCODE -eq 0) 'Could not restore previous renderer registration.'
    }
    if ($snapshotTaken) {
        if ($previousStartup) { New-ItemProperty -LiteralPath $runKey -Name $runName -Value $previousStartup -PropertyType String -Force | Out-Null }
        else { Remove-ItemProperty -LiteralPath $runKey -Name $runName -ErrorAction SilentlyContinue }
    }
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
