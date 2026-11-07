# エンジン組み込みの素材（EngineContent）のうち、UI 用のアイコンを書き出す。生成した PNG をコミットして使う（実行時には要らない）。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\GenerateEngineIcons.ps1 [-Destination EngineContent]
#
#   - すべて、このスクリプトで描いた 64 x 64 の単色（白）の画像。権利はこのリポジトリと同じ MIT。外部の素材は使わない。
#   - 白で描いてあるので、UI のブラシの色（乗算）で、好きな色にして使う。
#   - 形式は PNG（バックエンドに依存しない）。名前と検索用の言葉は、Icons/manifest.json に書く。
#   - 描き直すと、同じ内容の PNG になる（乱数を使わない）。
param(
    [string]$Destination = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$frameworkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($Destination -eq '') { $Destination = Join-Path $frameworkRoot 'EngineContent' }
$iconDirectory = Join-Path $Destination 'Icons'
New-Item -ItemType Directory -Force -Path $iconDirectory | Out-Null

$white = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
$brush = New-Object System.Drawing.SolidBrush($white)
$pen = New-Object System.Drawing.Pen($white, 5)
$pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
$thinPen = New-Object System.Drawing.Pen($white, 3)
$thinPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$thinPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$thinPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round

function P([double]$X, [double]$Y) { return New-Object System.Drawing.PointF([single]$X, [single]$Y) }
function Pts([double[]]$Values) {
    $list = New-Object System.Collections.Generic.List[System.Drawing.PointF]
    for ($i = 0; $i -lt $Values.Length; $i += 2) { $list.Add((P $Values[$i] $Values[$i + 1])) }
    return $list.ToArray()
}
function Stroke-Lines($G, [double[]]$Values) { $G.DrawLines($pen, (Pts $Values)) }
function Fill-Poly($G, [double[]]$Values) { $G.FillPolygon($brush, (Pts $Values)) }
function Stroke-Poly($G, [double[]]$Values) { $G.DrawPolygon($pen, (Pts $Values)) }
function Stroke-Circle($G, [double]$Cx, [double]$Cy, [double]$R) { $G.DrawEllipse($pen, [single]($Cx - $R), [single]($Cy - $R), [single]($R * 2), [single]($R * 2)) }
function Fill-Circle($G, [double]$Cx, [double]$Cy, [double]$R) { $G.FillEllipse($brush, [single]($Cx - $R), [single]($Cy - $R), [single]($R * 2), [single]($R * 2)) }
function Stroke-Rect($G, [double]$X, [double]$Y, [double]$W, [double]$H, [double]$R) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = [Math]::Min($R * 2, [Math]::Min($W, $H))
    if ($d -le 0) { $path.AddRectangle((New-Object System.Drawing.RectangleF([single]$X, [single]$Y, [single]$W, [single]$H))) }
    else {
        $path.AddArc($X, $Y, $d, $d, 180, 90); $path.AddArc($X + $W - $d, $Y, $d, $d, 270, 90)
        $path.AddArc($X + $W - $d, $Y + $H - $d, $d, $d, 0, 90); $path.AddArc($X, $Y + $H - $d, $d, $d, 90, 90); $path.CloseFigure()
    }
    $G.DrawPath($pen, $path)
    return $path
}
function Fill-Rect($G, [double]$X, [double]$Y, [double]$W, [double]$H, [double]$R) {
    $path = Stroke-Rect $G $X $Y $W $H $R
    $G.FillPath($brush, $path)
}
function Draw-Text($G, [string]$Text, [double]$Size) {
    $font = New-Object System.Drawing.Font('Segoe UI', [single]$Size, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $G.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $G.DrawString($Text, $font, $brush, (New-Object System.Drawing.RectangleF(0, 2, 64, 64)), $format)
}
function Clear-Circle($G, [double]$Cx, [double]$Cy, [double]$R) {
    $G.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $G.FillEllipse((New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(0, 0, 0, 0))), [single]($Cx - $R), [single]($Cy - $R), [single]($R * 2), [single]($R * 2))
    $G.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
}
function Arc-Points([double]$Cx, [double]$Cy, [double]$R, [double]$FromDeg, [double]$ToDeg, [int]$Steps) {
    $values = New-Object System.Collections.Generic.List[double]
    for ($i = 0; $i -le $Steps; ++$i) {
        $a = ($FromDeg + ($ToDeg - $FromDeg) * $i / $Steps) * [Math]::PI / 180
        $values.Add($Cx + $R * [Math]::Cos($a)); $values.Add($Cy + $R * [Math]::Sin($a))
    }
    return $values.ToArray()
}

$names = [ordered]@{}
function Icon([string]$Name, [string]$Tags, [scriptblock]$Draw, [string]$Rotate = '') {
    $bitmap = New-Object System.Drawing.Bitmap(64, 64, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
    & $Draw $g
    $g.Dispose()
    if ($Rotate -eq '90') { $bitmap.RotateFlip([System.Drawing.RotateFlipType]::Rotate90FlipNone) }
    if ($Rotate -eq '180') { $bitmap.RotateFlip([System.Drawing.RotateFlipType]::Rotate180FlipNone) }
    if ($Rotate -eq '270') { $bitmap.RotateFlip([System.Drawing.RotateFlipType]::Rotate270FlipNone) }
    $bitmap.Save((Join-Path $iconDirectory "$Name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    $script:names[$Name] = $Tags
}

# ---- 矢印・シェブロン（右向きを描いて、回して 4 方向にする）
$arrowRight = { param($g) Stroke-Lines $g @(10, 32, 54, 32); Stroke-Lines $g @(38, 16, 54, 32, 38, 48) }
$chevronRight = { param($g) Stroke-Lines $g @(22, 12, 42, 32, 22, 52) }
Icon 'ArrowRight' 'arrow right next forward' $arrowRight
Icon 'ArrowDown' 'arrow down' $arrowRight '90'
Icon 'ArrowLeft' 'arrow left back previous' $arrowRight '180'
Icon 'ArrowUp' 'arrow up' $arrowRight '270'
Icon 'ChevronRight' 'chevron right expand' $chevronRight
Icon 'ChevronDown' 'chevron down expand dropdown' $chevronRight '90'
Icon 'ChevronLeft' 'chevron left collapse' $chevronRight '180'
Icon 'ChevronUp' 'chevron up collapse' $chevronRight '270'

# ---- 基本操作
Icon 'Close' 'close cancel x delete' { param($g) Stroke-Lines $g @(14, 14, 50, 50); Stroke-Lines $g @(50, 14, 14, 50) }
Icon 'Plus' 'plus add new' { param($g) Stroke-Lines $g @(32, 12, 32, 52); Stroke-Lines $g @(12, 32, 52, 32) }
Icon 'Minus' 'minus remove subtract' { param($g) Stroke-Lines $g @(12, 32, 52, 32) }
Icon 'Check' 'check ok done confirm' { param($g) Stroke-Lines $g @(12, 34, 26, 48, 52, 16) }
Icon 'Menu' 'menu list hamburger' { param($g) Stroke-Lines $g @(12, 16, 52, 16); Stroke-Lines $g @(12, 32, 52, 32); Stroke-Lines $g @(12, 48, 52, 48) }
Icon 'More' 'more dots options' { param($g) Fill-Circle $g 14 32 5; Fill-Circle $g 32 32 5; Fill-Circle $g 50 32 5 }
Icon 'Search' 'search find zoom' { param($g) Stroke-Circle $g 28 28 16; Stroke-Lines $g @(40, 40, 54, 54) }
Icon 'Settings' 'settings gear options config' {
    param($g)
    $values = New-Object System.Collections.Generic.List[double]
    for ($i = 0; $i -lt 8; ++$i) {
        $a = $i * 45.0
        foreach ($pair in @(@(($a - 11), 20), @(($a - 7), 28), @(($a + 7), 28), @(($a + 11), 20))) {
            $r = [double]$pair[0] * [Math]::PI / 180
            $values.Add(32 + $pair[1] * [Math]::Cos($r)); $values.Add(32 + $pair[1] * [Math]::Sin($r))
        }
    }
    Fill-Poly $g $values.ToArray()
    Fill-Circle $g 32 32 21
    Clear-Circle $g 32 32 9
}
Icon 'Home' 'home house start' { param($g) Stroke-Lines $g @(8, 32, 32, 10, 56, 32); Stroke-Lines $g @(14, 28, 14, 54, 50, 54, 50, 28); Stroke-Lines $g @(27, 54, 27, 38, 37, 38, 37, 54) }
Icon 'Refresh' 'refresh reload retry' {
    param($g)
    $g.DrawLines($pen, (Pts (Arc-Points 32 32 20 -60 220 24)))
    Fill-Poly $g @(10, 8, 28, 10, 20, 26)
}
Icon 'Download' 'download save arrow' { param($g) Stroke-Lines $g @(32, 8, 32, 40); Stroke-Lines $g @(18, 28, 32, 42, 46, 28); Stroke-Lines $g @(10, 54, 54, 54) }
Icon 'Upload' 'upload send arrow' { param($g) Stroke-Lines $g @(32, 42, 32, 10); Stroke-Lines $g @(18, 24, 32, 10, 46, 24); Stroke-Lines $g @(10, 54, 54, 54) }
Icon 'Edit' 'edit pencil write rename' { param($g) Stroke-Poly $g @(12, 52, 14, 40, 42, 12, 52, 22, 24, 50); Stroke-Lines $g @(36, 18, 46, 28) }
Icon 'Copy' 'copy duplicate' { param($g) $null = Stroke-Rect $g 22 22 30 32 4; Stroke-Lines $g @(16, 42, 12, 42, 12, 10, 40, 10, 40, 14) }
Icon 'Trash' 'trash delete remove bin' { param($g) Stroke-Lines $g @(10, 16, 54, 16); Stroke-Lines $g @(24, 16, 24, 8, 40, 8, 40, 16); Stroke-Lines $g @(16, 16, 19, 54, 45, 54, 48, 16); Stroke-Lines $g @(28, 26, 28, 44); Stroke-Lines $g @(36, 26, 36, 44) }
Icon 'Save' 'save disk floppy' { param($g) $null = Stroke-Rect $g 10 10 44 44 5; Stroke-Lines $g @(20, 10, 20, 24, 42, 24, 42, 10); Stroke-Lines $g @(20, 54, 20, 38, 44, 38, 44, 54) }
Icon 'Folder' 'folder open directory' { param($g) Stroke-Lines $g @(8, 52, 8, 14, 24, 14, 30, 22, 56, 22, 56, 52, 8, 52) }
Icon 'File' 'file document page' { param($g) Stroke-Lines $g @(14, 8, 38, 8, 52, 22, 52, 56, 14, 56, 14, 8); Stroke-Lines $g @(38, 8, 38, 22, 52, 22) }
Icon 'Link' 'link chain url' { param($g) $null = Stroke-Rect $g 6 24 28 16 8; $null = Stroke-Rect $g 30 24 28 16 8 }
Icon 'Pin' 'pin location marker map' { param($g) Fill-Circle $g 32 26 16; Clear-Circle $g 32 26 6; Fill-Poly $g @(18, 34, 32, 58, 46, 34) }
Icon 'Lock' 'lock locked secure' { param($g) Fill-Rect $g 14 28 36 28 5; $g.DrawArc($pen, 20, 8, 24, 28, 180, 180) }
Icon 'Unlock' 'unlock unlocked open' { param($g) Fill-Rect $g 14 28 36 28 5; $g.DrawArc($pen, 20, 8, 24, 28, 180, 120) }
Icon 'Eye' 'eye view visible show' { param($g) $g.DrawLines($pen, (Pts (Arc-Points 32 52 40 -135 -45 12))); $g.DrawLines($pen, (Pts (Arc-Points 32 12 40 45 135 12))); Fill-Circle $g 32 32 9 }
Icon 'EyeClosed' 'eye closed hidden hide sleep blink' {
    param($g)
    $g.DrawLines($pen, (Pts (Arc-Points 32 6 40 45 135 12)))
    foreach ($deg in @(60, 90, 120)) {
        $r = $deg * [Math]::PI / 180
        Stroke-Lines $g @((32 + 40 * [Math]::Cos($r)), (6 + 40 * [Math]::Sin($r)), (32 + 50 * [Math]::Cos($r)), (6 + 50 * [Math]::Sin($r)))
    }
}
Icon 'EyeOff' 'eye off hidden hide invisible slash' { param($g) $g.DrawLines($pen, (Pts (Arc-Points 32 52 40 -135 -45 12))); $g.DrawLines($pen, (Pts (Arc-Points 32 12 40 45 135 12))); Fill-Circle $g 32 32 9; Stroke-Lines $g @(12, 8, 52, 56) }
Icon 'Bell' 'bell notification alert' { param($g) Stroke-Lines $g @(12, 46, 16, 40, 16, 28); $g.DrawArc($pen, 16, 8, 32, 40, 180, 180); Stroke-Lines $g @(48, 28, 48, 40, 52, 46, 12, 46); Fill-Circle $g 32 54 5 }
Icon 'Mail' 'mail envelope message' { param($g) $null = Stroke-Rect $g 8 14 48 36 5; Stroke-Lines $g @(10, 18, 32, 36, 54, 18) }
Icon 'User' 'user person profile account' { param($g) Fill-Circle $g 32 20 12; Stroke-Lines $g @(10, 56, 12, 44, 52, 44, 54, 56) }
Icon 'Clock' 'clock time wait' { param($g) Stroke-Circle $g 32 32 22; Stroke-Lines $g @(32, 18, 32, 32, 42, 38) }
Icon 'Flag' 'flag goal mark' { param($g) Stroke-Lines $g @(14, 56, 14, 8); Fill-Poly $g @(16, 10, 52, 10, 42, 24, 52, 38, 16, 38) }
Icon 'Star' 'star favorite rating' {
    param($g)
    $values = New-Object System.Collections.Generic.List[double]
    for ($i = 0; $i -lt 10; ++$i) { $r = if ($i % 2 -eq 0) { 27 } else { 11.5 }; $a = (-90 + $i * 36) * [Math]::PI / 180; $values.Add(32 + $r * [Math]::Cos($a)); $values.Add(34 + $r * [Math]::Sin($a)) }
    Fill-Poly $g $values.ToArray()
}
Icon 'Heart' 'heart like life health' {
    param($g)
    $values = New-Object System.Collections.Generic.List[double]
    for ($i = 0; $i -le 48; ++$i) { $t = $i * 2 * [Math]::PI / 48; $values.Add(32 + 1.55 * 16 * [Math]::Pow([Math]::Sin($t), 3) / 1.0 * 0.95); $values.Add(30 - (13 * [Math]::Cos($t) - 5 * [Math]::Cos(2 * $t) - 2 * [Math]::Cos(3 * $t) - [Math]::Cos(4 * $t)) * 1.55) }
    Fill-Poly $g $values.ToArray()
}

# ---- 状態
Icon 'Info' 'info information help' { param($g) Stroke-Circle $g 32 32 24; Fill-Circle $g 32 21 3.5; Stroke-Lines $g @(32, 29, 32, 45) }
Icon 'Warning' 'warning caution alert' { param($g) Stroke-Poly $g @(32, 8, 58, 54, 6, 54); Stroke-Lines $g @(32, 24, 32, 38); Fill-Circle $g 32 46 3 }
Icon 'Error' 'error failure stop' { param($g) Stroke-Circle $g 32 32 24; Stroke-Lines $g @(22, 22, 42, 42); Stroke-Lines $g @(42, 22, 22, 42) }
Icon 'Success' 'success done complete ok' { param($g) Stroke-Circle $g 32 32 24; Stroke-Lines $g @(21, 33, 29, 41, 44, 24) }
Icon 'Question' 'question help unknown' { param($g) Stroke-Circle $g 32 32 24; Draw-Text $g '?' 34 }

# ---- メディア・音
Icon 'Play' 'play start run' { param($g) Fill-Poly $g @(18, 10, 54, 32, 18, 54) }
Icon 'Pause' 'pause hold' { param($g) Fill-Rect $g 16 12 12 40 2; Fill-Rect $g 36 12 12 40 2 }
Icon 'Stop' 'stop halt' { param($g) Fill-Rect $g 14 14 36 36 4 }
Icon 'Next' 'next skip forward' { param($g) Fill-Poly $g @(10, 12, 40, 32, 10, 52); Fill-Rect $g 44 12 8 40 2 }
Icon 'Previous' 'previous skip back' { param($g) Fill-Poly $g @(54, 12, 24, 32, 54, 52); Fill-Rect $g 12 12 8 40 2 }
Icon 'Record' 'record rec' { param($g) Fill-Circle $g 32 32 20 }
Icon 'Volume' 'volume sound speaker audio' { param($g) Fill-Poly $g @(8, 24, 20, 24, 34, 12, 34, 52, 20, 40, 8, 40); $g.DrawArc($pen, 34, 20, 14, 24, -60, 120); $g.DrawArc($pen, 34, 10, 26, 44, -60, 120) }
Icon 'Mute' 'mute silent no sound' { param($g) Fill-Poly $g @(8, 24, 20, 24, 34, 12, 34, 52, 20, 40, 8, 40); Stroke-Lines $g @(42, 24, 58, 40); Stroke-Lines $g @(58, 24, 42, 40) }
Icon 'Music' 'music note song bgm' { param($g) Stroke-Lines $g @(24, 46, 24, 12, 50, 8, 50, 42); Fill-Circle $g 18 46 7; Fill-Circle $g 44 42 7 }
Icon 'Mic' 'microphone voice record' { param($g) $null = Stroke-Rect $g 22 8 20 30 10; $g.DrawArc($pen, 14, 22, 36, 28, 0, 180); Stroke-Lines $g @(32, 50, 32, 58) }

# ---- ゲーム
Icon 'Sword' 'sword attack weapon' { param($g) Stroke-Lines $g @(12, 52, 22, 42); Stroke-Lines $g @(16, 34, 30, 48); Stroke-Lines $g @(24, 40, 54, 10, 54, 20, 30, 44); Fill-Circle $g 12 52 4 }
Icon 'Shield' 'shield defense guard armor' { param($g) Stroke-Lines $g @(32, 8, 54, 14, 54, 32, 32, 56, 10, 32, 10, 14, 32, 8); Stroke-Lines $g @(32, 8, 32, 56) }
Icon 'Potion' 'potion bottle heal item' { param($g) Stroke-Lines $g @(26, 8, 38, 8); Stroke-Lines $g @(28, 8, 28, 24, 12, 50, 18, 56, 46, 56, 52, 50, 36, 24, 36, 8); Stroke-Lines $g @(20, 42, 44, 42) }
Icon 'Key' 'key unlock item' { param($g) Stroke-Circle $g 20 20 11; Stroke-Lines $g @(28, 28, 54, 54); Stroke-Lines $g @(44, 44, 50, 38); Stroke-Lines $g @(50, 50, 56, 44) }
Icon 'Coin' 'coin money gold currency' { param($g) Stroke-Circle $g 32 32 24; Stroke-Circle $g 32 32 15; Draw-Text $g '$' 26 }
Icon 'Gem' 'gem jewel crystal diamond' { param($g) Stroke-Poly $g @(18, 12, 46, 12, 58, 28, 32, 56, 6, 28); Stroke-Lines $g @(6, 28, 58, 28); Stroke-Lines $g @(24, 28, 32, 56, 40, 28) }
Icon 'Bag' 'bag inventory backpack items' { param($g) $null = Stroke-Rect $g 12 22 40 34 8; Stroke-Lines $g @(22, 22, 22, 14, 42, 14, 42, 22); Stroke-Lines $g @(12, 38, 52, 38) }
Icon 'Map' 'map world navigate' { param($g) Stroke-Poly $g @(8, 14, 24, 8, 40, 14, 56, 8, 56, 50, 40, 56, 24, 50, 8, 56); Stroke-Lines $g @(24, 8, 24, 50); Stroke-Lines $g @(40, 14, 40, 56) }
Icon 'Chest' 'chest treasure box loot' { param($g) $g.DrawArc($pen, 8, 10, 48, 36, 180, 180); Stroke-Lines $g @(8, 28, 8, 54, 56, 54, 56, 28); Stroke-Lines $g @(8, 32, 56, 32); Fill-Rect $g 28 28 8 10 2 }
Icon 'Skull' 'skull death danger' { param($g) $g.DrawArc($pen, 10, 6, 44, 40, 150, 240); Stroke-Lines $g @(15, 38, 20, 40, 20, 54, 44, 54, 44, 40, 49, 38); Fill-Circle $g 23 28 6; Fill-Circle $g 41 28 6; Stroke-Lines $g @(32, 42, 32, 54) }
Icon 'Bolt' 'bolt lightning energy power' { param($g) Fill-Poly $g @(36, 6, 14, 36, 28, 36, 24, 58, 50, 24, 34, 24) }
Icon 'Flame' 'flame fire burn' { param($g) Fill-Poly $g @(32, 6, 44, 22, 52, 36, 50, 50, 40, 58, 24, 58, 14, 50, 12, 36, 22, 26, 26, 36, 30, 22) }
Icon 'Drop' 'drop water liquid' { param($g) Fill-Poly $g @(32, 6, 46, 28, 52, 40, 48, 52, 38, 58, 26, 58, 16, 52, 12, 40, 18, 28) }
Icon 'Leaf' 'leaf nature plant' { param($g) Fill-Poly $g @(10, 54, 12, 30, 24, 14, 54, 10, 52, 40, 36, 54, 16, 56); Stroke-Lines $g @(10, 56, 36, 30) }
Icon 'Crown' 'crown king rank' { param($g) Fill-Poly $g @(8, 46, 8, 18, 22, 32, 32, 12, 42, 32, 56, 18, 56, 46); Fill-Rect $g 8 50 48 6 2 }
Icon 'Target' 'target aim crosshair' { param($g) Stroke-Circle $g 32 32 24; Stroke-Circle $g 32 32 12; Fill-Circle $g 32 32 3; Stroke-Lines $g @(32, 2, 32, 12); Stroke-Lines $g @(32, 52, 32, 62); Stroke-Lines $g @(2, 32, 12, 32); Stroke-Lines $g @(52, 32, 62, 32) }
Icon 'Gamepad' 'gamepad controller input' { param($g) $null = Stroke-Rect $g 4 18 56 30 14; Stroke-Lines $g @(18, 26, 18, 40); Stroke-Lines $g @(11, 33, 25, 33); Fill-Circle $g 42 29 3.5; Fill-Circle $g 50 37 3.5 }
Icon 'Trophy' 'trophy award win achievement' { param($g) Stroke-Lines $g @(16, 8, 48, 8, 48, 24); $g.DrawArc($pen, 16, 6, 32, 38, 0, 180); Stroke-Lines $g @(16, 8, 16, 24); Stroke-Lines $g @(32, 44, 32, 54); Stroke-Lines $g @(20, 56, 44, 56); Stroke-Lines $g @(16, 14, 8, 14, 10, 24, 18, 26); Stroke-Lines $g @(48, 14, 56, 14, 54, 24, 46, 26) }

# 名前と検索用の言葉の一覧
$manifest = [ordered]@{
    Description = 'Engine UI icons. 64x64, white, meant to be tinted by the brush color. Tags are search words.'
    Icons = @()
}
foreach ($key in $names.Keys) { $manifest.Icons += [ordered]@{ Name = $key; File = "$key.png"; Tags = $names[$key] } }
$json = $manifest | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText((Join-Path $iconDirectory 'manifest.json'), ($json -replace "`r?`n", "`r`n"), (New-Object System.Text.UTF8Encoding($true)))
Write-Host "Generated $($names.Count) icon(s) in $iconDirectory"
