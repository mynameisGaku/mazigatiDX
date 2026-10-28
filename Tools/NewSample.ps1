# Adds a new sample to the sample collection (Samples\).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\NewSample.ps1 -Name Physics [-Title "Physics: bodies and joints"] [-Description "..."] [-Order 30]
#
#   Samples\Source\<Name>\<Name>Sample.h / .cpp are created from Templates\Sample (a scene class that registers itself in the catalog),
#   and the file list of the Samples project is generated (GenerateProjectFiles.ps1).
#   -Title and -Description are shown in the list. -Order decides the place in the list (smaller is higher). By default, it goes to the end.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Title = '',
    [string]$Description = 'Describe what this sample shows.',
    [int]$Order = -1
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'TemplateTools.ps1')

if ($Name -cnotmatch '^[A-Z][A-Za-z0-9]*$') {
    throw "-Name must start with an upper case letter and contain only letters and digits (got '$Name'). It becomes the class C${Name}Sample and the --sample=$Name option."
}
if ($Name -ceq 'Browser') { throw "'Browser' is the name of the list screen." }

$frameworkRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$templateDir = Join-Path (Join-Path $frameworkRoot 'Templates') 'Sample'
$samplesRoot = Join-Path $frameworkRoot 'Samples'
if (-not (Test-Path -LiteralPath $templateDir)) { throw "Template not found: $templateDir" }
if (-not (Test-Path -LiteralPath (Join-Path $samplesRoot 'Samples.vcxproj'))) { throw "The Samples project was not found: $samplesRoot" }

$target = Join-Path (Join-Path $samplesRoot 'Source') $Name
if (Test-Path -LiteralPath $target) { throw "The sample already exists: $target" }

# The default order: 10 after the largest one that exists (the number is the last argument of the FSampleInfo in each sample)
if ($Order -lt 0) {
    $largest = 0
    foreach ($source in (Get-ChildItem -LiteralPath (Join-Path $samplesRoot 'Source') -Recurse -Filter '*Sample.cpp' -File)) {
        $match = [regex]::Match([IO.File]::ReadAllText($source.FullName), 'FSampleInfo\s*\{[^}]*,\s*(\d+)\s*\}')
        if ($match.Success) { $largest = [Math]::Max($largest, [int]$match.Groups[1].Value) }
    }
    $Order = $largest + 10
}
if ($Title -eq '') { $Title = $Name }

# The title and the description are written in C++ string literals
function ConvertTo-CppString([string]$Text) { return $Text.Replace('\', '\\').Replace('"', '\"') }

$replacements = [ordered]@{
    '__DESCRIPTION__' = ConvertTo-CppString $Description
    '__TITLE__'       = ConvertTo-CppString $Title
    '__ORDER__'       = [string]$Order
    '__NAME__'        = $Name
}

Write-Host "Sample: $Name (order $Order)"
Write-Host "Folder: $target"
Write-Host ''

# The template has the folders Source\<Name>\, so it is copied to the root of the Samples (not into the new folder)
Copy-TemplateTree $templateDir $samplesRoot $replacements

Write-Host ''
Write-Host 'Generating the file list of the Samples project...'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'GenerateProjectFiles.ps1') -Root $samplesRoot
if ($LASTEXITCODE -ne 0) { throw 'GenerateProjectFiles.ps1 failed.' }

Write-Host ''
Write-Host 'Done. Next:'
Write-Host "  1. Build the Samples project. The new sample is in the list; run it directly with: Samples.exe --sample=$Name"
Write-Host "  2. Put the classes of the sample in $target (one class per file; the scene only decides what to place, and the objects draw and move themselves)."
