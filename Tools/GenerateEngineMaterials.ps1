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

# 同じ画像を局所座標の三方向から投影し、面の向きに応じて混ぜる。
function TriplanarTexture([string]$Name, [string]$Path, [int]$Position, [int]$Weights)
{
    $x = Add-Node -Kind 'TextureSample' -Name $Name -TexturePath $Path -Inputs @((Op1 'Frac' (Mask $Position 'yz')))
    $y = Add-Node -Kind 'TextureSample' -Name $Name -TexturePath $Path -Inputs @((Op1 'Frac' (Mask $Position 'xz')))
    $z = Add-Node -Kind 'TextureSample' -Name $Name -TexturePath $Path -Inputs @((Op1 'Frac' (Mask $Position 'xy')))
    return Op2 'Add' (Op2 'Add' (Op2 'Multiply' $x (Mask $Weights 'x')) (Op2 'Multiply' $y (Mask $Weights 'y'))) (Op2 'Multiply' $z (Mask $Weights 'z'))
}

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

# さびた金属: 局所3次元ノイズで侵食の大小・色・粗さ・凹凸を分ける。
Reset-Graph
$position = Add-Node -Kind 'LocalPosition'
$scale = Scalar 'Scale' 6.0
$detailScale = Op2 'Multiply' $scale (Num 2.4)
$grainScale = Op2 'Multiply' $scale (Num 12.0)
$coarse = Add-Node -Kind 'Noise3D' -Inputs @($position, (Op2 'Multiply' $scale (Num 0.45)))
$medium = Add-Node -Kind 'Noise3D' -Inputs @($position, $scale)
$fine = Add-Node -Kind 'Noise3D' -Inputs @($position, $detailScale)
$grain = Add-Node -Kind 'Noise3D' -Inputs @($position, $grainScale)
# 大きな腐食域の輪郭に細かな欠けを重ね、独立した小さな腐食点も作る。
$noise = Op2 'Add' (Op2 'Add' (Op2 'Multiply' $coarse (Num 0.38)) (Op2 'Multiply' $medium (Num 0.34))) (Op2 'Add' (Op2 'Multiply' $fine (Num 0.20)) (Op2 'Multiply' $grain (Num 0.08)))
$amount = Scalar 'Amount' 0.82
$threshold = Op2 'Subtract' (Num 1.2) (Op2 'Multiply' $amount (Num 1.4))
$edgeNoise = Op2 'Add' $noise (Op2 'Multiply' (Op2 'Subtract' $grain (Num 0.5)) (Num 0.13))
$mask = Add-Node -Kind 'SmoothStep' -Inputs @((Op2 'Subtract' $threshold (Num 0.025)), (Op2 'Add' $threshold (Num 0.025)), $edgeNoise)
$speckles = Op2 'Multiply' (Add-Node -Kind 'SmoothStep' -Inputs @((Num 0.66), (Num 0.78), $fine)) (Add-Node -Kind 'SmoothStep' -Inputs @((Op2 'Subtract' $threshold (Num 0.20)), $threshold, $medium))
$mask = Op1 'Saturate' (Op2 'Add' $mask (Op2 'Multiply' $speckles $amount))
# 腐食の周囲は、金属も変色して光沢が鈍くなる。
$weathered = Add-Node -Kind 'SmoothStep' -Inputs @((Op2 'Subtract' $threshold (Num 0.16)), (Op2 'Add' $threshold (Num 0.02)), $edgeNoise)
$metalColor = Lerp3 (Mask (ColorParam 'Metal' 0.76 0.76 0.80) 'xyz') (Vec3 0.31 0.24 0.19) (Op2 'Multiply' $weathered (Num 0.65))
$metalRoughness = Add-Node -Kind 'Clamp' -Inputs @((Op2 'Add' (Op2 'Add' (Scalar 'MetalRoughness' 0.4) (Op2 'Multiply' $weathered (Num 0.3))) (Op2 'Multiply' (Op2 'Subtract' $fine (Num 0.5)) (Num 0.16))), (Num 0.03), (Num 0.95))
$metal = Add-Node -Kind 'SubstrateSlab' -Inputs @($metalColor, -1, $metalRoughness, (Num 1.0), -1, -1, -1, -1, -1, -1, -1)
$rustColor = Mask (ColorParam 'Rust' 0.45 0.20 0.08) 'xyz'
# 方向別の重みには配置前の法線を使い、回転しても画像の混ざり方を固定する。
$localNormal = Add-Node -Kind 'LocalNormal'
$normalWeights = Op2 'Power' (Op1 'Abs' $localNormal) (Num 4.0)
$weightSum = Op2 'Add' (Op2 'Add' (Mask $normalWeights 'x') (Mask $normalWeights 'y')) (Mask $normalWeights 'z')
$weights = Op2 'Divide' $normalWeights (Op2 'Max' $weightSum (Num 0.0001))
$texturePosition = Op2 'Add' (Op2 'Multiply' $position (Scalar 'RustTextureScale' 0.45)) (Num 0.5)
$surfaceColor = Mask (TriplanarTexture 'RustSurfaceColor' 'Engine:/Textures/Materials/Metal041C/Color.jpg' $texturePosition $weights) 'xyz'
$surfaceRoughness = Mask (TriplanarTexture 'RustSurfaceRoughness' 'Engine:/Textures/Materials/Metal041C/Roughness.jpg' $texturePosition $weights) 'x'
$surfaceMetallic = Mask (TriplanarTexture 'RustSurfaceMetallic' 'Engine:/Textures/Materials/Metal041C/Metalness.jpg' $texturePosition $weights) 'x'
$rustVariation = Op2 'Multiply' $surfaceColor (Op2 'Divide' $rustColor (Vec3 0.45 0.20 0.08))
$rustRoughness = Add-Node -Kind 'Clamp' -Inputs @($surfaceRoughness, (Num 0.1), (Num 1.0))
# 三方向の法線画像を局所面の接線方向へ投影し、物体の配置に合わせる。
$normalX = Op2 'Subtract' (Op2 'Multiply' (Mask (Add-Node -Kind 'TextureSample' -Name 'RustSurfaceNormal' -TexturePath 'Engine:/Textures/Materials/Metal041C/NormalDX.jpg' -Inputs @((Op1 'Frac' (Mask $texturePosition 'yz')))) 'xy') (Num 2.0)) (Num 1.0)
$normalY = Op2 'Subtract' (Op2 'Multiply' (Mask (Add-Node -Kind 'TextureSample' -Name 'RustSurfaceNormal' -TexturePath 'Engine:/Textures/Materials/Metal041C/NormalDX.jpg' -Inputs @((Op1 'Frac' (Mask $texturePosition 'xz')))) 'xy') (Num 2.0)) (Num 1.0)
$normalZ = Op2 'Subtract' (Op2 'Multiply' (Mask (Add-Node -Kind 'TextureSample' -Name 'RustSurfaceNormal' -TexturePath 'Engine:/Textures/Materials/Metal041C/NormalDX.jpg' -Inputs @((Op1 'Frac' (Mask $texturePosition 'xy')))) 'xy') (Num 2.0)) (Num 1.0)
$deltaX = Add-Node -Kind 'Append' -Inputs @((Num 0.0), $normalX)
$deltaY = Add-Node -Kind 'Append' -Inputs @((Add-Node -Kind 'Append' -Inputs @((Mask $normalY 'x'), (Num 0.0))), (Mask $normalY 'y'))
$deltaZ = Add-Node -Kind 'Append' -Inputs @($normalZ, (Num 0.0))
$signs = Op2 'Subtract' (Op2 'Multiply' (Op2 'Step' (Num 0.0) $localNormal) (Num 2.0)) (Num 1.0)
$signedWeights = Op2 'Multiply' $weights $signs
$delta = Op2 'Add' (Op2 'Add' (Op2 'Multiply' $deltaX (Mask $signedWeights 'x')) (Op2 'Multiply' $deltaY (Mask $signedWeights 'y'))) (Op2 'Multiply' $deltaZ (Mask $signedWeights 'z'))
$surfaceDelta = Op2 'Subtract' $delta (Op2 'Multiply' $localNormal (Op2 'Dot' $localNormal $delta))
$textureNormal = Add-Node -Kind 'LocalNormalToWorld' -Inputs @((Op1 'Normalize' (Op2 'Add' $localNormal (Op2 'Multiply' $surfaceDelta (Scalar 'RustBumpStrength' 0.8)))))
# 素材画像の凹凸に細かい粒子を重ねる。
$surfaceGrainScale = Op2 'Multiply' $scale (Num 24.0)
$rustGrain = Add-Node -Kind 'Noise3DNormal' -Inputs @($position, $surfaceGrainScale, (Scalar 'RustGrainStrength' 0.3))
$rustNormal = Op1 'Normalize' (Op2 'Subtract' (Op2 'Add' $textureNormal $rustGrain) (Add-Node -Kind 'WorldNormal'))
$rust = Add-Node -Kind 'SubstrateSlab' -Inputs @($rustVariation, -1, $rustRoughness, $surfaceMetallic, -1, -1, -1, -1, -1, -1, -1, $rustNormal)
$mixed = Add-Node -Kind 'SubstrateMix' -Inputs @($metal, $rust, $mask)
Save-Material 'Surface\RustedMetal.gxmaterial' 'RustedMetal' 'Surface' 'Substrate' @{ Surface = $mixed }

# キャンディ塗装: 金属の層の上に、色のついた透明な層を重ねる（SubstrateLayer。下の金属の光沢が、上の層の色に染まって見え、いちばん上に、色のない鋭い光沢が乗る）
Reset-Graph
$flakes = Add-Node -Kind 'SubstrateSlab' -Inputs @((Mask (ColorParam 'Metal' 0.85 0.85 0.88) 'xyz'), -1, (Scalar 'MetalRoughness' 0.4), (Num 1.0))
$clear = Add-Node -Kind 'SubstrateSlab' -Inputs @((Vec3 0.0 0.0 0.0), -1, (Scalar 'CoatRoughness' 0.08), (Num 0.0))
$tint = Mask (ColorParam 'Tint' 0.90 0.06 0.12) 'xyz'
$layered = Add-Node -Kind 'SubstrateLayer' -Inputs @($clear, $flakes, $tint, (Scalar 'Coverage' 1.0))
Save-Material 'Surface\CandyPaint.gxmaterial' 'CandyPaint' 'Surface' 'Substrate' @{ Surface = $layered }

# 槌目（つちめ）の金属: 局所座標の勾配から、UVの継ぎ目に依存しない凹凸を作る。
Reset-Graph
$position = Add-Node -Kind 'LocalPosition'
$scale = Scalar 'Scale' 14.0
$normal = Add-Node -Kind 'Noise3DNormal' -Inputs @($position, $scale, (Scalar 'Strength' 0.35))
$slab = Add-Node -Kind 'SubstrateSlab' -Inputs @((Mask (ColorParam 'Metal' 0.86 0.56 0.36) 'xyz'), -1, (Scalar 'Roughness' 0.3), (Num 1.0), -1, -1, -1, -1, -1, -1, -1, $normal)
Save-Material 'Surface\HammeredMetal.gxmaterial' 'HammeredMetal' 'Surface' 'Substrate' @{ Surface = $slab }
