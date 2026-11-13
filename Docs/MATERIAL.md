# マテリアル

マテリアルは、「見た目の計算」を、ノードのグラフ（または HLSL）で書いたものです。UI・描画・エフェクトのどれにも属さない、汎用の仕組みで（`Library/Framework/Material`）、ブラシ（UI）・メッシュの表面・ポストプロセスのどれにも使えます。グラフを、バックエンドに依らない中間表現にして、そこから HLSL を作ります（今の出力先は DxLib の Direct3D 11）。ほかのバックエンドは、同じ中間表現から自分の言語を出す設計です。

## ドメイン

使える入力と出力は、**ドメイン**で決まります。

|ドメイン|適用先|入力のノード|出力ピン|
|---|---|---|---|
|`UI`|部品のブラシ（矩形）|`TexCoord`・`Size`・`HoverAmount`・`PressAmount`・`Value01`・`Enabled`・`VertexColor`・`Time`|`Color`（Float3）・`Opacity`（Float）|
|`Particle`|エフェクトの粒子（ワールドの四角形）・矩形|`TexCoord`・`VertexColor`（粒子の色）・`Time`・`ParticleLife`・`ParticleAge`・`ParticleRandom`・`ParticleSpeed`|`Color`・`Opacity`（粒子の色が、出力に掛かる）|
|`PostProcess`|画面全体|`TexCoord`・`SceneColor`・`SceneDepth`・`Size`（画面の大きさ）・`Time`|`Color`（つながないと、シーンの色のまま）|
|`Surface`|メッシュの表面|`TexCoord`・`WorldPosition`・`WorldNormal`・`WorldTangent`・`WorldBitangent`・`CameraVector`・`VertexColor`・`Time`|陰影で変わる（下）|

Surface の陰影（`EMaterialShading`）: **Unlit**（`Emissive`・`Opacity`）/ **DefaultLit**（`BaseColor`・`Emissive`・`Opacity`・`Roughness`・`Metallic`・`Specular`・`Normal`。標準の光: 拡散 + 鏡面。下の万能モデルの、層 1 つとして計算される）/ **Substrate**（`Surface` 1 つ。`SubstrateSlab` ノードの層。下の「Substrate 風の万能モデル」）。光を計算する 2 つは、場面の光（下の「場面の光」）を受けます。

## グラフ（`FMaterialGraph`）

ノードは 1 つの値を出し、入力（ピン）に、別のノードの出力をつなぎます。つないでいないピンは、そのピンの既定の数（UV のピンは、`TexCoord`）です。

|分類|ノード|
|---|---|
|値|`Constant`・`ScalarParameter`・`VectorParameter`・`ParameterCollectionScalar`・`ParameterCollectionVector`・`TextureSample`|
|入力|`TexCoord`・`Time`・`Size`・`HoverAmount`・`PressAmount`・`Value01`・`Enabled`・`VertexColor`・`WorldPosition`・`WorldNormal`・`WorldTangent`・`WorldBitangent`・`CameraVector`・`SceneColor`・`SceneDepth`|
|算術|`Add`・`Subtract`・`Multiply`・`Divide`・`Min`・`Max`・`Power`・`Mod`・`Step`・`Dot`・`Distance`|
|3 つ|`Lerp`・`Clamp`・`SmoothStep`|
|1 つ|`Abs`・`Saturate`・`OneMinus`・`Negate`・`Sin`・`Cos`・`Frac`・`Floor`・`Ceil`・`Sqrt`・`Length`・`Normalize`|
|便利|`Append`・`Mask`（`"xyz"`・`"rgb"`・`"a"`）・`Panner`・`Tiling`・`Noise`・`Checker`・`Fresnel`・`NormalMap`（法線マップの色を、ワールドの法線にする）|
|Substrate|`SubstrateSlab`（`DiffuseColor`・`F0`・`Roughness`・`Metallic`・`Emissive`・`Opacity`・`ClearCoat`・`ClearCoatRoughness`・`Subsurface`・`Anisotropy`・`ThinFilm`・`Normal`）・`SubstrateMix`（2 つの層を、横に並べて混ぜる）・`SubstrateLayer`（2 つの層を、縦に重ねる）|

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

バックエンドの能力は `FMaterialCapabilities`（ピクセルシェーダーの有無・深度を読めるか・Substrate をフルに評価できるか・画像とパラメーターの数の上限）で宣言します。足りない機能は、簡略版へ落とし、Info の診断で知らせます（例: `bFullSubstrate` が false のバックエンドでは、Substrate は拡散・鏡面・発光・クリアコートの近似で、`Subsurface`・`Anisotropy`・`ThinFilm` は無視）。既定（DxLib 相当）は、Substrate をフルに評価します。

## Substrate 風の万能モデル

陰影を `Substrate` にした Surface のマテリアルは、**層（スラブ）**を出力します。層 1 つ（`SubstrateSlab`）が、表面の性質のすべてを持ち、`SubstrateMix` で、2 つの層を横に並べて混ぜ、`SubstrateLayer` で、縦に重ねられます。

|ピン|意味|
|---|---|
|`DiffuseColor`|色（非金属では、拡散の色。金属では、鏡面の色）|
|`F0`|正面から見たときの、鏡面の反射の色（非金属。既定は 4%）|
|`Roughness`・`Metallic`|粗さ（0 = 鏡、1 = つや消し）・金属か（0〜1）|
|`Emissive`・`Opacity`|発光（そのまま足される）・不透明度|
|`ClearCoat`・`ClearCoatRoughness`|上に重ねる、透明な塗膜の強さと、その粗さ|
|`Subsurface`|光が中へ回り込む色（ろう・肌・葉）。影の境目がやわらぎ、逆光で透ける|
|`Anisotropy`|光沢の伸び（-1〜1）。正なら、接線（UV の U）の向きに伸び、負なら、V の向きに伸びる（磨いた金属・髪）|
|`ThinFilm`|薄い膜の干渉の色（0〜1）。見る角度で、色相が変わる（真珠・シャボン玉・油膜）|
|`Normal`|この層の法線（ワールドの向き。`NormalMap` ノードの出力をつなぐ）。つながなければ、面の法線。層ごとに別の法線にできる（でこぼこの下地に、平らな塗膜、など）|

- **フルの評価**（既定。`FMaterialCapabilities::bFullSubstrate`）: 鏡面は GGX のマイクロファセット（フレネル・異方性の分布・遮蔽）。拡散は、鏡面が反射した残り。クリアコートは、もう 1 枚の鏡面で、下の層を、反射した分だけ暗くします。鏡面には、上（+Y）が明るく、下が暗い、ぼんやりした映り込みが入ります（金属が、真っ黒にならない）。
- **層を混ぜる**（`SubstrateMix`）: フルの評価では、値を混ぜずに、**層ごとに陰影を計算してから、重み（`Mix`。0〜1 に収める）で足します**。金属と、さび（非金属）を混ぜても、境目が「半分だけ金属」の、ありえない材質になりません。いちどに計算する層は、4 つまでです（`NMaterialSubstrate::MaxSlabs`）。それより多く混ぜると、その `SubstrateMix` の、それぞれの入力の下を、値を混ぜた 1 つの層にまとめ、Info の診断で知らせます。
- **層を重ねる**（`SubstrateLayer`。`Top`・`Bottom`・`Transmittance`・`Coverage`）: 上の層（`Top`）の下に、下の層（`Bottom`）が透けて見えます。下の層は、**上の層が反射しなかった光**（斜めから見るほど、少ない。金属は、通さない）**に、通り抜ける色（`Transmittance`。白 = 無色透明、色つき = 色ガラス・色つきのニス、黒 = 通さない）を掛けた分**だけ見えます。上の層の中で散らばる光（拡散・サブサーフェス）は、下へ抜ける分だけ減ります（光が増えない）。`Coverage`（0〜1）は、上の層が覆っている割合です（水たまり・はげた塗装・ほこり）。重ねたものを、さらに重ねたり、混ぜたりできます（合わせて 4 層まで。超えたら、その入力の下を 1 つの層にまとめ、Info で知らせます）。
- **法線マップ**（`NormalMap`。`Color`・`Strength`）: 法線マップの色（0〜1。DirectX の形式: 赤は U の向き、緑は V の向き、青は面の外向き）を、ワールドの法線にします。画像なら `TextureSample` の色を、手続きなら、ノイズなどから作った色をつなぎます。OpenGL の形式（緑が逆）の画像は、緑を `OneMinus` で反転してからつなぎます。接線と従接線は、画素ごとに、位置と UV の変化から作るので、メッシュに接線が無くても使えます（`WorldTangent`・`WorldBitangent` ノードでも読めます）。
- **簡略版**（`bFullSubstrate` が false のバックエンド）: 拡散・鏡面（Blinn-Phong 風）・発光・クリアコートの近似。`Subsurface`・`Anisotropy`・`ThinFilm` は無視し、つないでいれば、Info の診断で知らせます。`SubstrateMix` と `SubstrateLayer` は、値を混ぜた 1 つの層になります（透き通った上の層は、クリアコートになる）。光は、平行光の向きだけを使います（色・点光源・環境マップ・影は、無し）。
- **DefaultLit**: フルの評価のバックエンドでは、DefaultLit のマテリアルも、この計算の、層 1 つ（`F0` は `Specular` の 8%）として描きます。光・影・映り込みが、どのマテリアルでも揃います。
- **計算の置き場**: `Library/Framework/Material/MaterialSubstrate.h`。同じ計算を、C++（`NMaterialSubstrate::ShadeSlab`）と、HLSL の関数（`GetHlslFunctions`。`SubstrateShadeSlab`）の両方で持ちます。HLSL の関数は、定数バッファもレジスタも使わない、ただの関数なので、どのバックエンドでも、同じ文字列を使えます。検査が、本物の画面の画素と、C++ の結果を比べています（片方を変えたら、もう片方も変えます）。
- **新しいバックエンドを足すとき**: フルに評価できるなら、`CMaterialSubsystem::SetCapabilities` で `bFullSubstrate` を true のままにし、生成された HLSL（`CMaterial::GetHlsl`。環境マップ・影を読む版は `GetHlsl( Features )`）を、そのまま使います。できないなら false にすると、簡略版の HLSL が生成されます。場面の定数（`cbScene`）の並びは `MaterialSceneConstants.h`、画像を置く段は `NMaterialHlsl::GetEnvironmentRegister`・`GetShadowRegister` です。

```cpp
// CPU で、Substrate のマテリアルの色を計算する（検査や、ピクセルシェーダーの無いバックエンド用）
TArray<FVector4> Values;
Material->EvaluateCpu( Input, Values );
TArray<FSubstrateSlab> Slabs;
NMaterialSubstrate::ReadSlabs( Material->GetProgram(), Values, Slabs );
FSubstrateGeometry Geometry;   // Normal・Tangent・ToLight・ToCamera・Position
const FSubstrateLighting Lighting = Scene.MakeSubstrateLighting();   // 場面の光（FMaterialSceneState）と同じもの。影の中なら、Lighting.Shadow を 0 に
const FVector3 Color = NMaterialSubstrate::Shade( Slabs, Geometry, Material->GetProgram().bFullSubstrate, nullptr, Lighting );
```

## 場面の光（`FMaterialSceneState`）

光を計算する Surface のマテリアル（DefaultLit・Substrate）は、`BeginSurface` に渡す場面の状態から、光を受けます。

|項目|意味|
|---|---|
|`LightDirection`・`LightColor`|平行光（太陽）が進む向きと、色 x 強さ（既定は、白の 0.85）|
|`PointLights`・`NumPointLights`|点光源（4 つまで。`FSubstratePointLight`: 位置・色 x 強さ・半径）。光は、半径で 0 になるように、なめらかに弱まる|
|`SkyColor`・`GroundColor`・`AmbientColor`|環境マップが無いときの環境。鏡面には、上（+Y）が空の色、下が地面の色の、ぼんやりした映り込み。粗い面と拡散は、`AmbientColor`|
|`EnvironmentMap`・`EnvironmentIntensity`|環境マップ（`CMaterialEnvironmentMap`）と、明るさの倍率。あれば、上の 3 つの色の代わりに、これが映り込み、まわりからの光にもなる|
|`ShadowMap`|平行光の影（`CMaterialShadowMap`）。無ければ、影は落ちない|

```cpp
// 読み込みのとき（メインスレッド）
TSharedPtr<CMaterialEnvironmentMap> Environment = CMaterialEnvironmentMap::Load( *Assets, "Engine:/Environments/Sky.png" );
TSharedPtr<CMaterialShadowMap> Shadow = CMaterialShadowMap::Create( *Assets, "SceneShadow", 2048 );
FMaterialHlslFeatures Features;
Features.bEnvironmentMap = true;
Features.bShadowMap = true;
Material->PrepareShaders( Features );   // 環境マップ・影を読む版のシェーダーを、先に作っておく

// 毎フレーム（オブジェクトの Draw の中で）: ① 影を落とす形を、影のマップへ描く
Shadow->SetLight( Scene.LightDirection, Center, Radius );   // 影を落とす範囲（中心と半径）
if ( Shadow->Begin( Draw ) )
{
    Shadow->DrawMesh( Draw, Vertices, NumVertices, Indices, NumIndices, World );
    Shadow->End( Draw );
}
// ② 場面を描く
Scene.EnvironmentMap = Environment;
Scene.ShadowMap = Shadow;
Scene.NumPointLights = 1;
Scene.PointLights[0].Position = FVector3( 0.0f, 3.0f, 0.0f );
Material->BeginSurface( Draw, Scene );
```

- **環境マップ**: 横長の全周の絵（正距円筒図法。幅が高さの 2 倍。上端が真上、絵の中央が +Z の向き）から作ります。作るときに、粗さごとにぼかした絵を用意するので（`FSubstrateEnvironment`。CPU だけで作れる）、粗い面には、ぼけた映り込みが出ます。いちばんぼかした絵は、まわりからの光（拡散）になります。色は 0〜1（8 ビット）で、明るさは `EnvironmentIntensity` で調整します。組み込みの空は `Engine:/Environments/Sky.png` です。
- **影**: 光から見た深度を、画像（影のマップ）に描いておき、マテリアルが、それと比べます。境目は、まわりの画素をならして、ぼかします。影を落とす範囲（中心から半径の球）の外には、影が落ちません。範囲が広いほど、影は粗くなります。面に縞が出るときは `DepthBias`・`NormalOffsetTexels` を大きくします。DxLib の影（`CShadowMap`）とは別の仕組みで、マテリアルのシェーダーだけで完結します。
- **スレッド**: 環境マップ・影を読む版のシェーダーは、メインスレッドでしか作れません。`BeginSurface` は、メインスレッドなら自分で作りますが、ほかのスレッドで記録する描画（描画システムが、オブジェクトの `Draw` を並列に記録するとき）では、作ってあるものだけを使います。先に `PrepareShaders( Features )` を呼んでおいてください（作っていなければ、その画像を使わずに描きます）。
- **制限**: 影は、平行光だけです（点光源の影は、落ちません）。影を落とすのは、形の全体です（マテリアルの不透明度は、見ません）。環境マップは、場面に 1 つです（場所ごとの映り込みや、動く物の映り込みは、ありません）。

## 動かす（`CMaterial`）

```cpp
TSharedPtr<CMaterial> Material = CMaterial::Create( Definition );     // コンパイルまで行う
if ( !Material->IsValid() ) { /* Material->GetDiagnostics() */ }
Material->SetVector( "Tint", FVector4( 0.0f, 1.0f, 0.0f, 1.0f ) );

// UI・Particle: 矩形に描く
Material->DrawRect( Draw, X, Y, Width, Height, FColor32( 255, 255, 255, 255 ), FrameState );
// Particle: ワールドの四角形（粒子）を描く。先に、メインスレッドで PrepareShaders を呼んでおく
if ( Material->BeginParticles( Draw, FrameState ) )
{
    Material->DrawParticleQuad( Draw, Quad, ParticleState );   // FDrawQuad3DData（位置・大きさ・向き・色）と、粒子ごとの数（FMaterialParticleState。省ける）。何度でも
    Material->EndParticles( Draw );
}
// PostProcess: シーンの絵（と深度）を読んで、矩形に描く
Material->DrawPostProcess( Draw, X, Y, Width, Height, SceneImage, DepthImage, FrameState );
// Surface: 標準の頂点シェーダーで、メッシュ（FShaderVertex3D）を描く。SceneState は、カメラと光（上の「場面の光」）
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
|`Surface/BrushedMetal`|一方向に磨いた金属。光沢が伸びる（Substrate。`Metal`・`Roughness`・`Metallic`・`Anisotropy`）|
|`Surface/Pearl`|真珠。見る角度で色が変わる（Substrate。`Base`・`Roughness`・`ThinFilm`）|
|`Surface/Wax`|ろう。光が回り込む（Substrate。`Base`・`Scatter`・`Roughness`）|
|`Surface/RustedMetal`|さびた金属。金属と、さびの層を、ノイズで混ぜる（Substrate。`Metal`・`Rust`・`MetalRoughness`・`Scale`・`Amount`）|
|`Surface/CandyPaint`|キャンディ塗装。金属の上に、色のついた透明な層を重ねる（Substrate の `SubstrateLayer`。`Metal`・`MetalRoughness`・`Tint`・`CoatRoughness`・`Coverage`）|
|`Surface/HammeredMetal`|槌目の金属。ノイズから、でこぼこの法線を作る（Substrate の `NormalMap`。`Metal`・`Roughness`・`Scale`・`Strength`）|

## プレビュー用のメッシュ

`NMaterialPreview::MakeSphere` / `MakeCube` / `MakePlane` が、`FShaderVertex3D` とインデックス（`uint16`）のメッシュを返します（半径 1、UV は 0〜1、法線は外向き）。`DrawSurfaceMesh` に渡せます。

## マテリアルエディタ（`Editor.exe` の Material editor）

ノードのグラフを、見ながら作って、保存できます（`Library/DebugUI/Material`。`Editor.exe --tool=material`、`--material=Engine:/Materials/Surface/Marble.gxmaterial` で、開いて始められます）。

- 中央: グラフ。ノードはドラッグで動かし、右の出力のピンから、別のノードの左の入力のピンへドラッグでつなぎます（つないだ入力のピンを押すと、線を外して、つなぎ直せます）。右端の「Result」が、ドメインごとの出力のピン（Surface なら BaseColor・Roughness など）です。右クリック（または左の一覧）でノードを足し（検索つき。使えないドメインのノードは出しません）、Delete で消します。背景のドラッグでパン、ホイールでズームです。
- 左: ノードの一覧と、パラメーター（プレビューの値を変えられます）。下: 診断の一覧（押すと、そのノードへ移ります。エラーのノードは赤、警告は黄の枠になります）。
- 右: 選んだノードの設定（名前・値・成分の数・色・つないでいないピンの数）と、プレビュー（Surface は球・立方体・板をドラッグで回し、UI・Particle は矩形（後ろに市松模様）、PostProcess は決まった場面に掛けます）。
- 変更するたびに、コンパイルし直して（`CMaterial::Reload`）、診断とプレビューを更新します。Arrange は、つながり方に合わせて、左から右へ並べ直します（位置の無いマテリアルは、開くときに並べます）。
- 保存は `.gxmaterial`（JSON）です。`Engine:/` の組み込みのマテリアルは読み取り専用なので、別の名前で保存します。

## エフェクトの粒子に使う（Particle ドメイン）

`.gxeffect` のエミッターに `"MaterialAsset": "パス.gxmaterial"` と書くと、その四角形の粒子を、Particle ドメインのマテリアルで描きます。

```json
{
  "Name": "Flames",
  "MaterialAsset": "Engine:/Materials/Particle/Flame.gxmaterial",
  "Blend": "Add",
  "Rate": 10,
  "Size": { "Start": [0.7, 1.0], "End": 0.35 },
  "Color": { "Start": "#FFFFFF90", "End": "#FF906000" }
}
```

- 粒子の色（寿命に沿って変わる色と不透明度）は、マテリアルの出力（`Color`・`Opacity`）に掛かります。マテリアルの中で、もう一度掛ける必要はありません（`VertexColor` ノードでも読めます）。
- `TexCoord` は、四角形の左上が (0, 0)、右下が (1, 1) です。`Time` は、再生を始めてからの秒です（ループでも巻き戻りません）。
- 画像は、マテリアルの中のもの（`TextureSample`）を使います。エミッターの `Texture` は使いません。
- 向き（`Camera`・`CameraY`・`Fixed`）・大きさ・回転・ブレンドは、ふつうの粒子と同じです。奥行きの比べ方は、いまの描画の状態に従います。
- 四角形の粒子だけに使えます。帯・雷・メッシュ・コマ送りのエミッターに書くと、警告して無視します。
- マテリアルが読めない・Particle ドメインでない・シェーダーを使えない（ピクセルシェーダーの無いバックエンド）ときは、警告して、従来の描き方（画像の四角形）に戻ります。
- **粒子ごとの入力**（Particle ドメインだけ）: `ParticleLife`（寿命の位置。生まれたときが 0、消えるときが 1）・`ParticleAge`（生まれてからの秒）・`ParticleRandom`（粒子ごとに決まった、0〜1 のばらばらな数。消えるまで変わらない）・`ParticleSpeed`（速さ。ワールドの長さ / 秒。向きは読めません）。模様を粒子ごとにずらす・寿命で溶かす・速い粒子ほど明るくする、などに使います。矩形に描くとき（エディタのプレビュー・`DrawRect`）は、どれも 0 です。
- Effect Editor では、エミッターの **Material** で選びます（アセットのフォルダの `.gxmaterial` の一覧か、パスを直接。組み込みの炎・光の玉は、ボタンで入ります）。
- 組み込み: `Engine:/Materials/Particle/SoftGlow.gxmaterial`（やわらかい光の玉）・`Flame.gxmaterial`（上へ流れるノイズの炎）。見本は、サンプル `Effects` の **Material flame** です。
- 描く命令は `FDrawContext::ShaderQuad3D( 四角形, 4 つの数 )`（`Quad3D` と同じ向きの決め方の四角形を、決めてあるピクセルシェーダーで描く。4 つの数は、ピクセルシェーダーの `float4 : TEXCOORD1` に届く）です。自分のシェーダーでも使えます。

## これから

点光源の影、場所ごとの環境マップ（反射プローブ）、1 を超える明るさを持つ環境マップ（HDR）。
