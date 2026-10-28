# __NAME__

mazigatiDX（DxLib を使う C++ のフレームワーク）で作った、最小のゲームです。タイトル画面で Enter を押すと、矢印キー / WASD で四角形を動かせる画面へ進み、Esc でタイトルへ戻ります。

## 必要なもの

- Visual Studio（MSVC ツールセット v145）、Windows SDK 10.0.26100.0
- DxLib 3.24f。`C:\DxLib`（と、あれば `C:\DxLib_VC\プロジェクトに追加すべきファイル_VC用`）に置きます。別の場所に置いたときは、環境変数 `MazigatiDxLibDir`（と `MazigatiDxLibVcDir`）にその場所を入れます。
- フレームワーク（mazigatiDX）のフォルダー。`__NAME__.vcxproj` の `MazigatiDXRoot` が、このフォルダーからの相対パスで指しています（`__FRAMEWORK_RELPATH__`）。

## 動かす

1. `__NAME__.sln` を Visual Studio で開きます。
2. x64 の Debug か Release を選んでビルドします。フレームワーク本体の静的ライブラリ（`mazigatiDX`）が先にビルドされます。
3. 実行します。作業フォルダーは、このフォルダーです（`Config` と `Assets` を使います）。

## 構成

|場所|役割|
|---|---|
|`Source/main.cpp`|入口。`NGameMain::RunGame<CGameApplication>()` を呼ぶだけです|
|`Source/Application/`|ゲームインスタンス（`CGameApplication`）。シーンを登録して、最初のシーンへ入ります|
|`Source/Scene/`|シーン（`CTitleScene` `CPlayScene`）。画面の単位です|
|`Source/Object/`|オブジェクト（`CPlayer`）。シーンに置いて、毎フレーム Tick / Draw されます|
|`Config/ScreenConfig.json`|窓の大きさ・タイトル・窓の種類|
|`Assets/`|画像・音・モデルなどの置き場|

## 次にすること

- **シーンを足す**: `Source/Scene/` に `CScene` を継承したクラスを書き、`CGameApplication::OnInit` で `Scenes.RegisterScene<T>( "名前" )` します。切り替えは、シーンの中から `GetSceneManager()->ChangeScene( "名前" )` です。
- **オブジェクトを足す**: `Source/Object/` に `CGameObject` を継承したクラスを書き、シーンの `OnEnter` で `SpawnObject<T>( ... )` します（`CPlayer` が見本です）。
- **画面に描く**: オブジェクトの `Draw( FDrawContext& CommandList )` に、`CommandList.Box(...)` `CommandList.Text(...)` `CommandList.Image(...)` などで「何を描くか」を足します（DX12 のコマンドリストと同じ方式です。シーンがコマンドリストを渡して、記録されたものを描画順に実行します。`CPlayer` が見本です）。シーンの `OnDraw` では、`FImmediateDraw Screen( GetDrawExecutor() );` と書くと、同じ関数で描けて、関数を抜けるときに画面へ出ます。
- **デバッグ表示を使う**: `CGameApplication` に `CDebugOverlay Overlay;`（`Library/DebugUI/DebugOverlay.h`）を持たせ、`OnInit` / `OnTick` / `OnDraw` / `OnTerm` から `Overlay.Init()` / `Overlay.Tick()` / `Overlay.Draw( *this )` / `Overlay.Term()` を呼びます。F1 で窓（FPS・入力・シーン・ログ）、F2 でプロファイラー、F3 でコンソール（Debug だけ）、F11 でフルスクリーンです。
- **ファイルを足したり消したりしたとき**: `GenerateProjectFiles.bat` を実行して、プロジェクトのファイル一覧を作り直します。
- **画像・音・モデル**: `Assets/` に置いて、アセット管理（`CAssetManager`）から読み込みます。

詳しくは、フレームワークの文書（`Docs/DEVELOPMENT.md`、`Docs/WRAPPER_GUIDE.md`）を参照してください。

## ライセンス

このテンプレートのコードは MIT ライセンスです。自由に書き換えて使えます。配布するときに必要な、第三者のソフトウェアの表記は、フレームワークの `THIRD_PARTY_NOTICES.md` を参照してください。
