param([ValidateSet('x64', 'x86', 'both')][string]$Architecture = 'both')
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $sourceRoot
try {
    $architectures = if ($Architecture -eq 'both') { @('x64', 'x86') } else { @($Architecture) }
    foreach ($arch in $architectures) {
        & cmake --preset "windows-$arch"
        if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
        & cmake --build --preset "release-$arch" --parallel
        if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
        & ctest --preset "test-$arch"
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
        & cmake --install "out/$arch" --config Release --prefix "out/package/$arch"
        if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
    }
    & python scripts/Package-Release.py --replace --architecture $Architecture
    if ($LASTEXITCODE -ne 0) { throw 'Release archive creation failed.' }
} finally { Pop-Location }
