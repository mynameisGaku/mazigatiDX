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

## 画像の所有を、役割ごとに分ける（2026-11-22）

`CDX12TextureStore`（約1240行）は、画像の登録、GPU資源の作成、同期の提出と完了待ち、読み戻し、描画先パスの追跡を1つで持っていて、「記録 → 提出 → 完了待ち」の同じ手順が7か所に重なっていた。責務の分かれ目で、次のクラスへ分けた。公開窓口（`IRenderTextureResources`）と振る舞いは変えない。

|所有者|担当するもの|
|---|---|
|CDX12TextureStore|画像の番号と登録・更新・遅延解放、能力の照会、compute・描画パスへ渡す参照、描画先パスの追跡、画像のコピー|
|CDX12SyncSubmitter|資源の作成・転送・読み戻しのための短いGPU作業を、提出して完了まで待つ（専用の記録領域・命令列・完了待ち）。待機に失敗したら、GPUが参照しうる資源を保持して、以後を拒否する|
|CDX12TextureCreator|読み取り画像（ミップマップつきを含む）・compute画像（RGBA8・RGBA16F）・色の描画先・読み取れる深度の、GPU資源の作成と初期内容の転送・消去、SRVの作成|
|CDX12TextureReader|画像の全画素の、CPUへの読み戻し（形式ごとの変換は、Storeが行う）|
|CDX12CopyStaging|資源へのコピーを、完了を待たずに積むための、転送領域（UPLOADバッファ）と命令列の使い回し（借りる・提出する・戻す。数と大きさに予算。GPU完了後だけ、提出済みを再利用へ戻す）。画像の更新とバッファの作成が使う|
|CDX12TextureUpdater|読み取り用RGBA8画像の全画素の更新を、完了を待たずに積む（旧版の資源と記述子・転送領域・命令列の使い回し。旧版はGPU完了後にだけ再利用する。予算を超えたら完了を待つ経路へ）|
|FDX12TextureRecord|登録した画像1枚の実体（資源・記述子・寸法・消去値・用途）。`MakeBinding`で、提出や描画パスが保持する参照を作る|
|NDX12HalfFloat|半精度と単精度の変換|

数・寸法・能力の検査と登録はStoreが行い、CreatorとReaderは、借りた同期提出器とデバイスだけで動く。契約（compute・描画先・HDR画像のコピー・撮影・ミップマップ）の検査と、全サンプルの絵の比べは、分ける前と同じ結果になる。

## 描画の記録を、役割ごとに分ける（2026-11-22）

`CDX12GeometryRenderer`（約1200行）は、2Dと基本3Dの頂点の記録、保持する資源の管理、カスタムパイプラインの検査と記録を1つで持っていた。役割ごとに分けた（公開の窓口と、描画の振る舞い・順序は同じ）。

|所有者|担当するもの|
|---|---|
|CDX12GeometryRenderer|2Dと基本3Dの頂点の一時転送、深度とsampler、clip・画像・PSOの設定、記録中の状態の所有。カスタムパイプラインの描画は、状態を貸して、下の描画器へ渡す|
|CDX12FramePins|1フレームの記録が参照するバッファ・画像・カスタムパイプラインを、提出の完了まで、重複なく・上限内（バッファ256個・256MiB、画像256枚、パイプライン256個）で保持する|
|CDX12GraphicsPipelineDrawer|登録済みカスタムパイプラインの描画（登録したバッファを使う描画と、フレームの転送領域へ載せる描画）の検査と記録。記録先の状態は持たず、`FDX12GraphicsDrawTarget`（描画先・heap・範囲）と`FDX12GraphicsDrawLedger`（数え・保持・転送量）を、描画のたびに借りる|
|NDX12BufferBarriers|GPUバッファの、提出の間の待機状態から、読み取りの用途への遷移を、1つにまとめて組み立てる|
|NDX12Topology|共通の頂点のつなぎ方を、D3D12の列挙へ変換する|

ゲームスレッド側の窓口（`CThreadedRenderSurface`）も、往復を減らすために、役割で分けた（[スレッドの契約](THREADING.md)）。

|所有者|担当するもの|
|---|---|
|CThreadedRenderSurface|2D命令の所有コピーと順序、同期呼び出し、待たずに積む3D描画の窓口|
|CAsyncDrawGate|3Dの描画を、結果を待たずに積んでよいかを決める（同期で成功した組み合わせの記憶、実行先の上限の数え、書き込み中の画像との競合）|
|FQueuedGraphicsDraw|待たずに積む3D描画の所有コピー（頂点・添字・定数の中身と、バッファ・画像の番号）と、描画スレッドでの実行|
|CTextureViewCache|画像の描画用view（実行先の識別子と寸法）の、ゲームスレッドでの記憶。資源・描画先の変化で捨てる|

契約と全サンプルの絵の比較は、分ける前と同じ結果で通ることを確かめている。

## 共通命令の実行を、役割ごとに分ける（2026-11-22）

`CCanvasDrawExecutor::Execute`（約1900行の1つの関数。状態約25個とラムダ約30個）は、共通命令を2Dと3Dへ振り分ける。命令の系統ごとの役割クラスに分けた（公開の窓口・命令の順序・描画結果は同じ）。`Execute`は、命令の長さを検査して、役割へ渡すだけの約100行になった。

|所有者|担当するもの|
|---|---|
|CCanvasDrawExecutor|命令列の読み取りと、役割への振り分け。終了時に、描画先・クリップ・補間・合成を既定へ戻す|
|FCanvasExecution|1回の実行が持つ、借りた実行先（描画先・画像・フォント・シェーダーなど）と、3D・シェーダー・材質の実行器、描画の状態（`FCanvasDrawState`）、描画先の積み（`FCanvasTargetState`）。役割が共有する|
|CCanvasTargetScopes|描画先（画像・影の絵・複数の出力）の切り替えと積み。描けない描画先へ描く範囲は、対の終わりまで飛ばす。画面の写し・影の絵の表示・影を受ける設定|
|CCanvasStateCommands|消去・合成・補間・明るさ・2Dの変換・クリップ・マスク・足す色・アルファテストの命令|
|CCanvasShapePainter|平らな図形（四角・線・円・楕円・角丸・点・三角・四角形）|
|CCanvasImagePainter|画像・スプライト・タイル・円ゲージ|
|CCanvasTextPainter|文字列（既定・フォント指定・位置揃え・拡大・回転）|
|CCanvasImageEffects|画像のフィルター・混ぜ合わせ（変換先の画像のパスを開き、組み込みのピクセルシェーダーで描く）|
|CCanvasSceneCommands|2Dのどの役割にも当たらない命令（共有のシェーダーメッシュ・シェーダー・材質付きの3D・モデルの姿勢・基本の3D）|

各役割は、`FCanvasExecution`を借りるだけで、ほかの役割を直接は持たない（出力を外す・描画先へ入る操作に限り、`CCanvasTargetScopes`を借りる。文字は絵文字の画像を貼るために`CCanvasImagePainter`を借りる）。命令ごとの本文は、分ける前のまま移した。全サンプルのDxLibとの画像比較、描画順・スナップショットの契約と変異検査が、分ける前と同じ結果で通ることを確かめている。

## 3Dの命令の実行を、役割ごとに分ける（2026-11-22）

`CGeometryCommandExecutor`（約1640行。約850行の`Execute`のswitchと、約270行の`Submit`）は、カメラ・ワールド・深度・ライト・フォグ・影の状態、3Dの図形、静的モデル、頂点の提出、GPUで描く共有メッシュを1つで持っていた。役割ごとに分けた（公開の窓口と、命令の順序・描画結果は同じ）。

|所有者|担当するもの|
|---|---|
|CGeometryCommandExecutor|公開の窓口（`Execute`・`DrawModel`・`DrawModelView`・`BuildShaderQuad`・影の設定）と、役割への振り分け。状態の置き場（`FGeometryExecution`）を持つ|
|FGeometryExecution|このSpanだけの3Dの状態（ワールド・カメラ・深度と表裏・ライト・フォグ・画像の変換・影のスロット）と、借りた実行先。役割が共有する|
|CGeometryStateCommands|カメラ・ワールド行列・深度・表裏・消去・ライト・フォグ・画像の変換・補間・異方性の命令|
|CGeometryShapeCommands|3Dの図形（点・線・円錐・三角形・立方体・球・カプセル・四角形・ビルボード・リボン・点列・GPUバッファの点列）の命令を頂点にして、提出役へ渡す。`BuildShaderQuad`|
|CGeometryModelCommands|静的モデル（配置・アニメーション・固定姿勢・メッシュ）の命令。GPUで描ける共有メッシュは`CGeometryForwardMesh`、それ以外は頂点にして提出役へ|
|CGeometryVertexSubmitter|頂点を、ワールドからクリップ座標へ移し、光・フォグ・影を計算して、3Dの実行先へ渡す|
|CGeometryForwardMesh|GPUが変換と光を計算する共有メッシュの経路（光の定数・描けるかの判定・材質範囲ごとの描画）|

依存は一方向（Forward ← Submitter ← Shape／Model、Forward ← Model、Stateは独立）。値の検査・行列の変換・フォグの見え方は、`NGeometryCommand`（`GeometryCommandHelpers.h`）へ集めた。命令ごとの本文は、分ける前のまま移した。全サンプルのDxLibとの画像比較、描画順・スナップショット・材質・Deferred・Forward meshの契約と変異検査が、分ける前と同じ結果で通ることを確かめている。
