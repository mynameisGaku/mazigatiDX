# Clean.ps1 - deletes temporary files and (if asked) build outputs. Normally started through Clean.bat.
#
#   -Level temp         temporary files: logs, crash reports, profiler traces, the console history, Temp, __pycache__,
#                       and the temporary folders that the self tests leave in the OS temp folder (AssetSelfTest_*)   (default)
#   -Level build        build outputs: x64, project, mazigatiDX, Samples\x64, Editor\x64 and .vs (close Visual Studio first)
#   -Level all          both
#   -Screenshots        also Saved\Screenshots (pictures that were taken with F9 and so on)
#   -SaveGames          also Saved\SaveGames (the save data of the game: it cannot be restored!)
#   -DryRun             only lists what would be deleted
#   -Yes                does not ask before deleting
#
# What is never touched: source files, Assets, Config, imgui.ini (the window layout), Source\SelfTest (a junction to another repository).
# Folders are removed with "rd /s /q", which removes a junction itself and does not go into its target.
# ASCII only (PowerShell 5.1 reads a file without a BOM as the system code page).
param(
    [ValidateSet('temp', 'build', 'all')][string]$Level = 'temp',
    [switch]$Screenshots,
    [switch]$SaveGames,
    [switch]$DryRun,
    [switch]$Yes,
    [string]$Root = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Root) { $Root = Split-Path -Parent $PSScriptRoot }
$Root = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $Root 'Library')) -or -not (Test-Path -LiteralPath (Join-Path $Root 'project.sln'))) {
    Write-Host "This does not look like the framework folder (no Library folder or project.sln): $Root"
    exit 2
}

# The folders that hold the files the apps write at run time (the framework folder itself, and the sample / editor apps)
$appFolders = @('', 'Samples', 'Editor')
$targets = New-Object System.Collections.Generic.List[object]
function Add-Target([string]$Path, [string]$Kind, [string]$Why) {
    $targets.Add([pscustomobject]@{ Path = $Path; Kind = $Kind; Why = $Why })
}

if ($Level -eq 'temp' -or $Level -eq 'all') {
    foreach ($app in $appFolders) {
        $saved = if ($app) { Join-Path $app 'Saved' } else { 'Saved' }
        Add-Target (Join-Path $saved 'Logs') 'dir' 'logs'
        Add-Target (Join-Path $saved 'Crashes') 'dir' 'crash reports and dumps'
        Add-Target (Join-Path $saved 'Profiler') 'dir' 'profiler traces'
        Add-Target (Join-Path $saved 'ConsoleHistory.txt') 'file' 'console history'
    }
    Add-Target 'Temp' 'dir' 'Temp folder'
    foreach ($tool in @('Tools', 'Docs')) {
        $folder = Join-Path $Root $tool
        if (Test-Path -LiteralPath $folder) {
            Get-ChildItem -LiteralPath $folder -Recurse -Directory -Filter '__pycache__' -Force -ErrorAction SilentlyContinue | ForEach-Object {
                Add-Target ($_.FullName.Substring($Root.Length + 1)) 'dir' 'python cache'
            }
        }
    }
}
if ($Screenshots) {
    foreach ($app in $appFolders) {
        $saved = if ($app) { Join-Path $app 'Saved' } else { 'Saved' }
        Add-Target (Join-Path $saved 'Screenshots') 'dir' 'screenshots'
    }
}
if ($SaveGames) {
    foreach ($app in $appFolders) {
        $saved = if ($app) { Join-Path $app 'Saved' } else { 'Saved' }
        Add-Target (Join-Path $saved 'SaveGames') 'dir' 'SAVE DATA'
    }
}
if ($Level -eq 'build' -or $Level -eq 'all') {
    foreach ($folder in @('x64', 'project', 'mazigatiDX', 'Samples\x64', 'Editor\x64', '.vs')) {
        Add-Target $folder 'dir' 'build output / Visual Studio cache'
    }
}

function Get-TreeSize([string]$Path) {
    $sum = 0
    try {
        Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object { $sum += $_.Length }
    } catch { }
    return $sum
}
function Format-Size([double]$Bytes) {
    if ($Bytes -ge 1GB) { return ('{0:N2} GB' -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ('{0:N1} MB' -f ($Bytes / 1MB)) }
    return ('{0:N0} KB' -f ($Bytes / 1KB))
}

# What exists now (the absolute paths are kept inside the folders that are under the root, or in the OS temp folder)
$found = New-Object System.Collections.Generic.List[object]
foreach ($target in $targets) {
    $full = if ([IO.Path]::IsPathRooted($target.Path)) { $target.Path } else { Join-Path $Root $target.Path }
    if (-not (Test-Path -LiteralPath $full)) { continue }
    $item = Get-Item -LiteralPath $full -Force
    $isLink = ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    $size = if ($target.Kind -eq 'file') { $item.Length } elseif ($isLink) { 0 } else { Get-TreeSize $full }
    $found.Add([pscustomobject]@{ Full = $full; Display = $target.Path; Kind = $target.Kind; Why = $target.Why; Size = $size; IsLink = $isLink })
}
# The temporary folders of the self tests (in the OS temp folder)
if ($Level -eq 'temp' -or $Level -eq 'all') {
    Get-ChildItem -LiteralPath $env:TEMP -Directory -Filter 'AssetSelfTest_*' -Force -ErrorAction SilentlyContinue | ForEach-Object {
        $found.Add([pscustomobject]@{ Full = $_.FullName; Display = $_.FullName; Kind = 'dir'; Why = 'self test temporary folder'; Size = (Get-TreeSize $_.FullName); IsLink = $false })
    }
}

Write-Host "Root: $Root"
Write-Host "Level: $Level$(if ($Screenshots) { ' + screenshots' })$(if ($SaveGames) { ' + SAVE GAMES' })$(if ($DryRun) { '  (dry run: nothing is deleted)' })"
if ($found.Count -eq 0) {
    Write-Host 'Nothing to delete.'
    exit 0
}
$total = 0
foreach ($entry in $found) {
    $total += $entry.Size
    Write-Host ('  {0,10}  {1}  ({2}{3})' -f (Format-Size $entry.Size), $entry.Display, $entry.Why, $(if ($entry.IsLink) { ', a junction: only the link is removed' } else { '' }))
}
Write-Host ('Total: {0} in {1} place(s)' -f (Format-Size $total), $found.Count)
if ($DryRun) { exit 0 }

if (-not $Yes) {
    $answer = Read-Host 'Delete these? (y/N)'
    if ($answer -notmatch '^(y|yes)$') {
        Write-Host 'Canceled.'
        exit 1
    }
}

$failed = 0
$deleted = 0
foreach ($entry in $found) {
    try {
        if ($entry.Kind -eq 'file') {
            Remove-Item -LiteralPath $entry.Full -Force
        } else {
            # rd removes a junction itself (it does not go into the target), and a normal folder with everything inside
            & cmd.exe /c "rd /s /q `"$($entry.Full)`"" 2>$null
            if (Test-Path -LiteralPath $entry.Full) { throw 'some files are still there (in use?)' }
        }
        $deleted += $entry.Size
    } catch {
        $failed++
        Write-Host "  could not delete: $($entry.Display) ($($_.Exception.Message))"
    }
}
Write-Host ('Deleted {0}.{1}' -f (Format-Size $deleted), $(if ($failed -gt 0) { " $failed place(s) could not be deleted (close the program that uses them)." } else { '' }))
if ($failed -gt 0) { exit 1 }
exit 0
