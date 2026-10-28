# mazigatiDX Samples

mazigatiDX の機能を、1 つずつ動かして見られるサンプル集です。起動すると、サンプルの一覧が出ます。上下キーで選んで Enter で動かし、サンプルの中から Esc で一覧へ戻ります。

## 動かす

1. `project.sln` を開き、`Samples` プロジェクトを x64 の Debug か Release でビルドします（フレームワーク本体の `mazigatiDX` が先にビルドされます）。
2. `Samples` を起動します。作業フォルダーは `Samples` フォルダーです（`Config` と `Assets` を使います）。
3. サンプルを直接動かすには、起動引数 `--sample=Hello` のように、サンプルの名前を付けます。

デバッグ表示は、F1（窓）、F2（プロファイラー）、F3（コンソール。Debug だけ）、F11（フルスクリーン）です。

## サンプル

|名前|内容|
|---|---|
|`Hello`|図形・文字を、オブジェクトの `Draw( FDrawContext& CommandList )`（コマンドリストへの記録）で描く。`DeltaTime` で動くオブジェクト|
|`Shapes`|2D の図形を、整数の座標（ギザギザ）と、なめらかな `*AA`（float の座標）で、同じ動きで並べて比べる|
|`Images`|1 枚の画像を、等倍・拡大・自由な四角形・一部分・基準点と回転・タイル・円グラフの 7 通りで描く（画像は、メモリの上で作る）|
|`DrawState`|描画の状態を、動かして見る。ブレンド（6 種類とカスタムブレンド）・加算する色・アルファテスト、クリップの窓（入れ子）、Nearest と Bilinear の拡大、拡大・回転・移動する 2D のカメラ|
|`Pixels`|CPU 側の画像（`FPixelImage`）を、動かして見る。画素を毎フレーム書いて画像を更新する（模様の上に、半透明の光を重ねる）、スプライトシートを格子で 1 コマずつの画像に分ける、S キーで PNG に保存して読み直す|
|`RenderTarget`|描画先（描き込める画像）を、動かして見る。ミニマップへ描いて、ふつうの画像として貼る（等倍・2 倍）、画面の一部を別の描画先へ写す、描画先から色を読む、S キーで PNG に保存する|
|`Input`|キーボード・マウス・ゲームパッドの状態を表示する|

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
    Pixels/                     サンプル Pixels: シーン（CPixelsSample）と、オブジェクト（CPlasmaView、CTileView）、シートの画像を CPU で作る関数（NSpriteSheetMaker）
    Common/                     複数のサンプルで使うもの。確認用の画像を、メモリの上で作る関数（NTestPattern）
    Input/                      サンプル Input: シーン（CInputSample）と、オブジェクト（CKeyboardView、CMouseView、CGamepadView）
  Config/ScreenConfig.json      窓の設定
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
