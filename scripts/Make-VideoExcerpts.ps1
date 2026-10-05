param(
    [Parameter(Mandatory=$true)][string]$VideoDirectory,
    [string]$OutputDirectory,
    [double]$StartSeconds=60,
    [double]$DurationSeconds=8
)
$ErrorActionPreference='Stop'
if ($StartSeconds -lt 0 -or $DurationSeconds -le 0) { throw 'Invalid excerpt range.' }
if (!$OutputDirectory) { $OutputDirectory=Join-Path (Split-Path -Parent $PSScriptRoot) 'out/video-excerpts' }
$ffmpeg=(Get-Command ffmpeg -ErrorAction Stop).Source
$ffprobe=(Get-Command ffprobe -ErrorAction Stop).Source
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$manifest=[System.Collections.Generic.List[object]]::new()
foreach ($video in Get-ChildItem -LiteralPath $VideoDirectory -File) {
    $probe=& $ffprobe -v error -select_streams a -show_entries stream=index,codec_name,profile,sample_rate,channels,channel_layout -of json $video.FullName
    if ($LASTEXITCODE -ne 0) { throw ('Video inspection failed: '+$video.Name) }
    $streams=($probe | ConvertFrom-Json).streams
    for ($track=0; $track -lt $streams.Count; $track++) {
        $name=$video.BaseName+'-track'+$track+'.mka'
        $destination=Join-Path $OutputDirectory $name
        # Output-side seeking discards pre-seek packets while preserving the
        # compressed stream. Input-side copy seeking may include an earlier
        # video keyframe and silently produce a longer audio-only excerpt.
        & $ffmpeg -hide_banner -loglevel error -y -i $video.FullName -ss $StartSeconds -t $DurationSeconds -map "0:a:$track" -c:a copy -map_metadata -1 -map_chapters -1 $destination
        if ($LASTEXITCODE -ne 0) { throw ('Video excerpt failed: '+$name) }
        $excerptProbe=& $ffprobe -v error -show_entries format=duration:stream=codec_name,channels -of json $destination
        if ($LASTEXITCODE -ne 0) { throw ('Excerpt inspection failed: '+$name) }
        $excerpt=$excerptProbe | ConvertFrom-Json
        if ($excerpt.streams[0].channels -ne $streams[$track].channels -or $excerpt.streams[0].codec_name -ne $streams[$track].codec_name -or [Math]::Abs([double]$excerpt.format.duration-$DurationSeconds) -gt 0.1) { throw ('Excerpt changed codec/layout or has the wrong duration: '+$name) }
        $manifest.Add(@{file=$name; source=$video.Name; track=$track; codec=$streams[$track].codec_name; profile=$streams[$track].profile; channels=$streams[$track].channels; layout=$streams[$track].channel_layout; rate=$streams[$track].sample_rate; start_seconds=$StartSeconds; duration_seconds=[double]$excerpt.format.duration})
        Write-Output ('Created '+$name+': '+$excerpt.format.duration+' seconds, '+$streams[$track].codec_name+', '+$streams[$track].channels+' channels')
    }
}
if (!$manifest.Count) { throw 'No audio tracks found.' }
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding utf8
