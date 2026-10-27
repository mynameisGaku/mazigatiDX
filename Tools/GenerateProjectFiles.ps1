# Generates the file lists of the Visual Studio projects.
#   For every .vcxproj in -Root, the files under the folders named in its <MazigatiSourceRoots> (separated by ;) are written to the
#   ClCompile / ClInclude items and to the .vcxproj.filters file. Run it again after adding, removing or moving a source file.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\GenerateProjectFiles.ps1 -Root <the folder of the project>
param([Parameter(Mandatory = $true)][string]$Root)

$ErrorActionPreference = 'Stop'
$root = $Root.TrimEnd('\')

# ---- Helper: relative path (.NET 4.x compatible) ----
function Get-RelPath([string]$base, [string]$full) {
    $b = [IO.Path]::GetFullPath($base).TrimEnd('\') + '\'
    $f = [IO.Path]::GetFullPath($full)
    if ($f.StartsWith($b, [StringComparison]::OrdinalIgnoreCase)) {
        return $f.Substring($b.Length)
    }
    return $f
}

# ---- Find the .vcxproj files (the solution has more than one: the framework library, the game, ...) ----
$vcxprojFiles = @(Get-ChildItem -Path $root -Filter '*.vcxproj' -File |
                  Where-Object { $_.Name -notmatch '\.user$' })
if ($vcxprojFiles.Count -eq 0) {
    Write-Error "No .vcxproj file found in: $root"
    exit 1
}

# ---- Directories to exclude from scan ----
# Skips: hidden dirs (.*), build output dirs (named after the projects), known platform/config names
$excludeNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($d in @('Debug', 'Release', 'x64', 'x86', 'Win32', 'ARM', 'ARM64', 'bin', 'obj', 'out')) {
    [void]$excludeNames.Add($d)
}
foreach ($f in $vcxprojFiles) {
    [void]$excludeNames.Add([IO.Path]::GetFileNameWithoutExtension($f.FullName))
}

function Should-Exclude([string]$relPath) {
    foreach ($part in ($relPath -split '[\\\/]' | Select-Object -SkipLast 1)) {
        if ($part.StartsWith('.')) { return $true }
        if ($excludeNames.Contains($part)) { return $true }
    }
    return $false
}

$ns  = 'http://schemas.microsoft.com/developer/msbuild/2003'
$xws = New-Object System.Xml.XmlWriterSettings
$xws.Indent      = $true
$xws.IndentChars = '  '
$xws.Encoding    = New-Object System.Text.UTF8Encoding($false)

# Source\SelfTest is kept in a separate repository and may be absent (or be a junction to it).
$selfTestDir = 'Source\SelfTest'

# =========================================================
# One project: rewrite its ClCompile / ClInclude items and its .filters file
#   The folders to scan are written in the project itself:  <MazigatiSourceRoots>Library;VendorModules</MazigatiSourceRoots>
# =========================================================
function Update-Project([IO.FileInfo]$vcxprojFile) {
    $vcxprojPath = $vcxprojFile.FullName
    $filtersPath = $vcxprojPath + '.filters'
    Write-Host "Project : $($vcxprojFile.Name)"

    [xml]$vcx = [IO.File]::ReadAllText($vcxprojPath, [Text.Encoding]::UTF8)
    $nsm = New-Object System.Xml.XmlNamespaceManager($vcx.NameTable)
    $nsm.AddNamespace('ms', $ns)

    $rootsNode = $vcx.SelectSingleNode('//ms:MazigatiSourceRoots', $nsm)
    if (-not $rootsNode) {
        Write-Host '  (skipped: no <MazigatiSourceRoots> in the project)' -ForegroundColor Yellow
        Write-Host ''
        return
    }
    $sourceRoots = @($rootsNode.InnerText -split ';' | Where-Object { $_.Trim() -ne '' } | ForEach-Object { $_.Trim() })
    Write-Host "  Roots   : $($sourceRoots -join ', ')"

    # ---- Scan the source files under the roots ----
    $compileItems = [System.Collections.Generic.List[hashtable]]::new()
    $includeItems  = [System.Collections.Generic.List[hashtable]]::new()
    $allFilters    = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    $sourceFiles = @()
    foreach ($r in $sourceRoots) {
        $rootPath = Join-Path $root $r
        if (Test-Path -LiteralPath $rootPath) {
            $sourceFiles += @(Get-ChildItem -LiteralPath $rootPath -Recurse -File |
                              Where-Object { $_.Extension -in @('.cpp', '.c', '.h', '.hpp', '.inl') })
        }
    }

    # Visual Studio does not apply the .filters file to files that a wildcard item expands to: they would all show up loose at the project
    # root, whatever the .filters file says. So the SelfTest files that exist now are listed one by one in the .vcxproj too, each with a
    # condition that the file exists (the build works the same when Source\SelfTest is absent). A wildcard item that excludes the listed
    # files is kept after them, so a file added later still builds without regenerating (it shows at the root until this script is run again).
    $selfTestCompile = [System.Collections.Generic.List[hashtable]]::new()
    $selfTestInclude = [System.Collections.Generic.List[hashtable]]::new()
    $hasSource = $sourceRoots -contains 'Source'

    foreach ($file in $sourceFiles) {
        $relPath = Get-RelPath $root $file.FullName

        if (Should-Exclude $relPath) { continue }
        if ($relPath.StartsWith($selfTestDir + '\', [StringComparison]::OrdinalIgnoreCase)) { continue }

        $dirPart = [IO.Path]::GetDirectoryName($relPath)
        $filter  = if ($dirPart) { $dirPart } else { '' }

        # Register filter and all ancestor filters
        if ($filter) {
            $parts = $filter -split '[\\\/]'
            for ($i = 1; $i -le $parts.Length; $i++) {
                [void]$allFilters.Add(($parts[0..($i-1)] -join '\'))
            }
        }

        if ($file.Extension -in @('.cpp', '.c')) {
            $compileItems.Add(@{ RelPath = $relPath; Filter = $filter })
        } else {
            $includeItems.Add(@{ RelPath = $relPath; Filter = $filter })
        }
    }

    # Source\SelfTest is usually a junction, which the scan above does not enter: list it by its own path
    $selfTestPath = Join-Path $root $selfTestDir
    if ($hasSource -and (Test-Path -LiteralPath $selfTestPath)) {
        foreach ($file in (Get-ChildItem -LiteralPath $selfTestPath -Recurse -File | Where-Object { $_.Extension -in @('.cpp', '.h') })) {
            $relPath = Get-RelPath $root $file.FullName
            if (Should-Exclude $relPath) { continue }
            $selfTestFilter = [IO.Path]::GetDirectoryName($relPath)
            if ($file.Extension -eq '.cpp') { $selfTestCompile.Add(@{ RelPath = $relPath; Filter = $selfTestFilter }) }
            else { $selfTestInclude.Add(@{ RelPath = $relPath; Filter = $selfTestFilter }) }
        }
    }

    # .vcxproj: the wildcard items that pick up files added later (the ones listed one by one are excluded, so nothing is built twice)
    $wildcardCompile = "$selfTestDir\**\*.cpp"
    $wildcardInclude = "$selfTestDir\**\*.h"

    # .filters: the existing files, with their sub folders as filters
    foreach ($item in @($selfTestCompile) + @($selfTestInclude)) {
        $parts = $item.Filter -split '[\\\/]'
        for ($i = 1; $i -le $parts.Length; $i++) {
            [void]$allFilters.Add(($parts[0..($i-1)] -join '\'))
        }
    }

    Write-Host "  Compile : $($compileItems.Count) file(s)"
    Write-Host "  Include : $($includeItems.Count) file(s)"

    # =========================================================
    # Update .vcxproj
    # =========================================================
    # Remove old ClCompile / ClInclude ItemGroups
    foreach ($g in @($vcx.SelectNodes('//ms:ItemGroup[ms:ClCompile or ms:ClInclude]', $nsm))) {
        [void]$g.ParentNode.RemoveChild($g)
    }

    $proj = $vcx.DocumentElement

    $cg = $vcx.CreateElement('ItemGroup', $ns)
    foreach ($item in $compileItems) {
        $el = $vcx.CreateElement('ClCompile', $ns)
        $el.SetAttribute('Include', $item.RelPath)
        [void]$cg.AppendChild($el)
    }
    foreach ($item in $selfTestCompile) {
        $el = $vcx.CreateElement('ClCompile', $ns)
        $el.SetAttribute('Include', $item.RelPath)
        $el.SetAttribute('Condition', "Exists('$($item.RelPath)')")
        [void]$cg.AppendChild($el)
    }
    if ($hasSource) {
        $el = $vcx.CreateElement('ClCompile', $ns)
        $el.SetAttribute('Include', $wildcardCompile)
        $el.SetAttribute('Exclude', '@(ClCompile)')
        [void]$cg.AppendChild($el)
    }
    [void]$proj.AppendChild($cg)

    $ig = $vcx.CreateElement('ItemGroup', $ns)
    foreach ($item in $includeItems) {
        $el = $vcx.CreateElement('ClInclude', $ns)
        $el.SetAttribute('Include', $item.RelPath)
        [void]$ig.AppendChild($el)
    }
    foreach ($item in $selfTestInclude) {
        $el = $vcx.CreateElement('ClInclude', $ns)
        $el.SetAttribute('Include', $item.RelPath)
        $el.SetAttribute('Condition', "Exists('$($item.RelPath)')")
        [void]$ig.AppendChild($el)
    }
    if ($hasSource) {
        $el = $vcx.CreateElement('ClInclude', $ns)
        $el.SetAttribute('Include', $wildcardInclude)
        $el.SetAttribute('Exclude', '@(ClInclude)')
        [void]$ig.AppendChild($el)
    }
    [void]$proj.AppendChild($ig)

    $xw = [System.Xml.XmlWriter]::Create($vcxprojPath, $xws)
    $vcx.Save($xw)
    $xw.Dispose()

    # =========================================================
    # Generate .vcxproj.filters
    # =========================================================
    $fxml = New-Object System.Xml.XmlDocument
    [void]$fxml.AppendChild($fxml.CreateXmlDeclaration('1.0', 'utf-8', $null))
    $fp = $fxml.CreateElement('Project', $ns)
    $fp.SetAttribute('ToolsVersion', '4.0')
    [void]$fxml.AppendChild($fp)

    $fg1 = $fxml.CreateElement('ItemGroup', $ns)
    foreach ($item in $compileItems) {
        $el = $fxml.CreateElement('ClCompile', $ns)
        $el.SetAttribute('Include', $item.RelPath)
        if ($item.Filter) {
            $fe = $fxml.CreateElement('Filter', $ns)
            $fe.InnerText = $item.Filter
            [void]$el.AppendChild($fe)
        }
        [void]$fg1.AppendChild($el)
    }
    foreach ($item in $selfTestCompile) {
        $el = $fxml.CreateElement('ClCompile', $ns)
        $el.SetAttribute('Include', $item.RelPath)
        $fe = $fxml.CreateElement('Filter', $ns)
        $fe.InnerText = $item.Filter
        [void]$el.AppendChild($fe)
        [void]$fg1.AppendChild($el)
    }
    [void]$fp.AppendChild($fg1)

    $fg2 = $fxml.CreateElement('ItemGroup', $ns)
    foreach ($item in $includeItems) {
        $el = $fxml.CreateElement('ClInclude', $ns)
        $el.SetAttribute('Include', $item.RelPath)
        if ($item.Filter) {
            $fe = $fxml.CreateElement('Filter', $ns)
            $fe.InnerText = $item.Filter
            [void]$el.AppendChild($fe)
        }
        [void]$fg2.AppendChild($el)
    }
    foreach ($item in $selfTestInclude) {
        $el = $fxml.CreateElement('ClInclude', $ns)
        $el.SetAttribute('Include', $item.RelPath)
        $fe = $fxml.CreateElement('Filter', $ns)
        $fe.InnerText = $item.Filter
        [void]$el.AppendChild($fe)
        [void]$fg2.AppendChild($el)
    }
    [void]$fp.AppendChild($fg2)

    # Filter declarations with deterministic GUIDs
    $fg3 = $fxml.CreateElement('ItemGroup', $ns)
    $md5 = [Security.Cryptography.MD5]::Create()
    foreach ($filterName in $allFilters) {
        $el = $fxml.CreateElement('Filter', $ns)
        $el.SetAttribute('Include', $filterName)
        $gu = $fxml.CreateElement('UniqueIdentifier', $ns)
        $gu.InnerText = [Guid]::new($md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($filterName))).ToString('B').ToUpper()
        [void]$el.AppendChild($gu)
        [void]$fg3.AppendChild($el)
    }
    [void]$fp.AppendChild($fg3)

    $xw2 = [System.Xml.XmlWriter]::Create($filtersPath, $xws)
    $fxml.Save($xw2)
    $xw2.Dispose()
    Write-Host '  OK' -ForegroundColor Green
    Write-Host ''
}

Write-Host 'Scanning...'
Write-Host ''
foreach ($f in $vcxprojFiles) {
    Update-Project $f
}

Write-Host '============================================' -ForegroundColor Cyan
Write-Host '  Generation complete!' -ForegroundColor Cyan
Write-Host '============================================' -ForegroundColor Cyan
