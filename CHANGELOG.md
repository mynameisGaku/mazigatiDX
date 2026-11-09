# 変更履歴

[Keep a Changelog](https://keepachangelog.com/ja/1.1.0/) の形で、[セマンティック バージョニング](https://semver.org/lang/ja/)に従います。方針は [Docs/VERSIONING.md](Docs/VERSIONING.md) にあります。

## [未リリース]

### 追加
- RAWバッファの用途とIComputePipelineResources／IComputeQueue。DX12のCS5.0／5.1、非同期提出・完了照会・期限付き待機と提出後の資源保持を接続し、CPU参照／既定GPU／WARP・Debug／Releaseの検査を追加。描画との資源共有とDXC／SM6は未接続。
- 初期バイト列を所有するIRenderBufferResourcesと型付きのFBufferResourceHandle。DX12の同期転送・readback・登録解除後の回収を実装し、CPU参照／既定GPU／WARPの契約検査を追加。
- codec・UIに依存しない画像資源の窓口IRenderTextureResourcesと、アセット番号の共通登録FTextureAssetStore。DX12の画像・転送・遅延解放、記述子のpool、ImGui描画の接続を責務ごとのクラスへ分離。
- DX12構成のバックエンド互換台帳と自動更新・確認（`Docs/BackendCompatibility.csv`・`Tools/UpdateBackendCompatibility.ps1`）。公開窓口・描画命令・サンプル・Editor入口の欠落と宣言変更を検出し、既存の全検査手順へ追加。
- MV1・シェーダー・音声／動画の初期調査と、共通GPU基盤の責務と未解決事項（`Docs/BACKEND_AUDIT.md`）。
- meshletの生成・GPUカリング・index付きindirect drawをv1.0.0の性能計画へ追加。Mesh Shader対応は能力別の拡張経路として次版以降に計画。
- v1.0.0までのロードマップ（`Docs/ROADMAP.md`）。DX12のForward／Deferred／Forward+／Deferred+、既存DxLib公開機能の完全互換、高品質描画と制作機能、Vulkan／OpenGL以降の計画と段階ごとの完了条件を定義。
- バージョンの定義（`Library/Core/Version.h`）。起動ログとクラッシュ報告にバージョンを出す。
- DxLib の取得スクリプト（`FetchDxLib.bat`）、まっさらな環境でのビルドの確認（`Tools/VerifyCleanBuild.ps1`）、検査の取り付けと実行（`Tools/SetupSelfTests.ps1`・`Tools/RunAllChecks.ps1`）。
- Null バックエンドで、TGA の画像の大きさを読めるようにした。
- UI（ウィジェット）システム（`Library/Framework/UI`。[Docs/UI.md](Docs/UI.md)）: 部品（画像・文字・ボタン・チェック・スライダー・プログレスバー・スクロール枠・仮想化したリスト（縦・横・行ごとの大きさ）・入力欄（日本語の変換・範囲選択・コピーと貼り付け）・ドロップダウン・ダイアログ・描画先の表示）、ツールチップ、画面のスタック、結び付け（MVVM）、テーマ、XML のマークアップ（`.gxui`・ホットリロード）。
- クリップボードの窓口（`IClipboard`）。
- マテリアル（`Library/Framework/Material`。[Docs/MATERIAL.md](Docs/MATERIAL.md)）: ドメイン（UI・Surface・Particle・PostProcess）・ノードのグラフ（`.gxmaterial`）・中間表現・HLSL の生成・CPU での評価・診断・バックエンドの能力の宣言、インスタンス（`.gxmaterialinstance`）、パラメーターコレクション（`.gxmaterialparametercollection`）、ポストプロセスのチェーン、Substrate 風の万能モデル（簡略版）、HLSL の直指定、ホットリロード、UI のブラシ（`FUIBrush::MakeMaterial`）、エンジン組み込みのマテリアル（`EngineContent/Materials`）。
- マテリアルエディタ（`Editor.exe`。ノードのグラフ・プレビュー・診断・保存）と、サンプル `Materials`。

### 変更
- DX12でのMV1形式の読み込み・保存互換をv1.0.0の必須範囲から除外。モデルのスキン・モーフ・アニメーション等は引き続き対象。互換台帳へ明示した対象外のExcluded判定を追加。
- FBX取り込みのスキン影響数を指定可能にし、削減と未対応方式を記録。既存のMV1変換は8本を使い、X経由で保持できない変形は既定でエラーにする。省略を許す場合はbAllowUnsupportedDeformationsで明示し、既存の出力を再診断する場合はbForceを指定する。
- 内部のバージョンの数字を 4.1 から、セマンティック バージョニングの `0.9.0` に改めた。
- クラッシュ報告の先頭の行を `mazigatiDX crash report` に、次の行に `Version:` を足した。
- README を刷新した（エフェクトの使い方は [Docs/EFFECTS.md](Docs/EFFECTS.md) へ移した）。

## [0.9.0] - 公開前のバージョン（予定）

v1.0.0 に向けた公開準備のバージョンです。これまでの主な機能は、次のとおりです。

- 2D（図形・画像・描画状態・描画先・画素・フィルター・文字）と 3D（カメラ・描画状態・ライト・図形・頂点バッファ・影・シェーダー・ポストエフェクト）の窓口と、DxLib の実装（`Library/Platform/DxLib`）。
- モデル（FBX / OBJ の取り込み、アニメーション）、サウンド、ムービー、物理（Jolt）、AI（ステートマシン・ビヘイビアツリー）、ワールド（`.gxworld`）、エフェクト、セーブ。
- エディタ（`Editor.exe`: エフェクト・ワールド・AI・サウンド・アセットのブラウザー）と、組み込みのデバッグ機能（プロファイラー・ログ・コンソール）。
- サンプル集（`Samples.exe`）と、参照ゲーム（Adventure）。
