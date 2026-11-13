# エンジン組み込みのマテリアル（EngineContent/Materials）を、書き出す。
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\GenerateEngineMaterials.ps1
#   - マテリアルは、ノードのグラフ（.gxmaterial。Docs/MATERIAL.md）。ここに書いたものが、そのままファイルになる（手で書き換えず、このスクリプトを直して、書き出し直す）。
#   - ノードは、Add-Node で足す（番号が返る）。つなぐノードは、Inputs へ、ピンの順に番号を渡す（-1 はつながない）。
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)
$ErrorActionPreference = 'Stop'

$script:Nodes = @()
$script:NextId = 1

function Reset-Graph
{
    $script:Nodes = @()
    $script:NextId = 1
}

# ノードを足して、番号を返す。Value は 1〜4 個の数、Components は使う成分の数（Constant・VectorParameter）
function Add-Node
{
    param([string]$Kind, [object[]]$Inputs = @(), [string]$Name = '', [double[]]$Value = @(), [int]$Components = 0, [switch]$Color, [double[]]$PinDefaults = @(), [string]$Collection = '', [string]$TexturePath = '')
    $id = $script:NextId
    $script:NextId++
    $index = $script:Nodes.Count
    $node = [ordered]@{ Id = $id; Kind = $Kind }
    if ($Name -ne '') { $node['Name'] = $Name }
    if ($Collection -ne '') { $node['ParameterCollection'] = $Collection }
    if ($TexturePath -ne '') { $node['TexturePath'] = $TexturePath }
    if ($Value.Count -gt 0) { $node['Value'] = [object[]]$Value }
    if ($Components -gt 0) { $node['Components'] = $Components }
    if ($Color) { $node['Color'] = $true }
    # 位置は決めない（エディタが開くときに、つながり方に合わせて並べる）
    $node['Position'] = [object[]]@(0, 0)
    $node['Inputs'] = [object[]]$Inputs
    if ($PinDefaults.Count -gt 0) { $node['PinDefaults'] = [object[]]$PinDefaults }
    $script:Nodes += , $node
    return $id
}

function Num([double]$Value) { return Add-Node -Kind 'Constant' -Value @($Value) -Components 1 }
function Vec3([double]$X, [double]$Y, [double]$Z) { return Add-Node -Kind 'Constant' -Value @($X, $Y, $Z) -Components 3 }
function Scalar([string]$Name, [double]$Default) { return Add-Node -Kind 'ScalarParameter' -Name $Name -Value @($Default) }
function ColorParam([string]$Name, [double]$R, [double]$G, [double]$B) { return Add-Node -Kind 'VectorParameter' -Name $Name -Value @($R, $G, $B, 1.0) -Components 4 -Color }
function Mask([int]$Source, [string]$Swizzle) { return Add-Node -Kind 'Mask' -Name $Swizzle -Inputs @($Source) }
function Op2([string]$Kind, [int]$A, [int]$B) { return Add-Node -Kind $Kind -Inputs @($A, $B) }
function Op1([string]$Kind, [int]$A) { return Add-Node -Kind $Kind -Inputs @($A) }
function Lerp3([int]$A, [int]$B, [int]$Alpha) { return Add-Node -Kind 'Lerp' -Inputs @($A, $B, $Alpha) }

function Save-Material([string]$RelativePath, [string]$Name, [string]$Domain, [string]$Shading, [hashtable]$Outputs)
{
    $document = [ordered]@{ Format = 'gxmaterial'; Version = 1; Name = $Name; Domain = $Domain }
    if ($Domain -eq 'Surface') { $document['Shading'] = $Shading }
    $document['Nodes'] = [object[]]$script:Nodes
    # PowerShell の変数名は、大文字と小文字を区別しない（$Outputs と $outputs は、同じ）ので、別の名前にする
    $sortedOutputs = [ordered]@{}
    foreach ($key in $Outputs.Keys | Sort-Object) { $sortedOutputs[$key] = $Outputs[$key] }
    $document['Outputs'] = $sortedOutputs
    $path = Join-Path $Root ('EngineContent\Materials\' + $RelativePath)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    $json = ConvertTo-Json -InputObject $document -Depth 10
    [System.IO.File]::WriteAllText($path, $json + "`n", (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ('wrote ' + $RelativePath + ' (' + $script:Nodes.Count + ' nodes)')
}

# ---- UI -----------------------------------------------------------------------------------------
# ボタンの光: ホバーで光り、ゆっくり脈打ち、押すと暗くなる
Reset-Graph
$base = Mask (ColorParam 'Base' 0.16 0.20 0.32) 'xyz'
$glow = Mask (ColorParam 'Glow' 0.45 0.72 1.0) 'xyz'
$hover = Add-Node -Kind 'HoverAmount'
$press = Add-Node -Kind 'PressAmount'
$time = Add-Node -Kind 'Time'
$wave = Op1 'Sin' (Op2 'Multiply' $time (Scalar 'PulseSpeed' 2.5))
$pulse = Op2 'Multiply' (Op2 'Add' (Op2 'Multiply' $wave (Num 0.5)) (Num 0.5)) (Scalar 'PulseAmount' 0.25)
$amount = Op1 'Saturate' (Op2 'Add' $hover $pulse)
$lit = Lerp3 $base $glow $amount
$dim = Op1 'OneMinus' (Op2 'Multiply' $press (Num 0.35))
$color = Op2 'Multiply' $lit $dim
Save-Material 'UI\GlowButton.gxmaterial' 'GlowButton' 'UI' '' @{ Color = $color }

# 光の帯が、横へ流れる
Reset-Graph
$base = Mask (ColorParam 'Base' 0.10 0.13 0.22) 'xyz'
$light = Mask (ColorParam 'Highlight' 0.75 0.88 1.0) 'xyz'
$x = Mask (Add-Node -Kind 'TexCoord') 'x'
$sweep = Op1 'Frac' (Op2 'Multiply' (Add-Node -Kind 'Time') (Scalar 'Speed' 0.4))
$distance = Op1 'Abs' (Op2 'Subtract' $x $sweep)
$band = Op1 'OneMinus' (Add-Node -Kind 'SmoothStep' -Inputs @((Num 0.0), (Scalar 'Width' 0.18), $distance))
$color = Lerp3 $base $light (Op2 'Multiply' $band (Num 0.8))
Save-Material 'UI\Shimmer.gxmaterial' 'Shimmer' 'UI' '' @{ Color = $color }

# ---- PostProcess（Intensity の数のパラメーターで、効き具合を決められる）-------------------------
Reset-Graph
$scene = Mask (Add-Node -Kind 'SceneColor') 'rgb'
$uv = Add-Node -Kind 'TexCoord'
$centered = Op2 'Subtract' $uv (Num 0.5)
$distanceSquared = Add-Node -Kind 'Dot' -Inputs @($centered, $centered)
$dark = Op1 'Saturate' (Op2 'Multiply' $distanceSquared (Op2 'Multiply' (Scalar 'Strength' 0.6) (Num 4.0)))
$vignette = Op2 'Multiply' $scene (Op1 'OneMinus' $dark)
$color = Lerp3 $scene $vignette (Scalar 'Intensity' 1.0)
Save-Material 'PostProcess\Vignette.gxmaterial' 'Vignette' 'PostProcess' '' @{ Color = $color }

Reset-Graph
$scene = Mask (Add-Node -Kind 'SceneColor') 'rgb'
$gray = Add-Node -Kind 'Dot' -Inputs @($scene, (Vec3 0.299 0.587 0.114))
$color = Lerp3 $scene $gray (Scalar 'Intensity' 1.0)
Save-Material 'PostProcess\Grayscale.gxmaterial' 'Grayscale' 'PostProcess' '' @{ Color = $color }

Reset-Graph
$uv = Add-Node -Kind 'TexCoord'
$blocks = Scalar 'Blocks' 96.0
$cell = Op1 'Floor' (Op2 'Multiply' $uv $blocks)
$snapped = Op2 'Divide' (Op2 'Add' $cell (Num 0.5)) $blocks
$pixelated = Mask (Add-Node -Kind 'SceneColor' -Inputs @($snapped)) 'rgb'
$scene = Mask (Add-Node -Kind 'SceneColor') 'rgb'
$color = Lerp3 $scene $pixelated (Scalar 'Intensity' 1.0)
Save-Material 'PostProcess\Pixelate.gxmaterial' 'Pixelate' 'PostProcess' '' @{ Color = $color }

# ---- Particle（エフェクトの粒子の四角形。エミッターの MaterialAsset に書く。粒子の色は、出力に掛かるので、ここでは掛けない）--------
# やわらかい光の玉: 中心が明るく、縁へ向かって消える
Reset-Graph
$centered = Op2 'Subtract' (Add-Node -Kind 'TexCoord') (Num 0.5)
$falloff = Op1 'Saturate' (Op1 'OneMinus' (Op2 'Multiply' (Add-Node -Kind 'Dot' -Inputs @($centered, $centered)) (Num 4.0)))
$soft = Op2 'Multiply' $falloff $falloff
$core = Op2 'Multiply' (Op2 'Multiply' $soft $soft) (Scalar 'Core' 0.6)
$color = Op2 'Add' (Vec3 1.0 1.0 1.0) $core
Save-Material 'Particle\SoftGlow.gxmaterial' 'SoftGlow' 'Particle' '' @{ Color = $color; Opacity = $soft }

# 炎: 上へ流れるノイズで、形がゆらぐ。粒子ごとに模様をずらし（ParticleRandom）、寿命が進むと、外側の色に寄る（ParticleLife）
Reset-Graph
$uv = Add-Node -Kind 'TexCoord'
$centered = Op2 'Subtract' $uv (Num 0.5)
$falloff = Op1 'Saturate' (Op1 'OneMinus' (Op2 'Multiply' (Add-Node -Kind 'Dot' -Inputs @($centered, $centered)) (Num 4.0)))
$scroll = Op2 'Multiply' (Add-Node -Kind 'Constant' -Value @(0.0, 1.0) -Components 2) (Op2 'Multiply' (Add-Node -Kind 'Time') (Scalar 'Speed' 0.9))
$offset = Op2 'Multiply' (Add-Node -Kind 'ParticleRandom') (Num 7.3)
$noise = Add-Node -Kind 'Noise' -Inputs @((Op2 'Add' (Op2 'Add' $uv $scroll) $offset), (Scalar 'Scale' 4.0))
$shape = Add-Node -Kind 'SmoothStep' -Inputs @((Num 0.35), (Num 0.75), (Op2 'Add' (Op2 'Multiply' $noise (Num 0.6)) (Op2 'Multiply' $falloff (Num 0.7))))
$inner = Mask (ColorParam 'Inner' 1.0 0.92 0.55) 'xyz'
$outer = Mask (ColorParam 'Outer' 1.0 0.35 0.08) 'xyz'
$flame = Op2 'Multiply' $shape $falloff
$color = Lerp3 $outer $inner (Op2 'Multiply' $flame (Op1 'OneMinus' (Add-Node -Kind 'ParticleLife')))
Save-Material 'Particle\Flame.gxmaterial' 'Flame' 'Particle' '' @{ Color = $color; Opacity = $flame }

# ---- Surface -------------------------------------------------------------------------------------
Reset-Graph
$colorA = Mask (ColorParam 'ColorA' 0.85 0.86 0.92) 'xyz'
$colorB = Mask (ColorParam 'ColorB' 0.18 0.20 0.30) 'xyz'
$checker = Add-Node -Kind 'Checker' -Inputs @(-1, (Scalar 'Count' 8.0))
$base = Lerp3 $colorA $colorB $checker
Save-Material 'Surface\Checker.gxmaterial' 'Checker' 'Surface' 'DefaultLit' @{ BaseColor = $base; Roughness = (Num 0.6) }

Reset-Graph
$light = Mask (ColorParam 'Light' 0.92 0.92 0.88) 'xyz'
$dark = Mask (ColorParam 'Dark' 0.14 0.16 0.22) 'xyz'
$noise = Add-Node -Kind 'Noise' -Inputs @(-1, (Scalar 'Scale' 5.0))
$vein = Add-Node -Kind 'SmoothStep' -Inputs @((Num 0.35), (Num 0.65), $noise)
$base = Lerp3 $light $dark $vein
Save-Material 'Surface\Marble.gxmaterial' 'Marble' 'Surface' 'DefaultLit' @{ BaseColor = $base; Roughness = (Num 0.25) }

Reset-Graph
$rim = Mask (ColorParam 'Color' 0.35 0.75 1.0) 'xyz'
$edge = Add-Node -Kind 'Fresnel' -Inputs @((Scalar 'Exponent' 2.5))
$glow = Op2 'Multiply' $rim $edge
Save-Material 'Surface\RimGlow.gxmaterial' 'RimGlow' 'Surface' 'Unlit' @{ Emissive = $glow }

# Substrate 風: 赤い塗装の上に、クリアコート
Reset-Graph
$paint = Mask (ColorParam 'Paint' 0.70 0.06 0.06) 'xyz'
$slab = Add-Node -Kind 'SubstrateSlab' -Inputs @($paint, -1, (Scalar 'Roughness' 0.55), -1, -1, -1, (Scalar 'ClearCoat' 1.0), (Scalar 'ClearCoatRoughness' 0.12), -1, -1, -1)
Save-Material 'Surface\CarPaint.gxmaterial' 'CarPaint' 'Surface' 'Substrate' @{ Surface = $slab }

# Substrate 風（フルの評価）: 一方向に磨いた金属。光沢が、接線（UV の U）の向きに、長く伸びる（Anisotropy。負なら、V の向き）
Reset-Graph
$metal = Mask (ColorParam 'Metal' 0.80 0.80 0.83) 'xyz'
$slab = Add-Node -Kind 'SubstrateSlab' -Inputs @($metal, -1, (Scalar 'Roughness' 0.35), (Scalar 'Metallic' 1.0), -1, -1, -1, -1, -1, (Scalar 'Anisotropy' 0.8), -1)
Save-Material 'Surface\BrushedMetal.gxmaterial' 'BrushedMetal' 'Surface' 'Substrate' @{ Surface = $slab }

# 真珠: 見る角度で色の変わる、薄い膜の色（ThinFilm）
Reset-Graph
$base = Mask (ColorParam 'Base' 0.86 0.83 0.80) 'xyz'
$slab = Add-Node -Kind 'SubstrateSlab' -Inputs @($base, -1, (Scalar 'Roughness' 0.3), -1, -1, -1, -1, -1, -1, -1, (Scalar 'ThinFilm' 0.5))
Save-Material 'Surface\Pearl.gxmaterial' 'Pearl' 'Surface' 'Substrate' @{ Surface = $slab }

# ろう: 光が中へ回り込んで、影の境目がやわらぎ、逆光で透ける（Subsurface）
Reset-Graph
$base = Mask (ColorParam 'Base' 0.80 0.62 0.45) 'xyz'
$scatter = Mask (ColorParam 'Scatter' 0.90 0.35 0.20) 'xyz'
$slab = Add-Node -Kind 'SubstrateSlab' -Inputs @($base, -1, (Scalar 'Roughness' 0.6), -1, -1, -1, -1, -1, $scatter, -1, -1)
Save-Material 'Surface\Wax.gxmaterial' 'Wax' 'Surface' 'Substrate' @{ Surface = $slab }

# さびた金属: 金属の層と、さびの層を、ノイズで混ぜる（SubstrateMix。層ごとに陰影を計算するので、境目が、半分だけ金属の、ありえない材質にならない）
Reset-Graph
$metal = Add-Node -Kind 'SubstrateSlab' -Inputs @((Mask (ColorParam 'Metal' 0.76 0.76 0.80) 'xyz'), -1, (Scalar 'MetalRoughness' 0.3), (Num 1.0), -1, -1, -1, -1, -1, -1, -1)
$rust = Add-Node -Kind 'SubstrateSlab' -Inputs @((Mask (ColorParam 'Rust' 0.45 0.20 0.08) 'xyz'), -1, (Num 0.9), (Num 0.0), -1, -1, -1, -1, -1, -1, -1)
$noise = Add-Node -Kind 'Noise' -Inputs @(-1, (Scalar 'Scale' 6.0))
$amount = Scalar 'Amount' 0.5
$mask = Add-Node -Kind 'SmoothStep' -Inputs @((Op2 'Subtract' (Op1 'OneMinus' $amount) (Num 0.08)), (Op2 'Add' (Op1 'OneMinus' $amount) (Num 0.08)), $noise)
$mixed = Add-Node -Kind 'SubstrateMix' -Inputs @($metal, $rust, $mask)
Save-Material 'Surface\RustedMetal.gxmaterial' 'RustedMetal' 'Surface' 'Substrate' @{ Surface = $mixed }
