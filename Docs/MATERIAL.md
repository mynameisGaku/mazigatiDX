# マテリアル

マテリアルは、「見た目の計算」を、ノードのグラフ（または HLSL）で書いたものです。UI・描画・エフェクトのどれにも属さない、汎用の仕組みで（`Library/Framework/Material`）、ブラシ（UI）・メッシュの表面・ポストプロセスのどれにも使えます。グラフを、バックエンドに依らない中間表現にして、そこから HLSL を作ります（今の出力先は DxLib の Direct3D 11）。ほかのバックエンドは、同じ中間表現から自分の言語を出す設計です。

## ドメイン

使える入力と出力は、**ドメイン**で決まります。

|ドメイン|適用先|入力のノード|出力ピン|
|---|---|---|---|
|`UI`|部品のブラシ（矩形）|`TexCoord`・`Size`・`HoverAmount`・`PressAmount`・`Value01`・`Enabled`・`VertexColor`・`Time`|`Color`（Float3）・`Opacity`（Float）|
|`Particle`|粒子の矩形|`TexCoord`・`VertexColor`（粒子の色）・`Time`|`Color`・`Opacity`|
|`PostProcess`|画面全体|`TexCoord`・`SceneColor`・`SceneDepth`・`Size`（画面の大きさ）・`Time`|`Color`（つながないと、シーンの色のまま）|
|`Surface`|メッシュの表面|`TexCoord`・`WorldPosition`・`WorldNormal`・`CameraVector`・`VertexColor`・`Time`|陰影で変わる（下）|

Surface の陰影（`EMaterialShading`）: **Unlit**（`Emissive`・`Opacity`）/ **DefaultLit**（`BaseColor`・`Emissive`・`Opacity`・`Roughness`・`Metallic`・`Specular`。標準の光: 拡散 + 鏡面）/ **Substrate**（`Surface` 1 つ。`SubstrateSlab` ノードの層）。

## グラフ（`FMaterialGraph`）

ノードは 1 つの値を出し、入力（ピン）に、別のノードの出力をつなぎます。つないでいないピンは、そのピンの既定の数（UV のピンは、`TexCoord`）です。

|分類|ノード|
|---|---|
|値|`Constant`・`ScalarParameter`・`VectorParameter`・`ParameterCollectionScalar`・`ParameterCollectionVector`・`TextureSample`|
|入力|`TexCoord`・`Time`・`Size`・`HoverAmount`・`PressAmount`・`Value01`・`Enabled`・`VertexColor`・`WorldPosition`・`WorldNormal`・`CameraVector`・`SceneColor`・`SceneDepth`|
|算術|`Add`・`Subtract`・`Multiply`・`Divide`・`Min`・`Max`・`Power`・`Mod`・`Step`・`Dot`・`Distance`|
|3 つ|`Lerp`・`Clamp`・`SmoothStep`|
|1 つ|`Abs`・`Saturate`・`OneMinus`・`Negate`・`Sin`・`Cos`・`Frac`・`Floor`・`Ceil`・`Sqrt`・`Length`・`Normalize`|
|便利|`Append`・`Mask`（`"xyz"`・`"rgb"`・`"a"`）・`Panner`・`Tiling`・`Noise`・`Checker`・`Fresnel`|
|Substrate|`SubstrateSlab`（`DiffuseColor`・`F0`・`Roughness`・`Metallic`・`Emissive`・`Opacity`・`ClearCoat`・`ClearCoatRoughness`・`Subsurface`・`Anisotropy`・`ThinFilm`）・`SubstrateMix`（2 つの層を混ぜる）|

型: 算術は、同じ型どうし、または Float と、ほかの型（Float が広がる）。成分の数が違うベクトルどうし（Float2 と Float3 など）は、エラーです。ピンに求められる型より大きいベクトルは、切り詰めます（Float4 から Float3 へは、知らせなし。ほかは、注意）。

```cpp
FMaterialDefinition Definition;
Definition.Domain = EMaterialDomain::UI;
FMaterialGraph& G = Definition.Graph;
const int32 Tint = G.AddNode( EMaterialNodeKind::VectorParameter, "Tint", FVector4( 1.0f, 0.5f, 0.0f, 1.0f ) );
const int32 Rgb = G.AddNode( EMaterialNodeKind::Mask, "xyz" );       // Tint の RGB（Float3）
G.Connect( Rgb, 0, Tint );
const int32 White = G.AddNode( EMaterialNodeKind::Constant, FString(), FVector4( 1.0f, 1.0f, 1.0f, 0.0f ) );
G.FindNode( White )->ValueComponents = 3;
const int32 Hover = G.AddNode( EMaterialNodeKind::HoverAmount );
const int32 Lit = G.AddNode( EMaterialNodeKind::Lerp );              // ホバーで、白へ近づく
G.Connect( Lit, 0, Rgb );
G.Connect( Lit, 1, White );
G.Connect( Lit, 2, Hover );
G.SetOutput( "Color", Lit );
```

## コンパイルと診断

`NMaterialCompiler::Compile` が、型の検査・ドメインの検査・輪の検出・出力の検査・パラメーターと画像の置き場の割り当てをして、中間表現（`FMaterialProgram`）を作ります。問題は、止まらずにすべて `Diagnostics`（重さ: Info・Warning・Error。問題のあるノードの番号つき。エディタが、そのノードへジャンプできる）に入ります。Error が 1 つでもあれば、`bValid` は false です。

バックエンドの能力は `FMaterialCapabilities`（ピクセルシェーダーの有無・深度を読めるか・Substrate をフルに評価できるか・画像とパラメーターの数の上限）で宣言します。足りない機能は、簡略版へ落とし、Info の診断で知らせます（例: 既定の DxLib 相当では、Substrate は拡散・鏡面・発光・クリアコートの近似で、`Subsurface`・`Anisotropy`・`ThinFilm` は無視）。

## 動かす（`CMaterial`）

```cpp
TSharedPtr<CMaterial> Material = CMaterial::Create( Definition );     // コンパイルまで行う
if ( !Material->IsValid() ) { /* Material->GetDiagnostics() */ }
Material->SetVector( "Tint", FVector4( 0.0f, 1.0f, 0.0f, 1.0f ) );

// UI・Particle: 矩形に描く
Material->DrawRect( Draw, X, Y, Width, Height, FColor32( 255, 255, 255, 255 ), FrameState );
// PostProcess: シーンの絵（と深度）を読んで、矩形に描く
Material->DrawPostProcess( Draw, X, Y, Width, Height, SceneImage, DepthImage, FrameState );
// Surface: 標準の頂点シェーダーで、メッシュ（FShaderVertex3D）を描く
Material->BeginSurface( Draw, SceneState );
Material->DrawSurfaceMesh( Draw, Vertices, NumVertices, Indices, NumIndices, World, Tint );
Material->EndSurface( Draw );
```

- シェーダーは、最初に描くとき（または `PrepareShaders`）に、実行のときにコンパイルします（メインスレッドだけ）。失敗したら、描かず false を返し、理由が `GetDiagnostics()` に足されます。ピクセルシェーダーを実行できないバックエンド（`bPixelShaders` が false）では、描きません。
- `EvaluateCpu` は、GPU を使わずに、同じ計算を CPU で行います（テストと、エディタのノードのプレビュー用）。
- `Reload( 新しい定義 )` は、同じ名前・型のパラメーターの値を引き継いで作り直します（ホットリロード）。マテリアルを使っているブラシなどは、そのまま使えます。
- **インスタンス**: `CMaterial::CreateInstance( 親 )` は、親のコンパイル結果を共有して、パラメーターだけを上書きします（コンパイルし直さない）。上書きしていない値は、親に従います。`ResetParameter`・`ClearOverrides`・`IsOverridden`。
- **パラメーターコレクション**（`CMaterialParameterCollection`）: 名前つきの値（数・ベクトル・色）を、複数のマテリアルで共有します。`ParameterCollectionScalar` / `ParameterCollectionVector` ノード（コレクションの名前と値の名前）で読み、1 か所を変えると、読んでいるすべてのマテリアルに反映されます（天候・時間帯・ダメージの赤み・UI のテーマ色）。`Material->BindParameterCollection( Collection )`、または `CMaterialSubsystem::GetParameterCollection( "Weather" )`（名前で自動で結ばれる）。MVVM の `TObservable` の `OnChanged` から `SetScalar` を呼べば、ビューモデルで画面の見た目を動かせます。

## ポストプロセスのチェーン（`CPostProcessStack`）

```cpp
CPostProcessStack Stack;
Stack.Add( Vignette );
Stack.Add( Grayscale, 0.6f );     // 強さ: マテリアルに Intensity という数のパラメーターがあれば、その値になる
if ( !Stack.Record( Draw, Assets, Scene, Depth, 640, 360, 0.0f, 0.0f, 640.0f, 360.0f, FrameState ) ) { /* パスが無い: シーンを、そのまま描く */ }
```

順番・有効 / 無効・強さを持ち、途中の結果は、スタックが持つ 2 枚の作業用の描画先に、交互に描きます。適用先は、描画先・画面・UI の RenderTarget の部品の中身など、どこにでも（描く矩形を決める）。

## UI のブラシ

```cpp
Button->SetStyle( ... );                                                // ブラシの Normal などに
FUIBrush Glow = FUIBrush::MakeMaterial( GlowMaterial );                 // UI ドメインのマテリアル。色（Color）は、出力に掛ける
```

マテリアルの入力 `HoverAmount`・`PressAmount`・`Value01`・`Enabled` は、このブラシを描く**部品の状態**です（`CUIWidget::PaintRecursive` が決める。スライダー・プログレスバー・チェックボックスは、自分の値を `Value01` に）。時間は、`CMaterialSubsystem` の時間です。

## HLSL の直指定

グラフを使わずに、ピクセルシェーダーを直に書けます（バックエンド別の出口。移植性は、書いた人の責任）。`CustomHlsl` に、`main` を書きます。cbuffer・入力の構造体・補助関数（`MatNoise`・`MatLinearDepth`・`MatMod`）は、システムが、先頭に置きます（ドメインごとの決まりは `Library/Framework/Material/MaterialHlsl.h`）。パラメーターは、宣言の順に `g_Params[0]`、`g_Params[1]` ...（Float〜Float4 は、どれも 4 つ組 1 つ）、画像は、宣言の順に `t0` から（PostProcess は、`t0` がシーン）です。

## ファイル

|拡張子|内容|
|---|---|
|`.gxmaterial`|マテリアルの定義（JSON。`Format`・`Version`・`Name`・`Domain`・`Shading`・`Nodes`・`Outputs`、または `CustomHlsl` と `Parameters`）|
|`.gxmaterialinstance`|インスタンス（`Parent` のパスと、`Overrides`: 数 = Float / 配列 = Float2〜4 / 文字列 = 画像のパス）|
|`.gxmaterialparametercollection`|パラメーターコレクション（`Name` と `Parameters`: 数か、数の配列）|

`CMaterialSubsystem`（`GetAppSubsystem<CMaterialSubsystem>()`）が、パスで読み込んでキャッシュし（`LoadMaterial`・`LoadParameterCollection`）、時間とコレクションの名前の表を持ちます。**ホットリロード**: 既定で半秒ごとに、読み込んだファイルの中身を調べ、変わっていれば、同じ `CMaterial` を作り直します。壊れた中身へ書き換えられたら、古い見た目のまま（ログに理由）、同じ壊れた中身は、繰り返し試しません。

## エンジン組み込みのマテリアル（`EngineContent/Materials`）

`Tools/GenerateEngineMaterials.ps1` が書き出します（手で書き換えず、スクリプトを直して、書き出し直す）。

|パス（`Engine:/Materials/...`）|内容|
|---|---|
|`UI/GlowButton`|ホバーで光り、ゆっくり脈打ち、押すと暗くなる（`Base`・`Glow`・`PulseSpeed`・`PulseAmount`）|
|`UI/Shimmer`|光の帯が横へ流れる（`Base`・`Highlight`・`Speed`・`Width`）|
|`PostProcess/Vignette`|周りを暗くする（`Strength`・`Intensity`）|
|`PostProcess/Grayscale`|白黒（`Intensity`）|
|`PostProcess/Pixelate`|モザイク（`Blocks`・`Intensity`）|
|`Surface/Checker`|市松模様（DefaultLit。`ColorA`・`ColorB`・`Count`）|
|`Surface/Marble`|ノイズの大理石（DefaultLit。`Light`・`Dark`・`Scale`）|
|`Surface/RimGlow`|縁が光る（Unlit。`Color`・`Exponent`）|
|`Surface/CarPaint`|赤い塗装 + クリアコート（Substrate。`Paint`・`Roughness`・`ClearCoat`・`ClearCoatRoughness`）|

## プレビュー用のメッシュ

`NMaterialPreview::MakeSphere` / `MakeCube` / `MakePlane` が、`FShaderVertex3D` とインデックス（`uint16`）のメッシュを返します（半径 1、UV は 0〜1、法線は外向き）。`DrawSurfaceMesh` に渡せます。

## これから

ノードのエディタ（球のプレビューつき）、Particle ドメインの、エフェクトの粒子への適用、Substrate のフル評価（フルに評価できるバックエンドの追加）。
