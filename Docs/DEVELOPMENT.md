# 開発手順

## 構成と依存

`Library/Core`は値・コンテナ・インターフェース、`Library/Framework`はエンジンと資源の管理、`Library/Platform`はDxLibやSDLの実装、`Library/DebugUI`は編集画面、`Source`はアプリと自己確認を持ちます。

FrameworkからDxLibやImGuiの実装へ依存を追加せず、`IDrawExecutor`・`IAssetBackend`・入力など既存の窓口を使います。既存の型・所有・更新・終了処理を優先し、局所状態や単純な計算のためのsubsystemは作りません。

## コードとコメント

- 公開する主要な型は原則一つのヘッダーに置き、実装は同名cppへ分けます。templateの実装は同名ヘッダーまたはinlに置きます。
- 新しい型の役割は`A`（所有される多態オブジェクト）、`F`（値・ハンドル・サービス）、`T`（template）、`E`（enum）、`I`（インターフェース）で示します。既存の型は無関係な変更で一括改名しません。
- ヘッダー冒頭はSPDXとinclude guardを置き、長い使い方は文書へ分けます。
- 型や関数の宣言直前に、日本語で役割・入力・失敗条件を記述します。複数の引数を説明するときは引数ごとに改行します。
- field、変数、定数、列挙値にも、その場所の役割が分かる短い日本語コメントを付けます。専門用語が必要な場合は短く補足します。
- 関数・制御文・cast・macro・constructor・初期化子の括弧内部は一行に保ち、既存の波括弧の形式を維持します。途中のコメントで式を分断しません。
- 自作のh・cpp・inl・ps1はUTF-8 BOM一つ、CRLF、NULなしとします。第三者のソースとライセンスは元の形式を保ちます。
- 機能、公開API、ソースの配置を変えた場合は、関連する文書と参照も更新します。

## プロジェクト生成と確認

cpp・header・inlを追加・削除・移動したら、次でプロジェクトを再生成します。

```powershell
cmd /c "GenerateProjectFiles.bat < nul"
msbuild .\project.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild .\project.sln /p:Configuration=Release /p:Platform=x64 /m
```

自己確認はassets・world・ecsを両構成で実行します。ecsはワーカー0／1／4／15の結果を照合します。PowerShellから`-WorkerCounts`へ配列を渡す場合は、`-File`の文字列引数にせず、PowerShell内で呼び出します。

```powershell
.\Tools\RunSelfTests.ps1 -Suite assets -Configuration Debug
.\Tools\RunSelfTests.ps1 -Suite world -Configuration Debug
.\Tools\RunSelfTests.ps1 -Suite ecs -Configuration Debug
```

2026-10-20の安定版の確認値はworld 2695、assetsはDebug 1019／Release 1017、ecsは各ワーカー7,867,833項目・ハッシュ`403b39415ba4b9a6`です。新しい確認を加えると項目数は増えるため、数値だけで合格とせず失敗・スキップ・正常終了を確認します。Debugはリーク報告も調べます。描画の変更は実GPUの既知画素、状態復元、資源の解放を確認します。

## 変更の統合

`main`は安定版、`dev`は統合版です。作業は`Effect`・`Asset`・`Core`など責務に応じたブランチへ分けます。機能ブランチで実装と関連する確認を揃えてから`dev`へ統合し、安定版の確認を終えた区切りを`main`へ反映します。

コミットは変更のまとまりごとに分け、`feat`・`fix`・`refactor`・`other`と、原因や変化が分かる日本語の説明を使います。push前にはremoteとの差分と追跡ファイルを確認し、履歴の強制的な書き換えは行いません。

追跡するものは本体・必要なアセット・設定・ビルド手順・プロジェクト文書です。実行結果、個人のIDE設定、ローカルの作業記録、認証情報、一時テスト資料は含めません。第三者ライブラリの使用に必要なファイルとライセンスは保持します。
