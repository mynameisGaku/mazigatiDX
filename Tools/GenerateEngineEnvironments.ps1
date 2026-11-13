# エンジン組み込みの素材（EngineContent）のうち、環境マップの元になる、全周の絵を書き出す。生成した PNG をコミットして使う（ゲームの実行時には、このスクリプトは要らない）。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\GenerateEngineEnvironments.ps1 [-Destination EngineContent]
#
#   - すべて、このスクリプトで計算した絵（権利はこのリポジトリと同じ MIT）。外部の素材は使わない。
#   - 絵は、正距円筒図法（幅が高さの 2 倍。上端が真上、絵の中央が +Z の向き。CMaterialEnvironmentMap が読む形）。
#   - 描き直すと、同じ内容の PNG になる（乱数を使わない）。
param(
    [string]$Destination = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$frameworkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($Destination -eq '') { $Destination = Join-Path $frameworkRoot 'EngineContent' }
$directory = Join-Path $Destination 'Environments'
New-Item -ItemType Directory -Force -Path $directory | Out-Null

function Mix([double]$A, [double]$B, [double]$Alpha) { return $A + ($B - $A) * $Alpha }
function Saturate([double]$Value) { return [Math]::Min(1.0, [Math]::Max(0.0, $Value)) }
function SmoothStep([double]$Low, [double]$High, [double]$Value)
{
    $t = Saturate (($Value - $Low) / ($High - $Low))
    return $t * $t * (3.0 - 2.0 * $t)
}

# 晴れた昼の空: 天頂は青、地平線は白っぽく、地面は茶色がかった灰色。太陽は、マテリアルの既定の平行光の向き（光が来る側）に置く
function Write-Sky([string]$FileName, [int]$Width, [int]$Height)
{
    $sun = @(0.4969, 0.7951, 0.3478)
    $bytes = New-Object byte[] ($Width * $Height * 4)
    for ($y = 0; $y -lt $Height; $y++)
    {
        $polar = ($y + 0.5) / $Height * [Math]::PI
        $up = [Math]::Cos($polar)
        $ring = [Math]::Sin($polar)
        for ($x = 0; $x -lt $Width; $x++)
        {
            $azimuth = (($x + 0.5) / $Width - 0.5) * 2.0 * [Math]::PI
            $dx = [Math]::Sin($azimuth) * $ring
            $dz = [Math]::Cos($azimuth) * $ring
            if ($up -ge 0.0)
            {
                # 空: 地平線から天頂へ、白 → 青
                # PowerShell の変数名は、大文字と小文字を区別しない（$height は、引数の $Height と同じ）ので、別の名前にする
                $rise = [Math]::Pow($up, 0.55)
                $r = Mix 0.80 0.16 $rise
                $g = Mix 0.86 0.38 $rise
                $b = Mix 0.94 0.80 $rise
                # うすい雲の帯（方位と高さの、なめらかな波）
                $cloud = 0.5 + 0.5 * [Math]::Sin($azimuth * 3.0 + [Math]::Sin($azimuth * 2.0 + 1.3) * 1.5 + $up * 9.0)
                $cloud = (SmoothStep 0.62 0.95 $cloud) * (SmoothStep 0.05 0.35 $up) * (1.0 - (SmoothStep 0.75 1.0 $up)) * 0.45
                $r = Mix $r 0.97 $cloud
                $g = Mix $g 0.97 $cloud
                $b = Mix $b 0.98 $cloud
            }
            else
            {
                # 地面: 地平線の近くは明るく、真下へ暗く
                $depth = [Math]::Pow(-$up, 0.6)
                $r = Mix 0.52 0.20 $depth
                $g = Mix 0.50 0.18 $depth
                $b = Mix 0.47 0.16 $depth
            }
            # 地平線のかすみ
            $haze = 1.0 - (SmoothStep 0.0 0.12 ([Math]::Abs($up)))
            $r = Mix $r 0.78 ($haze * 0.6)
            $g = Mix $g 0.80 ($haze * 0.6)
            $b = Mix $b 0.82 ($haze * 0.6)
            # 太陽: 小さな円と、そのまわりの光
            $toSun = $dx * $sun[0] + $up * $sun[1] + $dz * $sun[2]
            $disc = SmoothStep 0.9965 0.9985 $toSun
            $glow = [Math]::Pow((Saturate $toSun), 48.0) * 0.55
            $r = Saturate ((Mix $r 1.0 $disc) + $glow)
            $g = Saturate ((Mix $g 0.98 $disc) + $glow * 0.92)
            $b = Saturate ((Mix $b 0.90 $disc) + $glow * 0.75)
            $index = ($y * $Width + $x) * 4
            $bytes[$index] = [byte][Math]::Round($b * 255.0)
            $bytes[$index + 1] = [byte][Math]::Round($g * 255.0)
            $bytes[$index + 2] = [byte][Math]::Round($r * 255.0)
            $bytes[$index + 3] = 255
        }
    }
    $bitmap = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rectangle = New-Object System.Drawing.Rectangle(0, 0, $Width, $Height)
    $data = $bitmap.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    [System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
    $bitmap.UnlockBits($data)
    $bitmap.Save((Join-Path $directory $FileName), [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Write-Host ('wrote Environments\' + $FileName + ' (' + $Width + ' x ' + $Height + ')')
}

Write-Sky 'Sky.png' 512 256
