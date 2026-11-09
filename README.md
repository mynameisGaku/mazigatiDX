# mazigatiDX

DxLib を使う、Windows 向けの C++ フレームワークです。描画・入力・音・アセットの窓口をインターフェースに分けて、上の層（ゲームのコード）が DxLib に依らないようにしてあります。ゲームを作るための部品（シーン・オブジェクト・ECS・ステートマシン・ビヘイビアツリー・物理・エフェクト・ワールドなど）と、それを作る道具（エディタ）を、1 つにまとめています。

## 主な機能

- **描画**: 描画命令を記録して、あとで実行する仕組み（2D・3D・ライト・影・シェーダー・描画先・ポストエフェクト）。
- **ゲームの骨組み**: シーン・オブジェクト・コンポーネント・ECS・ジョブ（並列）・サブシステム。
- **入力・音**: キーボード・マウス・ゲームパッド、ミキサーつきの音（ループ・パン・フェード・生の波形）。
- **物理**: Jolt Physics（剛体・関節・接触・空間クエリ）。
- **AI**: ステートマシン・ビヘイビアツリー・ブラックボード（データで定義して、エディタで編集）。
- **アセット**: 画像・モデル・アニメーション・サウンド・フォント・ワールドの管理。FBX / OBJ の取り込み。
- **ワールド**: レベルをファイル（`.gxworld`）で作り、タグとプロパティでコードに結び付ける。
- **エフェクト**: 粒子・リボン・雷・メッシュ・力場・材質のエフェクトの編集と再生。ピクセルアートのエフェクト。
- **セーブ**: 壊れにくい保存（一時ファイル → 置き換え、1 つ前を残す）。
- **開発の道具**: エディタ（アセット・ワールド・エフェクト・AI・サウンド・スプライト・モデルのアニメーション）、プロファイラー、コンソール、デバッグビュー、クラッシュ報告、スクリーンショットと GIF の撮影。
- **DxLib を使わない実行**（Null バックエンド）: 画面なしで、ゲームを動かして確かめられます。

## 必要なもの

- Windows 10 / 11（64 ビット）。ほかの OS は、将来の目標です（v1.0 の約束には含みません。[バージョン方針](Docs/VERSIONING.md)）。
- Visual Studio（MSVC ツールセット v145、Windows SDK 10.0.26100.0）。C++17。
- DxLib 3.24f（リポジトリには含めません。下の手順で取得します）。

そのほかの第三者のライブラリ（Jolt・Dear ImGui・SDL・nlohmann/json・ufbx）は、`VendorModules` に入っています。ライセンスは [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) と、各フォルダーにあります。

## はじめる

1. `FetchDxLib.bat` を実行します。DxLib 3.24f を、公式サイトから取得して、`C:\DxLib` へ置きます（約 206 MB。ハッシュを確かめます）。別の場所へ置くときは、`FetchDxLib.bat -Destination D:\DxLib` とし、`Build\mazigatiDX.props` の `MazigatiDxLibDir`（または同じ名前の環境変数）を合わせます。
2. `GenerateProjectFiles.bat` を実行します（プロジェクトのファイル一覧を作ります。ファイルを足した・消したときも、もう一度）。
3. `project.sln` を開いて、x64 の Debug または Release をビルドします。次の 4 つができます。

   |プロジェクト|できるもの|
   |---|---|
   |`mazigatiDX`|フレームワーク本体の静的ライブラリ|
   |`project`|ゲームの実行ファイル（最小のゲームと、デバッグ画面）|
   |`Samples`|機能別のサンプル集|
   |`Editor`|エディタ（アセット・ワールド・エフェクトなど）の実行ファイル|

4. 起動します。作業フォルダーが大事です（`Config` と `Assets` を、作業フォルダーから探します）。`project.exe` と `Editor.exe` は、リポジトリのルートで、`Samples.exe` は、`Samples` フォルダーで起動します。

   ```powershell
   .d\Debug\project.exe                        # ルートから（ゲーム）
   cd Samples; ..d\Debug\Samples.exe           # Samples フォルダーから（サンプル集）
   ```

Developer PowerShell から:

```powershell
msbuild .\project.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild .\project.sln /p:Configuration=Release /p:Platform=x64 /m
```

## サンプルを見る

`Samples.exe` を、`Samples` フォルダーで起動すると、サンプルの一覧が出ます（上下キーで選び、Enter で動かし、Esc で一覧へ戻ります）。`Samples.exe --sample=Platformer3D` のように名前を付けると、そのサンプルから始まります。1 つのサンプルは 1 つのフォルダーで、シーンと、それが置くオブジェクトのクラスを、1 ファイルに 1 クラスで書いてあります。

|分類|サンプル|
|---|---|
|描画の基礎|`Hello` `Shapes` `Images` `DrawState` `RenderTarget` `Filters` `Pixels` `Text` `PixelEffects`|
|3D|`Camera` `RenderState` `Lighting` `Shapes3D` `Shadows` `Shaders` `PostEffects` `Island`|
|入力・音・物理|`Input` `Audio` `Physics`|
|ワールド・エフェクト・AI|`World` `Effects` `AI`|
|ゲーム|`Village`（2D の見下ろし）、`Platformer2D` / `Platformer3D`（横スクロール / 3D のアクション）、`Inventory`（UI とセーブ）、`Adventure`（3D 探索アクションの参照ゲーム）|

サンプルの足し方と構成は [Samples/README.md](Samples/README.md) に、各サンプルの内容は同じ文書の表にあります。

## 新しいゲームを作る

`Tools\NewProject.ps1` が、テンプレート（`Templates\Minimal`: タイトルから、矢印キーで四角形を動かす画面へ進む最小のゲーム）から、新しいプロジェクトを作ります。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\NewProject.ps1 -Name MyGame -Destination C:\dev
```

作ったプロジェクトの `README.md` に、構成と、シーン・オブジェクトの足し方があります。

## 文書

|文書|内容|
|---|---|
|[開発手順](Docs/DEVELOPMENT.md)|構成と依存、各機能の使い方、コードの書き方の決まり、プロジェクトの生成と確認、変更の統合|
|[UI](Docs/UI.md)|UI（ウィジェット）の構成・使い方・レイアウト・ブラシ・入力・描画先の表示|
|[エフェクトの使い方](Docs/EFFECTS.md)|エフェクトエディタの使い方と、見本のエフェクト|
|[バージョン方針](Docs/VERSIONING.md)・[変更履歴](CHANGELOG.md)|バージョンの付け方、互換を約束する範囲、対象環境、ブランチとタグ、変更の記録|
|[Samples/README.md](Samples/README.md)|サンプル集の構成と、サンプルの足し方|
|[第三者のソフトウェア](THIRD_PARTY_NOTICES.md)|使っているライブラリと、配布の条件|

## 確認

自己確認（単体の検査・画面の検査）は、このリポジトリに含めず、別のリポジトリ `framework-test` で管理しています。ルートの `Source\SelfTest` へ取り付けたときだけ、ビルドに加わります（取り付けなくても、ビルドできます）。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\SetupSelfTests.ps1    # framework-test を取得して、取り付ける（1 回だけ）
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\RunAllChecks.ps1       # すべての検査を実行する
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\VerifyCleanBuild.ps1   # まっさらな環境（新しい clone）で、ビルドして動くか
```

詳しくは [開発手順](Docs/DEVELOPMENT.md) の「プロジェクト生成と確認」にあります。

## ライセンス

mazigatiDX のコードは [MIT ライセンス](LICENSE)です。使っている第三者のソフトウェアと、配布するときの条件は、[第三者のソフトウェア](THIRD_PARTY_NOTICES.md)にあります。

## ブランチ

|ブランチ|役割|
|---|---|
|`main`|検証を終えた安定バージョン|
|`dev`|全機能を統合して確認する開発バージョン|
|`Effect`|エフェクト機能の開発|
|`Asset`|アセット機能の開発|
|`Core`|基盤・共通インターフェースの開発|

機能ブランチから `dev` へ統合し、必要な確認を終えた区切りを `main` へ反映します。


DX12による2Dバックエンドは`--backend=dx12`で選べます。`Samples.exe --backend=dx12 --sample=Canvas2D`で図形と画像の見本を起動できます。現段階では3D・音声等は未対応です。設計・対応範囲・確認手順は[開発手順](Docs/DEVELOPMENT.md#dx12バックエンド)を参照してください。
