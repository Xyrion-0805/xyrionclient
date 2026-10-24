param([string]$ToolsDirectory = '')

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskTools = if ($ToolsDirectory) { [System.IO.Path]::GetFullPath($ToolsDirectory) } else { Join-Path (Split-Path -Parent $taskRoot) '.build-tools' }
$archive = Join-Path $taskTools 'ffmpeg-playback-7.1.zip'
$packageName = 'ffmpeg-n7.1.1-56-gc2184b65d2-win64-gpl-shared-7.1'
$playbackRoot = Join-Path (Join-Path $taskTools 'ffmpeg-playback-7.1') $packageName
$url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2025-07-31-14-15/$packageName.zip"
$expectedHash = '12B52671D0C992EF4AB82FF40CBEF0FB8DB728BAEB0BCA567B2012A9D344B2D7'
New-Item -ItemType Directory -Force -Path $taskTools | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'FFmpeg playback archive SHA256 mismatch. Replace the archive with the pinned release.'
}
if (!(Test-Path -LiteralPath (Join-Path $playbackRoot 'bin\avcodec-61.dll'))) {
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $taskTools 'ffmpeg-playback-7.1') -Force
}
Write-Output $playbackRoot
