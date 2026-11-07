# エンジン組み込みの素材（EngineContent）のうち、画像を書き出す。生成した PNG をコミットして使う（ゲームの実行時には、このスクリプトは要らない）。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\GenerateEngineContent.ps1 [-Destination EngineContent]
#
#   - すべて、このスクリプトで描いた画像（権利はこのリポジトリと同じ MIT）。外部の素材は使わない。
#   - 形式は PNG（バックエンドに依存しない）。9 分割で使う画像の余白は、UI/manifest.json に書く。
#   - 描き直すと、同じ内容の PNG になる（乱数を使わない）。
param(
    [string]$Destination = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$frameworkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($Destination -eq '') { $Destination = Join-Path $frameworkRoot 'EngineContent' }
$uiDirectory = Join-Path $Destination 'UI'
New-Item -ItemType Directory -Force -Path $uiDirectory | Out-Null

function New-Canvas([int]$Width, [int]$Height) {
    $bitmap = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
    return @{ Bitmap = $bitmap; Graphics = $graphics }
}

function Get-RoundedPath([float]$X, [float]$Y, [float]$Width, [float]$Height, [float]$Radius) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = [Math]::Min($Radius * 2, [Math]::Min($Width, $Height))
    if ($diameter -le 0) {
        $path.AddRectangle((New-Object System.Drawing.RectangleF($X, $Y, $Width, $Height)))
        return $path
    }
    $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
    $path.AddArc($X + $Width - $diameter, $Y, $diameter, $diameter, 270, 90)
    $path.AddArc($X + $Width - $diameter, $Y + $Height - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($X, $Y + $Height - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function Save-Canvas($Canvas, [string]$Name) {
    $Canvas.Graphics.Dispose()
    $Canvas.Bitmap.Save((Join-Path $uiDirectory $Name), [System.Drawing.Imaging.ImageFormat]::Png)
    $Canvas.Bitmap.Dispose()
}

function Get-Color([int]$A, [int]$R, [int]$G, [int]$B) { return [System.Drawing.Color]::FromArgb($A, $R, $G, $B) }

# 角丸の面（塗り + 枠 + 上の薄いハイライト）。余白 Radius の 9 分割で使う
function New-Box([string]$Name, [int]$Size, [float]$Radius, $Fill, $Border, [float]$BorderWidth, [bool]$Highlight) {
    $canvas = New-Canvas $Size $Size
    $g = $canvas.Graphics
    $inset = $BorderWidth / 2.0
    $path = Get-RoundedPath $inset $inset ($Size - $BorderWidth) ($Size - $BorderWidth) $Radius
    $g.FillPath((New-Object System.Drawing.SolidBrush($Fill)), $path)
    if ($Highlight) {
        $g.SetClip($path)
        $highlightBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush((New-Object System.Drawing.RectangleF(0, 0, $Size, ($Size / 2.0))), (Get-Color 70 255 255 255), (Get-Color 0 255 255 255), 90.0)
        $g.FillRectangle($highlightBrush, 0, 0, $Size, ($Size / 2.0))
        $g.ResetClip()
    }
    if ($BorderWidth -gt 0) {
        $g.DrawPath((New-Object System.Drawing.Pen($Border, $BorderWidth)), $path)
    }
    Save-Canvas $canvas $Name
}

# 窓・パネル・枠（9 分割。64 x 64、角の余白 14）
New-Box 'Window.png' 64 12 (Get-Color 235 36 40 52) (Get-Color 255 120 132 160) 2 $true
New-Box 'Panel.png' 64 10 (Get-Color 210 28 32 42) (Get-Color 160 90 100 125) 1 $false
New-Box 'Frame.png' 64 10 (Get-Color 0 0 0 0) (Get-Color 255 150 162 190) 2 $false
New-Box 'Tooltip.png' 48 8 (Get-Color 245 18 20 28) (Get-Color 255 200 205 220) 1 $false

# ボタン（9 分割。48 x 48、余白 12）の 4 つの状態
New-Box 'Button_Normal.png' 48 10 (Get-Color 255 62 74 104) (Get-Color 255 130 148 190) 2 $true
New-Box 'Button_Hover.png' 48 10 (Get-Color 255 84 100 142) (Get-Color 255 170 188 235) 2 $true
New-Box 'Button_Pressed.png' 48 10 (Get-Color 255 42 52 78) (Get-Color 255 110 126 170) 2 $false
New-Box 'Button_Disabled.png' 48 10 (Get-Color 200 52 54 62) (Get-Color 200 90 92 100) 2 $false

# 入力欄
New-Box 'TextBox.png' 40 6 (Get-Color 255 16 18 26) (Get-Color 255 100 110 140) 2 $false
New-Box 'TextBox_Focused.png' 40 6 (Get-Color 255 16 18 26) (Get-Color 255 120 190 255) 2 $false

# 影（9 分割。ぼかした黒の角丸。窓の下に敷く）
$shadow = New-Canvas 64 64
$shadowGraphics = $shadow.Graphics
for ($i = 0; $i -lt 14; ++$i) {
    $alpha = [int](8 + $i * 3)
    $path = Get-RoundedPath $i $i (64 - $i * 2) (64 - $i * 2) (24 - $i)
    $shadowGraphics.FillPath((New-Object System.Drawing.SolidBrush((Get-Color $alpha 0 0 0))), $path)
}
Save-Canvas $shadow 'Shadow.png'

# チェックボックス・ラジオ（24 x 24）
function New-Check([string]$Name, [bool]$On, [bool]$Radio) {
    $canvas = New-Canvas 24 24
    $g = $canvas.Graphics
    if ($Radio) {
        $g.FillEllipse((New-Object System.Drawing.SolidBrush((Get-Color 255 16 18 26))), 2, 2, 20, 20)
        $g.DrawEllipse((New-Object System.Drawing.Pen((Get-Color 255 150 162 190), 2)), 2, 2, 20, 20)
        if ($On) { $g.FillEllipse((New-Object System.Drawing.SolidBrush((Get-Color 255 120 190 255))), 7, 7, 10, 10) }
    } else {
        $path = Get-RoundedPath 2 2 20 20 4
        $g.FillPath((New-Object System.Drawing.SolidBrush((Get-Color 255 16 18 26))), $path)
        $g.DrawPath((New-Object System.Drawing.Pen((Get-Color 255 150 162 190), 2)), $path)
        if ($On) {
            $pen = New-Object System.Drawing.Pen((Get-Color 255 120 190 255), 3)
            $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            $g.DrawLines($pen, @((New-Object System.Drawing.PointF(6, 12.5)), (New-Object System.Drawing.PointF(10.5, 17)), (New-Object System.Drawing.PointF(18, 7))))
        }
    }
    Save-Canvas $canvas $Name
}
New-Check 'CheckBox_Off.png' $false $false
New-Check 'CheckBox_On.png' $true $false
New-Check 'Radio_Off.png' $false $true
New-Check 'Radio_On.png' $true $true

# スライダーの溝（9 分割。左右の余白 8、32 x 12）とつまみ（20 x 20）
$track = New-Canvas 32 12
$trackPath = Get-RoundedPath 1 2 30 8 4
$track.Graphics.FillPath((New-Object System.Drawing.SolidBrush((Get-Color 255 16 18 26))), $trackPath)
$track.Graphics.DrawPath((New-Object System.Drawing.Pen((Get-Color 255 100 110 140), 1)), $trackPath)
Save-Canvas $track 'Slider_Track.png'

$handle = New-Canvas 20 20
$handle.Graphics.FillEllipse((New-Object System.Drawing.SolidBrush((Get-Color 255 210 220 245))), 2, 2, 16, 16)
$handle.Graphics.DrawEllipse((New-Object System.Drawing.Pen((Get-Color 255 120 190 255), 2)), 2, 2, 16, 16)
Save-Canvas $handle 'Slider_Handle.png'

# プログレスバー（9 分割。32 x 16、左右の余白 7）
$barBack = New-Canvas 32 16
$barBackPath = Get-RoundedPath 1 1 30 14 7
$barBack.Graphics.FillPath((New-Object System.Drawing.SolidBrush((Get-Color 255 16 18 26))), $barBackPath)
$barBack.Graphics.DrawPath((New-Object System.Drawing.Pen((Get-Color 255 100 110 140), 1)), $barBackPath)
Save-Canvas $barBack 'ProgressBar_Back.png'

$barFill = New-Canvas 32 16
$barFillPath = Get-RoundedPath 2 2 28 12 6
$barFill.Graphics.FillPath((New-Object System.Drawing.Drawing2D.LinearGradientBrush((New-Object System.Drawing.RectangleF(0, 2, 32, 12)), (Get-Color 255 140 210 255), (Get-Color 255 70 140 230), 90.0)), $barFillPath)
Save-Canvas $barFill 'ProgressBar_Fill.png'

# 矢印（右向き 16 x 16。回して使う）と、閉じる印（16 x 16）、丸（白。32 x 32。色を掛けて使う）、角丸の白（9 分割。32 x 32、余白 10）
$arrow = New-Canvas 16 16
$arrowPen = New-Object System.Drawing.Pen((Get-Color 255 230 236 250), 2.5)
$arrowPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$arrowPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$arrowPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
$arrow.Graphics.DrawLines($arrowPen, @((New-Object System.Drawing.PointF(5, 3)), (New-Object System.Drawing.PointF(11, 8)), (New-Object System.Drawing.PointF(5, 13))))
Save-Canvas $arrow 'Arrow.png'

$close = New-Canvas 16 16
$close.Graphics.DrawLine($arrowPen, 4, 4, 12, 12)
$close.Graphics.DrawLine($arrowPen, 12, 4, 4, 12)
Save-Canvas $close 'Close.png'

$circle = New-Canvas 32 32
$circle.Graphics.FillEllipse((New-Object System.Drawing.SolidBrush((Get-Color 255 255 255 255))), 1, 1, 30, 30)
Save-Canvas $circle 'Circle.png'

New-Box 'RoundedWhite.png' 32 10 (Get-Color 255 255 255 255) (Get-Color 0 0 0 0) 0 $false

# 9 分割の余白の一覧（左・上・右・下。画素）。UI のブラシが、この表から余白を読む
$manifest = [ordered]@{
    Description = 'Engine UI images. NineSlice margins are Left, Top, Right, Bottom in pixels.'
    Images = [ordered]@{
        'Window.png'          = @{ NineSlice = @(14, 14, 14, 14) }
        'Panel.png'           = @{ NineSlice = @(12, 12, 12, 12) }
        'Frame.png'           = @{ NineSlice = @(12, 12, 12, 12) }
        'Tooltip.png'         = @{ NineSlice = @(10, 10, 10, 10) }
        'Button_Normal.png'   = @{ NineSlice = @(12, 12, 12, 12) }
        'Button_Hover.png'    = @{ NineSlice = @(12, 12, 12, 12) }
        'Button_Pressed.png'  = @{ NineSlice = @(12, 12, 12, 12) }
        'Button_Disabled.png' = @{ NineSlice = @(12, 12, 12, 12) }
        'TextBox.png'         = @{ NineSlice = @(8, 8, 8, 8) }
        'TextBox_Focused.png' = @{ NineSlice = @(8, 8, 8, 8) }
        'Shadow.png'          = @{ NineSlice = @(24, 24, 24, 24) }
        'Slider_Track.png'    = @{ NineSlice = @(8, 0, 8, 0) }
        'ProgressBar_Back.png' = @{ NineSlice = @(8, 0, 8, 0) }
        'ProgressBar_Fill.png' = @{ NineSlice = @(8, 0, 8, 0) }
        'RoundedWhite.png'    = @{ NineSlice = @(11, 11, 11, 11) }
        'CheckBox_Off.png'    = @{}
        'CheckBox_On.png'     = @{}
        'Radio_Off.png'       = @{}
        'Radio_On.png'        = @{}
        'Slider_Handle.png'   = @{}
        'Arrow.png'           = @{}
        'Close.png'           = @{}
        'Circle.png'          = @{}
    }
}
$json = $manifest | ConvertTo-Json -Depth 6 -Compress:$false
[IO.File]::WriteAllText((Join-Path $uiDirectory 'manifest.json'), ($json -replace "`r?`n", "`r`n"), (New-Object System.Text.UTF8Encoding($true)))

Write-Host "Generated $((Get-ChildItem $uiDirectory -Filter *.png).Count) image(s) in $uiDirectory"
