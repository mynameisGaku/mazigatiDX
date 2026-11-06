# mazigatiDX Samples

mazigatiDX の機能を、1 つずつ動かして見られるサンプル集です。起動すると、サンプルの一覧が出ます。上下キーで選んで Enter で動かし、サンプルの中から Esc で一覧へ戻ります。

## 動かす

1. `project.sln` を開き、`Samples` プロジェクトを x64 の Debug か Release でビルドします（フレームワーク本体の `mazigatiDX` が先にビルドされます）。
2. `Samples` を起動します。作業フォルダーは `Samples` フォルダーです（`Config` と `Assets` を使います）。
3. サンプルを直接動かすには、起動引数 `--sample=Hello` のように、サンプルの名前を付けます。

デバッグ表示は、F1（ウィンドウ）、F2（プロファイラー）、F3（コンソール。Debug だけ）、F11（フルスクリーン）です。

## サンプル

|名前|内容|
|---|---|
|`Hello`|図形・文字を、オブジェクトの `Draw( FDrawContext& CommandList )`（コマンドリストへの記録）で描く。`DeltaTime` で動くオブジェクト|
|`Shapes`|2D の図形を、整数の座標（ギザギザ）と、なめらかな `*AA`（float の座標）で、同じ動きで並べて比べる|
|`Images`|1 枚の画像を、等倍・拡大・自由な四角形・一部分・基準点と回転・タイル・円グラフの 7 通りで描く（画像は、メモリの上で作る）|
|`DrawState`|描画の状態を、動かして見る。ブレンド（6 種類とカスタムブレンド）・加算する色・アルファテスト、クリップのウィンドウ（入れ子）、Nearest と Bilinear の拡大、拡大・回転・移動する 2D のカメラ|
|`Filters`|画像の処理とマスク。1 枚の画像に、フィルター（単色化・反転・ぼかし・色相・レベル補正・2 階調化・グラデーションマップ）と、別の画像との混ぜ方（乗算・加算・スクリーン・減算）を並べる。動くスポットライトの形のマスクで、絵を段階的に見せる|
|`Pixels`|CPU 側の画像（`FPixelImage`）を、動かして見る。画素を毎フレーム書いて画像を更新する（模様の上に、半透明の光を重ねる）、スプライトシートを格子で 1 コマずつの画像に分ける、S キーで PNG に保存して読み直す|
|`RenderTarget`|描画先（描き込める画像）を、動かして見る。ミニマップへ描いて、ふつうの画像として貼る（等倍・2 倍）、画面の一部を別の描画先へ写す、描画先から色を読む、S キーで PNG に保存する|
|`Text`|文字とフォント。フォントの指定（アンチエイリアスの段階・斜体と太さ・縁取り・字間・行の送り幅・文字の代わりのアイコン）を 1 つずつ変えた行、幅が動く箱への折り返し（日本語と英語。句読点は行頭に来ない）と、右揃え・3 行で切る箱、拡大・回転・影・基準点・縦書き|
|`Village`|小さな村を歩き回る、トップダウンの 2D のゲーム画面（2D の機能を 1 つにまとめた見本）。タイルの地図、2D の変換で主人公を追うカメラ、歩く主人公（キーを押すまでは、ひとりで歩く）、回る金貨、村人との会話（折り返し・1 文字ずつ表示）、夜の明かり（描画先とカスタムブレンド）、金貨の数と体力（円グラフ）とミニマップ|
|`Camera`|3D のカメラ。注視点のまわりを回る軌道カメラ（透視射影 / 正射影、視野角）で景色を見る。真上から見下ろす 2 つ目のカメラを別の描画先に描いて右下に貼り、メインのカメラが映す地面の範囲を重ねる。世界の点が画面のどこに見えるか（WorldToScreen）で物の名前を置き、マウスの位置を通る光線（ScreenToRay）で指している物を調べ、視錐台に入らない物は描かない|
|`RenderState`|3D の描画状態。一つの舞台に、描画状態の使いどころを並べる: 深度バッファ（半透明のガラス）、カリング（部屋）、画像の貼り方と変換（壁・水面）、距離のフォグと高さのフォグ（遠くの柱・足元の霧）、フィルターと異方性（地面の市松模様）、3D 図形の変換の積み重ね（腕）、深度バッファの消去（手前に描く光線）。数字キーの 1〜8 で切り替える|
|`Lighting`|3D のライティング。夜の中庭を、月明かり（方向ライト）・床の上を回る電球（点光源）・床を舐めるスポットライト・周りの街灯（同時に 6 つまで）で照らす。材質の違う 5 つの球（つや消し・つや・自己発光・プラスチック・金色の金属）と、床・壁・柱・箱の陰影。数字キーの 1〜9 で、各ライト・全体の環境光・画素ごとの光・ハイライト・やわらかい光・街灯の数・光の計算そのものの入り切りを切り替える|
|`Shapes3D`|3D 図形の追加。夜の広場に、松（円錐）・星（点）・蛍（点の並び）・座標軸とらせん（線）・波（帯）・傘（扇）・宝石（インデックスつきの三角形）と、カメラへ向ける画像（足元が基準点の草・4 つの角を動かす旗・回る星・脈打つ心臓・揺れるランタン）と、頂点バッファ・インデックスバッファで描く森（木 1 本の形を、位置を変えて何度も描く）・池の水面（頂点を毎フレーム書き換える）を並べる。数字キーの 1〜7 で、円錐の線描き・フォグ・多角形のカリング・ビルボードの基準点・画像の半透明・光の入り切り・森の木の数を切り替える|
|`Shadows`|影（シャドウマップ）。昼の中庭に、家・木・木箱・すき間のある柵・歩き回る旅人を置き、動く太陽の影を落とす。影の絵（`BeginShadowMap` ～ `EndShadowMap`）を毎フレーム描き、`UseShadowMap` で影を受ける。数字キーで、影の入り切り・影の絵の大きさ・影を計算する範囲・太陽の動き・補正の深さ（0 だと床が縞になる）・影の絵の中身の表示（`PreviewShadowMap`）・ランタン（影を受けない点光源。`FLight::ShadowMapSlots`）を切り替える。マウスで回り、ホイールで近づく|
|`Shaders`|シェーダー。庭に球と柱を置き、自分で書いた HLSL のシェーダー（物の行列とカメラの行列で位置を決める頂点シェーダー、トゥーン・法線の色・奥行き・波紋・市松模様のピクセルシェーダー）で描画先へ描き、ポストエフェクト（白黒・周りを暗く・モザイク・ゆがみ。`ShaderRect`）をかけて画面に貼る。数字キーの 1〜4 で、物の見え方・地面・ポストエフェクト・時間を切り替える|
|`PostEffects`|描画先とポストエフェクト。夜の広場（光る玉・ネオンの溝）を 16 ビットの浮動小数点の描画先へ描き（1 を超える明るさが残る）、同じピクセルシェーダーで深度を 32 ビットの描画先へ同時に書く（`ShaderOutputTarget`。複数の出力）。明るい所を抜き出してぼかし（ブルーム）、深度で霧をかけ、トーンマップ（ACES・Reinhard）で画面に貼る。1 キーで見せるもの（仕上がり・8 ビット相当・ブルームだけ・深度）、2〜5 キーでトーンマップ・ブルーム・霧・時間、Q / E キーで露出を切り替える|
|`Island`|3D の見本（ゲームの画面らしい構成）。探検家が島を歩き、光る結晶を集める。地形（頂点バッファ + インデックスバッファ。決まったシードから作る）・木と灯台（3D 図形）・空を動く太陽と影（シャドウマップ）・回る灯台の光（スポットライト）・霧・半透明の海・加算の光のにじみ・地図・名札（`WorldToScreen`）・右クリックの目的地（`ScreenToRay`）。WASD で歩き、右クリックでその場所へ歩き、ドラッグでカメラを回し、ホイールで近づく。数字キーの 1〜3 で、影・霧・時刻を切り替える|
|`Input`|キーボード・マウス・ゲームパッドの状態を表示する|
|`AI`|AI（ステートマシンとビヘイビアツリー）。Hero（`Hero.gxstate`。Idle / Run / Attack / Hurt の階層ステートマシン）を A / D キーで歩かせ、Space / J で攻撃させる。Guard（`Guard.gxbt`。見回り・追いかけ・攻撃のビヘイビアツリー）が Hero を見つけて追う。F1 > AI Debugger で、ステート・ノード・ブラックボードを見て、グラフの矢印が遷移で光るのを確認できる（`--tool=ai` で最初から開く）|

## 構成（クラスとファイルの分け方）

```
Samples/
  Source/
    main.cpp                    入口（NGameMain::RunGame を呼ぶだけ）
    Application/                ゲームインスタンス（CSamplesApplication）。一覧の画面と、すべてのサンプルを登録する
    Catalog/                    サンプルの一覧（CSampleCatalog）、サンプルの情報（FSampleInfo）、サンプルのシーンの基底（CSampleScene）
    Browser/                    一覧の画面（CBrowserScene）
    Hello/                      サンプル Hello: シーン（CHelloSample）と、それが置くオブジェクト（CShapeGallery、CBouncingBox）
    Shapes/                     サンプル Shapes: シーン（CShapesSample）と、オブジェクト（CShapeShowcase）
    Images/                     サンプル Images: シーン（CImagesSample）と、オブジェクト（CImageShowcase）
    DrawState/                  サンプル DrawState: シーン（CDrawStateSample）と、オブジェクト（CBlendShowcase、CViewShowcase）
    RenderTarget/               サンプル RenderTarget: シーン（CRenderTargetSample）と、オブジェクト（CTargetShowcase）
    Filters/                    サンプル Filters: シーン（CFiltersSample）と、オブジェクト（CFilterGallery、CSpotlightView）、画像を CPU で作る関数（NFilterImages）
    Pixels/                     サンプル Pixels: シーン（CPixelsSample）と、オブジェクト（CPlasmaView、CTileView）、シートの画像を CPU で作る関数（NSpriteSheetMaker）
    Text/                       サンプル Text: シーン（CTextSample）と、オブジェクト（CFontShowcase、CWrapShowcase、CStyledShowcase）
    Camera/                     サンプル Camera: シーン（CCameraSample）と、オブジェクト（CCameraRig、CMainView、CTopView、CLabelOverlay）、共有する状態（FCameraDemoState）、景色（NScenery）
    RenderState/                サンプル RenderState: シーン（CRenderStateSample）と、オブジェクト（CRenderStateRig、CStageView、CInfoPanel）、共有する状態（FRenderStateDemoState）、舞台（NStageScenery）
    Lighting/                   サンプル Lighting: シーン（CLightingSample）と、オブジェクト（CLightingRig、CLightingView、CLightingPanel）、共有する状態（FLightingDemoState）、中庭（NLightingScenery）
    Shapes3D/                   サンプル Shapes3D: シーン（CShapes3DSample）と、オブジェクト（CShapes3DRig、CShapes3DView、CShapes3DPanel）、共有する状態（FShapes3DDemoState）、広場（NShapes3DScenery）
    Shadows/                    サンプル Shadows: シーン（CShadowsSample）と、オブジェクト（CShadowsRig、CShadowsView、CShadowsPanel）、共有する状態（FShadowsDemoState）、中庭（NShadowsScenery）
    Shaders/                    サンプル Shaders: シーン（CShadersSample）と、オブジェクト（CShadersRig、CShadersView、CShadersPanel）、共有する状態（FShadersDemoState）、HLSL のソース（NShadersSources）
    PostEffects/                サンプル PostEffects: シーン（CPostEffectsSample）と、オブジェクト（CPostEffectsRig、CPostEffectsView、CPostEffectsPanel）、共有する状態（FPostEffectsDemoState）、シェーダーのソースと定数（NPostEffectsSources）
    Island/                     サンプル Island: シーン（CIslandSample）と、オブジェクト（CIslandClock、CIslandExplorer、CIslandCrystals、CIslandView、CIslandHud）、共有する状態（FIslandState）、地形（NIslandTerrain）、舞台（NIslandScenery）
    AI/                         サンプル AI: シーン（CAISample）と、オブジェクト（CAIActor、CAIHero、CAIGuard）、AI の部品（AIParts）
    Village/                    サンプル Village: シーン（CVillageSample）と、オブジェクト（CTileMap、CPlayer、CCoin、CVillager、CDialogue、CDarkness、CStatusPanel）、共有する状態（FVillageState）、絵を CPU で作る関数（NVillageArt）
    Common/                     複数のサンプルで使うもの。確認用の画像を、メモリの上で作る関数（NTestPattern）、光の当たらない側を真っ黒にしない、やわらかい光（FSoftLightingScope）、シェーダーで描く形（球・立方体・地面。NShaderMeshes）
    Input/                      サンプル Input: シーン（CInputSample）と、オブジェクト（CKeyboardView、CMouseView、CGamepadView）
  Config/ScreenConfig.json      ウィンドウの設定
  Assets/                       画像・音・モデルの置き場
```

- **1 つのサンプル = 1 つのフォルダー**。フォルダーの中は、シーン 1 つと、そのシーンが置くオブジェクトのクラスです。**1 ファイルに 1 クラス**です。
- **シーンは、何をどこに置くかだけを決めます**。描き方・動き方は、オブジェクトのクラスが持ちます。
- サンプルは、自分の `.cpp` の中で `TSampleRegistrar` を使って、自分を一覧へ登録します。サンプルを足しても、アプリケーションや一覧の画面は書き換えません。

## サンプルを足す

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\NewSample.ps1 -Name Physics -Title "Physics: bodies and joints" -Description "Bodies, joints and contacts."
```

`Samples/Source/Physics/` に、シーンのクラス（`CPhysicsSample`）が作られ、プロジェクトのファイル一覧も更新されます。あとは、同じフォルダーに、オブジェクトのクラスを 1 ファイルずつ足していきます。

## ライセンス

このフォルダーのコードは MIT ライセンスです。
