# SPDX-License-Identifier: MIT
# GPUデバイスと参照先の自作ヘッダーへ、UIやFrameworkの依存が混入しないか調べる。
param([string]$Root = '')
$ErrorActionPreference = 'Stop'
$rootPath = if ($Root) { [IO.Path]::GetFullPath($Root) } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) } # 調べる作業コピー。
$pending = New-Object 'System.Collections.Generic.Stack[string]' # 間接的なincludeも調べる未確認のファイル。
$visited = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase) # 循環参照と重複確認を避ける。
foreach ($extension in @('.h', '.cpp')) { $pending.Push((Join-Path $rootPath "Library/Platform/DX12/Device/DX12Device$extension")) }
$failed = $false # すべての違反をまとめて報告する。
while ($pending.Count -gt 0)
{
    $path = $pending.Pop() # このファイルの参照も同じ基準で検査する。
    if (-not $visited.Add($path)) { continue }
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing device source: $path" }
    $line = 0 # 原因の場所を示す行番号。
    foreach ($text in [IO.File]::ReadAllLines($path))
    {
        ++$line
        if ($text -match '^\s*#\s*include\s*[<"]([^>"]+)[>"]')
        {
            $include = $Matches[1].Replace('\', '/') # 相対参照でも同じ依存を拒否する。
            if ($include -match '(?i)imgui|dxlib|(^|/)Framework/')
            {
                Write-Output "Forbidden GPU dependency: ${path}:$line $include"
                $failed = $true
            }
            $candidate = if ($include.StartsWith('Library/')) { Join-Path $rootPath $include } else { Join-Path ([IO.Path]::GetDirectoryName($path)) $include } # 自作の参照先だけをたどる。
            $candidate = [IO.Path]::GetFullPath($candidate)
            if ($candidate.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { $pending.Push($candidate) }
        }
        elseif (($text -replace '//.*$', '') -match '\b(ImGui\w*|ImDraw\w*|ImTexture\w*)\b')
        {
            Write-Output "Forbidden GPU UI type: ${path}:$line $text"
            $failed = $true
        }
    }
}
if ($failed) { exit 1 }
Write-Output "DX12 DEVICE DEPENDENCIES PASS ($($visited.Count) files)"
