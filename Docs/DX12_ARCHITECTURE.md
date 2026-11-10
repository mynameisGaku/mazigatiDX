# DX12の責務・命名・配置

2026-11-10（日本時間）。描画用PSOの作成・所有を描画命令の記録から分け、機能を担う型のCとデータのFを整理しました。既存の固定28バイト頂点の描画結果と、ゲーム描画／ImGuiの境界を維持します。

## 描画パイプラインを分ける位置

従来も描画用PSOはありましたが、GeometryRendererのGetPipelineと初期化処理へシェーダー・資源配置・PSOキャッシュをまとめていました。これをCDX12GeometryPipelineStoreへ分離しました。GPUの命令記録と、実行するGPU状態の作成・所有は別の責務です。

|所有者|担当するもの|
|---|---|
|CDX12GeometryPipelineStore|固定の同次頂点用のVS／PS、画像付きPS、root signature、描画状態からPSOを作る処理、128状態までのキャッシュ|
|CDX12GeometryRenderer|頂点の一時転送、GPUバッファの範囲確認と参照保持、深度とsampler、clip・画像・PSOの設定、描画命令の記録|
|CDX12ComputePipelineStore|computeバイナリと資源宣言の照合、root signatureとcompute PSOの作成・所有|
|CDX12ComputeQueue|computeの入力範囲検査、専用の記述子と記録領域、状態遷移、提出・完了照会と提出済み参照の保持|
|CDX12Device|GPU・キュー・swapchainと通常フレーム提出の所有|
|CDX12ImGuiRenderer／CDX12ImGuiFrameRenderer|GPUを借りるUIの利用者。ゲーム描画用PSO・GPU本体の所有者にはしない|

GeometryPipelineStoreは既存の頂点形式に限定したネイティブ実装です。描画先・頂点バッファ・ゲームのIRenderSurface・ImGuiを参照せず、描画記録もGPU提出も行いません。作成済みPSOの借用は所有者のResetまで有効で、Resetと破棄は所有者がGPU完了を保証してから行います。未知の描画状態はGPUへ渡す前に拒否し、状態が同じなら同じPSOを返します。samplerだけの違いではPSOを増やしません。

Forward／Deferredなどのパスの順序・入力・出力を決める描画方式は、このPSO所有者とは別です。その収集と順序はロードマップのFramework/Renderに置く責務で、今回その実装が完成した意味ではありません。内部の所有者を分けるために、用途のない共通インターフェースやSubsystemは追加しません。

## CとFの判断

[開発規約](DEVELOPMENT.md#コードとコメント) と [ラッパー規約](WRAPPER_GUIDE.md#2-型と単位) を、責務による同じ分類へ揃えました。classかstructかで接頭辞を決めません。

- C: 作成・実行・変換・登録・外部資源管理などの機能を担う型。Device、Renderer、PipelineStore、ComputeQueue、資源Store、描画命令の実行先、形状生成、文字生成、UI接続が該当します。
- F: データ・値・handle・効率的なデータ保持を担う型。操作関数や内部メモリを持つこともできます。FGeometryDrawState、FGeometryBufferDraw、FComputeBufferSlot、FDX12BufferBinding、FDX12ComputePipelineBindingはこの役割を保ちます。
- T／E／I／Nは、それぞれtemplate／enum／interface／名前空間です。

今回の対象はDX12基盤と、直接接続する共通描画・文字・UIの機能型25個です。新設のGeometryPipelineStoreもCです。互換aliasで旧F名を残さず、製品・検査・文書の参照を同時に更新しました。プロジェクト内の無関係なF型を、関数を持つという理由だけで一括改名していません。ファイル名は既存の接頭辞を除く役割名と同名のh／cppを維持します。

## 役割ごとの配置

DX12の既存実装32ファイルを移動し、新しい描画用パイプラインの二ファイルを加えました。

```text
Platform/DX12/
  Asset/                  アセット窓口の接続
  Device/                 GPU・フレーム所有とキュー完了待ち
  Resource/
    Buffer/               GPUバイト列と借用参照の値
    Texture/              GPU画像の所有と更新
    Descriptor/           記述子の貸し出しと返却
    Readback/             GPU画像のCPU読み戻し
  Pipeline/
    Compute/              computeの宣言照合とPSO所有
    Geometry/             基本頂点描画のシェーダー・配置・PSO所有
  Command/Compute/        compute命令の記録と提出
  Render/
    Geometry/             基本頂点の描画記録
    Game/                 2D・基本3D・文字と撮影の組み立て
    Capture/              UIやゲームを知らないGPU撮影先
    Draw/                 共通描画命令からの接続
  ImGui/
    Render/               公式UI描画実装との接続
    Frame/                GPUを借りるUIフレームの利用者
  Window/                 プラットフォーム窓口とフレーム進行
```

includeとプロジェクトのソース一覧、Visual Studioの仮想フォルダー、文書・互換台帳と別置きの検査参照も更新します。移動元に転送ヘッダーを残して配置の境界を曖昧にしません。AssetとWindowは各領域の入口になる少数の実装として残します。共通のCanvas／Geometry／Texture／ModelとWindows文字は、それぞれ既存の機能フォルダーを維持します。

## 検査

Tools/CheckDX12Architecture.ps1は機能26型とデータ4型の配置・宣言を確認し、Render／ImGui直下へ実装を再集積した場合も拒否します。別置きのRunDX12ArchitectureCheckTest.ps1は、CからF・FからCへの誤分類と平坦な配置の負例を確かめます。既存のGPU／UI依存検査にも新しいパイプライン所有者を含めました。

RunGeometryPipelineTests.ps1は既定GPU／WARPで、初期化前と二重初期化、不正列挙、深度・表裏・合成・頂点接続・深度偏り・画像のキャッシュキー、samplerによる再利用、128状態の境界、resetと再初期化を24条件で検査します。描画先・キュー・ImGuiを作らず、SDK警告以上を失敗とし、実行ファイルのmapでもImGui未リンクを確認します。実際の描画と撮影の画素は既存の検査と新しい [compute配置](R1_COMPUTE_BUFFER_LAYOUTS.md) の検査で比較します。

|検査|Debug|Release|
|---|---|---|
|全体ビルド|4 project、警告・エラー0|同じ結果|
|描画PSOの独立した作成・キャッシュ|既定GPU／WARP各24条件、失敗0|同じ結果|
|定数・構造体・RAWの混在と直接描画|既定GPU／WARP各90条件、失敗0|同じ結果|
|GPUバッファの直接描画|既定GPU／WARP各188条件、失敗0|同じ結果|
|UIなしのゲーム描画・保存|両GPUで各84条件、失敗0|同じ結果|
|UI合成と撮影|両GPUで各91条件、実アプリはUIあり／なしでPNG・高解像度・透明撮影・GIF成功|同じ結果|
|SDKデバッグ層|上記の独立GPU検査で警告・エラー0|同じ結果|

移動後のRunAllChecks.ps1 -SkipBuild -Quickは計26項目が成功しました。命名・配置30型と負例、1742ファイルの書式、既存のECS・assets・world、転送・compute・窓・撮影を含みます。ECSは全ワーカー数で7,867,833値・ハッシュ403b39415ba4b9a6、assetsはDebug 9,956件／Release 9,953件、worldは両構成3,635件で既存の省略を保ちます。GPUデバイスの間接参照68ファイルと依存検査の負例も成功しました。互換台帳は459項目、Verifiedは0です。

Releaseでは既知のリンクキャッシュの不整合を避けるため、検証用コピーの既存iobj／ipdb六ファイルを退避し、元の最適化設定でビルドしています。VendorModulesの変更や最適化を下げる回避はありません。元の作業コピーのOptional.h／project.slnと、検証用コピーで見つかった実装外のproject.sln変更は保持し、コミットへ含めていません。

検査側の対応コミットは [8a9291180c4a9dd8c7250e6e374bc47e9ca64869](https://github.com/mynameisGaku/framework-test/commit/8a9291180c4a9dd8c7250e6e374bc47e9ca64869) です。sample／templateの全検査、配布物のクリーンビルド、複数GPUベンダーと性能測定、R1／R2全体の完了は今回の結果に含めません。検証用コピーのSaved/RenderArchitectureにビルド・書式・一括検査のログを分け、各独立GPU検査の中間と実行物はSaved/GeometryPipelineContractなどの構成別フォルダーへ置きます。
