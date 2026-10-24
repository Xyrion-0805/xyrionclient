param([int]$ParentId = 0, [switch]$Once)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$managerType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType=WindowsRuntime]
$propertiesType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties, Windows.Media.Control, ContentType=WindowsRuntime]
$streamType = [Windows.Storage.Streams.IRandomAccessStreamWithContentType, Windows.Storage.Streams, ContentType=WindowsRuntime]
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1
$asReadStream = [System.IO.WindowsRuntimeStreamExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsStreamForRead' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IInputStream'
} | Select-Object -First 1
function Await-Media($operation, $resultType) {
    $task = $asTask.MakeGenericMethod($resultType).Invoke($null, @($operation))
    if (!$task.Wait(1500)) { throw 'Media request timed out' }
    return $task.Result
}
# Standard input is handled by a background .NET reader, so polling never blocks.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using System.Threading;
public static class MediaCommands {
    static readonly ConcurrentQueue<string> Queue = new ConcurrentQueue<string>();
    public static void Start() {
        var reader = new Thread(() => { string line; while ((line = Console.ReadLine()) != null) Queue.Enqueue(line); });
        reader.IsBackground = true; reader.Start();
    }
    public static string Next() { string value; return Queue.TryDequeue(out value) ? value : null; }
}
'@
if (!$Once) { [MediaCommands]::Start() }
$manager = $null
$lastTrack = ''
$cover = ''
$nextCoverRead = [DateTime]::MinValue
do {
    $state = @{title=''; artist=''; album=''; app=''; position=0L; duration=0L; playing=$false; previous=$false; next=$false; playpause=$false; cover=''}
    try {
        if (!$manager) { $manager = Await-Media ($managerType::RequestAsync()) $managerType }
        $session = $manager.GetSessions() | Where-Object { $_.SourceAppUserModelId -match '(?i)qqmusic' } | Select-Object -First 1
        if (!$session) { $session = $manager.GetCurrentSession() }
        if ($session) {
            while ($command = [MediaCommands]::Next()) {
                switch ($command) {
                    'previous' { $null = Await-Media ($session.TrySkipPreviousAsync()) ([bool]) }
                    'next' { $null = Await-Media ($session.TrySkipNextAsync()) ([bool]) }
                    'playpause' { $null = Await-Media ($session.TryTogglePlayPauseAsync()) ([bool]) }
                }
            }
            $media = Await-Media ($session.TryGetMediaPropertiesAsync()) $propertiesType
            $timeline = $session.GetTimelineProperties()
            $playback = $session.GetPlaybackInfo()
            $state.title = $media.Title; $state.artist = $media.Artist; $state.album = $media.AlbumTitle
            $state.app = $session.SourceAppUserModelId
            $state.playing = $playback.PlaybackStatus.ToString() -eq 'Playing'
            $state.previous = $playback.Controls.IsPreviousEnabled
            $state.next = $playback.Controls.IsNextEnabled
            $state.playpause = $playback.Controls.IsPlayPauseToggleEnabled
            $state.position = [long]$timeline.Position.TotalMilliseconds
            $state.duration = [long]($timeline.EndTime - $timeline.StartTime).TotalMilliseconds
            if ($state.playing) {
                $elapsed = ([DateTimeOffset]::Now - $timeline.LastUpdatedTime).TotalMilliseconds
                if ($elapsed -ge 0 -and $elapsed -lt 86400000) { $state.position += [long]$elapsed }
            }
            if ($state.duration -gt 0) { $state.position = [Math]::Min($state.duration, $state.position) }
            $track = $state.app + '|' + $state.title + '|' + $state.artist + '|' + $state.album
            if ($track -ne $lastTrack) {
                $cover = ''; $lastTrack = $track
                $nextCoverRead = [DateTime]::MinValue
            }
            # SMTC may publish the title before the thumbnail. Retry missing
            # artwork, and refresh successful reads when a player updates it.
            if ([DateTime]::UtcNow -ge $nextCoverRead) {
                $nextCoverRead = [DateTime]::UtcNow.AddSeconds(2)
                try {
                    if ($media.Thumbnail) {
                        $randomStream = Await-Media ($media.Thumbnail.OpenReadAsync()) $streamType
                        # WinRT interface results can be raw __ComObject values
                        # in Windows PowerShell: .Size and .Dispose are absent.
                        # The managed adapter queries the interface correctly.
                        $stream = $asReadStream.Invoke($null, @($randomStream))
                        $memory = [System.IO.MemoryStream]::new()
                        try {
                            $buffer = New-Object byte[] 65536
                            while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                                if ($memory.Length + $read -gt 8388608) { throw 'Media thumbnail is too large' }
                                $memory.Write($buffer, 0, $read)
                            }
                            if ($memory.Length -gt 0) {
                                $cover = [Convert]::ToBase64String($memory.ToArray())
                                $nextCoverRead = [DateTime]::UtcNow.AddSeconds(10)
                            }
                        } finally { $memory.Dispose(); $stream.Dispose() }
                    }
                } catch { }
            }
            $state.cover = $cover
        }
    } catch { $manager = $null }
    [Console]::WriteLine(($state | ConvertTo-Json -Compress))
    if ($Once) { break }
    Start-Sleep -Milliseconds 250
} while (!$ParentId -or (Get-Process -Id $ParentId -ErrorAction SilentlyContinue))
