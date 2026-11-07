# 変更履歴

[Keep a Changelog](https://keepachangelog.com/ja/1.1.0/) の形で、[セマンティック バージョニング](https://semver.org/lang/ja/)に従います。方針は [Docs/VERSIONING.md](Docs/VERSIONING.md) にあります。

## [未リリース]

### 追加
- バージョンの定義（`Library/Core/Version.h`）。起動ログとクラッシュ報告にバージョンを出す。
- DxLib の取得スクリプト（`FetchDxLib.bat`）、まっさらな環境でのビルドの確認（`Tools/VerifyCleanBuild.ps1`）、検査の取り付けと実行（`Tools/SetupSelfTests.ps1`・`Tools/RunAllChecks.ps1`）。
- Null バックエンドで、TGA の画像の大きさを読めるようにした。

### 変更
- 内部のバージョンの数字を 4.1 から、セマンティック バージョニングの `0.9.0` に改めた。
- クラッシュ報告の先頭の行を `mazigatiDX crash report` に、次の行に `Version:` を足した。
- README を刷新した（エフェクトの使い方は [Docs/EFFECTS.md](Docs/EFFECTS.md) へ移した）。

## [0.9.0] - 公開前のバージョン（予定）

v1.0.0 に向けた公開準備のバージョンです。これまでの主な機能は、次のとおりです。

- 2D（図形・画像・描画状態・描画先・画素・フィルター・文字）と 3D（カメラ・描画状態・ライト・図形・頂点バッファ・影・シェーダー・ポストエフェクト）の窓口と、DxLib の実装（`Library/Platform/DxLib`）。
- モデル（FBX / OBJ の取り込み、アニメーション）、サウンド、ムービー、物理（Jolt）、AI（ステートマシン・ビヘイビアツリー）、ワールド（`.gxworld`）、エフェクト、セーブ。
- エディタ（`Editor.exe`: エフェクト・ワールド・AI・サウンド・アセットのブラウザー）と、組み込みのデバッグ機能（プロファイラー・ログ・コンソール）。
- サンプル集（`Samples.exe`）と、参照ゲーム（Adventure）。
