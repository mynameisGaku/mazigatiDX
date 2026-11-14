# SPDX-License-Identifier: MIT
# 公開窓口・描画命令・見本・Editor入口の互換表を更新する。-Checkではファイルを変更しない。
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) # 対象プロジェクト。
$catalogPath = Join-Path $projectRoot 'Docs/BackendCompatibility.csv' # 手で更新する判定と検査計画。
$reportPath = Join-Path $projectRoot 'Docs/BACKEND_COMPATIBILITY.md' # 再生成できる一覧。
$symbols = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal) # ソースから集める一意な項目。

# ソースの場所と行を保って登録する。重複は、同名のオーバーロード等の扱いを決めるまで失敗にする。
function Add-Symbol([string]$Id, [string]$Kind, [string]$Source, [int]$Line, [string]$Signature)
{
    if ($symbols.ContainsKey($Id))
    {
        throw "Duplicate compatibility id: $Id. Define separate ids for overloads before regenerating."
    }
    $symbols.Add($Id, [pscustomobject]@{ Id = $Id; Kind = $Kind; Source = $Source; Line = $Line; Signature = $Signature })
}

# コメントと文字列を空白にし、行数と位置は変えない。文字列中の//等をコメントと誤認しない。
function Get-CodeText([string]$Text, [bool]$KeepStrings = $false)
{
    return [regex]::Replace($Text, '"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*''|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
        param($token)
        if ($KeepStrings -and ($token.Value.StartsWith('"') -or $token.Value.StartsWith("'")))
        {
            return $token.Value
        }
        return [regex]::Replace($token.Value, '[^\r\n]', ' ')
    })
}

# Core/Interfacesに宣言された非破棄virtual関数を収集する。継承だけの窓口は重複して数えない。
$interfaceRoot = Join-Path $projectRoot 'Library/Core/Interfaces' # 公開窓口の根。
foreach ($file in Get-ChildItem -LiteralPath $interfaceRoot -Filter '*.h' -Recurse -File | Sort-Object FullName)
{
    $text = [IO.File]::ReadAllText($file.FullName) # コメントを含む元のソース。
    $code = Get-CodeText $text # 宣言の探索に使う字句。
    $source = $file.FullName.Substring($projectRoot.Length + 1).Replace('\', '/') # 文書からの参照先。
    $classes = [regex]::Matches($code, '\b(?:class|struct)\s+(I[A-Z]\w*)\b[^;{]*\{') # 窓口の宣言位置。
    $methods = [regex]::Matches($code, '\bvirtual(?!\s*~)\s+(?:[^;{}]+?)\b(\w+)\s*\(') # 名前までの宣言。
    foreach ($method in $methods)
    {
        $owner = @($classes | Where-Object { $_.Index -lt $method.Index } | Select-Object -Last 1) # 直前の窓口。
        if ($owner.Count -ne 1)
        {
            throw "Cannot locate interface for $source at $($method.Index)."
        }
        $line = 1 + ([regex]::Matches($text.Substring(0, $method.Index), '\n')).Count # 元の行番号。
        $depth = 1 # 引数内の配列参照・関数型の括弧も数える。
        $end = $method.Index + $method.Length # 最初の開き括弧の次。
        while ($end -lt $code.Length -and $depth -gt 0)
        {
            if ($code[$end] -eq '(')
            {
                ++$depth
            }
            elseif ($code[$end] -eq ')')
            {
                --$depth
            }
            ++$end
        }
        if ($depth -ne 0)
        {
            throw "Unbalanced method declaration in $source."
        }
        while ($end -lt $code.Length -and $code[$end] -notin @('{', '=', ';'))
        {
            ++$end
        }
        $signature = ([regex]::Replace($code.Substring($method.Index, $end - $method.Index), '\s+', ' ')).Trim() # 戻り値・引数・const等の宣言。
        Add-Symbol ('API.' + $owner[0].Groups[1].Value + '.' + $method.Groups[1].Value) 'API' $source $line $signature
    }
    $virtualCount = ([regex]::Matches($code, '\bvirtual(?!\s*~)\s+')).Count # 未対応の宣言書式を見逃さない。
    if ($methods.Count -ne $virtualCount)
    {
        throw "Unrecognized virtual declaration in $source."
    }
}

# 命令番号を読む。追加・削除が互換表へ必ず現れるよう、enum全体を対象にする。
$commandSource = 'Library/Framework/Render/Draw/DrawCommand.h' # 命令の正本。
$commandText = [IO.File]::ReadAllText((Join-Path $projectRoot $commandSource)) # 元の行番号も使う。
$commandCode = Get-CodeText $commandText # コメントを除いたenum。
$commandEnum = [regex]::Match($commandCode, 'enum\s+class\s+EDrawCommand\s*:\s*uint8\s*\{([^}]+)\}') # 対象のenumだけを読む。
if (-not $commandEnum.Success)
{
    throw 'EDrawCommand was not found.'
}
$commandIndex = 0 # 暗黙の命令番号を変える挿入・順序変更も検出する。
foreach ($entry in $commandEnum.Groups[1].Value.Split(','))
{
    if ($entry.Trim() -eq '')
    {
        continue
    }
    $name = [regex]::Match($entry.Trim(), '^([A-Za-z_]\w*)(?:\s*=\s*[^,]+)?$') # 列挙値の名前。
    if (-not $name.Success)
    {
        throw "Unrecognized draw command: $entry"
    }
    $position = $commandEnum.Groups[1].Index + $commandEnum.Groups[1].Value.IndexOf($entry) + ([regex]::Match($entry, '\S')).Index # 宣言の位置。
    $line = 1 + ([regex]::Matches($commandText.Substring(0, $position), '\n')).Count # 参照する行。
    Add-Symbol ('Draw.' + $name.Groups[1].Value) 'Draw' $commandSource $line ('EDrawCommand[' + $commandIndex + ']: ' + $entry.Trim())
    ++$commandIndex
}

# 登録されたサンプルの名前を収集する。説明や実行結果から対応済みとは判定しない。
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Samples/Source') -Filter '*.cpp' -Recurse -File | Sort-Object FullName)
{
    $text = [IO.File]::ReadAllText($file.FullName) # 登録の文字列を含むソース。
    $registrationCode = Get-CodeText $text $true # 登録名は残し、コメント内の登録例を除く。
    foreach ($sample in [regex]::Matches($registrationCode, 'TSampleRegistrar<[^;]+?FSampleInfo\s*\{\s*"([^"\r\n]+)"'))
    {
        $source = $file.FullName.Substring($projectRoot.Length + 1).Replace('\', '/') # 登録元。
        $line = 1 + ([regex]::Matches($text.Substring(0, $sample.Index), '\n')).Count # 登録の行。
        Add-Symbol ('Sample.' + $sample.Groups[1].Value) 'Sample' $source $line $sample.Groups[1].Value
    }
}

# EditorのTools入口を収集する。各画面内の保存・編集等は、項目ごとの検査計画で扱う。
$editorSource = 'Library/DebugUI/Asset/AssetTools.cpp' # Editorとゲーム内で共有するToolsメニュー。
$editorText = [IO.File]::ReadAllText((Join-Path $projectRoot $editorSource)) # メニューの正本。
$editorCode = Get-CodeText $editorText $true # コメント中のメニュー例を除く。
$editorStart = $editorCode.IndexOf('void NDebugUI::DrawAssetToolsMenuItems(') # 対象関数の始まり。
if ($editorStart -lt 0)
{
    throw 'DrawAssetToolsMenuItems was not found.'
}
$editorBody = $editorCode.Substring($editorStart) # 次の関数より前だけに絞る。
$nextFunction = [regex]::Match($editorBody, '\r?\n(?:void|bool) NDebugUI::') # 同じファイルの次の画面関数。
if ($nextFunction.Success)
{
    $editorBody = $editorBody.Substring(0, $nextFunction.Index)
}
foreach ($menu in [regex]::Matches($editorBody, 'ImGui::MenuItem\(\s*"([^"\r\n]+)"'))
{
    $name = [regex]::Replace($menu.Groups[1].Value, '[^A-Za-z0-9]+', '_').Trim('_') # 表示名から作る安定した項目名。
    $line = 1 + ([regex]::Matches($editorText.Substring(0, $editorStart + $menu.Index), '\n')).Count # 元の行番号。
    Add-Symbol ('Editor.' + $name) 'Editor' $editorSource $line $menu.Groups[1].Value
}

$items = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal) # 人が持つ判定を項目へ対応させる。
$states = @('Unreviewed', 'Missing', 'Partial', 'Implemented', 'Verified', 'Excluded') # Verifiedだけが互換確認済み。
if (Test-Path -LiteralPath $catalogPath)
{
    foreach ($item in Import-Csv -LiteralPath $catalogPath -Encoding UTF8)
    {
        if ($items.ContainsKey($item.Id))
        {
            throw "Duplicate catalog id: $($item.Id)"
        }
        if ($states -notcontains $item.State -or $item.Milestone -notmatch '^R(?:[0-9]|10)$')
        {
            throw "Invalid status or milestone: $($item.Id)"
        }
        if ([string]::IsNullOrWhiteSpace($item.Plan) -or [string]::IsNullOrWhiteSpace($item.Note))
        {
            throw "Plan and note are required: $($item.Id)"
        }
        if ($item.State -eq 'Verified' -and [string]::IsNullOrWhiteSpace($item.Evidence))
        {
            throw "Verified requires evidence: $($item.Id)"
        }
        if (-not $symbols.ContainsKey($item.Id))
        {
            throw "Removed or renamed source item: $($item.Id). Review compatibility and update the catalog explicitly."
        }
        if ($null -eq $item.PSObject.Properties['Signature'])
        {
            if ($Check)
            {
                throw "Missing source signature: $($item.Id). Regenerate the compatibility catalog."
            }
            $item | Add-Member -NotePropertyName Signature -NotePropertyValue $symbols[$item.Id].Signature
        }
        elseif ($item.Signature -cne $symbols[$item.Id].Signature)
        {
            if ($Check)
            {
                throw "Changed source signature: $($item.Id). Regenerate and review compatibility."
            }
            $item.Signature = $symbols[$item.Id].Signature
            $item.State = 'Unreviewed'
            $item.Evidence = ''
            if (-not $item.Note.StartsWith('宣言または命令の並びが変更されたため再確認する。'))
            {
                $item.Note = '宣言または命令の並びが変更されたため再確認する。' + $item.Note
            }
        }
        $items.Add($item.Id, $item)
    }
}
$newCount = 0 # 追加された未分類項目の数。
foreach ($symbol in $symbols.Values)
{
    if (-not $items.ContainsKey($symbol.Id))
    {
        if ($Check)
        {
            throw "Missing compatibility item: $($symbol.Id). Run Tools/UpdateBackendCompatibility.ps1 and review the new entry."
        }
        $items.Add($symbol.Id, [pscustomobject][ordered]@{ Id = $symbol.Id; State = 'Unreviewed'; Milestone = 'R0'; Plan = '未分類。期待結果と比較方法を決める'; Note = '追加された公開機能。ソース・実装・寿命を確認する'; Evidence = ''; Signature = $symbol.Signature })
        ++$newCount
    }
}
$ordered = @($items.Values | Sort-Object Id) # ソース探索の順に依存しない出力。
$report = [Text.StringBuilder]::new() # 一覧は手で編集せず、台帳から生成する。
[void]$report.AppendLine('# バックエンド互換表')
[void]$report.AppendLine('')
[void]$report.AppendLine('<!-- Generated by Tools/UpdateBackendCompatibility.ps1. Edit Docs/BackendCompatibility.csv. -->')
[void]$report.AppendLine('')
[void]$report.AppendLine('[ロードマップ](ROADMAP.md)のR0で使うDX12構成の互換台帳です。Core/Interfacesの非破棄virtual関数、EDrawCommand、登録サンプル、Toolsメニューの入口を列挙します。値の全組み合わせや各編集画面内の全操作を自動で網羅するものではなく、検査計画で補います。')
[void]$report.AppendLine('')
[void]$report.AppendLine('`Unreviewed`は未分類、`Missing`は未実装／未接続、`Partial`は部分対応、`Implemented`は実装を確認したが互換検査前、`Verified`は証拠を伴う互換確認済み、`Excluded`はロードマップで明示したv1.0.0の対象外です。Excludedの理由と未対応時の期待結果もNoteとPlanに記録します。関数の宣言や正常な戻り値だけから、Verifiedへ自動で進めません。共通実装の機能と内部描画契約も含むため、すべてがDxLib専用機能ではありません。')
[void]$report.AppendLine('')
[void]$report.AppendLine('更新は `Tools/UpdateBackendCompatibility.ps1`、台帳の欠落・削除・重複と文書の古さの確認は `Tools/UpdateBackendCompatibility.ps1 -Check`。判定と検査計画はCSVへ記録し、確認結果はEvidenceへ残します。ソースが増えたときはUnreviewedへ追加し、消えた項目は自動で取り除きません。関数宣言や命令の並びが変わった項目はUnreviewedへ戻し、以前の確認結果を外します。宣言の字句を比較するもので、C++の完全な構文・ABI検査ではありません。')
[void]$report.AppendLine('')
[void]$report.AppendLine('|分類|項目数|Unreviewed|Missing|Partial|Implemented|Verified|Excluded|')
[void]$report.AppendLine('|---|---:|---:|---:|---:|---:|---:|---:|')
foreach ($kind in @('API', 'Draw', 'Sample', 'Editor'))
{
    $members = @($ordered | Where-Object { $symbols[$_.Id].Kind -eq $kind }) # 分類内の台帳。
    $counts = @($states | ForEach-Object { $state = $_; @($members | Where-Object { $_.State -eq $state }).Count }) # 判定別の数。
    [void]$report.AppendLine(('|{0}|{1}|{2}|' -f $kind, $members.Count, ($counts -join '|')))
}
foreach ($kind in @('API', 'Draw', 'Sample', 'Editor'))
{
    [void]$report.AppendLine('')
    [void]$report.AppendLine('## ' + $kind)
    [void]$report.AppendLine('')
    [void]$report.AppendLine('|項目|判定|段階|検査計画|現在の制限・根拠|確認結果|')
    [void]$report.AppendLine('|---|---|---|---|---|---|')
    foreach ($item in $ordered | Where-Object { $symbols[$_.Id].Kind -eq $kind })
    {
        $symbol = $symbols[$item.Id] # ソースへ戻れる位置。
        $cells = @($item.Plan, $item.Note, $item.Evidence) | ForEach-Object { ([string]$_).Replace('|', '&#124;').Replace("`r", '').Replace("`n", '<br>') } # 表の区切りを保つ。
        [void]$report.AppendLine(('|[{0}](../{1}#L{2})|{3}|{4}|{5}|{6}|{7}|' -f $item.Id, $symbol.Source, $symbol.Line, $item.State, $item.Milestone, $cells[0], $cells[1], $cells[2]))
    }
}
$reportText = $report.ToString().Replace("`r`n", "`n").Replace("`n", "`r`n") # OSにかかわらずCRLF。
$catalogText = (($ordered | Select-Object Id, State, Milestone, Plan, Note, Evidence, Signature | ConvertTo-Csv -NoTypeInformation) -join "`r`n") + "`r`n" # 台帳の列と順序も固定する。
if ($Check)
{
    if (-not (Test-Path -LiteralPath $reportPath) -or [IO.File]::ReadAllText($reportPath) -cne $reportText)
    {
        throw 'Compatibility report is stale. Run Tools/UpdateBackendCompatibility.ps1.'
    }
    Write-Host ("PASS: {0} compatibility items; report and source inventory agree." -f $symbols.Count)
}
else
{
    $encoding = [Text.UTF8Encoding]::new($true) # 日本語を含む台帳と文書のBOM付きUTF-8。
    [IO.File]::WriteAllText($catalogPath, $catalogText, $encoding)
    [IO.File]::WriteAllText($reportPath, $reportText, $encoding)
    Write-Host ("Updated {0} compatibility items ({1} new entries require review)." -f $symbols.Count, $newCount)
}
