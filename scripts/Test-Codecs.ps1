param(
    [ValidateSet('x64','x86','both')][string]$Architecture='both',
    [Parameter(Mandatory=$true)][string]$LavDirectory,
    [string]$FixturesDirectory,
    [string]$VideoExcerptsDirectory,
    [string]$ResultsDirectory,
    [string]$BuildDirectory,
    [switch]$IncludeSinc,
    [switch]$SkipCodecs
)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
if (!$FixturesDirectory) { $FixturesDirectory=Join-Path $sourceRoot 'out/codec-fixtures' }
if (!$ResultsDirectory) {
    $releaseVersion=[regex]::Match((Get-Content -LiteralPath (Join-Path $sourceRoot 'CMakeLists.txt') -Raw),'project\(ASIORenderEngine VERSION (\d+\.\d+\.\d+)').Groups[1].Value
    if (!$releaseVersion) { throw 'Cannot determine the renderer version.' }
    $ResultsDirectory=Join-Path $sourceRoot ("tests/results/$releaseVersion")
}
if (!$BuildDirectory) { $BuildDirectory=Join-Path $sourceRoot 'out' }
$BuildDirectory=(Resolve-Path -LiteralPath $BuildDirectory).Path
$LavDirectory=(Resolve-Path -LiteralPath $LavDirectory).Path
$FixturesDirectory=(Resolve-Path -LiteralPath $FixturesDirectory).Path
if ($VideoExcerptsDirectory) { $VideoExcerptsDirectory=(Resolve-Path -LiteralPath $VideoExcerptsDirectory).Path }
New-Item -ItemType Directory -Force -Path $ResultsDirectory | Out-Null
$architectures=if ($Architecture -eq 'both') {@('x64','x86')} else {@($Architecture)}
foreach ($arch in $architectures) {
    $binary=Join-Path $BuildDirectory "$arch/Release"
    $lav=Join-Path $LavDirectory $arch
    $inputs=@()
    if (!$SkipCodecs) { $inputs+=@{Path=$FixturesDirectory; Kind='synthetic'; Name='codecs'} }
    if ($VideoExcerptsDirectory) { $inputs+=@{Path=$VideoExcerptsDirectory; Kind='real-content'; Name='videos'} }
    foreach ($inputSet in $inputs) {
        foreach ($outputs in @(8,2,1)) {
            $log=Join-Path $ResultsDirectory "$($inputSet.Name)-$arch-$outputs-outputs.txt"
            & (Join-Path $binary 'codec_smoke_tests.exe') (Join-Path $binary 'ARE-Audio-Renderer.dll') (Join-Path $binary 'fake_asio_driver.dll') (Join-Path $lav 'LAVSplitter.ax') (Join-Path $lav 'LAVAudio.ax') $inputSet.Path $outputs 48000 $inputSet.Kind r8brain | Tee-Object -FilePath $log
            $testExit=$LASTEXITCODE
            $logText=Get-Content -LiteralPath $log -Raw
            [IO.File]::WriteAllText($log,$logText,[Text.UTF8Encoding]::new($false))
            if ($testExit -ne 0) { throw "Codec validation failed: $log" }
        }
        if ($IncludeSinc) {
            $log=Join-Path $ResultsDirectory "$($inputSet.Name)-$arch-2-outputs-sinc.txt"
            & (Join-Path $binary 'codec_smoke_tests.exe') (Join-Path $binary 'ARE-Audio-Renderer.dll') (Join-Path $binary 'fake_asio_driver.dll') (Join-Path $lav 'LAVSplitter.ax') (Join-Path $lav 'LAVAudio.ax') $inputSet.Path 2 44100 $inputSet.Kind sinc | Tee-Object -FilePath $log
            $testExit=$LASTEXITCODE
            $logText=Get-Content -LiteralPath $log -Raw
            [IO.File]::WriteAllText($log,$logText,[Text.UTF8Encoding]::new($false))
            if ($testExit -ne 0) { throw "Sinc codec validation failed: $log" }
        }
    }
}
