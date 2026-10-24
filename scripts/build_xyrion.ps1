param(
    [ValidateSet('Configure', 'Build', 'Test', 'Package', 'All')]
    [string]$Action = 'All',
    [int]$Jobs = 4,
    [ValidateSet('build-xyrion')]
    [string]$BuildDirectory = 'build-xyrion',
    [string]$ToolsDirectory = '',
    [string]$PackageDirectory = 'dist\XyrionClient',
    [ValidateSet('XyrionClient')]
    [string]$ClientExecutable = 'XyrionClient'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tools = if ($ToolsDirectory) { [System.IO.Path]::GetFullPath($ToolsDirectory) } else { Join-Path (Split-Path -Parent $root) '.build-tools' }
function Find-XyrionTool([string]$BundledPath, [string]$Command) {
    if (Test-Path -LiteralPath $BundledPath -PathType Leaf) { return $BundledPath }
    $found = Get-Command $Command -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    throw "Build dependency is missing: $Command. See docs/xyrion/BUILDING.md."
}
$gcc = Split-Path -Parent (Find-XyrionTool (Join-Path $tools 'w64devkit\bin\g++.exe') 'g++.exe')
$cmake = Find-XyrionTool (Join-Path $tools 'python-packages\cmake\data\bin\cmake.exe') 'cmake.exe'
$ninjaBundled = Join-Path $tools 'python-packages\bin\ninja.exe'
if (!(Test-Path -LiteralPath $ninjaBundled)) { $ninjaBundled = Join-Path $tools 'python-packages\ninja\data\bin\ninja.exe' }
$ninja = Find-XyrionTool $ninjaBundled 'ninja.exe'
$python = (Get-Command python.exe -ErrorAction SilentlyContinue).Source
if (!$python -or $python -match 'WindowsApps') {
    $python = Get-ChildItem "$env:LOCALAPPDATA\Programs\Python" -Filter python.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
foreach ($file in @($cmake, $ninja, $python, (Join-Path $gcc 'g++.exe'))) {
    if (!$file -or !(Test-Path -LiteralPath $file)) {
        throw "Build dependency is missing: $file. See docs/xyrion/BUILDING.md."
    }
}
$cargo = Find-XyrionTool (Join-Path $tools 'cargo\bin\cargo.exe') 'cargo.exe'
if (Test-Path -LiteralPath (Join-Path $tools 'cargo\bin\cargo.exe')) { $env:CARGO_HOME = Join-Path $tools 'cargo' }
if (Test-Path -LiteralPath (Join-Path $tools 'rustup')) { $env:RUSTUP_HOME = Join-Path $tools 'rustup' }
$env:PATH = "$gcc;$(Split-Path $cargo);$(Split-Path $ninja);$(Split-Path $cmake);$(Split-Path $python);$env:PATH"
$env:CC = (Join-Path $gcc 'gcc.exe').Replace('\', '/')
$env:CXX = (Join-Path $gcc 'g++.exe').Replace('\', '/')
# Rust's bundled GNU runtime supplies libgcc_eh, which current w64devkit
# folds into libgcc. Keep Rust's linker runtime selection explicit.
$env:RUSTFLAGS = '-C link-self-contained=yes'
$gccCmake = $gcc.Replace('\', '/')
$build = Join-Path $root $BuildDirectory
$playbackRoot = & (Join-Path $PSScriptRoot 'setup_playback.ps1') -ToolsDirectory $tools
$webviewRoot = & (Join-Path $PSScriptRoot 'setup_webview2.ps1') -ToolsDirectory $tools
$playbackRuntime = (Join-Path $playbackRoot 'bin').Replace('\', '/')

$cachePath = Join-Path $build 'CMakeCache.txt'
$executableMatches = (Test-Path -LiteralPath $cachePath) -and
    (Select-String -LiteralPath $cachePath -Pattern "^CLIENT_EXECUTABLE:STRING=$ClientExecutable$" -Quiet)
if ($Action -in @('Configure', 'All') -or !$executableMatches -or !(Select-String -LiteralPath $cachePath -Pattern '^XYRION_WEBVIEW2_DIR:PATH=.+' -Quiet) -or !(Select-String -LiteralPath $cachePath -Pattern '^STEAM:BOOL=ON$' -Quiet)) {
    & $cmake -S $root -B $build -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_PROJECT_INCLUDE=' '-DCLIENT=ON' `
        "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_C_COMPILER=$env:CC" "-DCMAKE_CXX_COMPILER=$env:CXX" `
        "-DCMAKE_RC_COMPILER=$gccCmake/windres.exe" "-DCMAKE_AR=$gccCmake/ar.exe" "-DPython3_EXECUTABLE=$python" `
        '-DPREFER_BUNDLED_LIBS=ON' '-DVIDEORECORDER=ON' '-DVULKAN=OFF' '-DDISCORD=OFF' '-DSTEAM=ON' `
        '-DAUTOUPDATE=OFF' '-DSERVER=ON' '-DTOOLS=OFF' '-DDOWNLOAD_GTEST=ON' '-DIPO=OFF' "-DXYRION_WEBVIEW2_DIR=$($webviewRoot.Replace('\', '/'))" `
        "-DCLIENT_EXECUTABLE=$ClientExecutable" "-DXYRION_PLAYBACK_RUNTIME_DIR=$playbackRuntime" '-DEXCEPTION_HANDLING=ON'
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
}
function Restore-PlaybackDlls {
    foreach ($playbackDll in @('avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll')) {
        $sourceDll = Join-Path $playbackRoot "bin\$playbackDll"
        $targetDll = Join-Path $build $playbackDll
        # An already-correct runtime may be in use by a running client.
        if (!(Test-Path -LiteralPath $targetDll) -or
            (Get-FileHash -LiteralPath $sourceDll).Hash -ne (Get-FileHash -LiteralPath $targetDll).Hash) {
            Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
        }
    }
}
function Remove-ObsoleteUiAssets([string]$dataDirectory) {
    # Exact obsolete leaves only; preserve user assets and current shaders.
    foreach ($relative in @('shader\liquid_glass.frag', 'shader\liquid_glass.vert',
        'tclient\music_bridge.ps1', 'tclient\LICENSE-liquid-glass.txt')) {
        $obsolete = Join-Path $dataDirectory $relative
        if (Test-Path -LiteralPath $obsolete -PathType Leaf) {
            Remove-Item -LiteralPath $obsolete -Force
        }
    }
}
if ($Action -in @('Build', 'Package', 'All')) {
    # CMake can copy recording-only DLLs during generation. Restore playback
    # libraries even when a following compile fails.
    $playbackRoot = & (Join-Path $PSScriptRoot 'setup_playback.ps1') -ToolsDirectory $tools
    Restore-PlaybackDlls
    try {
        & $cmake --build $build --target game-client --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw 'Client build failed' }
    } finally {
        # Regeneration happens inside --build and may replace these DLLs.
        Restore-PlaybackDlls
    }
    # Keep direct build launches consistent with packaged updates. Test
    # profiles must use a separate directory and their own storage.cfg.
    Copy-Item -LiteralPath "$root\storage.cfg" -Destination $build -Force
    Copy-Item -LiteralPath "$root\data\tclient\editor_zh.txt" -Destination "$build\data\tclient" -Force
    Copy-Item -LiteralPath "$root\data\tclient\bestclient_media.ps1" -Destination "$build\data\tclient" -Force
    foreach ($language in @('simplified_chinese.txt', 'traditional_chinese.txt')) {
        Copy-Item -LiteralPath (Join-Path "$root\data\languages" $language) -Destination "$build\data\languages" -Force
    }
    Copy-Item -LiteralPath "$root\data\shader\ui_glass.vert", "$root\data\shader\ui_glass.frag" -Destination "$build\data\shader" -Force
    Remove-ObsoleteUiAssets (Join-Path $build 'data')
    # DDNet's bundled FFmpeg only supports recording. Replace the five ABI
    # compatible runtime libraries with the pinned MP4/H.264 playback build.
    $playbackRoot = & (Join-Path $PSScriptRoot 'setup_playback.ps1') -ToolsDirectory $tools
    Restore-PlaybackDlls
}
if ($Action -in @('Test', 'All')) {
    & $cmake --build $build --target testrunner --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
    & "$build\testrunner.exe" '--gtest_filter=AiReply*:*ModSystem*:*Modsystem*:*Xyrion*:*Axiom*:ServerInfo.*:Qm*:SteamManualPath.*:TextGradientGeometry.*:RenderTargetGeometry.*:DummyMiniView*.*'
    if ($LASTEXITCODE -ne 0) { throw 'Xyrion tests failed' }
}
if ($Action -in @('Package', 'All')) {
    $package = Join-Path $root $PackageDirectory
    $packageData = Join-Path $package 'data'
    New-Item -ItemType Directory -Force -Path $packageData | Out-Null
    Copy-Item -LiteralPath "$build\$ClientExecutable.exe" -Destination (Join-Path $package 'XyrionClient.exe') -Force
    Get-ChildItem -LiteralPath $build -Filter '*.dll' -File | Copy-Item -Destination $package -Force
    Get-ChildItem -LiteralPath "$build\data" | Copy-Item -Destination $packageData -Recurse -Force
    Remove-ObsoleteUiAssets $packageData
    Copy-Item -LiteralPath "$root\storage.cfg", "$root\license.txt", "$root\LICENSE" -Destination $package -Force
    Copy-Item -LiteralPath "$root\steam_api.dll", "$root\steam_appid.txt" -Destination $package -Force
    Copy-Item -LiteralPath (Join-Path $playbackRoot 'LICENSE.txt') -Destination (Join-Path $package 'LICENSE-FFMPEG.txt') -Force
    Copy-Item -LiteralPath (Join-Path $webviewRoot 'LICENSE.txt') -Destination (Join-Path $package 'LICENSE-WebView2.txt') -Force
    Copy-Item -LiteralPath (Join-Path $webviewRoot 'NOTICE.txt') -Destination (Join-Path $package 'NOTICE-WebView2.txt') -Force
    Copy-Item -LiteralPath "$root\THIRD_PARTY.md" -Destination $package -Force
    $packageDocs = Join-Path $package 'docs'
    New-Item -ItemType Directory -Force -Path $packageDocs | Out-Null
    Get-ChildItem -LiteralPath "$root\docs\xyrion" | Copy-Item -Destination $packageDocs -Recurse -Force
    Copy-Item -LiteralPath "$root\docs\xyrion\RELEASE.md" -Destination (Join-Path $package 'README.txt') -Force
    Write-Output "Ready to run: $package\XyrionClient.exe"
}
