# DX12開発の引き継ぎ

最終更新: 2026-11-11（日本時間）。この文書は作業中も更新します。実装済み、未接続、検証済みを分け、予定を完了扱いにしません。

## 目的と公開済みの基点

作業ブランチは`backend-dx12`です。[ロードマップ](ROADMAP.md)のR0／R1を整え、共有メッシュ・材質とForwardへ進む依存を先に満たします。v1.0.0のR1全体、四描画方式、完全互換は未完了です。

- 本体の公開済み基点: `d580e767a37bfb42b49684ad43832a2368480a9d`。
- 対応する検査: framework-testの`codex/dx12-windows-validation`、`1d02c862a043f364d4e5f05b988a3993699073c2`。
- この基点は、DX12主画面の専用描画、IsInRenderThread／RENDER_THREAD_ONLY、描画コンテキスト、2D操作の所有コピーまで実装済みです。Debug／Release各4プロジェクトは警告・エラー0件、全30項目を確認して公開しました。
- 現在の変更はこの基点からのR1追加です。下の作業欄と新しい検査結果を確認してから次へ進めてください。

## この端末の作業コピーと保護対象

|作業コピー|役割と注意|
|---|---|
|framework-dx12|ユーザーの`backend-dx12`。公開後は履歴を前進させるだけで反映する。ユーザーの実行中アプリを停止せず、ここではビルドしない|
|framework-dx12-verify|実装とビルドの作業コピー。明示的にこの作業ディレクトリを指定する。公開済み履歴に揃えつつ既存の未コミット設定を保持する|
|framework-dx12-selftests|別の検査リポジトリ。検証側のSource/SelfTestがこのコピーへ接続されている。検査と本体の対応コミットを文書へ残す|
|framework-dev|開発規約の参照先。ここや他の作業コピーの未コミット変更を上書きしない|

本体側の`Library/Core/Collection/Optional.h`と`project.sln`はユーザーの変更です。検証側の`Samples/Samples.vcxproj`、同filters、`project.sln`、`project.vcxproj`、同filtersも、今回より前からある変更です。検査リポジトリの未追跡`x64/`も対象外です。まとめてstageせず、自分の変更だけを明示します。

現在の保護対象のハッシュと生成物は検証側の`Saved/ComputeTextureResources/`へ保存しています。Savedは共有する実装ではなく、端末ごとの作業記録です。

## 継続する規約と責務

実装前に[開発手順](DEVELOPMENT.md)、[ラッパー規約](WRAPPER_GUIDE.md)、対象機能のローカル設計と公式オンライン文書を確認します。機能を実行・管理する型はC、値・データ・handleはF、templateはT、enumはE、窓口はIです。公開する主要型は1 header、実装は同名cppに分けます。機能別フォルダーを責務の境界とし、既存の所有と終了処理を優先します。

DX12はゲームとUIが使うグラフィックス実装です。GPUデバイス・資源・ゲーム描画へImGuiやFrameworkの依存を加えません。共通の命令記録側もAPIを知りません。[スレッド設計](THREADING.md)に従い、公開のゲーム側サービスと、バックエンドの実行側を使い分けます。

- 並列のCPU準備は既存IJobSystem／CJobPipe、同じ実行スレッドへの固定はCRenderCommandThreadを使います。
- 主画面のGPU記録・資源・compute操作は専用描画側です。Present・外部窓はCPU完了の区切りで排他的に窓側へ貸します。
- FTaskFenceはCPU完了です。GPU資源・descriptor・allocatorの回収は別のGPU fenceで確認します。
- 描画命令ではconst FRenderCommandContext&のCanvas／Geometryだけを実行中に借ります。窓・UI文脈・ゲームの可変状態へ触れません。フレーム外は両方nullptrです。
- 複数フレームの同時進行、複数native command listの並列記録、DXC／SM6は未対応です。

## 現在の作業: R1のGPUタイムスタンプ計測契約（2026-11-12）

選定理由: [ロードマップ](ROADMAP.md) R1 GPU基盤の `timestamp` 項目およびR4/R9のGPU時間計測基礎として、API非依存のGPUタイムスタンプ能力照会・計測ハンドル作成・遅延スロット回収（`IGpuTimestampResources`）、主画面および `BeginRenderTargetPass`〜`EndRenderTargetPass` 内の範囲計測（`IRenderSurface::BeginGpuTimestamp`／`EndGpuTimestamp`）、ならびに `IComputeQueue::SubmitComputeWithTimestamp` を追加し、同一 `DIRECT` キュー上のGraphics→Compute→Readback区間のGPU計測と未完了スロット保護を成立させました。詳細は [GPUタイムスタンプ計測契約](R1_GPU_TIMESTAMPS.md) を参照してください。

|項目|現在の状態|
|---|---|
|タイムスタンプ共通値・窓口|`FGpuTimestampHandle`・`FGpuTimestampCapabilities`・`FGpuTimestampResult`・`IGpuTimestampResources` を追加し、`IRenderSurface`（`BeginGpuTimestamp`／`EndGpuTimestamp`）と `IComputeQueue`（`SubmitComputeWithTimestamp`、既存 `FComputeDispatch` 初期化を保持）を `CThreadedRenderSurface`・`CDX12PlatformSubsystem` へ接続済み|
|DX12実装・同期・寿命|`CDX12TimestampStore`（`D3D12_QUERY_HEAP_TYPE_TIMESTAMP` 512スロット = 256ペア、`D3D12_HEAP_TYPE_READBACK` 4KiB、専用 `ID3D12Fence`、`RetiredSlots` によるGPU完了後物理スロット回収）を `CDX12Device`・`CDX12ComputeQueue`・`CDX12RenderTargetPass`・`CDX12GameSurface` へ接続し、主画面・パス中未閉じスコープ・撮影中・別スレッド・デバイス喪失を安全に制御済み|
|検査と互換台帳|Debug／Release x64 `project.sln` ビルド（警告・エラー0件）、`RunTimestampTests.ps1 -Platform`（CPU 11・既定GPU 34・WARP 34・デバイス喪失 5、通常アプリ6起動 `dx12`/`warp`/`dx12-no-ui`/`warp-no-ui`/`dxlib`/`null`）および `Tools/RunAllChecks.ps1 -SkipBuild -Quick` 全38ステップ合格（`Saved/ArgonCoordination/run_all_checks_r1_timestamps.log`）。互換台帳は481項目（API 337、Draw 100、Sample 33、Editor 11；Implemented 195、Verified 0）|

検査側の対応コミット: `a50b46154a5fda99657ce4fc509efcda78f8f4b3`（tree `a85bd573d9feaf4b39e94a7c4a768bc847f58dae`、ローカル）。
残る範囲（R1後続・R2以降）: `RGBA16F`／HDR形式・MSAA・`SV_Depth`・swapchain `resize` と終了/フレーム別領域・DXC／SM6・Copyキュー専用／Pipeline Statistics／`CProfilerSubsystem`連携・全R1横断のデバイス喪失復旧・R2共有メッシュ／材質／Forward・R3フレームグラフ／Deferred。

## 先行の作業: R1のカスタムグラフィックスパイプライン契約とMRT複数描画先（2026-11-12）

選定理由: 先行の単一描画先・D32深度に続き、API非依存のカスタムグラフィックスパイプライン作成・所有・遅延解放（`IGraphicsPipelineResources`）、複数描画先（MRT：1〜8枚 `RGBA8` ＋ 任意 `D32` 深度）へのパス記録と同一 `DIRECT` キュー提出、汎用入力を伴う `IRenderGeometry3D::DrawGraphicsPipeline`、ならびにMRT色＋`R32Float` 深度の合成描画・compute・撮影・主画面連携を接続し、R1の汎用描画契約とR2の共有メッシュ／材質・ForwardおよびR3のDeferred基礎を成立させました。詳細は [カスタムグラフィックスパイプラインとMRTの記録](R1_GRAPHICS_PIPELINE_MRT.md) および先行の [オフスクリーン描画先と深度の記録](R1_RENDER_TARGET_RESOURCES.md) を参照してください。

|項目|現在の状態|
|---|---|
|パイプライン・MRT共通値|`FGraphicsPipelineHandle`、`FGraphicsPipelineDesc`、`FGraphicsPipelineDraw`、`ERenderTargetColorFormat`、`FRenderTargetColorAttachment`、`EGraphicsBufferLayout`／`EGraphicsVertexAttributeFormat`、および `FRenderTargetCapabilities::MaxColorTargets`・`FRenderTargetPassDesc::ColorTargets` を追加・実装済み（`ERenderTargetColorFormat` は `Framework` 側 `ERenderTargetFormat` との衝突回避のため分離）|
|既存・新規窓口|`IGraphicsPipelineResources`（`CreateGraphicsPipeline`／`ReleaseGraphicsPipeline`）と `IRenderGeometry3D::DrawGraphicsPipeline`（既定実装 `false`）を追加し、`CThreadedRenderSurface`・`CDX12Device`・`CDX12PlatformSubsystem` へ接続済み|
|reflection・MRT実装|`CDX12GraphicsPipelineStore`（DXBC SM5.0／5.1、VS入力・PS `SV_Target0..N-1`・`Constant`/`Raw`/`Structured`・`RGBA8`/`R32Float` reflection照合、遅延破棄 `Retired`）、`CDX12RenderTargetPass`（1〜8枚MRT、任意色クリアPSO、未完了寿命保持）、`CDX12GeometryRenderer::DrawGraphicsPipeline`、`CDX12GameRenderer::BeginFrame`（MRT時の `Clip` 初期化）を実装済み|
|検査と互換台帳|Debug／Release x64 `project.sln` ビルド（警告・エラー0件）、`RunGraphicsPipelineTests.ps1 -Platform`（CPU 3・既定GPU 66・WARP 66、通常アプリ6起動 `dx12`/`warp`/`dx12-no-ui`/`warp-no-ui`/`dxlib`/`null`）、および `Tools/RunAllChecks.ps1 -SkipBuild -Quick` 全36ステップ合格。互換台帳は472項目（API 328、Draw 100、Sample 33、Editor 11；Implemented 186、Verified 0）|

検査側の対応コミット: `a8ad0b810bc1059237dfed51f844b013486b9ec2`（tree `00dc9638fbf05eabc9baf37af8253a8c93f9d42e`、ローカル。公開は運用担当で実施）。
残る範囲（R1後続・R2以降）: `RGBA16F`／HDR形式・MSAA・`SV_Depth`・読み取り専用DSV同時参照・DXC／SM6・timestamp計測・全R1横断のデバイス喪失復旧・R2共有メッシュ／材質／Forward・R3フレームグラフ／Deferred。

## 先行の作業: R1のオフスクリーン描画先資源と読み取れる深度・compute連携

選定理由: R1のcompute画像に続き、オフスクリーン色描画先（RGBA8 RTV＋SRV）と読み取れる深度（D32 DSV＋R32_FLOAT SRV）の能力照会・資源所有、共通窓口からのオフスクリーン描画と同一 `DIRECT` キューへの実GPU提出、色とスカラー深度を同時に読むcompute、通常描画と撮影への受け渡しを完結させ、R2の共有メッシュ／材質・ForwardおよびR3のフレームグラフ・Deferredの基礎を整えます。新しい資源番号体系や別インターフェースは増やさず、既存の `IRenderTextureResources`・`IRenderSurface`・`CDX12TextureStore`・`CDX12ComputePipelineStore`・`CDX12ComputeQueue` を拡張して接続しました。詳細は [オフスクリーン描画先と深度の記録](R1_RENDER_TARGET_RESOURCES.md) および先行の [compute画像の記録](R1_COMPUTE_TEXTURE_RESOURCES.md) を参照してください。

|項目|現在の状態|
|---|---|
|共通の形式・能力・パス値|`EComputeTextureFormat`（`RGBA8`、`R32Float`）、`FComputeTextureCapabilities::bR32FloatRead`、`FComputeTextureSlot::Format`、`FRenderTargetCapabilities`、`FRenderTargetPassDesc` を追加・実装済み|
|既存窓口の追加操作|`IRenderTextureResources` へ描画先能力照会・RGBA8描画先作成・D32深度作成、`IRenderSurface` へ `BeginRenderTargetPass`／`EndRenderTargetPass` を追加・実装済み。既定実装は未対応で、他の提供元を壊さない|
|GPU実体とオフスクリーン提出|`CDX12RenderTargetPass` を追加し、`CDX12TextureStore`（`R32_TYPELESS`／`D32_FLOAT`／`R32_FLOAT`、パス中ハザード拒否）・`CDX12GameRenderer`・`CDX12Device`・`CDX12GameSurface`・`CThreadedRenderSurface`・`CDX12PlatformSubsystem` へ接続済み。`EndRenderTargetPass` で同一 `DIRECT` キューへ実GPU提出を行う|
|スカラー深度compute|`CDX12ComputePipelineStore` で `Texture2D<float>`（1成分）と `Texture2D<float4>`（4成分）をreflectionで識別し、`CDX12ComputeQueue` で `DXGI_FORMAT_R32_FLOAT` のSRV束縛を実装済み|
|検査と互換台帳|Debug／Release各4プロジェクトのビルド（警告・エラー0件）、`RunRenderTargetTests.ps1 -Platform`、および `RunAllChecks.ps1 -SkipBuild -Quick` 全34項目を検証。互換台帳は469項目（Implemented 183、Verified 0）|

実装・確認した接続は以下です。

1. `CDX12TextureStore` で `CreateRenderTargetRGBA8`（RTV＋SRV）と `CreateDepthTextureD32`（`R32_TYPELESS` 資源、`D32_FLOAT` DSV、`R32_FLOAT` SRV）を所有し、作成時に初期クリア値でクリアして定常SRV状態へ遷移する。深度に対する `GetTextureResourceView`／`UpdateTextureRGBA8`／UAV要求を拒否する。
2. `CDX12RenderTargetPass` でオフスクリーン用のRTV／DSV記述子とパス状態を管理し、`BeginRenderTargetPass` で描画先と深度の所有・種別・寸法一致・パス中ハザード（自己参照描画・更新・compute競合・二重開始・撮影中開始）を検査する。
3. `EndRenderTargetPass` で描画バッチを記録してSRV定常状態へ戻した後、コマンドリストを閉じて主画面・computeと共有する同じ `DIRECT` キューへ直ちに `ExecuteCommandLists` で提出し、完了まで提出記録へ参照を保持して、後続の `SubmitCompute` が描画結果を確実に読めるようにする。
4. `CDX12ComputePipelineStore` と `CDX12ComputeQueue` で `EComputeTextureFormat::R32Float`（`Texture2D<float>` 読み取り専用）を検査・束縛し、色（`Texture2D<float4>`）とスカラー深度の同時読み取り・可視化画像生成を通す。
5. 描画した色ターゲットの通常画面／撮影への直接描画、深度のcompute可視化経由の描画、提出直後の `ReleaseTexture` と別寸法再作成をCPU参照実装・既定GPU・WARP・通常アプリ6起動で確認した。

検査側の対応コミット: `633d2edc7f25ebe181c7f0e78031454c2dafd9e4`（ローカル。公開はCodex担当）。

参照した公式文書（2026-11-11確認）:

- [Typed UAV loads](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads): 追加形式群と個別形式の能力を照会する。
- [形式のサポート1](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_format_support1) / [サポート2](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_format_support2): RTV・DSV・SRV sample／load・typed UAV load／storeの能力を照合する。
- [DXGI_FORMAT](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format): 同一深度資源でDSVとSRVを共存させるための `DXGI_FORMAT_R32_TYPELESS`・`DXGI_FORMAT_D32_FLOAT`・`DXGI_FORMAT_R32_FLOAT` の対応。
- [資源状態の同期](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12) / [コマンドリストの実行と同期](https://learn.microsoft.com/en-us/windows/win32/direct3d12/executing-command-lists): 同一 `DIRECT` キューでの描画先遷移・提出順序と資源寿命を管理する。
- [reflectionの入力flags](https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_shader_input_flags): `D3D_SIF_TEXTURE_COMPONENTS` により `Texture2D<float>` と `Texture2D<float4>` の成分数を検査する。

## 検査、書式、公開

C++とPowerShellはUTF-8 BOM＋CRLF、vcxproj／filtersはBOMなしCRLFです。既存Markdownの改行を一括変換しません。Tools/GameRendering.clang-formatを明示適用し、ラムダを引数に直接書いて括弧内が複数行になる場合は、名前付きの関数値に分けます。source format、依存境界の負例、互換台帳の確認を行います。

ソースを追加・移動したらGenerateProjectFilesを実行します。検証側の既存5設定は事前のバイト列とハッシュで保全し、生成結果のうち自分の登録だけを残します。ビルドは検証側でx64 Debug／Releaseの4プロジェクトを/m:1で行い、完了した生成物を固定してからRunAllChecks -SkipBuild -Quickを実行します。同じ構成のビルドと実行検査を競合させません。

MSVC v145では、型の変更後のRelease増分リンクでLNK1103、Debugの生成物でLNK1236を観測しました。前回はソースの最適化設定を変えず、検証側の該当obj、またはproject／Samples／Editorのx64/Releaseのiobj・ipdbをSavedへ退避して再生成すると通りました。再発時も対象が検証側の配下であることを絶対パスで確認し、必要な生成物だけを扱います。ユーザーの作業コピーを再帰削除しません。

commitはfeat:／fix:／refactor:／other:に日本語の説明を続けます。検査を先に対応する専用ブランチへ公開し、その実コミットを本体の文書へ記録します。本体はbackend-dx12へ履歴を前進させて公開し、GitHub上のtreeと検証したtreeが一致すること、ユーザーの既存変更が保持されることを確認します。


## Goalによる継続と現在の担当（2026-11-11）

Docs/ROADMAP.mdのR1完了をGoalとして継続中です。ArgonはAIクレジット不足で停止したため、Codexが未コミットの描画先・深度対応を引き継いでレビューしています。Argonの状態通知は停止前のworkingのままなので、それを現在の進行と解釈しません。現在の所有と作業記録はSaved/ArgonCoordination/owner.json、codex-status.jsonを確認します。同じ実装ファイルを別担当で並行編集しません。

公開基点は本体a4fca5021e83387c132564fe29c3f8ea841dff9a、検査45c2635ce7fd854eec7de99e44f68ad0f69b1451です。未公開の単一描画先・読み取れる深度の詳細とレビュー残件はR1_RENDER_TARGET_RESOURCES.mdへ記録します。MRT、timestamp、フレーム領域、resize・終了・GPU失敗とgraphics/computeの包括的検査もR1完了条件に照合して進めます。


### Codexレビューで追加した回帰確認

撮影の検査用接続を製品の描画先パスから除去し、検査側の組み合わせへ移しました。省略した内部深度の初回Loadと保持、最初のパスが失敗した場合の未提出消去値も検査します。通常画面へ既に記録した画像の読み取りが、後続の描画先更新・compute書き込みより後に実行される不具合を画素で再現し、Deviceの主画面命令を同じDIRECTキューへ区切って提出する接続へ修正しました。区切りではallocator・色・深度・クリップを初期化せず、追加のCPU側GPU待機も行いません。ゲーム用接続では描画先パス・撮影を閉じてからcomputeを提出します。

Debug／Releaseの生成物と検査はSaved/ArgonCoordination/codex-status.jsonに最新結果を残します。全34項目の回帰と公開前の保全確認が終わるまでは、この節だけで公開済みとは扱いません。
