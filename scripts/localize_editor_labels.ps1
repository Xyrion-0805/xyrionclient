# Mechanical migration of literal display labels into the editor translation layer.
# Never translate filenames, identifiers, console commands, or serialized map data.
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$files = Get-ChildItem -LiteralPath (Join-Path $taskRoot 'src/game/editor') -Filter '*.cpp' -Recurse
foreach ($file in $files) {
    $original = [System.IO.File]::ReadAllText($file.FullName)
    $changed = [regex]::Replace($original, '(Ui\(\)->DoLabel\(\s*[^,\r\n]+,\s*)("(?:[^"\\]|\\.)*")', '$1EditorLocalize($2)')
    if ($changed -ne $original) {
        if ($changed -notmatch '#include ["<]game/editor/localization.h[">]' -and $changed -notmatch '#include "localization.h"') {
            $changed = "#include <game/editor/localization.h>`n" + $changed
        }
        [System.IO.File]::WriteAllText($file.FullName, $changed, [System.Text.UTF8Encoding]::new($false))
        Write-Output $file.Name
    }
}
