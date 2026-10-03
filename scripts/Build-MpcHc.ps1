param(
    [Parameter(Mandatory)][string]$SourceDirectory,
    [ValidateSet('x64', 'x86', 'both')][string]$Architecture = 'both',
    [string]$NasmDirectory = '',
    [string]$WindowsSdkVersion = '',
    [string]$LocalMfcProps = '',
    [string]$PlatformToolset = 'v145'
)
$ErrorActionPreference = 'Stop'
$mpcRoot = (Resolve-Path -LiteralPath $SourceDirectory).ProviderPath
$project = Join-Path $mpcRoot 'src\mpc-hc\mpc-hc.vcxproj'
if (!(Test-Path -LiteralPath $project)) { throw 'Supply the MPC-HC 2.8.2 source directory.' }
$revision = 'a84d0cf38a1866f3518bb819c901300dacff5a9b'
if (Test-Path -LiteralPath (Join-Path $mpcRoot '.git')) {
    $head = & git -C $mpcRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $head -ne $revision) { throw "This integration targets MPC-HC 2.8.2 ($revision)." }
}
$patch = Join-Path (Split-Path -Parent $PSScriptRoot) 'integration\mpc-hc\audio-renderer-settings.patch'
# git apply also supports source archives without a Git checkout.
$previousErrorPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    & git -C $mpcRoot apply --reverse --check $patch 2>$null
    $alreadyPatched = $LASTEXITCODE -eq 0
} finally { $ErrorActionPreference = $previousErrorPreference }
if (!$alreadyPatched) {
    & git -C $mpcRoot apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw 'The player source does not match this integration patch.' }
    & git -C $mpcRoot apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'Applying the integration patch failed.' }
}
if ($NasmDirectory) { $env:PATH = (Resolve-Path -LiteralPath $NasmDirectory).ProviderPath + ';' + $env:PATH }
if (!(Get-Command nasm.exe -ErrorAction SilentlyContinue)) { throw 'Install current NASM and add it to PATH, or pass -NasmDirectory.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Install Visual Studio with Desktop development with C++, ATL and MFC.' }
$msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe'
if (!$WindowsSdkVersion) {
    $sdkInclude = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
    $WindowsSdkVersion = Get-ChildItem -LiteralPath $sdkInclude -Directory | Where-Object {
        $_.Name -match '^10\.0\.\d+\.0$' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'um\Windows.h'))
    } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
}
if (!$WindowsSdkVersion) { throw 'Install a current Windows SDK.' }
$common = @('/m:6', '/t:Build', '/p:Configuration=Release', "/p:PlatformToolset=$PlatformToolset",
    "/p:WindowsTargetPlatformVersion=$WindowsSdkVersion", "/p:MPCHC_WINSDK_VER=$WindowsSdkVersion",
    ('/p:SolutionDir=' + $mpcRoot.TrimEnd('\') + '\'), '/v:minimal', '/nologo')
if ($LocalMfcProps) { $common += '/p:ForceImportBeforeCppTargets=' + (Resolve-Path -LiteralPath $LocalMfcProps).ProviderPath }
$arches = if ($Architecture -eq 'both') { @('x64', 'x86') } else { @($Architecture) }
Push-Location -LiteralPath $mpcRoot
try {
    foreach ($arch in $arches) {
        $platform = if ($arch -eq 'x64') { 'x64' } else { 'Win32' }
        $arguments = $common + "/p:Platform=$platform"
        # libass does not declare all link-library dependencies as project references.
        # Build them first so a clean parallel build cannot race the libass link.
        foreach ($dependency in @('zlib\zlib', 'libiconv\libiconv', 'freetype2\freetype2',
                'fribidi\libfribidi', 'harfbuzz\libharfbuzz', 'libunibreak\libunibreak')) {
            & $msbuild (Join-Path $mpcRoot "src\thirdparty\$dependency.vcxproj") @arguments
            if ($LASTEXITCODE -ne 0) { throw "Building $dependency for $arch failed." }
        }
        & $msbuild $project @arguments
        if ($LASTEXITCODE -ne 0) { throw "Building MPC-HC for $arch failed." }
        $name = if ($arch -eq 'x64') { 'mpc-hc64.exe' } else { 'mpc-hc.exe' }
        $binary = Join-Path $mpcRoot "bin\mpc-hc_$arch\$name"
        Write-Host "Built $binary. Use the codecs and other runtime files from official MPC-HC 2.8.2 of the same architecture."
    }
} finally { Pop-Location }
