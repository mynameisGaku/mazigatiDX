# 変更履歴

[Keep a Changelog](https://keepachangelog.com/ja/1.1.0/) の形で、[セマンティック バージョニング](https://semver.org/lang/ja/)に従います。方針は [Docs/VERSIONING.md](Docs/VERSIONING.md) にあります。

## [未リリース]

### 追加
- バージョンの定義（`Library/Core/Version.h`）。起動ログとクラッシュ報告にバージョンを出す。
- DxLib の取得スクリプト（`FetchDxLib.bat`）、まっさらな環境でのビルドの確認（`Tools/VerifyCleanBuild.ps1`）、検査の取り付けと実行（`Tools/SetupSelfTests.ps1`・`Tools/RunAllChecks.ps1`）。
- Null バックエンドで、TGA の画像の大きさを読めるようにした。
- UI（ウィジェット）システム（`Library/Framework/UI`。[Docs/UI.md](Docs/UI.md)）: 部品（画像・文字・ボタン・チェック・スライダー・プログレスバー・スクロール枠・仮想化したリスト（縦・横・行ごとの大きさ）・入力欄（日本語の変換・範囲選択・コピーと貼り付け）・ドロップダウン・ダイアログ・描画先の表示）、ツールチップ、画面のスタック、結び付け（MVVM）、テーマ、XML のマークアップ（`.gxui`・ホットリロード）。
- クリップボードの窓口（`IClipboard`）。
- マテリアル（`Library/Framework/Material`。[Docs/MATERIAL.md](Docs/MATERIAL.md)）: ドメイン（UI・Surface・Particle・PostProcess）・ノードのグラフ（`.gxmaterial`）・中間表現・HLSL の生成・CPU での評価・診断・バックエンドの能力の宣言、インスタンス（`.gxmaterialinstance`）、パラメーターコレクション（`.gxmaterialparametercollection`）、ポストプロセスのチェーン、Substrate 風の万能モデル（簡略版）、HLSL の直指定、ホットリロード、UI のブラシ（`FUIBrush::MakeMaterial`）、エンジン組み込みのマテリアル（`EngineContent/Materials`）。
- UI の動き（`CUIAnimator`・部品の描くときの変換・画面の出入りの演出・ホバーや押下に合わせた動き・マークアップの `<Animation>`）と、どこからでも使えるイージング（`Library/Math/Easing.h`）。
- UI のテーマのファイル（`.gxuitheme`。`CUIThemeLoader`）と、色の文字の読み取り（`FColor32::TryParseHex`）。
- エフェクトの粒子を、Particle ドメインのマテリアルで描く（エミッターの `MaterialAsset`。`CMaterial::BeginParticles`・描画命令 `ShaderQuad3D`・組み込みの `Engine:/Materials/Particle`）。
- マテリアルエディタ（`Editor.exe`。ノードのグラフ・プレビュー・診断・保存）と、サンプル `Materials`。

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
