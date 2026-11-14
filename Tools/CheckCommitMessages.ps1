# SPDX-License-Identifier: MIT
# 公開前の変更だけを対象に、種別と日本語の説明が付いたコミットか確認する。
param(
    [Parameter(Mandatory = $true)][string]$Base,
    [string]$Head = 'HEAD',
    [string]$Repository = ''
)
$ErrorActionPreference = 'Stop'
$repositoryPath = if ($Repository) { [IO.Path]::GetFullPath($Repository) } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) } # 比較する作業コピー。
$baseSha = & git -C $repositoryPath rev-parse --verify "$Base`^{commit}" # 名前を検証済みのコミット番号へ固定する。
if ($LASTEXITCODE -ne 0) { throw 'Base commit was not found.' }
$headSha = & git -C $repositoryPath rev-parse --verify "$Head`^{commit}" # 未公開の変更の終点。
if ($LASTEXITCODE -ne 0) { throw 'Head commit was not found.' }
& git -C $repositoryPath merge-base --is-ancestor $baseSha $headSha
if ($LASTEXITCODE -ne 0) { throw 'Base is not an ancestor of Head.' }
$subjects = @(& git -C $repositoryPath log '--format=%s' "$baseSha..$headSha") # 既に公開済みの履歴は検査範囲へ含めない。
if ($LASTEXITCODE -ne 0) { throw 'Cannot read commit messages.' }
$invalid = @($subjects | Where-Object { $_ -notmatch '^(feat|fix|refactor|other): .*[\p{IsHiragana}\p{IsKatakana}\p{IsCJKUnifiedIdeographs}].*[^\s]$' }) # 開発手順で定めた種別と日本語の説明。
if ($invalid.Count -gt 0)
{
    $invalid | ForEach-Object { Write-Output "Invalid commit subject: $_" }
    exit 1
}
Write-Output "COMMIT MESSAGES PASS ($($subjects.Count) commits)"
