@echo off
chcp 65001 > nul
setlocal

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "_BATFILE=%~f0"

echo ============================================
echo  GenerateProjectFiles
echo  Root: %PROJECT_ROOT%
echo ============================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$c=[IO.File]::ReadAllText($env:_BATFILE,[Text.Encoding]::UTF8);" ^
  "$s=$c.LastIndexOf('##PS_START##')+12;" ^
  "$e=$c.LastIndexOf('##PS_END##');" ^
  "Invoke-Expression $c.Substring($s,$e-$s)"

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Project file generation failed.
    pause
    exit /b 1
)

echo.
echo Press any key to close...
pause > nul
endlocal
exit /b 0

##PS_START##
$ErrorActionPreference = 'Stop'
$root = $env:PROJECT_ROOT

# ---- Helper: relative path (.NET 4.x compatible) ----
function Get-RelPath([string]$base, [string]$full) {
    $b = [IO.Path]::GetFullPath($base).TrimEnd('\') + '\'
    $f = [IO.Path]::GetFullPath($full)
    if ($f.StartsWith($b, [StringComparison]::OrdinalIgnoreCase)) {
        return $f.Substring($b.Length)
    }
    return $f
}

# ---- Find .vcxproj ----
$vcxprojFile = Get-ChildItem -Path $root -Filter '*.vcxproj' -File |
               Where-Object { $_.Name -notmatch '\.user$' } |
               Select-Object -First 1
if (-not $vcxprojFile) {
    Write-Error "No .vcxproj file found in: $root"
    exit 1
}
$vcxprojPath = $vcxprojFile.FullName
$filtersPath  = $vcxprojPath + '.filters'
$projName     = [IO.Path]::GetFileNameWithoutExtension($vcxprojPath)

Write-Host "  Project : $(Split-Path $vcxprojPath -Leaf)"
Write-Host ''

# ---- Directories to exclude from scan ----
# Skips: hidden dirs (.*), build output dirs, known platform/config names
$excludeNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($d in @($projName, 'Debug', 'Release', 'x64', 'x86', 'Win32', 'ARM', 'ARM64', 'bin', 'obj', 'out')) {
    [void]$excludeNames.Add($d)
}

function Should-Exclude([string]$relPath) {
    foreach ($part in ($relPath -split '[\\\/]' | Select-Object -SkipLast 1)) {
        if ($part.StartsWith('.')) { return $true }
        if ($excludeNames.Contains($part)) { return $true }
    }
    return $false
}

# ---- Scan all source files under project root ----
Write-Host 'Scanning...'
$compileItems = [System.Collections.Generic.List[hashtable]]::new()
$includeItems  = [System.Collections.Generic.List[hashtable]]::new()
$allFilters    = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

$sourceFiles = Get-ChildItem -Path $root -Recurse -File |
               Where-Object { $_.Extension -in @('.cpp', '.c', '.h', '.hpp', '.inl') }

foreach ($file in $sourceFiles) {
    $relPath = Get-RelPath $root $file.FullName

    if (Should-Exclude $relPath) { continue }

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

Write-Host "  Compile : $($compileItems.Count) file(s)"
Write-Host "  Include : $($includeItems.Count) file(s)"
Write-Host ''

$ns  = 'http://schemas.microsoft.com/developer/msbuild/2003'
$xws = New-Object System.Xml.XmlWriterSettings
$xws.Indent      = $true
$xws.IndentChars = '  '
$xws.Encoding    = New-Object System.Text.UTF8Encoding($false)

# =========================================================
# Update .vcxproj
# =========================================================
Write-Host "Updating $(Split-Path $vcxprojPath -Leaf)..."

[xml]$vcx = [IO.File]::ReadAllText($vcxprojPath, [Text.Encoding]::UTF8)
$nsm = New-Object System.Xml.XmlNamespaceManager($vcx.NameTable)
$nsm.AddNamespace('ms', $ns)

# Remove old ClCompile / ClInclude ItemGroups
foreach ($g in @($vcx.SelectNodes('//ms:ItemGroup[ms:ClCompile or ms:ClInclude]', $nsm))) {
    [void]$g.ParentNode.RemoveChild($g)
}

# Prepend $(SolutionDir) and SDL3 include path to AdditionalIncludeDirectories
foreach ($node in @($vcx.SelectNodes('//ms:AdditionalIncludeDirectories', $nsm))) {
    if ($node.InnerText -notmatch '\$\(SolutionDir\)') {
        $node.InnerText = '$(SolutionDir);' + $node.InnerText
    }
    if ($node.InnerText -notmatch 'VendorModules\\SDL3\\include') {
        $node.InnerText = '$(SolutionDir)VendorModules\SDL3\include;' + $node.InnerText
    }
}

# Add SDL3 library directory to AdditionalLibraryDirectories
foreach ($node in @($vcx.SelectNodes('//ms:AdditionalLibraryDirectories', $nsm))) {
    if ($node.InnerText -notmatch 'VendorModules\\SDL3\\lib') {
        $node.InnerText = '$(SolutionDir)VendorModules\SDL3\lib\$(PlatformTarget);' + $node.InnerText
    }
}

$proj = $vcx.DocumentElement

# Ensure CopySdlDll target exists to copy SDL3.dll to output directory
if (-not $vcx.SelectSingleNode('//ms:Target[@Name="CopySdlDll"]', $nsm)) {
    $tgt = $vcx.CreateElement('Target', $ns)
    $tgt.SetAttribute('Name', 'CopySdlDll')
    $tgt.SetAttribute('AfterTargets', 'Link')
    $cpy = $vcx.CreateElement('Copy', $ns)
    $cpy.SetAttribute('SourceFiles', '$(SolutionDir)VendorModules\SDL3\lib\$(PlatformTarget)\SDL3.dll')
    $cpy.SetAttribute('DestinationFolder', '$(OutDir)')
    $cpy.SetAttribute('SkipUnchangedFiles', 'true')
    $cpy.SetAttribute('Condition', 'Exists(''$(SolutionDir)VendorModules\SDL3\lib\$(PlatformTarget)\SDL3.dll'')')
    [void]$tgt.AppendChild($cpy)
    [void]$proj.AppendChild($tgt)
}

$cg = $vcx.CreateElement('ItemGroup', $ns)
foreach ($item in $compileItems) {
    $el = $vcx.CreateElement('ClCompile', $ns)
    $el.SetAttribute('Include', $item.RelPath)
    [void]$cg.AppendChild($el)
}
[void]$proj.AppendChild($cg)

$ig = $vcx.CreateElement('ItemGroup', $ns)
foreach ($item in $includeItems) {
    $el = $vcx.CreateElement('ClInclude', $ns)
    $el.SetAttribute('Include', $item.RelPath)
    [void]$ig.AppendChild($el)
}
[void]$proj.AppendChild($ig)

$xw = [System.Xml.XmlWriter]::Create($vcxprojPath, $xws)
$vcx.Save($xw)
$xw.Dispose()
Write-Host '  OK' -ForegroundColor Green

# =========================================================
# Generate .vcxproj.filters
# =========================================================
Write-Host "Generating $(Split-Path $filtersPath -Leaf)..."

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
Write-Host '============================================' -ForegroundColor Cyan
Write-Host '  Generation complete!' -ForegroundColor Cyan
Write-Host '============================================' -ForegroundColor Cyan
##PS_END##
