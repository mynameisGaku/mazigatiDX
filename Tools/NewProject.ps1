# Creates a new game project from a template of mazigatiDX.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\NewProject.ps1 -Name MyGame [-Destination C:\dev] [-Template Minimal]
#
#   <Destination>\<Name> is created from Templates\<Template>:
#     - the placeholders (__NAME__, __PROJECT_GUID__, __FRAMEWORK_RELPATH__) are replaced
#     - the project points at this framework folder with a relative path
#     - the file list of the project is generated (GenerateProjectFiles.ps1)
#   -Destination defaults to the current folder. The new folder must not exist yet (or must be empty).
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Destination = (Get-Location).Path,
    [string]$Template = 'Minimal'
)

$ErrorActionPreference = 'Stop'

if ($Name -notmatch '^[A-Za-z][A-Za-z0-9_]*$') {
    throw "-Name must start with a letter and contain only letters, digits and '_' (got '$Name')."
}

$frameworkRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$templateDir = Join-Path (Join-Path $frameworkRoot 'Templates') $Template
if (-not (Test-Path -LiteralPath $templateDir)) {
    throw "Template not found: $templateDir"
}

$target = Join-Path ([IO.Path]::GetFullPath($Destination)) $Name
if ((Test-Path -LiteralPath $target) -and @(Get-ChildItem -LiteralPath $target -Force).Count -gt 0) {
    throw "The folder already exists and is not empty: $target"
}

# The path from the new project to the framework (absolute when they are on different drives)
function Get-RelativeFolder([string]$from, [string]$to) {
    $f = [IO.Path]::GetFullPath($from).TrimEnd('\') + '\'
    $t = [IO.Path]::GetFullPath($to).TrimEnd('\') + '\'
    if ([IO.Path]::GetPathRoot($f) -ne [IO.Path]::GetPathRoot($t)) { return $t.TrimEnd('\') }
    $rel = ([Uri]$f).MakeRelativeUri([Uri]$t).ToString()
    $rel = [Uri]::UnescapeDataString($rel).Replace('/', '\').TrimEnd('\')
    if ($rel -eq '') { return '.' }
    return $rel
}

$replacements = [ordered]@{
    '__NAME__'               = $Name
    '__PROJECT_GUID__'       = [Guid]::NewGuid().ToString('B').ToUpper()
    '__FRAMEWORK_RELPATH__'  = Get-RelativeFolder $target $frameworkRoot
}

# Files with these extensions get their placeholders replaced (the encoding and the BOM are kept as they are)
$textExtensions = @('.sln', '.vcxproj', '.cpp', '.h', '.json', '.md', '.bat', '.gitignore', '.ps1', '.txt')

Write-Host "Template : $templateDir"
Write-Host "Project  : $target"
Write-Host "Framework: $($replacements['__FRAMEWORK_RELPATH__'])"
Write-Host ''

$templateBase = $templateDir.TrimEnd('\')
foreach ($file in (Get-ChildItem -LiteralPath $templateDir -Recurse -Force -File)) {
    $relative = $file.FullName.Substring($templateBase.Length + 1)
    foreach ($key in $replacements.Keys) { $relative = $relative.Replace($key, $replacements[$key]) }
    $destFile = Join-Path $target $relative
    $destDir = Split-Path -Parent $destFile
    if (-not (Test-Path -LiteralPath $destDir)) { [void](New-Item -ItemType Directory -Path $destDir -Force) }

    if ($textExtensions -contains $file.Extension.ToLowerInvariant()) {
        $bytes = [IO.File]::ReadAllBytes($file.FullName)
        $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
        $text = [Text.Encoding]::UTF8.GetString($bytes, $(if ($hasBom) { 3 } else { 0 }), $bytes.Length - $(if ($hasBom) { 3 } else { 0 }))
        foreach ($key in $replacements.Keys) { $text = $text.Replace($key, $replacements[$key]) }
        [IO.File]::WriteAllText($destFile, $text, (New-Object Text.UTF8Encoding($hasBom)))
    } else {
        Copy-Item -LiteralPath $file.FullName -Destination $destFile
    }
    Write-Host "  $relative"
}

Write-Host ''
Write-Host 'Generating the file list of the project...'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'GenerateProjectFiles.ps1') -Root $target
if ($LASTEXITCODE -ne 0) { throw 'GenerateProjectFiles.ps1 failed.' }

Write-Host ''
Write-Host 'Done. Next:'
Write-Host "  1. Open $(Join-Path $target ($Name + '.sln')) in Visual Studio and build x64 Debug / Release."
Write-Host '  2. DxLib 3.24f is expected in C:\DxLib (set MazigatiDxLibDir if it is somewhere else).'
Write-Host "  3. See $(Join-Path $target 'README.md')."
