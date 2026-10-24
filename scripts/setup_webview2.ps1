param([string]$ToolsDirectory = '')

$ErrorActionPreference = 'Stop'
$sdkVersion = '1.0.2903.40'
$toolsRoot = if ($ToolsDirectory) { [System.IO.Path]::GetFullPath($ToolsDirectory) } else { Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) '.build-tools' }
$sdkRoot = Join-Path $toolsRoot "webview2-$sdkVersion"
New-Item -ItemType Directory -Force -Path $toolsRoot | Out-Null
if (!(Test-Path -LiteralPath (Join-Path $sdkRoot 'build/native/include/WebView2.h'))) {
    $archive = Join-Path $toolsRoot "webview2-$sdkVersion.zip"
    if (!(Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$sdkVersion" -OutFile $archive
    }
    $expectedHash = 'EF128016DD1E51C59178C827ED5B8AA3322C57AFA8675D930F8109505542AD74'
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
        throw 'WebView2 SDK SHA256 mismatch. Replace the archive with the pinned NuGet package.'
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $sdkRoot -Force
}
Write-Output $sdkRoot
