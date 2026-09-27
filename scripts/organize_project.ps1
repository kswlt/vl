param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
function Inside([string]$relative) {
    $path = [IO.Path]::GetFullPath((Join-Path $root $relative))
    if (-not $path.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Outside workspace: $path"
    }
    if ((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing reparse point: $path"
    }
    return $path
}
$zipPath = Inside 'archive/historical-root.zip'
if (Test-Path -LiteralPath $zipPath) { throw 'One-shot migration already run; review the manifest instead.' }
$keep = @('start_gateway.ps1','setup_pc.ps1','requirements.txt','requirements-dev.txt')
$moves = [ordered]@{
    'xt_camera.cpp'='board/src/xt_camera.cpp'; 'xt_radar.cpp'='board/src/xt_radar.cpp'
    'xt_camera'='board/bin/xt_camera'; 'xt_radar'='board/bin/xt_radar'
    'S98xtnet'='board/init/S98xtnet'; 'S99xtcamera'='board/init/S99xtcamera'; 'S99xtradar'='board/init/S99xtradar'
    'zenoh-pico-main'='vendor/zenoh-pico'; 'xtsdk_cpp-main'='vendor/xtsdk'
    'xtsdk_cpp_mirror'='archive/vendor-sdk-mirror'; 'dlwork'='archive/camera-investigation'
}
Get-ChildItem -LiteralPath $root -File -Filter '*.pdf' | ForEach-Object { $moves[$_.Name] = 'docs/reference/' + $_.Name }
$files = @(Get-ChildItem -LiteralPath $root -File | Where-Object { $_.Name -notin $keep -and -not $moves.Contains($_.Name) })
$generatedDirs = @('FFmpeg-n6.1.1','build-win','build-win-dbg','frames')
$manifest = [ordered]@{date='2026-08-31'; archived_files=@(); moves=$moves; removed_generated_directories=$generatedDirs}
foreach ($file in $files) {
    $manifest.archived_files += [ordered]@{path=$file.Name; bytes=$file.Length; sha256=(Get-FileHash -LiteralPath $file.FullName).Hash}
}
"Archive $($files.Count) root files; relocate $($moves.Count) paths; remove only listed generated/obsolete directories."
if (-not $Apply) { $manifest | ConvertTo-Json -Depth 6; exit 0 }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
New-Item -ItemType Directory -Path (Inside 'archive') -Force | Out-Null
$zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $files) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $file.Name) | Out-Null
    }
} finally { $zip.Dispose() }
# Verify contents byte-for-byte before deleting any original file.
$zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    foreach ($entry in $manifest.archived_files) {
        $stream = $zip.GetEntry($entry.path).Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($hash -ne $entry.sha256) { throw "Archive verification failed: $($entry.path)" }
    }
} finally { $zip.Dispose() }
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Inside 'archive/cleanup-manifest-20260831.json') -Encoding utf8
foreach ($entry in $manifest.archived_files) {
    $path = Inside $entry.path
    if ((Get-FileHash -LiteralPath $path).Hash -ne $entry.sha256) { throw "File changed since archive: $path" }
    Remove-Item -LiteralPath $path
}
foreach ($entry in $moves.GetEnumerator()) {
    $src = Inside $entry.Key
    $dst = Inside $entry.Value
    if (Test-Path -LiteralPath $dst) { throw "Destination exists: $dst" }
    if ((Get-Item -LiteralPath $src).PSIsContainer) {
        if (Get-ChildItem -LiteralPath $src -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
            throw "Reparse point within directory: $src"
        }
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $dst) -Force | Out-Null
    Move-Item -LiteralPath $src -Destination $dst
}
foreach ($relative in $generatedDirs) {
    $target = Inside $relative
    if (-not (Test-Path -LiteralPath $target)) { continue }
    if (Get-ChildItem -LiteralPath $target -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
        throw "Reparse point within directory: $target"
    }
    # FFmpeg source is obsolete for native snapshot; its original tarball is in the verified archive.
    Remove-Item -LiteralPath $target -Recurse -Force
}
"Completed. Verified archive: $zipPath"
