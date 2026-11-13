# EngineContent

エンジンが最初から用意している素材です（Unity の組み込みリソース、UE の `Engine/Content` に当たります）。ユーザーの `Assets` とは別に、読み取り専用で扱います。

- **読み方**: `Engine:/UI/Window.png` のように、接頭辞 `Engine:/` を付けたパスで読みます（`CAssetSubsystem` の通常のパスと同じ関数で読めます）。保存はできません。
- **置き場所**: 実行ファイルや作業フォルダーの近く（親のフォルダーも探します）にある `EngineContent` フォルダーです。ゲームを配るときは、実行ファイルと同じ場所へ同梱します。
- **権利**: `UI` と `Icons` は、このリポジトリのスクリプトで作った素材です（[MIT ライセンス](../LICENSE)）。外部の素材は含みません。`Effects` は、以前 `Assets/Effects` にあったもので、AI が作成したものです（このリポジトリの持ち主が、AI を使って作った素材です）。
- **形式**: バックエンドに依存しない形式だけです（画像は PNG）。DxLib 専用の形式は置きません。

|フォルダー|内容|作り方|
|---|---|---|
|`UI`|窓・パネル・枠・ボタンの 4 状態・入力欄・影・チェック・ラジオ・スライダー・プログレスバーなどの基本画像。9 分割の余白は `UI/manifest.json`|`Tools/GenerateEngineContent.ps1`|
|`Effects`|Effect Editor の `Preset` が読む、エフェクトの定義（`.gxeffect`）と、その画像（TGA・`.gxsprite`）。`Engine:/Effects/campfire.gxeffect` のように読み、`Preset` から開くと、保存先はユーザーの `Assets/Effects/..._copy.gxeffect` になる（元は書き換えない）|（以前の `Assets/Effects` から移した）|
|`Icons`|UI 用のアイコン（73 個。64 x 64 の白一色。ブラシの色で好きな色にして使う）。名前と検索用の言葉は `Icons/manifest.json`|`Tools/GenerateEngineIcons.ps1`|
|`Environments`|マテリアルに映り込む、まわりの景色（環境マップ）の元になる、全周の絵。`Sky.png` は、晴れた昼の空（`CMaterialEnvironmentMap::Load( Assets, "Engine:/Environments/Sky.png" )`）|`Tools/GenerateEngineEnvironments.ps1`|

画像は、スクリプトで書き出した PNG をコミットしています（スクリプトは、描き直したいときだけ実行します）。素材を足すときは、スクリプトに足して、書き出し直します。
