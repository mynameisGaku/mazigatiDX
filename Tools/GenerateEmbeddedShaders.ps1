# SPDX-License-Identifier: MIT
# EngineContentのシェーダーをビルド用の文字列へ変換する。変更がない場合は出力を書き換えない。
param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$OutputHeader
)
$ErrorActionPreference = 'Stop'
$sourceDirectory = [IO.Path]::GetFullPath($SourceRoot) # 正本のシェーダーの場所。
$outputPath = [IO.Path]::GetFullPath($OutputHeader) # 構成ごとの中間ファイル。
if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) { throw "Shader directory is missing: $sourceDirectory" }
$files = @(Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File | Where-Object { $_.Extension -in @('.hlsl', '.hlsli') } | Sort-Object FullName)
if ($files.Count -eq 0) { throw "No shader sources found: $sourceDirectory" }
$symbols = @{} # 同じC++識別子への衝突を拒否する。
$text = New-Object Text.StringBuilder
[void]$text.AppendLine('// SPDX-License-Identifier: MIT')
[void]$text.AppendLine('#pragma once')
[void]$text.AppendLine('// Generated from EngineContent/Shaders. Edit the shader files, not this header.')
[void]$text.AppendLine('namespace NEngineShaderSources {')
foreach ($file in $files) {
    $relative = $file.FullName.Substring($sourceDirectory.TrimEnd('\', '/').Length + 1) # フォルダーを含む一意な名前。
    $name = ($relative -replace '\.[^\\/.]+$', '') -replace '[^A-Za-z0-9_]', '_'
    if ($symbols.ContainsKey($name)) { throw "Duplicate shader symbol: $relative and $($symbols[$name])" }
    $symbols[$name] = $relative
    $source = [IO.File]::ReadAllText($file.FullName, [Text.Encoding]::UTF8) # HLSLはUTF-8で読む。
    if ($source.IndexOf([char]0) -ge 0 -or $source.Contains(')MG_SHADER_SRC"')) { throw "Unsupported shader text: $relative" }
    [void]$text.AppendLine("inline constexpr char $name`[] = R`"MG_SHADER_SRC(")
    [void]$text.Append($source)
    [void]$text.AppendLine(')MG_SHADER_SRC";')
}
[void]$text.AppendLine('}')
$result = $text.ToString() # 出力が同じならコンパイルのやり直しを発生させない。
if ((Test-Path -LiteralPath $outputPath) -and [IO.File]::ReadAllText($outputPath, [Text.Encoding]::UTF8) -ceq $result) { exit 0 }
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath))
[IO.File]::WriteAllText($outputPath, $result, (New-Object Text.UTF8Encoding($true)))
Write-Host "Embedded $($files.Count) shader sources: $outputPath"
