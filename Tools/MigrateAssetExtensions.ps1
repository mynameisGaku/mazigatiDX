# 古い資産の拡張子（.effect.json / .sheet.json / .modelanim.json）を、新しい拡張子（.gxeffect / .gxsprite / .gxmodelanim）へ移す。
#   1. Root の下の、古い拡張子のファイルの名前を、新しい拡張子に替える（同じ名前のファイルがあるときは、何もせず知らせる）
#   2. Root の下の文字のファイル（資産・ワールド・文書など）の中の、古い名前の参照を、新しい名前へ書き換える
# 中身（JSON の形式）は変わらない。文字コードと改行は、書き換えたファイルでも元のまま。x64 / .git / .vs / VendorModules の下は見ない。
#
#   .\Tools\MigrateAssetExtensions.ps1 -Root C:\MyGame\Assets            # 移す
#   .\Tools\MigrateAssetExtensions.ps1 -Root C:\MyGame\Assets -DryRun    # 何が起きるかだけ表示する（変えない）
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

# 古い拡張子 -> 新しい拡張子
$map = [ordered]@{
    '.effect.json'    = '.gxeffect'
    '.sheet.json'     = '.gxsprite'
    '.modelanim.json' = '.gxmodelanim'
}
# 中の参照を書き換える文字のファイルの拡張子（新しい拡張子のファイルも、JSON なので入れる）
$textExtensions = @('.gxeffect', '.gxsprite', '.gxmodelanim', '.world', '.json', '.md', '.txt')
$skipFolders = @('\x64\', '\.git\', '\.vs\', '\VendorModules\')

if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "Root was not found: $Root" }
$rootFull = (Resolve-Path -LiteralPath $Root).Path

function Test-Skipped([string]$Path) {
    foreach ($folder in $skipFolders) { if ($Path.Contains($folder)) { return $true } }
    return $false
}

# 1. 名前を替える
$renamed = 0
$conflicts = 0
foreach ($file in (Get-ChildItem -LiteralPath $rootFull -Recurse -File)) {
    if (Test-Skipped $file.FullName) { continue }
    foreach ($old in $map.Keys) {
        if ($file.Name.EndsWith($old, [StringComparison]::OrdinalIgnoreCase)) {
            $newName = $file.Name.Substring(0, $file.Name.Length - $old.Length) + $map[$old]
            $target = Join-Path $file.DirectoryName $newName
            if (Test-Path -LiteralPath $target) {
                Write-Host "CONFLICT (kept as is): $($file.FullName) -> $newName already exists"
                $conflicts++
            } else {
                Write-Host "rename: $($file.FullName) -> $newName"
                if (-not $DryRun) { Move-Item -LiteralPath $file.FullName -Destination $target }
                $renamed++
            }
            break
        }
    }
}

# 2. 中の参照を書き換える（BOM と改行は、そのまま）
$rewritten = 0
$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($file in (Get-ChildItem -LiteralPath $rootFull -Recurse -File)) {
    if (Test-Skipped $file.FullName) { continue }
    $extension = $file.Extension.ToLowerInvariant()
    if (($textExtensions -notcontains $extension) -and -not ($map.Values -contains $extension)) { continue }
    # DryRun では、名前を替えていないので、古い名前のままのファイルも読む
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $text = $utf8.GetString($bytes, $(if ($hasBom) { 3 } else { 0 }), $bytes.Length - $(if ($hasBom) { 3 } else { 0 }))
    $changedText = $text
    foreach ($old in $map.Keys) {
        $changedText = [regex]::Replace($changedText, [regex]::Escape($old), $map[$old], 'IgnoreCase')
    }
    if ($changedText -ne $text) {
        Write-Host "update references: $($file.FullName)"
        if (-not $DryRun) {
            $body = $utf8.GetBytes($changedText)
            $out = if ($hasBom) { [byte[]](0xEF, 0xBB, 0xBF) + $body } else { $body }
            [IO.File]::WriteAllBytes($file.FullName, $out)
        }
        $rewritten++
    }
}

$mode = if ($DryRun) { ' (dry run: nothing was changed)' } else { '' }
Write-Host "done: $renamed file(s) renamed, $rewritten file(s) rewritten, $conflicts conflict(s)$mode"
if ($conflicts -gt 0) { exit 1 }
