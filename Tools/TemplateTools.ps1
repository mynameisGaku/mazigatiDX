# Functions shared by NewProject.ps1 and NewSample.ps1 (they dot-source this file).

# The path from a folder to another one (absolute when they are on different drives).
function Get-RelativeFolder([string]$from, [string]$to) {
    $f = [IO.Path]::GetFullPath($from).TrimEnd('\') + '\'
    $t = [IO.Path]::GetFullPath($to).TrimEnd('\') + '\'
    if ([IO.Path]::GetPathRoot($f) -ne [IO.Path]::GetPathRoot($t)) { return $t.TrimEnd('\') }
    $rel = ([Uri]$f).MakeRelativeUri([Uri]$t).ToString()
    $rel = [Uri]::UnescapeDataString($rel).Replace('/', '\').TrimEnd('\')
    if ($rel -eq '') { return '.' }
    return $rel
}

# Copies the files of a template folder to a target folder.
#   -Replacements is a dictionary of placeholder -> text. The placeholders are replaced in the file names and in the contents of the text
#   files (the encoding and the BOM of a text file are kept as they are). The other files are copied as they are.
#   A line is written for each file.
function Copy-TemplateTree([string]$TemplateDir, [string]$Target, $Replacements) {
    # Files with these extensions get their placeholders replaced
    $textExtensions = @('.sln', '.vcxproj', '.cpp', '.h', '.json', '.md', '.bat', '.gitignore', '.ps1', '.txt')

    $templateBase = $TemplateDir.TrimEnd('\')
    foreach ($file in (Get-ChildItem -LiteralPath $TemplateDir -Recurse -Force -File)) {
        $relative = $file.FullName.Substring($templateBase.Length + 1)
        foreach ($key in $Replacements.Keys) { $relative = $relative.Replace($key, $Replacements[$key]) }
        $destFile = Join-Path $Target $relative
        $destDir = Split-Path -Parent $destFile
        if (-not (Test-Path -LiteralPath $destDir)) { [void](New-Item -ItemType Directory -Path $destDir -Force) }

        if ($textExtensions -contains $file.Extension.ToLowerInvariant()) {
            $bytes = [IO.File]::ReadAllBytes($file.FullName)
            $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
            $text = [Text.Encoding]::UTF8.GetString($bytes, $(if ($hasBom) { 3 } else { 0 }), $bytes.Length - $(if ($hasBom) { 3 } else { 0 }))
            foreach ($key in $Replacements.Keys) { $text = $text.Replace($key, $Replacements[$key]) }
            [IO.File]::WriteAllText($destFile, $text, (New-Object Text.UTF8Encoding($hasBom)))
        } else {
            Copy-Item -LiteralPath $file.FullName -Destination $destFile
        }
        Write-Host "  $relative"
    }
}
