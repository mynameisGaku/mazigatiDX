# R3 G-buffer と Deferred 照明

2026-11-21。R3 の「G-buffer、深度からの位置再構成、Deferred 照明、HDR のシーンカラーから SDR 表示まで」の最初の縦の経路を、公開のシェーダー窓口（`CShader`・複数出力 `ShaderOutputTarget`・`ShaderTexture`）の上に作った。DxLib・DX12 のどちらでも、同じコードと同じ HLSL で動く。描画先は 8 ビット・16 ビット・32 ビットの浮動小数点を混ぜた 4 枚で、DX12 の MRT と DxLib の複数出力の既存の接続を使う。

## 構成

|配置|役割|
|---|---|
|`Library/Framework/Render/Deferred/GBuffer.*`|`CGBuffer`: 描画先 4 枚の作成・受け取り（形式の検査つき）、ジオメトリのパスの始まりと終わり、ライティングのパスの画像の置き方を記録する|
|`Library/Framework/Render/Deferred/GBufferView.*`|`FGBufferView`: 深度と画面の位置から世界の位置を求める基準。CPU の計算と、64 バイトのシェーダー定数を兼ねる|
|`Library/Framework/Render/Deferred/DeferredShaders.*`|`NDeferredShaders`: 製品の HLSL の正本（G-buffer の書き出し・読み出し）を文字列で渡す|
|`EngineContent/Shaders/Deferred/GBufferWrite.hlsli`・`GBufferRead.hlsli`|G-buffer へ書く（`PackGBuffer`）部品と、読んで位置を再構成する（`LoadGBuffer`・`ReconstructWorldPosition`）部品|
|`Samples/Assets/Deferred/Sample*.hlsl`・`Sample*.hlsli`|サンプルだけが使う 8 つのシェーダーと共通の部品。製品には含まれないので、`EngineContent` ではなく、サンプルの素材に置き、サンプルが実行時に読んで、製品の部品と連ねてコンパイルする（[シェーダーの管理](SHADERS.md)）|
|`Samples/Source/Deferred`|サンプル `Deferred`: 夜の広場（動く光 64 個・光る溝・球と柱・半透明のガラス）を Forward と Deferred で描く。光は、タイルごとの一覧で絞れる（[R4 ライトカリング](R4_LIGHT_CULLING.md)）|

## G-buffer の中身

ジオメトリのパスのピクセルシェーダーが `PackGBuffer` の結果を返すと、1 回の描画で次の 4 枚へ書かれる（`EGBufferTarget` の並びが、出力 `SV_Target0〜3` と、ライティングで画像を置く段 `t0〜t3` に対応する）。

|出力・段|内容|形式|何も描かれていない画素|
|---|---|---|---|
|0 Albedo|地の色 rgb|8 ビット RGBA。ジオメトリのパスの描き先で、奥行きバッファを持つ|黒|
|1 Normal|世界の法線 xyz（長さ 1）|16 ビット浮動小数点 RGBA|0|
|2 Depth|カメラの前向きの奥行き / Far|32 ビット浮動小数点 R|1|
|3 Emission|自分で光る色 rgb（1 を超えてよい）|16 ビット浮動小数点 RGBA|0|

`CGBuffer::Adopt` は、4 枚が同じ大きさで、この表の形式・チャンネル数・奥行きバッファの有無・マルチサンプルなしに合うときだけ受け取る（違えば何も変えず `false`）。`Create` は `CAssetSubsystem` で 4 枚を作ってから、同じ検査を通す。

## 深度と位置の再構成

深度は「カメラからの距離」ではなく「**カメラの前向きの奥行き / Far**」で保存する。距離だと、Far の面の隅で 1 を超えるので、何も描かれていない印（1）と区別できなくなる（広角では距離が Far の 4 倍になる点がある）。前向きの奥行きなら、視錐台の中で 0 以上 1 以下に収まり、位置の再構成が正規化なしの一次式になる。

`FGBufferView::Make( Camera, Viewport, View )` は、Far の面の画面の端の位置を、射影行列と同じ規則（縦横比 ÷ 横の拡大、レンズのずらし）で、逆行列を使わずに求め、「カメラから Far の面の点までのベクトル」を画面の位置 UV の一次式（`RayOrigin + RayU * u + RayV * v`）で表す。世界の位置は `CameraPosition + (そのベクトル) * 深度`。正射影は、光線の始点が画面の位置で変わるので扱わず、`Make` は `false` を返す。

```cpp
CGBuffer GBuffer;
GBuffer.Create( *Assets, "scene", 1280, 720 );

// ジオメトリのパス（FDrawContext の中。ピクセルシェーダーは PackGBuffer を返す）
GBuffer.BeginGeometryPass( CommandList );
// ... UseVertexShader / UsePixelShader、PrimitiveIndexedShader3D ...
GBuffer.EndGeometryPass( CommandList );

// ライティングのパス: 画像の段 0〜3 に G-buffer を置いて、画面全体の四角形を描く
FGBufferView View;
FGBufferView::Make( Camera, FCameraViewport { 1280, 720 }, View );
CommandList.PushTarget( HdrColor );
GBuffer.UseForLighting( CommandList );
CommandList.ShaderConstants( EShaderStage::Pixel, CGBuffer::ViewConstantSlot, View );
CommandList.UsePixelShader( Lighting );
CommandList.ShaderRect( 0.0f, 0.0f, 1280.0f, 720.0f );
CommandList.PopTarget();
```

画像は最近傍・端は伸ばす設定で読む（`UseForLighting` が設定する）。四角形は、G-buffer と同じ大きさの描画先の全体へ描く。

## Forward と同じ絵になること

サンプル `Deferred` は、Forward（物を描くときに光を計算する）と Deferred（G-buffer へ書いて、読み戻して計算する）が、同じ光の式 `ShadeView`（`SampleCommon.hlsli`）を使う。キー 1 で見せるもの（仕上がり・地の色・法線・位置・深度・自分で光る色）、キー 2 で Forward / Deferred を切り替える。**位置**は、再構成した世界の位置を `0.5 + 0.5 * sin( 位置 * 4 )` の模様にしたもので、再構成が少しでもずれると、模様がずれて画素の差になる。

仕上がりは、16 ビット浮動小数点の場面の色へ描いたあと、露出と ACES のトーンマップで画面へ貼る。

コマンドラインで設定を固定できる（絵を比べる検査に使う）: `--deferred-view=<0〜5>`、`--deferred-forward`、`--deferred-time=<秒>`（時間を止め、ひとりで回るのもやめる）。

## 半透明の合成（Forward 透明合成）

不透明の物だけを G-buffer へ書き、光を当てた場面の色へ、半透明の物（サンプルのガラスの球 2 つと板 1 つ）を、あとから重ねる。半透明の物は、Forward で（描きながら光を計算して）、奥から手前の順に、アルファ合成で描く。奥行きは書かない（あとの半透明の物の邪魔をしない）。不透明度は、物の定数 `g_Albedo.w`。

- **Forward 経路**: 場面の色の描き先が、不透明の物の奥行きバッファを持つので、ハードウェアの奥行きの比べ（書かずに比べるだけ）で、不透明の物より奥の画素を省く。
- **Deferred 経路**: 不透明の物の奥行きバッファは G-buffer の地の色の描き先が持ち、場面の色の描き先とは別なので、ハードウェアの比べが使えない。半透明のピクセルシェーダー（`SampleTransparent.hlsl`）が、G-buffer の深度を読んで（画面の位置は `SV_Position / 描画先の大きさ`）、不透明の物より奥の画素を `clip` で省く。
- **並べ替え**: 物の中心のカメラからの距離で、奥から手前へ並べる簡単な方法。1 つの物の中の表裏は、メッシュの順。食い込んだ半透明の物どうしや、物の中を別の物が通るときの、正しい合成は扱わない。両経路が同じ順を使うので、画像の比べは、順が正しいことは確かめない（同じであることだけ）。
- **置き方**: ガラスは、地面・光る玉の輪・不透明の物と食い込まない位置に置く（食い込む線の上では、ハードウェアの奥行きバッファの精度と、線形の深度の比べで、1 画素ほど境目がずれることがある）。

## 検査

`Source/SelfTest/RunDeferredTests.ps1 -FrameworkRoot <ルート> -Configuration Debug|Release [-Platform]`

- **契約（63 項目）**: `FGBufferView` が、6 種類のカメラ（既定・向きと正方形・横の拡大・レンズのずらし・広角・狭い視野）で、`WorldToScreen`（逆行列を使わない射影）で画面へ進めた点を、画素と深度から戻す誤差が、距離の 2e-6 倍未満（実測は 5e-8〜4e-7 倍）。深度が Far の面の隅でも 1 を超えず、距離なら 1.2 倍を超える点があること。作れない入力（正射影・使えないカメラ・空の大きさ）の拒否。`CGBuffer` の形式、`Adopt` が受け取れない 9 通りの拒否と状態の保持、ジオメトリのパス・終わり・ライティングで記録する命令の列。C++ と HLSL の取り決め（定数のスロット 6・画像の段・出力の並び）。
- **HLSL の計算を、WARP の D3D11 のコンピュートシェーダーで実際に走らせ**（`Source/SelfTest/DeferredContract.hlsl`）、C++ の再構成（位置・Far の面までのベクトル）と `PackGBuffer` の 4 つの出力を比べる。定数バッファは、`FGBufferView` のメモリをそのまま渡す。差は 0。
- サンプルのシェーダー（8 つ）は、サンプルの実行で、実際にコンパイルして描くので、下の画像の比べで確かめる（コンパイルに失敗すると、ログの `Cannot compile` で検査が失敗する）。
- **`-Platform`**: `Deferred` サンプルを、DX12・WARP・DxLib の 3 つの実行先で、見せるもの 6 種類ずつ、Forward と Deferred で撮って、全画素を比べる（半透明のガラス 3 つを含む）。差が 3 を超える画素が 0（実測の最大差は 1）。場面が描かれていること（画面の 2 割以上）と、見せるものが実際に絵を変えることも確かめる。
- **変異検査**: 下の「変異検査の結果」。C++ 側の 14 件は `RunDeferredMutationTests.ps1` として、`RunAllChecks.ps1` に登録している。

## 変異検査の結果

契約の実行ファイルは `x64/<構成>/mazigatiDX.lib` にリンクし、HLSL はライブラリへ埋め込まれるので、変異ごとにライブラリを作り直して確かめた。

- **契約（Debug）37 件**: 光線の基準 12 件（Far の面の端の位置・上下の向き・レンズのずらし・横の拡大・視野角・奥行きを距離にする・カメラの位置を足さない・正射影を受ける・UV の取り違えなど）、描画先と記録 16 件（深度の消去値・出力の番号・終わりの後始末・画像の段の順・フィルター・端の扱い・`Adopt` の各検査・消去の漏れなど）、HLSL 9 件（再構成の UV の取り違え・深度を掛けない・カメラの位置を足さない・定数のスロット・法線の正規化・深度の反転・自分で光る色・色の並び・出力の並び）。**37 件すべて検出**（生き残りなし）。
- **画像でだけ見つかる HLSL の変異 12 件**（空の画素を描かれた画素として扱う・自分で光る色を半分にする・世界の位置の UV を取り違える・地の色を別の画像から読む・ライティングで深度を別のチャンネルから読む・地面の法線を下向きにする・深度を別の値で割る、半透明の奥行きの比べを外す・逆にする・画面の位置の上下を反転する、不透明度を無視する、Forward 側の不透明度を無視する）は、実行先を DX12 に絞ったサンプルの比べ（`-Platform -Modes dx12`）で確かめた。**12 件すべて検出**（差のある画素は 3,700〜55 万。地面の法線が下向きの変異は、場面が暗くなって「描かれていない」と検出）。サンプルの素材の HLSL は実行時に読むので、ライブラリを作り直さずに確かめられる。

## 現在の範囲

- 標準の材質（`FMaterialProgram` の Surface）の G-buffer への出力、深度だけのパス、描画グラフ（`CRenderGraphBuilder`）の名前付きパスとしての接続は、まだ。半透明は、サンプルの中の簡単な並べ替えで、`BeginScene3D` の分類・並べ替え（R2）とは接続していない。サンプルは、公開のシェーダー窓口を直接使う。
- G-buffer は、地の色・法線・深度・自分で光る色の 4 枚で、粗さ・金属度・材質の識別番号は持たない。ライティングは、サンプルの簡単な光の式（空・月・点の光）で、PBR ではない。
- マルチサンプル、深度の読み取り用の専用バッファ（ハードウェアの深度は、DxLib から読めないので、深度を 2 つ目の出力で書く）、光のカリング（R4 の Forward+ / Deferred+）は、まだ。
- 光の計算は、全画素で全部の光を回す。光の数に応じて重くなるので、R4 のクラスタ分割で絞る。
