# DxLib の公式配布物（Visual C++ 版）を、固定版・固定ハッシュで取得して、ビルドが探す場所（既定は C:\DxLib）へ置く。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\FetchDxLib.ps1 [-Destination C:\DxLib] [-ArchivePath <取得済みの zip>]
#
#   - 置くのは、配布物の「プロジェクトに追加すべきファイル_VC用」フォルダーの中身（ヘッダーと .lib。424 個）だけ。
#   - 置き場所を変えたら、ビルドの Build\mazigatiDX.props の MazigatiDxLibDir（または同じ名前の環境変数）を、同じ場所に合わせる。
#   - DxLib のライセンスと、再配布の条件は、配布物の DxLib.txt と、THIRD_PARTY_NOTICES.md を読む（このスクリプトは、公式サイトから取得するだけで、
#     DxLib をこのリポジトリへは含めない）。
#   - -ArchivePath を渡すと、ダウンロードせず、その zip を使う（ハッシュは確かめる）。
param(
    [string]$Destination = 'C:\DxLib',
    [string]$ArchivePath = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$dxVersionHex = '0x324f'
$dxVersionText = '3.24f'
$archiveUrl = 'https://dxlib.xsrv.jp/DxLib/DxLib_VC3_24f.zip'
$archiveHash = '82FDE04562E4A5128710BB95AD8A73DCA708A9E080439ACC14D8E1AD5A095675'

# 配布物の中の、取り出す（ヘッダーと .lib が入った）フォルダーの名前。日本語のフォルダー名なので、文字コードで組み立てる
# （スクリプトの文字コードに依らず、同じ名前になる）。「プロジェクトに追加すべきファイル_VC用」
$folderName = [string]::new([char[]]( 0x30D7, 0x30ED, 0x30B8, 0x30A7, 0x30AF, 0x30C8, 0x306B, 0x8FFD, 0x52A0, 0x3059, 0x3079, 0x304D, 0x30D5, 0x30A1, 0x30A4, 0x30EB )) + '_VC' + [string]::new([char[]]( 0x7528 ))
$folderPrefix = 'DxLib_VC/' + $folderName + '/'

$header = Join-Path $Destination 'DxLib.h'
if (Test-Path -LiteralPath $header) {
    if ((Get-Content -LiteralPath $header -Raw) -match ('#define\s+DXLIB_VERSION\s+' + $dxVersionHex)) {
        Write-Host "DxLib $dxVersionText is already installed in $Destination."
        exit 0
    }
    throw "$Destination already contains another DxLib. Preserve it before replacing it (or pass another -Destination)."
}
if ((Test-Path -LiteralPath $Destination) -and (Get-ChildItem -LiteralPath $Destination -Force | Select-Object -First 1)) {
    throw "$Destination already exists and is not empty. Pass an empty or new -Destination."
}

$staging = Join-Path ([IO.Path]::GetTempPath()) ('FrameworkDxLib_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null
try {
    $archive = $ArchivePath
    if ($archive -eq '') {
        $archive = Join-Path $staging 'DxLib.zip'
        Write-Host "Downloading DxLib $dxVersionText (about 206 MB) from $archiveUrl ..."
        Invoke-WebRequest -Uri $archiveUrl -OutFile $archive -UseBasicParsing
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $archiveHash) { throw 'DxLib archive checksum mismatch.' }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entries = @($zip.Entries | Where-Object { $_.FullName.StartsWith($folderPrefix) -and $_.Name -ne '' })
        if (-not ($entries | Where-Object { $_.Name -eq 'DxLib.h' })) { throw 'DxLib.h is missing in the archive.' }
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        foreach ($entry in $entries) {
            # 配布物の、このフォルダーの直下のファイルだけを、そのまま置く
            $relative = $entry.FullName.Substring($folderPrefix.Length)
            if ($relative.Contains('/')) { continue }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $Destination $relative), $true)
        }
    } finally { $zip.Dispose() }
    if (-not (Test-Path -LiteralPath $header)) { throw 'DxLib.h was not installed.' }
    Write-Host "Installed DxLib $dxVersionText in $Destination."
} finally {
    # 作成した一時ディレクトリーだけを消す。
    $resolvedStaging = [IO.Path]::GetFullPath($staging)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedStaging.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and (Split-Path $resolvedStaging -Leaf).StartsWith('FrameworkDxLib_')) {
        Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
    }
}
