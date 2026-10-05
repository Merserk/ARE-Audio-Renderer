param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
if (!$OutputDirectory) { $OutputDirectory=Join-Path (Split-Path -Parent $PSScriptRoot) 'out/codec-fixtures' }
$ffmpeg=(Get-Command ffmpeg -ErrorAction Stop).Source
$ffprobe=(Get-Command ffprobe -ErrorAction Stop).Source
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$cases=[System.Collections.Generic.List[object]]::new()
function Add-Cases($prefix,$extension,$arguments,$layouts) {
    foreach ($layout in $layouts) { $cases.Add(@{Name="$prefix-$($layout.Replace('(','-').Replace(')','')).$extension"; Args=$arguments; Layout=$layout}) }
}
$basic=@('mono','stereo','5.1')
Add-Cases 'wav-pcm16' 'wav' @('-c:a','pcm_s16le') $basic
Add-Cases 'wav-pcm24' 'wav' @('-c:a','pcm_s24le') @('mono','stereo','quad','5.1','7.1')
Add-Cases 'wav-pcm32' 'wav' @('-c:a','pcm_s32le') $basic
Add-Cases 'wav-float32' 'wav' @('-c:a','pcm_f32le') $basic
Add-Cases 'wav-float64' 'wav' @('-c:a','pcm_f64le') $basic
Add-Cases 'aiff-pcm16' 'aiff' @('-c:a','pcm_s16be') $basic
Add-Cases 'aiff-pcm24' 'aiff' @('-c:a','pcm_s24be') $basic
Add-Cases 'aiff-pcm32' 'aiff' @('-c:a','pcm_s32be') $basic
Add-Cases 'alac' 'm4a' @('-c:a','alac','-sample_fmt','s32p') @('mono','stereo','5.1','7.1(wide)')
Add-Cases 'mp3' 'mp3' @('-ar','44100','-c:a','libmp3lame','-b:a','192k') @('mono','stereo')
Add-Cases 'aac-adts' 'aac' @('-c:a','aac','-b:a','384k','-f','adts') $basic
Add-Cases 'aac-mp4' 'm4a' @('-c:a','aac','-b:a','384k') @('mono','stereo','5.1','7.1')
Add-Cases 'ac3' 'mka' @('-c:a','ac3','-b:a','448k') @('mono','stereo','5.1(side)')
Add-Cases 'eac3' 'mka' @('-c:a','eac3','-b:a','640k') @('mono','stereo','5.1(side)')
Add-Cases 'truehd' 'mka' @('-c:a','truehd','-strict','-2') @('mono','stereo','5.1(side)')
Add-Cases 'dts' 'mka' @('-c:a','dca','-strict','-2','-b:a','1411200') @('mono','stereo','5.1(side)')
Add-Cases 'opus' 'opus' @('-c:a','libopus','-b:a','128k') @('mono','stereo','5.1','7.1')
Add-Cases 'vorbis' 'ogg' @('-c:a','libvorbis','-q:a','6') @('mono','stereo','5.1','7.1')
Add-Cases 'flac-44100' 'flac' @('-ar','44100','-c:a','flac','-sample_fmt','s32') @('mono','stereo','5.1','7.1')
Add-Cases 'wavpack' 'wv' @('-c:a','wavpack') @('mono','stereo','5.1','7.1')
Add-Cases 'wma-44100' 'wma' @('-ar','44100','-c:a','wmav2','-b:a','128k') @('mono','stereo')
$manifest=[System.Collections.Generic.List[object]]::new()
$frequencies=@(997,431,601,73,811,1201,1423,1601)
foreach ($case in $cases) {
    $channels=switch -Regex ($case.Layout) { '^mono$' {1} '^stereo$' {2} '^quad$' {4} '^5\.1' {6} '^7\.1' {8} default {throw 'Unknown fixture layout'} }
    $signals=for ($channel=0; $channel -lt $channels; $channel++) { '0.04*sin(2*PI*'+$frequencies[$channel]+'*t)' }
    $tone='aevalsrc='+($signals -join '|')+':s=48000:d=1.25:c='+$case.Layout
    $destination=Join-Path $OutputDirectory $case.Name
    & $ffmpeg -hide_banner -loglevel error -y -f lavfi -i $tone @($case.Args) $destination
    if ($LASTEXITCODE -ne 0) { throw ('Fixture encoding failed: '+$case.Name) }
    $probe=& $ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,sample_rate,channels,channel_layout -of json $destination
    if ($LASTEXITCODE -ne 0) { throw ('Fixture inspection failed: '+$case.Name) }
    $stream=($probe | ConvertFrom-Json).streams[0]
    if ($stream.channels -ne $channels) { throw ('Fixture lost channels: '+$case.Name) }
    $manifest.Add(@{file=$case.Name; codec=$stream.codec_name; rate=$stream.sample_rate; channels=$channels; layout=$stream.channel_layout; seconds=1.25})
    Write-Output ('Created '+$case.Name)
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding utf8
Write-Output ('Created '+$manifest.Count+' channel/codec fixtures.')
