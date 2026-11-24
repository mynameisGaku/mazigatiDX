# DX12開発の引き継ぎ

最終更新: 2026-11-21（日本時間）。この文書は作業中も更新します。実装済み、未接続、検証済みを分け、予定を完了扱いにしません。

**未対応・未確認の事項の一覧は [DX12_KNOWN_GAPS.md](DX12_KNOWN_GAPS.md)。push は ChatGPT（Codex）の `framework-dx12` の作業が行います（Claude は push しません）。**

**Codex への引き継ぎ（最新。最初に読む）: [CODEX_HANDOFF_2026-11-24.md](CODEX_HANDOFF_2026-11-24.md)。** 状態・取り込みと push の手順・次にやること・決まり・落とし穴を、1 つにまとめています。

## 目的と公開の基点

作業ブランチは`backend-dx12`です。[ロードマップ](ROADMAP.md)のR1基盤を進めています。R1全体、四描画方式、完全互換は未完了です。

- 今回の公開前基点: 本体`a4fca5021e83387c132564fe29c3f8ea841dff9a`、検査`45c2635ce7fd854eec7de99e44f68ad0f69b1451`。
- 描画先・MRT・GPU計測はローカルで実装・コミット済みです。フレーム転送領域とresizeのレビューを含めて検証し、対応する検査コミットを確定してから公開します。
- 最新の公開位置は[本体ブランチ](https://github.com/mynameisGaku/mazigatiDX/tree/backend-dx12)と検査ブランチを確認します。ローカルの作業完了を公開済みとは扱いません。

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

## 現在の作業: R1終了・失敗時安全性検証（2026-11-13）

- 今回の検証基点: 本体`fef199f`、検査`31a61c4`。調査・検証結果は`Saved/ArgonCoordination/r1-lifetime-review.md`へ記録しています。
- 再現・修正した不具合:
  - `CDX12Device`先行解放（`TryShutdown()`や`ResizeBuffers`/`GetBuffer`失敗時の`ReleaseOwnedResources()`）後に`CDX12ImGuiFrameRenderer::TryShutdown()`を呼ぶと、`Device->Wait()`と`Device->IsDeviceRemoved()`がともに`false`を返して`Shutdown()`が無限待機する問題を、`DeviceLifecycleMain.cpp`（`ImGuiFrameLifecycle`）で再現しました。`CDX12Device::IsInitialized() const`を追加し、`CDX12ImGuiFrameRenderer::TryShutdown()`が`Device->IsInitialized()`のときのみ`Wait()`・`IsDeviceRemoved()`を待機するよう修正しました（`PrepareShutdown()`タイムアウト中は`bInitialized == true`のため待機を継続）。
  - `CDX12ImGuiRenderer`が借用する`ID3D12Device`・`ID3D12CommandQueue`・`ID3D12DescriptorHeap`を`ComPtr`（`DeviceRef`/`QueueRef`/`DescriptorHeapRef`）で保持し、`ImGui_ImplDX12_Shutdown()`（外部窓破棄を含む）完了後に解放するよう修正しました。`ViewportContractMain.cpp`の`CheckExternalViewportOwnedShutdown`で実際に外部窓を作成し、`CDX12Device::TryShutdown`先行解放後および`ID3D12Device5::RemoveDevice()`後の`Renderer.TryShutdown`を既定GPU・WARPで検証しました。
- タイムアウト時の強制解放（リトライ上限での破棄）はGPU使用中資源のUAFを招くため採用せず、GPU完了またはデバイス喪失までの参照保持を維持しています。この時点では`Signal`/`SetEventOnCompletion`の非喪失失敗を未検証としましたが、後の故障注入検査と修正は本書末尾を参照してください。`ResizeBuffers`単体失敗は引き続き未検証です。
- 検査: Debug／Release各4プロジェクトのビルド（エラー0・警告0）、両構成の`RunDX12WindowTests.ps1`（`ImGuiFrameLifecycle`・実外部窓`ViewportContract`含む全件、既定GPU／WARP／`RemoveDevice`）、`Tools/RunAllChecks.ps1 -SkipBuild -Quick`が成功しました。 全体回帰は42項目で、Quickではサンプル集・テンプレートの全検査を省略しています。検査側の公開コミットは[8d56109](https://github.com/mynameisGaku/framework-test/commit/8d561098e212a6dabb0a76755e66120682ec8484)です。

## 先行の作業: Phase B `RGBA16F`と画像コピー・readback（2026-11-13）

- 今回の実装基点: 本体`f7c4eef`、検査`725b8be`。設計調査は`Saved/ArgonCoordination/phase-b-design-review.md`に記録しています。
- 既存の`IRenderTextureResources`、`CDX12TextureStore`、`CDX12ComputePipelineStore`、`CDX12ComputeQueue`、`CDX12GraphicsPipelineStore`、`CDX12RenderTargetPass`、`CDX12GeometryRenderer`、`CDX12GameRenderer`、`CDX12PlatformSubsystem`を拡張し、`CreateComputeTextureRGBA16F`、`CreateRenderTargetRGBA16F`、`CopyTexture`、`ReadTextureRGBA8`、`ReadTextureRGBA16F`、および`RGBA16F`＋`RGBA8`混合MRTを接続しました。
- `CDX12PlatformSubsystem::CopyTexture`／`ReadTextureRGBA8`／`ReadTextureRGBA16F`は`ExecuteRender`内で`NativeSurface->SubmitPendingMainCommands()`を呼び、主画面の未提出描画を確定してから同一`DIRECT`キューへコピー／readbackを積みます。撮影中・描画先パス中・未閉鎖`BeginGpuTimestamp`・非フレーム中・不正寸法・非有限値・異形式・自己コピー・デバイス喪失・非ゲームスレッドでは出力引数（`OutPixels`/`OutWidth`/`OutHeight`等）を変更せず`false`/無効ハンドルを返します。固定2D/3DのSDR経路は非`RGBA8`ターゲットへの描画を拒否し、`ColorClearPipeline16F`で`RGBA16F`クリアに対応します。
- 検査: Debug／Release各4プロジェクトのビルドと、両構成の`RunHdrTextureCopyTests.ps1 -Platform`（CPU 8・既定GPU 65・WARP 65・デバイス喪失3条件、アプリ6モード、主画面の更新前後の画素と撮影／パス／未閉鎖timestampでの拒否を含む）が成功しました。`Tools/RunAllChecks.ps1 -SkipBuild -Quick`も全42項目成功。Quickはサンプル集・テンプレートの全検査を含まず、複数GPUベンダーでは未検証です。検査側の公開コミットは[31a61c4](https://github.com/mynameisGaku/framework-test/commit/31a61c4a27e5a96b0e3ae54a86954a9a3f43a41f)です。

## 先行の作業: フレーム転送領域とresize（2026-11-13）

`CDX12UploadArena`を既存デバイスへ所有させ、主画面・描画先パス・撮影の一時頂点を共有ページへ書き込みます。記録中の予約とGPUフェンス完了まで先頭を再利用せず、主画面の途中提出後に失敗した場合も未提出として取り消しません。窓の寸法変更はCPU命令を完了させて窓側へ排他的に貸し、GPU完了後に交換先を更新してから描画側で深度を再作成します。詳細は[転送領域と寸法変更](R1_FRAME_UPLOAD_RESIZE.md)を参照します。

レビューで、最大64MiB要求へ整列余裕を余分に足して拒否する不具合を再現・修正しました。GPU待機の失敗後に別パス資源を回収する順序も修正しました。修正後のDebug／Release各4プロジェクトは警告・エラー0件。両構成でCPU615・既定GPU1306・WARP1306・デバイス喪失6と通常アプリ6モードが成功し、全40項目の回帰も通過しました。記録はSaved/ArgonCoordination/codex-r1-upload-resize-all-checks.log（詳細ログmazigati_checks_20261113_083017）です。-Quick指定によりサンプル集とテンプレートの全検査は含みません。

現在の編集・検証・公開担当はCodexです。Antigravity側は引き継ぎを受領し、既存ログの報告を終えて待機しています。古いtask-2808のRUNNING表示や11月11日のクレジット不足記録を、現在のソース所有の根拠にはしません。Saved/ArgonCoordination/codex-status.jsonとstatus.jsonを照合し、同じソースの編集と同じ構成のビルドを競合させません。

今回の転送領域・resize検査の対応コミット: `725b8be50b6fc45f2e28ffb77190b13812708d4f`（`codex/dx12-windows-validation`）。

## 先行の作業: R1のGPUタイムスタンプ計測契約（2026-11-12）

選定理由: [ロードマップ](ROADMAP.md) R1 GPU基盤の `timestamp` 項目およびR4/R9のGPU時間計測基礎として、API非依存のGPUタイムスタンプ能力照会・計測ハンドル作成・遅延スロット回収（`IGpuTimestampResources`）、主画面および `BeginRenderTargetPass`〜`EndRenderTargetPass` 内の範囲計測（`IRenderSurface::BeginGpuTimestamp`／`EndGpuTimestamp`）、ならびに `IComputeQueue::SubmitComputeWithTimestamp` を追加し、同一 `DIRECT` キュー上のGraphics→Compute→Readback区間のGPU計測と未完了スロット保護を成立させました。詳細は [GPUタイムスタンプ計測契約](R1_GPU_TIMESTAMPS.md) を参照してください。

|項目|現在の状態|
|---|---|
|タイムスタンプ共通値・窓口|`FGpuTimestampHandle`・`FGpuTimestampCapabilities`・`FGpuTimestampResult`・`IGpuTimestampResources` を追加し、`IRenderSurface`（`BeginGpuTimestamp`／`EndGpuTimestamp`）と `IComputeQueue`（`SubmitComputeWithTimestamp`、既存 `FComputeDispatch` 初期化を保持）を `CThreadedRenderSurface`・`CDX12PlatformSubsystem` へ接続済み|
|DX12実装・同期・寿命|`CDX12TimestampStore`（`D3D12_QUERY_HEAP_TYPE_TIMESTAMP` 512スロット = 256ペア、`D3D12_HEAP_TYPE_READBACK` 4KiB、専用 `ID3D12Fence`、`RetiredSlots` によるGPU完了後物理スロット回収）を `CDX12Device`・`CDX12ComputeQueue`・`CDX12RenderTargetPass`・`CDX12GameSurface` へ接続し、主画面・パス中未閉じスコープ・撮影中・別スレッド・デバイス喪失を安全に制御済み|
|検査と互換台帳|Debug／Release x64 `project.sln` ビルド（警告・エラー0件）、`RunTimestampTests.ps1 -Platform`（CPU 11・既定GPU 34・WARP 34・デバイス喪失 5、通常アプリ6起動 `dx12`/`warp`/`dx12-no-ui`/`warp-no-ui`/`dxlib`/`null`）および `Tools/RunAllChecks.ps1 -SkipBuild -Quick` 全38ステップ合格（`Saved/ArgonCoordination/run_all_checks_r1_timestamps.log`）。互換台帳は481項目（API 337、Draw 100、Sample 33、Editor 11；Implemented 195、Verified 0）|

検査側の対応コミット: `803133630f3e69f64ddc682dcbe03c9524f51069`（tree `a85bd573d9feaf4b39e94a7c4a768bc847f58dae`、対応する公開済み検査）。
残る範囲（R1後続・R2以降）: `RGBA16F`／HDR形式・MSAA・`SV_Depth`・swapchain `resize` と終了/フレーム別領域・DXC／SM6・Copyキュー専用／Pipeline Statistics／`CProfilerSubsystem`連携・全R1横断のデバイス喪失復旧・R2共有メッシュ／材質／Forward・R3フレームグラフ／Deferred。

## 先行の作業: R1のカスタムグラフィックスパイプライン契約とMRT複数描画先（2026-11-12）

選定理由: 先行の単一描画先・D32深度に続き、API非依存のカスタムグラフィックスパイプライン作成・所有・遅延解放（`IGraphicsPipelineResources`）、複数描画先（MRT：1〜8枚 `RGBA8` ＋ 任意 `D32` 深度）へのパス記録と同一 `DIRECT` キュー提出、汎用入力を伴う `IRenderGeometry3D::DrawGraphicsPipeline`、ならびにMRT色＋`R32Float` 深度の合成描画・compute・撮影・主画面連携を接続し、R1の汎用描画契約とR2の共有メッシュ／材質・ForwardおよびR3のDeferred基礎を成立させました。詳細は [カスタムグラフィックスパイプラインとMRTの記録](R1_GRAPHICS_PIPELINE_MRT.md) および先行の [オフスクリーン描画先と深度の記録](R1_RENDER_TARGET_RESOURCES.md) を参照してください。

|項目|現在の状態|
|---|---|
|パイプライン・MRT共通値|`FGraphicsPipelineHandle`、`FGraphicsPipelineDesc`、`FGraphicsPipelineDraw`、`ERenderTargetColorFormat`、`FRenderTargetColorAttachment`、`EGraphicsBufferLayout`／`EGraphicsVertexAttributeFormat`、および `FRenderTargetCapabilities::MaxColorTargets`・`FRenderTargetPassDesc::ColorTargets` を追加・実装済み（`ERenderTargetColorFormat` は `Framework` 側 `ERenderTargetFormat` との衝突回避のため分離）|
|既存・新規窓口|`IGraphicsPipelineResources`（`CreateGraphicsPipeline`／`ReleaseGraphicsPipeline`）と `IRenderGeometry3D::DrawGraphicsPipeline`（既定実装 `false`）を追加し、`CThreadedRenderSurface`・`CDX12Device`・`CDX12PlatformSubsystem` へ接続済み|
|reflection・MRT実装|`CDX12GraphicsPipelineStore`（DXBC SM5.0／5.1、VS入力・PS `SV_Target0..N-1`・`Constant`/`Raw`/`Structured`・`RGBA8`/`R32Float` reflection照合、遅延破棄 `Retired`）、`CDX12RenderTargetPass`（1〜8枚MRT、任意色クリアPSO、未完了寿命保持）、`CDX12GeometryRenderer::DrawGraphicsPipeline`、`CDX12GameRenderer::BeginFrame`（MRT時の `Clip` 初期化）を実装済み|
|検査と互換台帳|Debug／Release x64 `project.sln` ビルド（警告・エラー0件）、`RunGraphicsPipelineTests.ps1 -Platform`（CPU 3・既定GPU 66・WARP 66、通常アプリ6起動 `dx12`/`warp`/`dx12-no-ui`/`warp-no-ui`/`dxlib`/`null`）、および `Tools/RunAllChecks.ps1 -SkipBuild -Quick` 全36ステップ合格。互換台帳は472項目（API 328、Draw 100、Sample 33、Editor 11；Implemented 186、Verified 0）|

検査側の対応コミット: `3d9de630583479533a8eb109fd0b0b41db8a9ef4`（tree `00dc9638fbf05eabc9baf37af8253a8c93f9d42e`、対応する公開済み検査）。
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

検査側の対応コミット: `5c4020649ada15663a19202282359b0f964e9a95`（対応する公開済み検査）。

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


## 継続と現在の担当

R1の残件は、失敗・終了の横断検査などロードマップの未完了項目です。RGBA16F資源・画像コピーとreadbackの今回の対応範囲は上の作業欄を参照してください。現在の担当と最新検証結果は上の作業欄とSaved/ArgonCoordination/codex-status.jsonに記録します。MRT・タイムスタンプの成功だけでR1完了とは扱いません。

### Codexレビューで追加した回帰確認

撮影の検査用接続を製品の描画先パスから除去し、検査側の組み合わせへ移しました。省略した内部深度の初回Loadと保持、最初のパスが失敗した場合の未提出消去値も検査します。通常画面へ既に記録した画像の読み取りが、後続の描画先更新・compute書き込みより後に実行される不具合を画素で再現し、Deviceの主画面命令を同じDIRECTキューへ区切って提出する接続へ修正しました。区切りではallocator・色・深度・クリップを初期化せず、追加のCPU側GPU待機も行いません。ゲーム用接続では描画先パス・撮影を閉じてからcomputeを提出します。

Debug／Releaseの生成物と検査はSaved/ArgonCoordination/codex-status.jsonに最新結果を残します。全34項目の回帰と公開前の保全確認が終わるまでは、この節だけで公開済みとは扱いません。

---

## 外部ビューポート同期失敗の故障注入と所有境界修正（2026-11-13）

- **背景と設計記録**: 詳細は `Saved/ArgonCoordination/viewport-sync-failure-design.md` §6 を参照。
- **変更概要**:
  - `VendorModules/ImGui/imgui_impl_dx12.h/.cpp`: 故障注入専用の公開APIを追加せず、`MAZIGATI_IMGUI_DX12_SYNC_HOOK` による内部同期境界と、終了可否を表す連携用API `ImGui_ImplDX12_TryShutdown(bool bGpuConfirmedIdleOrRemoved)` を導入。提出後 `Signal` 失敗時の描画・テクスチャ更新・破棄停止、`DestroyWindow` 失敗時の `RetiredViewports` 退避、`SetEventOnCompletion` 失敗時のポーリング待機、`MainViewport` (`vd->CommandQueue == nullptr`) の `bd->pCommandQueue` フォールバックを実装。
  - `Library/Platform/DX12/ImGui/Render/DX12ImGuiRenderer.h/.cpp`: `CDX12QueueCompletion` による自前キュー待機 `WaitQueue` と `Shutdown()` の完了・喪失まで待機ループを追加。
  - `Library/Platform/DX12/ImGui/Frame/DX12ImGuiFrameRenderer.cpp`: `Device->IsInitialized() && Device->GetNativeQueue() == ImGuiRenderer.GetQueue()` 一致時のみ `Device->Wait()` を採用し、それ以外は `ImGuiRenderer.WaitQueue()` へフォールバック。
  - 検査側（`framework-dx12-selftests`）: `ViewportContract.vcxproj` で `imgui_impl_dx12.cpp` を `MAZIGATI_IMGUI_DX12_SYNC_HOOK=1` 付きで独立ビルドし、`ViewportContractMain.cpp` にて非喪失時の `Signal` / `SetEventOnCompletion` 故障注入、`RetiredViewports` 保持、`Device::Wait` 回復、`RemoveDevice()` 後解放を実GPU・WARP で検証。 Debug／Releaseのウィンドウ検査と全42項目のクイック回帰が成功。Quickはサンプル集・テンプレートの全検査を含まない。検査側の公開コミットは[a28343b](https://github.com/mynameisGaku/framework-test/commit/a28343bebaba6b560682127e0a64cecae85940f4)。

---

## Substrate並行開発（`framework-stl` `19e7170`）との統合引き継ぎメモ（時点限定）

- **調査対象（2026-11-13時点）**: `C:/Users/g0190/.claude/worktrees/framework-stl`（`Asset` ブランチ、当時のHEAD `19e7170`）。後の公開・追加コミットと検証状況は本書末尾を参照する。Claude側は作業中のため、統合時にローカルの状態を再確認する。
- **ローカル進行中の内容（2026-11-13確認）**: `MaterialCompiler`・`MaterialEval`・`MaterialGraph`・`MaterialHlsl`・`MaterialProgram`・`MaterialSubstrate`に未コミット変更があり、`MaterialEnvironmentMap`・`MaterialShadowMap`・`MaterialSceneConstants`に未追跡ファイルがある。シーン定数、環境画像、影用描画先・深度の契約も統合時に確認する。これらの変更やClaude側の`project.sln`を編集・破棄しない。
- **能力・バックエンド前提の差異（要注意）**:
  - Claude 側の `Docs/MATERIAL.md` では `FMaterialCapabilities` の既定値が DxLib 相当で `bFullSubstrate = true` となっており、生成 HLSL も現時点で DxLib / D3D11 向けである。
  - **DX12 バックエンドが同等に評価できると仮定して能力フラグ（`bFullSubstrate = true` 等）を宣言してはならない。** 既存 DX12 側は DXBC SM5 グラフィックスパイプライン・専用描画スレッド・GPU 完了までの資源保持を提供するが、共通マテリアルの DX12 描画接続は未検証である。
- **マージ競合の時点確認結果（`git merge-tree` による作業ツリーを変更しない比較：`backend-dx12 016a605` vs `Asset 19e7170`）**:
  - merge-base `c1d214` 以降、双方で `DrawCommand.h`、`DxLibDrawExecutorSubsystem.cpp`、`mazigatiDX.vcxproj` / `.filters`、`project.vcxproj` / `.filters`、`Docs/DEVELOPMENT.md` などが重複変更されている。
  - 時点限定の `git merge-tree` では実際の内容競合は **`mazigatiDX.vcxproj`** と **`mazigatiDX.vcxproj.filters`** の 2 件であり、`DrawCommand.h` / `DxLibDrawExecutorSubsystem.cpp` / `project.vcxproj` 等はテキスト上は自動統合される（ただし Claude 側は進行中であり、上記は時点限定の結果）。
- **統合時の必須検証課題**:
  1. 自動統合される `DrawCommand.h` / `DxLibDrawExecutorSubsystem.cpp` 等について、双方の変更意図（DxLib 実行系と DX12 描画スレッド・コマンド境界）の意味的整合性を確認すること。
  2. Surface の頂点・定数・画像バインドの DX12 側対応を実装・確認すること。
  3. 能力診断（`FMaterialCapabilities` の DX12 用定義とフォールバック診断）を未対応のまま `true` にしないこと。
  4. マテリアル／シェーダーのホットリロード時の GPU 資源寿命（使用中パイプライン・定数バッファ・テクスチャの GPU 完了後解放）を確認すること。
  5. CPU / GPU 比較および描画スレッド境界（専用描画スレッド契約）を回帰検査で確認すること。

---

## Substrate 互換縦の経路の調査・検証結果（2026-11-13）

- **調査・検査記録**: 詳細は `Saved/ArgonCoordination/substrate-dx12-integration.md` を参照。
- **実測で判明した事実**:
  - `FShaderVertex3D`（`Library/Framework/Render/Types/ShaderVertex.h`）は `alignas(16)` の `FVector3`/`FVector4` を持つため **`sizeof == 112`、`alignof == 16`**（`Position 0 / SubPosition 16 / Normal 32 / Tangent 48 / Binormal 64 / Diffuse 80 / Specular 84 / U 88 / SubU 96`）であり、Claude 側 `DrawCommand.h` のパック済み `FDrawShaderVertex3D`（`sizeof == 88`）とはレイアウトが異なる。
  - `D3DCompile` (`vs_5_0`) の DXBC 反射では `VertexInput` 内の未使用属性が落ちず全 9 属性が `VsDesc.InputParameters` に残るため、`CDX12GraphicsPipelineStore::ValidateVertexInputs` には 9 要素すべてを渡す必要がある（`ps_5_0` の未使用 `cbObject : b5` は `PsDesc.BoundResources` から除去されるが、VS 側で参照されるため `b4`/`b5`/`b6` 宣言は通る）。
  - `MaterialShadowMap.cpp` は `ERenderTargetFormat::Float32`・`Channels = 1` のカラー描画先＋`bDepth = true` で深度を色へ出力しており、`NumRenderTargets == 0` 必須とは読めない。この調査時点では DX12 の `R32Float` カラー描画先は未対応だったが、後述の対応を実装した。`Texture2D g_ShadowMap` のグラフィックス読み取りも後述のとおり対応したが、材質全体の統合は未完了。
- **検証結果**:
  - この調査時点の検査側公開コミットは[a2ec253](https://github.com/mynameisGaku/framework-test/commit/a2ec25380653e44f62bea15909fa24b7f203b9b4)。Claude側の自動生成HLSLそのもの、88Bの描画命令用頂点によるGPU描画、影用R32単一成分カラー描画先はこの時点では未検証。後者のDX12基盤については後述の検査を参照。
  - `framework-dx12-verify` の製品コードおよび保護 7 ファイルは変更なし（共通 `Library/Framework/Material` も DX12/ImGui 非依存のまま）。
  - `framework-dx12-selftests/GraphicsPipelineContractMain.cpp` に実型 `FShaderVertex3D`（112B）を用いた Substrate Surface 互換の縦経路検査（検証用 HLSL、`b4`/`b5`/`b6`、`t0`/`t1`/`s0`、`RGBA16F` + `D32Float`、HDR readback）を追加し、`RunGraphicsPipelineTests.ps1` の Debug / Release 双方で `--cpu`（3 checks, 0 failures）・`--default`（71 checks, 0 failures）・`--warp`（71 checks, 0 failures）が全件成功した。

---

## R32Float カラー描画先の設計・検証結果と後続課題（2026-11-13）

- **設計と実装（DX12 / Core 側）**:
  - `ERenderTargetColorFormat::R32Float`、`FRenderTargetCapabilities`（`bR32FloatRenderTarget` / `bR32FloatBlendable`）、`IRenderTextureResources::CreateRenderTargetR32Float` / `ReadTextureR32Float` を追加。
  - `CDX12TextureFormat` で `DXGI_FORMAT_R32_FLOAT` の `TEXTURE2D | RENDER_TARGET | SHADER_LOAD | SHADER_SAMPLE` および任意の `BLENDABLE` を照会し、`bAlphaBlend` 指定時のみ `bR32FloatBlendable` を要求。
  - `CDX12TextureStore` に `ETextureKind::RenderTargetR32Float` を追加し、`DepthD32`（深度専用・DSV）とは厳格に分離（`CopyTexture` は `R32Float` 同士のみ許可、`GetTextureResourceView` / 固定2D/3D SDR経路は拒否、MRT ターゲット順の形式照合を実施）。
  - `CDX12RenderTargetPass::PrepareColorClearPipeline(ERenderTargetColorFormat)` は要求された形式のクリア PSO のみを遅延生成し、非対応デバイスで既存の `RGBA8` / `RGBA16F` クリアが連鎖失敗しないように分離（`ComPtr` のアドレス取得には `std::addressof` を使用）。
  - `CDX12PlatformSubsystem` のゲームスレッド転送窓口（`CreateRenderTargetR32Float` / `ReadTextureR32Float` 等）において、`ReadTexture*` 前に `SubmitPendingMainCommands()` を実行し、`Device->GetTextureResources()` の `nullptr` 返却時（未初期化・終了中・リサイズ失敗等）に出力引数を保持して安全に `false` / 無効ハンドルを返すようにした。
- **検査上の任意能力分岐と未実測範囲**:
  - 検査側の公開コミットは[ecf628a](https://github.com/mynameisGaku/framework-test/commit/ecf628a327179b260f351b8261328a15f523b097)。Debug の Quick 検査は全項目成功、Release は容量不足を解消後に再実行した Quick 検査 24 項目が全て成功。両構成の GraphicsPipeline 検査は CPU 3・既定 GPU 102・WARP 102 件成功した。Quick はサンプル集・テンプレートの全検査を省略しており、複数 GPU ベンダーでは未検証。
  - `GraphicsPipelineContractMain.cpp` および `HdrTextureCopyPlatformSelfTest.cpp` は `FRenderTargetCapabilities::bR32FloatRenderTarget` に応じて分岐し、`true` の場合は `R32Float` 検査を実施、`false` の場合は `CreateRenderTargetR32Float` の拒否を確認したうえで既存の `RGBA8` / `RGBA16F` 検査を継続し、未対応スキップであることを明示表示する。
  - **未実施**: 手元の実機 GPU / WARP 環境ではいずれも `bR32FloatRenderTarget == true` であるため、`bR32FloatRenderTarget == false` 環境および偽能力による実機・製品経路の未対応分岐検証は未実施（テスト専用 API は製品側へ追加しない方針を維持）。
- **Claude 側 `MaterialShadowMap` / `Texture2D g_ShadowMap` との互換性（未統合）**:
  - この時点の DX12 側 Graphics / Compute 読み取りはスカラー `Texture2D<float>` のみ許可し、Claude 側の `Texture2D g_ShadowMap` を拒否していた。後述の変更で Graphics 側の浮動小数点ベクトル読み取りを許可した。Compute 側は従来どおりスカラーのみ。
- **後続課題（急ぎではない・今スコープ外）: 埋め込みシェーダーの `.hlsl` 外部化／単位分割**:
  - 現在 `DX12RenderTargetPass.cpp` やテスト等に文字列埋め込みされているシェーダーについて、将来的に `.hlsl` ファイル外部化またはシェーダー単位のソース分割を検討する。
  - 検討時は **ビルド統合・配布同梱・作業ディレクトリ非依存の探索経路・実行時更新／キャッシュ・ImGui/DX12 の責務分離・既存の `ShaderCompile` 仕組みとの整合** を考慮すること。

## GPU待機期限切れ時のサイズ変更（2026-11-13）

- R1の失敗・再試行検査として、既存の `UploadResizeContractMain.cpp` に、DIRECTキューをフェンスで止めた状態で `CDX12Device::Resize` を呼ぶ検査を追加した。既定の待機期限切れで `false` を返し、寸法・初期化状態・バッファを保持すること、待機解除後にバッファ内容が変わらず、サイズ変更を再試行して描画を再開できることを確認した。
- Debug / Release とも CPU 615、既定GPU 1314、WARP 1314、デバイス喪失 6 条件が成功。SDK debug layer は有効。検査側の公開コミットは[16203c5](https://github.com/mynameisGaku/framework-test/commit/16203c5e354c2ae6437dc4f3c2548bdd6578ca84)。
- この検査は `ResizeBuffers` 呼び出し前のGPU待機期限切れを対象とする。`ResizeBuffers` 自体や `GetBuffer` の単独失敗、および複数GPUベンダーでの実測は引き続き未検証。Quick全体やアプリ6起動の再実行は今回行っていない。
- 参考: [MicrosoftのResizeBuffers契約](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers)（旧バッファへの参照を解放してからサイズ変更）、[Unreal Engineの描画スレッド](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)（スレッド間の所有と完了同期）。

## R32Floatのグラフィックス読み取りと材質統合の準備（2026-11-14）

- Claude側のローカル作業では、影画像を `Texture2D g_ShadowMap` として宣言し、`Load(...).r` を使っている。DX12のグラフィックスパイプラインの照合を拡張し、`R32Float` の単一成分カラーまたはD32深度を、浮動小数点のスカラーだけでなく `Texture2D<float4>` 等でも読めるようにした。整数型やRGBA資源のスカラー宣言は従来どおり拒否する。Compute側の契約は変更していない。
- Direct3Dでは欠けた成分はG/Bが0、Aが1となる。[MicrosoftのTexture Object仕様](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-type)を参照。資源とシェーダーの所有・提出・寿命は既存の仕組みを使用し、[Unreal Engineの描画スレッドの説明](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)も参照した。
- Debug / Releaseのソリューションビルド、および両構成のGraphicsPipeline検査が成功。CPU各3、既定GPU・WARP各107条件で、R32FloatカラーとD32深度の `Load` / `SampleLevel` を実画素で確認し、整数型の拒否と既存のスカラー経路も検査した。SDK debug layer は有効。アプリの `dx12` / `warp` / 各no-ui / `dxlib` / `null` も両構成で成功した。検査側コミットは[e25fb91](https://github.com/mynameisGaku/framework-test/commit/e25fb9123b1a56f68c60b26461ca224a2e402db1)。
- `Tools/RunAllChecks.ps1 -SkipBuild -Quick` はDebug / Release合計42項目すべて成功。Quickではサンプル集とテンプレートの全検査を省略する。
- これは影画像の読み取り基盤までの対応であり、Claude側の生成HLSL全体、影パス、材質のDX12統合や複数GPUベンダーでの検証は完了していない。

## 実材質の生成HLSLと一時統合の検査（2026-11-14）

- 固定した比較対象はDX12 `a39ebfc`、Claude側ローカル `d0433ab`（`19e7170`からの追加は材質拡張 `1f53c58` と配布スクリプト `d0433ab`）。検査中にClaude側へ権利表記の文書コミット `ece4df6` が追加されたため、統合時には最新のローカル状態を再確認する。この検査は両ブランチを実際にマージ・公開したことを意味しない。
- この時点の `git merge-tree` の内容競合は `mazigatiDX.vcxproj` / `.filters` の2件。別の一時作業コピーでDX12側の一覧を基に `Tools/GenerateProjectFiles.ps1 -Root .` を実行し、Claude側の材質とDX12側の描画・スレッド関連の両方を含めた。自動統合された他のファイルについても、テキスト上の競合がないことだけでは動作互換を保証しない。
- 一時統合コピーのライブラリはDebug / Releaseでビルド成功。検査側の[363fc4a](https://github.com/mynameisGaku/framework-test/commit/363fc4aba976a79656695167aa3e8affdbc19210)に `RunSubstrateIntegrationTests.ps1 -FrameworkRoot <統合コピー> -Configuration Debug|Release` を追加した。実際のCandyPaint / HammeredMetal材質からフル評価用HLSLを生成し、環境・影の4組合せを、両構成・既定GPU / WARPでコンパイルしDX12パイプライン作成まで確認した。両GPUでSDKデバッグ層は有効だった。必要な材質またはDX12のファイルがない場合は不足を表示してスキップする。
- ここでの `bFullSubstrate = true` は検査用設定であり、DX12製品の能力宣言ではない。実描画画素、影・照明の正しさ、88バイト描画命令頂点との接続、材質や画像の更新・解放寿命は未検証。検査は現在 `RunAllChecks.ps1` に自動登録していない。
- Claude側 `d0433ab` のDebugソリューションビルドと `PackageGame.ps1` による配布フォルダー・ZIP生成、Nullバックエンド30フレーム起動も別途成功。Gitの対話なし認証ではClaude側の追加履歴をpushできず、GitHubの `dev` はこの確認時点で `19e7170` のまま。元のコミット履歴とClaude側作業を維持するため、別の履歴を作って置き換えていない。
- 実材質のHLSL変換は [Microsoftのコンパイラ関数](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-d3dcompiler-reference-functions)、材質の層と能力の考え方は [Unreal EngineのSubstrate概要](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-substrate-materials-in-unreal-engine) を参照した。

## DX12初期化失敗の回復と終了状態（2026-11-14）

- `CDX12Device::InitializeOnQueue` はGPU資源の確保後に失敗すると途中の `Device` / `Queue` 等が残り、明示的な終了を挟まなければ再初期化できなかった。同じ窓で2個目のフリップ方式交換先を作る実DXGI失敗で再現。初期化中はこの所有者からGPU命令を提出しないため、失敗時は新規資源を戻し、同じキューを使う別の所有者のGPU処理を待たずに再試行できるようにした。入力拒否や、既存の初期化済み所有者に対する二重初期化では既存状態を保つ。
- `ReleaseOwnedResources` では未提出・完了済み・デバイス喪失のいずれかを前提として資源を戻し、撮影要求・コピー済み・提出済みの状態も消去する。修正前は終了後の再初期化直後に旧撮影要求が無関係なGPU処理を待ち得たことを、キュー待機を使う検査で再現した。失敗時の呼出側の画素配列も保持する。
- 前回追加した `SubstrateIntegrationMain.cpp` が通常アプリのワイルドカードに含まれ、検査専用の `main` と文字コードにより通常ビルドを壊した問題も修正した。専用マクロとBOM / CRLFで分離し、専用マクロを有効にした検査コードのDebug / Releaseコンパイルも確認。検査側の対応コミットは[4954a6b](https://github.com/mynameisGaku/framework-test/commit/4954a6b6a3f5429d5cf9eeea1352c6dcdae52f95)。
- 他のVisual Studio作業と分離したクリーンな作業コピーでDebug / Releaseソリューションをビルドし、`RunDX12WindowTests.ps1 -Platform` と `RunCaptureTests.ps1 -Platform` が両構成で成功。新しいデバイス所有者の正常系は既定GPU / WARP各41条件。`CheckSourceFormat.ps1`、DX12構成・依存の静的検査、コミット書式も成功した。全42項目のQuick回帰は今回実行していない。
- **未解決の診断**: Debugのデバイス喪失検査で、待機中のcompute提出のdescriptor heap / PSO / root signature / resourceについてSDKメッセージ921（実行前に解放済み）が断続的に発生した。新実装の喪失検査でも発生し、修正前の `DX12Device.cpp` を同じ残りのライブラリにリンクした比較でも60回中2回発生した。別の比較30回では新実装1回・修正前0回。原因は未確定で、警告を抑制していない。喪失検査を繰り返し、完了通知と資源・キューの寿命を調べること。[D3D12コマンドリストの資源寿命](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles) と [RemoveDevice時のfence](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-removedevice) を参照。
- **未解決の検査タイムアウト**: Debugの撮影スイートは `NativeGameContract --default` で30秒のタイムアウトが2回発生。その後の単独起動とスイート再実行は成功したが、原因は特定できていない。Releaseの撮影スイートは成功。ユーザーが起動しているVisual Studio / アプリを停止せず、別の作業コピーで検査した。

## Claude側との最新比較と文書の整合（2026-11-14）

- Claude側のローカル `Asset` は確認時点で `c82402e` まで進み、未コミットの `project.sln` を保持していた。DX12 `9ed4ad6` との作業ツリーを変更しない `git merge-tree` では、従来の `mazigatiDX.vcxproj` と `.filters` に加えて `README.md` も内容競合となった。この結果は両HEADが進むと変わる。マージ・編集は行っていない。
- computeと描画先の文書に残っていた画像形式・スレッド・後続機能の古い記述を、現在の実装と当時の検査記録を区別して更新した。製品コードや動作は変更していない。
- デバイス喪失時、共有キュー上の提出資源をいつ解放できるかは引き続き未解決。Microsoftの[デバイス喪失サンプル](https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12xGPU/src/D3D12xGPU.cpp)は待機を試みた後に資源を解放・再作成するが、外部所有者が保持する共有キュー上の未完了提出について警告921を解消する解放順序を保証するものではない。[破棄通知](https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/nn-d3dcommon-id3ddestructionotifier)も破棄対象へのコールバック中のアクセスを禁じており、これだけで安全なGPU完了を証明できない。

## デバイス喪失と撮影の追加調査（2026-11-14）

- `RemoveDevice` は待機用fenceを `UINT64_MAX` にする。検査側の[3e871f9](https://github.com/mynameisGaku/framework-test/commit/3e871f98e5936d87007fd1bc21faa50052f823f4)で、実際に待機していたfenceの最大値と、未完了computeの `GetComputeCompletion` / `WaitCompute` が喪失を完了と誤認せず、出力も保持することを追加検査した。Debug / Releaseの既定GPU / WARPでDeviceLifecycleおよびImGuiFrameLifecycle、ViewportContractが成功し、SDKデバッグ層も有効だった。
- SDKメッセージ921の原因を切り分けるため、別の一時コピーだけで提出記録の資源参照をプロセス終了まで追加保持し、通常版と比較した。直列80回と同時起動80回の各160回は両方とも警告なし。喪失で待機fenceが最大値になることは確認できたが、警告は前回から低頻度であり、この結果だけで追加保持の有効性や原因を証明できない。追加保持や検査用のアクセス変更は製品に入れておらず、メッセージ921は未解決のまま。通常の完了番号と喪失値の区別については [Microsoftのfence仕様](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue) を参照。
- 以前30秒タイムアウトしたDebugの `NativeGameContract --default` について、新しい実行先で30回反復し全件成功。前回の停止ログは異なる検査箇所で途切れており、原因の特定には至っていない。タイムアウトを延長したり失敗を無視したりしていない。


## 喪失時のSDK診断の観測点（2026-11-14）

- 検査側の[b933cbb](https://github.com/mynameisGaku/framework-test/commit/b933cbbba3363572b2cc010498fbc17900c6544d)は、DeviceLifecycle／ImGuiFrameLifecycleでSDKの警告以上を最初に観測した確認点を表示する。`RemoveDevice` の前後、完了照会後、終了後、所有者破棄後を区別する。SDKによる非同期の記録時刻を示すもので、警告の原因となった命令の実行時刻を証明するものではない。警告921などの扱いを緩めていない。
- 別の一時検査で、各確認点の直前に意図的な警告を1件ずつ追加すると、それぞれ期待した確認点で表示され、検査自体は5件の警告により意図どおり失敗した。製品・公開検査へ人工的な警告は入れていない。
- Debug／Release、既定GPU／WARP、GPU単独／ImGui利用者の組合せで通常終了とデバイス喪失の計16起動が成功し、SDKデバッグ層も有効だった。両検査のReleaseリンクを既存の他検査と同じLTCG・増分リンク無効に揃え、警告なくリビルドした。`CheckSourceFormat.ps1` は1831ファイル成功。
- 別の一時比較では、通常版と提出記録をプロセス終了まで追加保持する版を2プロセスで交互の起動順に実行し、既定GPU各200回・WARP各100回は両版とも警告なし。比較中に上記の検査ビルドも行っており、実行条件は完全には一定でない。警告が再現しなかったため、追加保持の効果や根本原因は判断できず、SDK警告921は未解決のまま。
- 資源保持と喪失通知は [Microsoftのコマンドリスト仕様](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles) と [fence仕様](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue)、CPU側の所有と完了は [Unreal Engineの描画スレッド](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)を参照した。


## R0 モデル取り込みの数値と採取量（2026-11-15）

- `FImportMatrix::NearlyEquals` はNaNを含む行列を同じ姿勢と誤認し、アニメーションのtrackを落とし得た。非有限な行列や許容値は一致しないとし、有限floatの差はfloat64で比較する。`NImportedScene::Validate` はノード行列、スキンのオフセット、アニメーションキーとフレームレートの非有限値も診断する。ModelExporterは従来どおりこの検証に失敗すると書き出し前に中断する。全ての値・変形方式の検証を実装したわけではない。
- `FImportOptions::AnimFps` が非有限の場合、および全クリップ・全ノードの見積もりが1048576行列を超える場合は採取前に失敗理由を返す。1未満の有限値は従来どおり1 fpsとして扱い、アニメーションを取り込まない場合は頻度を参照しない。静止ノードも見積もりに含むため、実際に保持するtrackが少ないモデルでも上限を超える場合がある。詳細は[開発手順](DEVELOPMENT.md#モデル変換での変形データの診断)を参照する。
- 検査側[726d2e2](https://github.com/mynameisGaku/framework-test/commit/726d2e2d53afdc80d4cfc53e2e84b94859a85a2a)で有限値・NaN・無限大・不正な許容値・過大なサンプリング・無効な出力と正常FBXの維持を検査。分離した作業コピーでDebug／Releaseソリューションをビルドし、アセット検査はDebugのワーカー0本で9960条件・4省略、Releaseのワーカー4本で9961条件・4省略が成功した。`StaticModel` OBJサンプルは両構成のDX12既定GPU／WARPで30フレーム正常終了、取り込みエラーなし。画素比較や全形式の検査ではない。
- `CheckSourceFormat.ps1` は1831ファイル成功し、`UpdateBackendCompatibility.ps1 -Check` は488項目で成功。参考: [Microsoftの有限値判定](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/finite-finitef?view=msvc-170)、[Unreal Engineの描画スレッドと所有](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)。

## Claude側との統合と、サンプルを描ける範囲の拡大（2026-11-15）

- **統合**: `framework-stl`（`C:/Users/g0190/.claude/worktrees/framework-stl`）の新しいブランチ`Dx12`で、`Asset`（Claude側の作業）へ`backend-dx12`を取り込んだ（コミット`d057f7e`）。競合は`README.md`・`mazigatiDX.vcxproj`・`.filters`だけで、`GenerateProjectFiles.bat`で作り直した。検査リポジトリ（`framework-test`）にも同じ名前のブランチ`Dx12`を作り、`codex/dx12-windows-validation`を取り込んだ（`6a25382`）。どちらも**未push**。`Asset`と`backend-dx12`は元のまま。
- **方針**: ロードマップのR2へ進み、「既存のサンプルがDX12でも同じ絵になること」を目標にする。確認は、`Docs/AI_WORKFLOW/tools/shot_dx12.ps1`（サンプルをDX12で動かして絵と未対応の報告を集める。`-Backend dxlib`で比べる絵も撮れる）と`contact_sheet.py`（並べて見る）で行う。
- **描けるようになったもの**（共通の実行側`Platform/Render`に実装。DxLibと同じ式・同じ意味）:
  - 未対応の命令で、残りを丸ごと飛ばさず、その命令だけを飛ばす（描画先・シャドウマップへ描く範囲は、対になる終わりまで飛ばす）。
  - ライティング（`CVertexLighting`。頂点ごとにCPUで計算。ライト6つ・既定のライト・環境光・材質・光の計算の仕方）。`Cone3D`・`Pixel3D`、法線つきの`Cube3D`。
  - 2D: `Oval`・`RoundBox`・`Sprite`・`SpriteRect`・`SpritePivot`・`Transform2D`・幅0の`Box`（線）、合成（Add・Subtract・Multiply・Pma系。PSOの混ぜ方）。
  - 文字: フォント指定（`CWindowsGdiText`・`CWindowsFontStore`・`IFontDrawSource`。縁取り・字間・行の高さ・太さ・斜体・位置揃え・`TextStyled`の拡大・回転・基準点）。`IFontBackend`の全関数。
  - 描画先（`PushTarget`/`PopTarget`。8ビットRGBA＋深度。`CreateRenderTarget`・`ReadRenderTargetPixels`・`SaveRenderTarget`）。
  - 距離フォグ（頂点ごとのCPU計算。画像つきの図形には掛からない）。
  - TGAの画像（`NTgaDecoder`。WICはTGAを読めない。エフェクトの組み込みの丸などが使う）。
- **まだ描けない・未実装**（サンプルで確認済み）: 影（`BeginShadowMap`/`UseShadowMap`。固定の光は頂点ごとのCPU計算なので、影を受けるには、画素ごとの光と影のパイプラインが要る）、`CopyScreen`・`ImageFilter`・`ImageBlend`、`Tile`・`CircleGauge`、高さフォグ・異方性・`TextureTransform`、`AddColor`・`AlphaTest`・マスク、アニメーションつきのモデル（`Model3D`の`AnimIndex`）、音声・動画。`Shadows`のサンプルは、見出しだけで3Dが出ない。
- **シェーダー・浮動小数点の描画先・MRTを実装した**（同じ日の後半）: `CDX12ShaderService`（`Platform/DX12/Pipeline/Shader`。登録・5_0でのコンパイル・DXBCの調査・パイプラインの作成と使い回し・頂点／添字／定数のGPUバッファを、描くたびに作って、すぐ手放す）、共通側の`CShaderCommandExecutor`（`Platform/Render/Shader`。`UseShader`・`ShaderConstants`・`ShaderTexture`・`ShaderOutputTarget`・`PrimitiveShader3D`・`ShaderRect`・`ShaderQuad3D`）、窓口`IShaderDrawSource`。描画先は、8ビットRGBA・RGBA16F・R32Floatで、`ShaderOutputTarget`で出力を置くと、描くときにMRTのパスへ開き直す（`CCanvasDrawExecutor`の`PrepareTargets`）。2Dを描けない描画先は、何も描かないキャンバスで受ける。`Shaders`・`PostEffects`・`Materials`のサンプルが、DxLibとほぼ同じ絵になった（`Materials`は、影も材質のシェーダーで出る）。つまずき: 浮動小数点の描画先は、資源のviewを借りられないので、大きさを`CTextureAssetStore`が持つ。定数バッファは、読む段（頂点が読む番号は頂点側、読まない番号はピクセル側）の中身を使う。
- **固定の光の影を実装した**: 影の絵は、R32Floatの描画先（`MakeShadowMap`）へ、光から見た深度を書く（`NShadowMapMath`。範囲の箱を囲む平行投影）。影を受ける三角形は、頂点ごとに「影の外の色」と「影の中で消える色」（影を受けるライトの分）を計算し、組み込みのシェーダー（`ShadowLit`。3x3の比較）で、画素ごとに混ぜる。`Shadows`サンプルが、DxLibとほぼ同じ絵になった。制限: 受けるのは、使っているスロットのうち最小の1つ。画像つき・半透明（アルファ250未満）の図形は、影を落とさない。影を受ける図形には、フォグが掛からない。`PreviewShadowMap`は未対応。
- **一時データの描画**: `CreateBuffer`は、1回ごとにGPUの完了を待ち、保持数も256までなので、シェーダー描画には使えない。`IRenderGeometry3D::DrawGraphicsPipelineTransient`（`FGraphicsPipelineTransientDraw`）で、頂点・添字・定数を、フレームの転送領域（アリーナ）へ載せて描く（1フレーム1024回まで）。
- **次の作業**: `CopyScreen`・`ImageFilter`・`ImageBlend`（Filtersサンプル）→ `Tile`・`CircleGauge`・`AddColor`・`AlphaTest`・マスク → アニメーションつきのモデル（スキニング）。性能: シェーダーの描画は、描くたびにGPUバッファを作る（同期）ので、多数のシェーダー描画（粒子など）は、遅くなる可能性がある。実測して、必要なら、定数・頂点を転送領域（`CDX12UploadArena`）へ載せる。
- **検査**: Debug／Release各4プロジェクトの警告・エラー0、`RunAllChecks.ps1 -SkipBuild -Quick`の42項目すべて成功、`CheckSourceFormat.ps1`成功、互換台帳496項目で成功。サンプル集とテンプレートの全検査（`RunSamplesTest.ps1`）は、統合後まだ回していない。互換台帳は、実装した項目を`Partial`にした（DxLibとの画素比較が済んだものは無いので`Verified`は0のまま）。
- **確認の道具（追加）**: `--dx12-debug-layer=true`でSDKのデバッグレイヤーを有効にし、警告以上のメッセージを`LogDX12`へ出す（`DX12Device.cpp`）。`Docs/AI_WORKFLOW/tools/dx12_layer_check.ps1`（Claude側の私的な道具。サンプルを、このオプションつきで動かして、メッセージを集める）で、`Hello`・`Shaders`・`PostEffects`・`Shadows`・`Island`・`UI`・`Text`・`RenderTarget`・`Materials`・`Effects`を確認し、メッセージは0件だった（有効なことは、ログの1行で確かめた）。
- **速度**（Release、300フレーム、窓あり）: `Island` 76 fps・`Shaders` 217 fps・`PostEffects` 151 fps・`Effects` 213 fps・`Shadows` 219 fps（DX12）。一時データの描画へ替える前は、`CreateBuffer`の同期と保持数の上限（256）で、`Island`が描けなかった。
- **検査**: 影・一時データ・デバッグレイヤーの追加後も、Debug／Releaseのビルド警告0、`RunAllChecks.ps1 -SkipBuild -Quick`の42項目すべて成功、サンプル集の検査（`RunSamplesTest.ps1`）は、影の前の時点で成功（影の後は未実施）。

## 2026-11-15 追記: Ribbon3D と、シェーダー描画の合成
- **`Ribbon3D`**: 帯の展開（miter・端の補助点・UV）を、DxLib専用の`NDxLibRibbonMesh`から、共通の`NDrawRibbonMesh`（`Library/Framework/Render/Draw/DrawRibbonMesh.{h,cpp}`。`FDrawVertex3D`と`uint16`の番号）へ切り出した。DxLib側（`DxLibRibbonMesh.cpp`）は、DxLibのカメラの値を渡して、結果を`VERTEX3D`へ写すだけ（材質の帯・自己テストの呼び出しは、そのまま）。DX12は、`CGeometryCommandExecutor`の`Ribbon3D`で、同じ展開を使い、ワールド行列なし・両面・光の計算ありで描く（DxLibの`DrawPolygonIndexed3D`と同じ）。`MaterialRibbon3D`・`DistortedMaterialRibbon3D`は未対応。
- **合成**: 利用者のシェーダー（カスタムパイプライン）は、通常のアルファ合成しか持たず、`Add`などを黙って無視していた（炎の芯が暗かった）。`FGraphicsPipelineDesc::Blend`を足し、`DX12BlendState.h`の表（組み込みの図形と共通）で、PSOへ写す。パイプラインの保管キーにも、混ぜ方を入れた。
- **確認**: Effectsの`Material flame`の芯が、DxLibと同じ白黄色になった。帯は、頭の明るさ・白青の芯・細くなる尾が、DxLibと同じ（位相は、実時間の差で違う）。Debug／Releaseの警告0、`RunAllChecks.ps1 -SkipBuild -Quick`の42項目すべて成功（ワールドの自己テストの帯の実描画テストを含む）。
- **撮影の注意**: Effectsは実時間で動く。DX12は速い（約200 fps）ので、DxLib（60 fps）の`--capture-frame=90`と同じ時間は、DX12の約330フレーム。
- **次の作業**: `Tile`・`CircleGauge`・`AddColor`・`AlphaTest`・マスク→`CopyScreen`・`ImageFilter`・`ImageBlend`→高さフォグ・異方性・`TextureTransform`→`MaterialRibbon3D`→アニメーションつきのモデル。

## 2026-11-15 追記: Tile / CircleGauge / AddColor / AlphaTest / CustomBlend
- **`Tile`**: 角度なしだけ。DxLibのDrawTileと同じ並べ方（拡大した画像の中心を、矩形の中心から整数のずれ分動かす）で、矩形で切った画像（UVを切る）を並べて描く。角度つき・4096枚を超える並びは、飛ばして報告する。
- **`CircleGauge`**: キャンバスに`IRenderCanvas2D::ImagePolygon`（凸な多角形へ画像を貼る。頂点はx, y, u, v）を足し、真上から時計回りの扇を、画像の縁までの多角形で貼る。描き始め・描く量・100を超える回り込みはDxLibと同じ解釈。反転の指定は飛ばす。DxLibは縁が1画素ほど長い。
- **`AddColor`**: 平らな図形（箱・線・円・三角形・四角形・多角形・点）だけ。明るさのあとで、色を足す。画像・文字・3Dには効かない（初めて描くときに報告する）。
- **`AlphaTest`**: 平らな図形は、アルファが1つに決まるので、通らなければ丸ごと飛ばす（`Never`は何も描かない）。画像・文字は、0〜いまの不透明度のどれも通らないときだけ飛ばす（画素ごとの比べは、まだ無い）。3Dには効かない。
- **`CustomBlend`**: `EGeometryBlend::Custom`と`FGeometryCustomBlend`（色・アルファの係数と演算。`FGeometryDrawState::CustomBlend`）を足し、`DX12BlendState.h`でD3D12のブレンドへそのまま写す（2D・3D・利用者のシェーダー）。アルファの係数に色の係数を使うときは、同じ種類のアルファへ読み替える。幾何パイプラインの保管の見出しは、`FGeometryPipelineKey`（状態＋カスタムの係数）になった。Villageの灯り（スクリーン合成）が、DxLibと同じになった。`Invert`・`MultiplyAlpha`・`PmaInvert`は、まだ通常の合成で描く（送る色を変える必要があり、固定のブレンドでは表せない）。
- **確認**: Images・DrawState・Villageを、DxLibと並べて確認した。Debug／Releaseの警告0、`RunAllChecks.ps1 -SkipBuild -Quick`の42項目すべて成功。
- **気づいた差（未対応）**: Villageの会話の文字が、DxLibは太く、DX12は細い（文字の太さ・スタイルの扱い）。
- **次の作業**: マスク（`PushMask`/`PopMask`）→`CopyScreen`・`ImageFilter`・`ImageBlend`→高さフォグ・異方性・`TextureTransform`→`MaterialRibbon3D`→アニメーションつきのモデル→文字の太さ。

## 2026-11-15 追記: CopyScreen・高さフォグ・TextureTransform・画像つきのフォグ
- **`CopyScreen`**: `IRenderSurface::CopyColorRegion( X0, Y0, X1, Y1, 描き先 )`を足した（既定は`false`＝未対応）。主画面と描画先パスの両方で、いまの色の矩形を、同じ大きさのRGBA8の描画先画像へ写す。`DX12TextureCopy.{h,cpp}`の`RecordRegionCopy`が、コマンドリストへ、バリア（描画先↔コピー元、画像↔コピー先）つきの箱のコピーを記録し、`DX12Device::RecordFrameColorCopy`が主画面の画像を渡す。コピー先の画像は、`TexturePins`（上限256）へ載せて、描き終わるまで解放されないようにする。主画面の区間は、描画先パスより先に順に出るので、記録したコピーの順序は保たれる。大きさが違うと、DxLibと同じく何もしない。HDR・R32Floatの描画先は、失敗として報告する。
- **経路の落とし穴**: `CDX12PlatformSubsystem`も`IRenderSurface`なので、`CopyColorRegion`を`ThreadedSurface`へ転送しないと、ログに何も出ないまま「失敗」になる（転送を足して直した）。
- **高さフォグ（`HeightFog`）**: `GeometryCommandExecutor`の状態へ持ち、頂点ごとのCPU計算で、距離フォグのあとに混ぜる（`GetHeightFogVisibility`。Start より下を、低いほど濃く）。DxLibの式との細かい差は、未確認。
- **`TextureTransform`**: 頂点のUVへ、中心を軸にした拡大・回転と移動を掛ける（CPU）。移動だけの使い方で、DxLibと一致を確認。
- **画像つきの三角形のフォグ**: 組み込みのピクセルシェーダー`EBuiltInPixelShader::FogTextured`を足した（`DX12ShaderService`。頂点の`Specular`のrgb＝いまのフォグの色、a＝残る割合で、`lerp`する）。画像のない図形は、従来どおり頂点ごと。画像つきの線・点と、影を受ける面には、フォグを掛けない。
- **未対応のまま**: 異方性（`Anisotropy`）は、報告して飛ばす。台帳（504項目）は、`CopyScreen`・`HeightFog`・`TextureTransform`・`Fog`・`Anisotropy`を`Partial`に分類し、`API.IRenderSurface.CopyColorRegion`を足した。
- **確認**: RenderTarget（`CopyScreen`）・RenderState（フォグ・水）・Islandを、DxLibと並べて確認し、見た目はほぼ同じ。デバッグレイヤーは、RenderTarget・RenderState・Images・DrawStateで、警告・エラー0件。Debug／Releaseの警告0、`CheckSourceFormat.ps1`成功、台帳の整合の検査成功（504項目）、`RunAllChecks.ps1 -SkipBuild -Quick`の42項目すべて成功。
- **次の作業**: マスク（`PushMask`/`PopMask`）→`ImageFilter`・`ImageBlend`（Filtersサンプルの全項目が空白）→異方性→`MaterialRibbon3D`→アニメーションつきのモデル→文字の太さ・縦書き→`Invert`系の合成。

## 2026-11-15 追記: マスク・画像フィルター・画像の混ぜ合わせ
- **画像フィルター・混ぜ合わせ（`ImageFilter` / `ImageBlend`）**: 組み込みのピクセルシェーダー（`EBuiltInPixelShader::ImageFilter` / `ImageBlend`。HLSLは`DX12ShaderService.cpp`の組み込みの文字列）で、変換先（RGBA8の描画先）の矩形へ、全面の四角形を描く。`CCanvasDrawExecutor`の`RunImageOperation`が、変換先のパスを開き（`PushTarget`と同じ手順）、定数（b5）・画像（t0 = 元、t2 = 混ぜる画像・グラデーションマップ）を渡して描き、元の描画先へ戻る。元の画像は、画素の座標で（`Load`）読む。定数の並びは、`cbImageOp`（`g_Op`・`g_Where`・`g_ArgsA`・`g_ArgsB`）。
- **対応**: フィルターは`Mono`・`Gauss`・`DownScale`・`BrightClip`・`BrightScale`・`Hsb`・`Invert`・`Level`・`TwoColor`・`GradientMap`・`Replacement`・`PremulAlpha`・`InterpAlpha`・`BicubicScale`・`Lanczos3Scale`・`FloatColorScale`（矩形つき・変換先に重ねる指定・乗算済みアルファ版を含む）。混ぜ合わせは全18種。**その場の変換**（元と先が同じ画像）と、RGBA8でない変換先は、飛ばして報告する（読みながら同じ画像へ書けないため。元を複写する一時の画像が要る）。
- **DxLibとの比較**（`Filters`サンプル）: `Mono`・`Invert`・`Level`・`TwoColor`・`GradientMap`・乗算・加算（160）・スクリーン・差は、画素が一致（最大差0〜2）。`GradientMap`は、補間せず、明るさから1色を選ぶ（`floor( 明るさ × 幅 )`）。**`Gauss`**: DxLibの画素から核を実測した。重みは、中心の画素が`2 × G( 0 )`、そこから2画素ずつ（1と2、3と4…）が同じ重み`G( 2j - 0.5 )`（`G`はガウス分布）、範囲は`PixelWidth / 2`、標準偏差は強さ`S`に対して`0.1436 × S^0.544`（6つの時刻で当てはめ、残差は画素値で約0.4。DX12の出力は、この式と最大0.5の差）。幅8・16の標準偏差は、32と同じ式（未比較）。
- **マスク（`PushMask` / `PopMask`）**: 2Dの描画（図形・画像・文字）が、マスクの画像の値で画素ごとに薄まる。`IRenderCanvas2D::SetMask`（描画スレッドへ順序どおり積む）→`CGeometryCanvas2D`→`FGeometryDrawState::Mask`→`CDX12GeometryRenderer`（マスクつきのピクセルシェーダー2種・ルートシグネチャのt1とb0）。マスクの値は、描画先の画素の座標で読み（画像の外は薄めない）、アルファに掛ける。成分（アルファ・赤・緑・青）と反転に対応。実行側（`CCanvasDrawExecutor`）が、入れ子（内側が替わる）・描画先を替えたときの退避と復元を持つ。**3D・シェーダー描画には効かない**。`Filters`のスポットライトは、DxLibも、DX12も、画像の生成式から求めた期待値と一致（平均誤差0.06、最大0.5）。
- **確認**: Debug／Releaseの警告0、全サンプル（33個）の未対応の報告は、`DrawState`（反転系の合成）・`RenderState`（異方性）・`Text`（縦書き）の3つだけ、デバッグレイヤーは`Filters`・`Images`・`Text`・`PostEffects`で警告・エラー0件、台帳は505項目で整合、`CheckSourceFormat.ps1`成功、`RunAllChecks.ps1 -SkipBuild -Quick`の42項目すべて成功。
- **確認の道具（追加）**: `Docs/AI_WORKFLOW/tools/png_compare.py`（2枚のPNGの矩形ごとの差・矩形の色の数え上げ）。時間で動く絵（ぼかし・色相）は、`Hsb`のタイルの色相（`int( 時間 × 60 )`度）から時刻を逆算して、強さ・位置を決める。
- **次の作業**: 異方性→`MaterialRibbon3D`→アニメーションつきのモデル→文字の太さ・縦書き→`Invert`系の合成→その場のフィルター（一時の画像の確保）。

## 2026-11-16 追記: 派生画像（SplitTexture）・文字の太さとなめらかさ
- **全サンプルの並べた比較**（DxLib と DX12 を、33個ぜんぶ撮って、並べた）で、時間で動く絵（物理・エフェクトなど）を除いた本物の差を洗い出した。直したのは、`Pixels`の分割画像（`SplitTexture`）と、文字の太さ。残るのは、`Lighting`の画素ごとの光、`Text`のアイコン入りの文字・縦書き、異方性、反転系の合成。
- **派生画像（`CreateTextureRegion` / `GetTextureUvBounds`）**: `SplitTexture`・エフェクトの連番画像が使う、元の画像の矩形を共有する画像を、DX12のアセットが作れず、空の画像だった。`CTextureAssetStore::CreateRegion`で、GPU資源を持たない登録（元の番号と矩形）として作る。`FTextureDrawView`に`UvRect`（元の画像の中の矩形。ふつうの画像は0, 0, 1, 1）を足し、2Dの描画（`DrawImage` / `DrawImagePolygon`）が、画像の中のUVを元の画像のUVへ写す。3Dは、`GetTextureUvBounds`（半texel内側）を呼ぶ側が使う。シェーダー・画像フィルターの入力には使えない（空の画像）。
- **文字の太さ**: DxLibの標準の太さ（6）は、太さ×100（600）で、OSの太字になる（2階調の文字は、線が2画素）。DX12は、6以下を普通にしていて、細かった。2階調（`EFontAntialias::None`）は、太さ×100にして、Textサンプルの文字のインク量がDxLibと一致した。なめらかな文字（標準・4／8／16）は、OSの太字にするとDxLibより約1画素太いので、太さを4までに収めた。
- **文字のなめらかさ**（`WindowsGdiText.cpp`）: 標準は、OSの描き方（`TextOutW`のなめらか）が、DxLibより重かった（+11%〜）ので、`GetGlyphOutlineW( GGO_GRAY8_BITMAP )`の65階調の被覆率をそのまま使う（DxLibの画素の値247・175が、65階調の62・44に当たることから）。4／8／16は、その倍の大きさ（2階調）で文字ごとに描いて、倍×倍の平均へ縮める。`FRenderTextFont::bAntialias`は、`Antialias`（0 = なし、1 = 標準、4／8／16）になった。
- **確認**（Textサンプルの、行ごとのインク量。DxLib → DX12）: 標準 936 → 886、なし 651 → 651、16x16 860 → 860、斜体の太字 903 → 868、縁取り 923 → 947。字形の細かい差と、`AddCharImage`（文字の代わりに画像を入れる）は、未対応のまま。Debug／Releaseの警告0、デバッグレイヤーは`Pixels`・`Text`・`Village`・`Effects`・`UI`で警告・エラー0件、台帳は505項目で整合、`RunAllChecks.ps1 -SkipBuild -Quick`の42項目すべて成功。
- **確認の道具（追加）**: `Docs/AI_WORKFLOW/tools/png_crop.py`（矩形を切り出して、整数倍に拡大する）。画素の行の比較は、`png_compare.py`と、作業用の`png_row.py`相当を、その場で書く。
- **次の作業**: `Lighting`の画素ごとの光（DxLibは画素ごと、DX12は頂点ごと）→ 反転系の合成 → 異方性 → `Text`のアイコン入りの文字・縦書き → `MaterialRibbon3D` → アニメーションつきのモデル。

## 2026-11-16 追記: 画素ごとの光・反転の合成
- **画素ごとの光（`LightingOptions`の`PixelLighting`）**: 頂点ごとのCPU計算（`CVertexLighting`）と同じ式を、組み込みのシェーダーで画素ごとに計算する。`EBuiltInVertexShader::PixelLit`（世界の位置`Position`・世界の法線`Normal`・色・画像の座標を渡す）と、`EBuiltInPixelShader::PixelLit` / `PixelLitTextured`（HLSLは`DX12ShaderService.cpp`の組み込みの文字列。`PixelLightColor`）。定数b6は、カメラ・材質・光の一覧（先頭7つのfloat4 + ライトごとに7つ。`cbPixelLight`）。実行側（`CGeometryCommandExecutor::Submit`）が、`bPixelLighting`・光の計算が有効・フォグと影を受ける描画でない・三角形のときに、この経路で描く（そうでなければ、これまでどおり頂点ごと）。`CVertexLighting`に`GetOptions` / `GetAmbient` / `CollectActiveLights`（効くライトを、最初からあるライトが先の順に取り出す）を足した。
- **確認**: `Lighting`サンプルで、奥の壁のスポットライトの円・床のランプの光だまり・球のハイライトが、DxLibと同じ形で出る（ランプなどは時間で動くので、位置の厳密な比較はしていない）。デバッグレイヤーは`Lighting`・`Island`・`Shadows`で警告・エラー0件。
- **反転の合成（`Invert`）**: 平らな図形だけ、明るさと`AddColor`のあとの色を反転して、アルファブレンドする（`DrawState`サンプルの円の色がDxLibと一致）。画像・文字・3Dは通常の合成で描いて、報告する。`MultiplyAlpha`・`PmaInvert`は、通常の合成のまま。
- **確認**: Debug／Releaseの警告0、台帳は505項目で整合、`CheckSourceFormat.ps1`成功、`RunAllChecks.ps1 -SkipBuild -Quick`の42項目すべて成功。
- **次の作業**: 異方性→`Text`のアイコン入りの文字・縦書き→`MaterialRibbon3D`→アニメーションつきのモデル→その場のフィルター。

## 2026-11-16 追記: 縦書き・字の送り
- **縦書き（`TextStyled`の`Vertical`）**: DxLibの縦書きは、実測すると、横書きを時計回りに90度回した見た目（字形も横倒し。1行目が右の列で、行が進むと左の列）だった。DX12も、横書きを`Angle + π/2`で描く（`CCanvasDrawExecutor`の`bVertical`）。基準点は、縦書きの割合（`AnchorY`が読む向き、`AnchorX`が列の並ぶ向き）を、回す前の横書きの割合へ入れ替え、拡大率も、画面の横が`ScaleX`・縦が`ScaleY`のままになるよう入れ替える（`DxLibDrawExecutorSubsystem`と同じ解釈）。
- **字の送り**: DxLibは、標準のなめらかさの文字を、普通の太さの送りに、大きさに応じた分を足して並べる（実測: 大きさ16で+0、22で+1、32で+3 ≒ 0.19 × ( 大きさ − 16 ) 画素 / 文字。`Text`サンプルの横幅・見出しの幅・縦書きの間隔で確かめた）。DX12は、標準のとき、普通の太さのフォントで送りを測り、この分を足す（`GetBoldAdvance`）。2階調（太さ6 = 600の太字）は、OSの太字の送りが、DxLibと合う（大きさ22で確認）ので、足さない。4／8／16は、普通の太さの送りのまま。見出し・行の幅は、DxLibと±1画素（標準の行 412 = 412、太字斜体 414 / 417）。
- **確認**: Debug／Releaseの警告0、台帳は505項目で整合、書式検査成功、デバッグレイヤーは`Text`・`UI`・`Village`で警告・エラー0件、`RunAllChecks.ps1 -SkipBuild -Quick`の42項目すべて成功。
- **次の作業**: 異方性（画像の縮小の段（mip）が要るため、まず、DxLibの画像にmipがあるかの確認）→`Text`のアイコン入りの文字（`AddFontCharImage`）→`MaterialRibbon3D`・アニメーションつきのモデル（どのサンプルも使わない）→その場のフィルター。

## 2026-11-16 追記: アイコン入りの文字・その場のフィルター・異方性・画素の回帰検査
- **文字へのアイコンの差し込み（`AddFontCharImage`）**: `FRenderTextFont::CharImages`（文字のコードポイント・画像の番号・ずれ・次の文字までの幅）をフォントの値に持たせた（比較・キャッシュの鍵に含む）。`CWindowsGdiText::Arrange`は、その文字の幅を`Advance`にして、文字は描かない（`NumUnits = 0`）。画像は、`IFontDrawSource::GetFontImagePlacements`（文字ごとの位置。`CWindowsFontStore`が、レイアウトの位置 + 縁取り + ずれで返す）で取り、`CCanvasDrawExecutor`が、文字と同じ拡大・回転・移動で`DrawImage`する。`Text`サンプルの「Icon in text」が、DxLibと同じ位置で出る。
- **その場のフィルター・混ぜ合わせ**: 元（か混ぜる画像）が変換先と同じなら、`IRenderTargetDrawSource::AcquireScratchTarget`（大きさごとに1つの、RGBA8の一時の描画先。`CTextureAssetStore`が、8種類まで持ち続ける）へ処理してから、種類99（そのまま写す）で変換先へ写す（`RunImageOperationBuffered`）。矩形つきも同じ。DxLibと同じく、その場の処理は、変換先を置き換える。`Filters`を一時的に書き換えて、Invertを2回掛けると、元の画像の色に戻ることを確かめた（書き換えは戻した）。
- **異方性**: `FGeometryDrawState::MaxAnisotropy`（1〜16）を足し、`Filter`が異方性のとき、`Anisotropy`の強さを入れる（`CGeometryCommandExecutor`）。`CDX12GeometryRenderer`のサンプラーの表を160個（32 x 5段）にして、異方性のサンプラーを選ぶ。DX12の画像には縮小の段（mip）が無いので、見た目は、線形補間とほぼ同じ（遠くの地面で、DxLibと差が見えなかった）。mipを作ると効くが、画像の更新（`UpdateTexture`）でmipを作り直す必要があり、まだ。
- **画素の回帰検査（新）**: 検査用リポジトリの`RunDX12SampleImageTests.ps1`（`RunAllChecks.ps1`の「DX12 sample images」。Debug／Release）。`Filters`の、時間に依らないタイル（Mono・Invert・Level・TwoColor・GradientMap・乗算・加算・スクリーン・差）を、画像の生成式から求めた期待値と、4点ずつ比べ、マスクの絵（光から離れた角が暗い部屋の色、光の中が明るい）、`Pixels`の分割した画像、`Text`の縦書き（2列）とアイコン（赤い画素）、どのサンプルも「未対応」を出さないこと、を確かめる（45項目）。
- **確認**: Debug／Releaseの警告0（Release初回は、Editorのコンパイラ内部エラーC1001で失敗し、再実行で成功。他の検査を並行して回していたため）、台帳は507項目で整合、書式検査成功、`RunAllChecks.ps1 -SkipBuild -Quick`は、新しい検査を含めて成功。
- **次の作業**: `MaterialRibbon3D`・`DistortedMaterialRibbon3D`・アニメーションつきのモデル（どのサンプルも使わない）→画像のmip（異方性が効く）→`Invert`系の合成（画像・文字・3D）。

## 2026-11-16 追記: エディタ（Editor.exe）を DX12 で動かす
- **全 11 ツール**（assets・audio・sprite・sound・model・world・effect・material・ai・aieditor・project）を、`--backend=dx12 --capture-ui` で動かし、DxLib と並べて絵を見比べた。「未対応」の報告は 0。Debug レイヤー付きの Debug でも、レイヤーのエラーは 0。
- **直した不具合 1: UI が真っ黒（3D のプレビューを持つツール全部）**: `CDX12ImGuiFrameRenderer::Render` は、ImGui を描く前に、描画先を画面へ戻していなかった。画像（描画先）へ描いたあと、最後に結ばれていた描画先が、そのプレビューの画像のままで、UI が、そこへ描かれていた。`Device->GetFrameColorView()` で、画面（バックバッファ）へ必ず結び直す。
- **直した不具合 2: ImGui に画像が出ない**: DX12 は、`NDebugUI::SetImGuiTextureCallbacks` を差し込んでいなかった（DxLib だけ）。`CDX12AssetBackendSubsystem` が、画像の番号 → GPU 記述子（`FTextureDrawView::Binding`。ImGui の描画と同じ heap）を返す `GetTexture` を差し込む（終了時に外す）。無効な番号は 0 を返し、`imgui_impl_dx12.cpp`（取り込んだ公式実装。変更はこの 2 行だけ）は、0 の画像の描画を、飛ばす（以前は、レイヤーが「記述子が無い」と報告）。
- **足したログ**: DX12 がフレームを捨てたとき、`DX12 frame dropped: game commands ok/failed, renderer ..., UI ...` を警告で出す（以前は、無言で真っ黒になった）。
- **足した情報**: `GetMemoryInfo` に、このプロセスの物理メモリ（Assets ツールの「Process memory」）。ビデオメモリは、まだ 0（DX12 の資源の窓口にアダプターの問い合わせが無い）。
- **検査**: `RunDX12SampleImageTests.ps1` に、Editor の material / model / world / effect を足した（UI の灰色の面が、画面の 2 割以上、material の球が描かれている、フレームを捨てない、未対応を出さない。58 項目）。描画先を戻す 1 行を消す変異で、5 項目が失敗することを確かめた。
- **見た目の差**: DxLib の撮影は、何も描かない場所が透明で、`flatten_png.py` が暗い青で埋めるだけ（実際の窓は、どちらも黒）。material の球は、時間（`DeltaTime` で回る）で見た目が変わるため、フレーム番号が同じでも、2 つのバックエンドで揃わない（どちらも、明るい／半分暗い／地平線の帯、の見た目を巡る）。
- **次の作業**: ビデオメモリの実測 → `MaterialRibbon3D`・アニメーションつきのモデル（どのサンプルも使わない）→ 画像の mip → `Invert` 系の合成。

## 2026-11-16 追記: DX12 の文字入力・クリップボード・カーソル（Windows の入力層）
- **目的**: DX12 では、UI の入力欄（`UITextBox`）が使う `ITextInput`・`IClipboard`・`ICursorControl` の実装が無く、文字を打てなかった。描画 API を知らない Windows の層（`Library/Platform/Windows/Input`）に、役割ごとのクラスで足した（DxLib の入力は、DxLib の層のまま）。
- **窓のメッセージ**: `INativeWindow` に `AddMessageObserver` / `RemoveMessageObserver` と、受け取り手 `INativeWindowMessageObserver::OnNativeWindowMessage`（OS の値だけ。Core は windows.h を読まない）を足した。`CDX12PlatformSubsystem` のウィンドウ手続きが、ImGui より前に、足した順に渡す。true を返すと、OS の既定の処理（と ImGui）へ進まない。
- **`CWindowsTextInputSubsystem`（`ITextInput`）**: `SetTextInputActive( true )` の間だけ受ける（false の間は、すべて素通し。ゲーム操作に変換が割り込まない）。`WM_CHAR` → 文字（UTF-16 → UTF-8、サロゲートペアは 2 つで 1 文字、制御文字と前半の無い後半は捨てる。ImGui にも渡す）。`WM_KEYDOWN` → 編集の操作（Backspace / Delete / Enter / Tab / Escape / 矢印・Home・End・PageUp / PageDown、Shift で範囲を広げる、Ctrl + C / X / V / A。AltGr は文字を打つ操作なので除く。キーの自動反復は、`WM_KEYDOWN` がそのまま繰り返し届く）。IME は、`WM_IME_SETCONTEXT` で OS の変換の窓・候補の窓を出さず、`WM_IME_COMPOSITION`（確定した文字 / 途中の文字列とカーソル）と `WM_IME_NOTIFY`（候補。窓が見せている 1 ページぶんと、選んでいる番号）を、`FImeComposition` にする。`Tick`（PreUpdate）で、このフレームの入力に確定する（メッセージは、`BeginFrame` で処理済み）。
- **`CWindowsClipboardSubsystem`（`IClipboard`）**: `CF_UNICODETEXT`。別のアプリが開いている間は、少し待って開き直す。文字列でない中身は false。
- **`CWindowsCursorControlSubsystem`（`ICursorControl`）**: 表示は `IPlatform`（窓の `WM_SETCURSOR`）、位置は論理座標 → クライアント座標 → `SetCursorPos`（窓がフォーカスを持たない間は false。他のアプリのカーソルを動かさない）。
- **検査**: 検査用リポジトリの `RunWindowsInputTests.ps1`（独立した実行ファイル `WindowsInputContract`。`RunAllChecks` の「Windows input contracts」。Debug／Release）。窓なしで、合成したメッセージを渡す 40 項目（受けていない間の素通し・文字の順序・全角・サロゲートペア・制御文字・13 種類のキー・自動反復・IME のメッセージ・切り替えでの破棄・クリップボードの往復（ユーザーのクリップボードが文字列か空のときだけ行い、元に戻す）・カーソル）。制御文字の除外とサロゲートの除外を外す変異で、失敗することを確かめた。
- **未確認（実機）**: Microsoft IME の変換中の見た目（途中の文字列・候補の位置は、UI が描く）、モダンな IME が `ISC_SHOWUI*` を守るか。マウスを使う入力欄の操作は、このツールからは動かせない（DX12 の入力は OS の実カーソルを読むため、PostMessage では動かない）。`IDisplayInfo`・`IWindowControl`・`ITouchInput` は、まだ無い。
- **確認**: Debug／Release の警告 0、台帳は 510 項目（新しい 3 項目を分類）で整合、書式検査成功、`RunAllChecks -SkipBuild -Quick` は 46 ステップ成功。UI サンプルを DX12 で起動すると、3 つのサブシステムが生成される。
- **次の作業**: `IDisplayInfo`・`IWindowControl`（ウィンドウモード・解像度・タイトル）→ ビデオメモリの実測 → `MaterialRibbon3D`・アニメーションつきのモデル → 画像の mip → `Invert` 系の合成。

## 2026-11-16 追記: DX12 のディスプレイ情報と窓の操作（R8）
- ロードマップ（`Docs/ROADMAP.md` の R8「非描画の完全互換」: 音声・動画・ゲームパッド・文字入力／IME・タッチ・クリップボード・窓制御・モニター・フルスクリーン）の続き。`Library/Platform/Windows/Window` に、描画 API も DxLib も知らない 2 クラスを足した。
- **`CWindowsDisplayInfoSubsystem`（`IDisplayInfo`）**: `EnumDisplayMonitors` / `EnumDisplaySettings` / `GetDpiForMonitor`（Shcore を実行時に読む）/ `GetDpiForWindow`。解像度の一覧は、色数違いの重複を 1 つにする。ゲームの窓は `INativeWindow` から借りる。
- **`CWindowsWindowControlSubsystem`（`IWindowControl`）**: 位置は `GetWindowRect` / `SetWindowPos`。倍率は、クライアント領域の幅 / ゲーム画面（論理解像度）の幅。倍率を変えるときは、枠を足した大きさへ（左上は動かさない）。ディスプレイの中央へは、`FDisplayLayout`（DxLib 版と同じ計算）。通常の窓（Windowed）以外では、位置・倍率・移動は false。
- 両クラスは `Bind(...)` で、窓・プラットフォーム・ディスプレイの情報を借りる（`Initialize` が呼ぶ。検査が、自分で作った窓と偽のプラットフォームを渡すのにも使う）。
- **検査**: `WindowsInputContract`（`RunWindowsInputTests.ps1`）に 25 項目を足し、65 項目に。見せない本物の窓で、位置・倍率 2.0 でクライアント 640x480・中央への移動・範囲外の番号・通常の窓以外・窓なし、を確かめる。倍率の計算の取り違えと、通常の窓の条件を外す変異で、別々の項目が失敗することを確かめた。
- **未確認（実機）**: 拡大率の違う複数ディスプレイの間の移動。台帳は `IDisplayInfo` 6 項目・`IWindowControl` 6 項目を Partial に。
- **R8 の残り**: 音声・PCM・ミキサー（`IAudioBackend` / `ISoundBackend` / `IPcmStreamBackend`）、動画（`IMovieBackend`）、タッチ（`ITouchInput`）、全画面（排他的フルスクリーンは、Borderless で代替の報告）。

## 2026-11-16 追記: DX12 の音（R8）
- DX12 は、これまで音のバックエンドが無く、全部無音だった（`CAudioSubsystem` が「バックエンドなし」）。`Library/Platform/Windows/Audio` に、描画 API も DxLib も知らない 5 つを足した。起動すると `Audio ready (max 48 voice(s), 6 bus(es))`、`XAudio2 ready (2 output channel(s), 48000 Hz)`。
  - **`CWindowsAudioDeviceSubsystem`**: XAudio2 のエンジンと出力先（マスタリングボイス）を 1 つだけ持つ。出力の機器が無ければ作れず（`IsAvailable()` が false）、借りる側は失敗を返す。
  - **`CWindowsSoundStoreSubsystem`（`ISoundBackend`）**: 音のハンドル ↔ デコード済みの音（`FPcmBuffer`）。WAV は `NWavFile`、mp3 / m4a / aac / wma / flac は Media Foundation（`NWindowsMediaDecoder`）。同期で読み、Stream も全部メモリに展開。3 チャンネル以上はステレオに。`DuplicateSound` はデータを共有。`CDX12AssetBackendSubsystem` の音の窓口は、これへ渡す。ogg は未対応。
  - **`CWindowsAudioBackendSubsystem`（`IAudioBackend`）**: ボイスの番号 = 音のハンドル。再生のとき、音のデータをコピーせず XAudio2 のソースボイスへ渡す。音量・パン（反対側を弱める）・ピッチ（`SetFrequencyRatio`、0.001〜16 に収める）を覚えておき、反映する。位置は、ソースボイスの再生済みサンプル数から計算する（止めると覚える）。ループ区間は、最初の区間（今の位置から終わりまで）+ 無限ループの区間の 2 つの積み方。`Tick` で、音が消えたボイスを片づける。
  - **`CWindowsPcmStreamBackendSubsystem`（`IPcmStreamBackend`）**: ストリームごとにソースボイス。積んだ波形（-1..1 に切り、数でない値は無音）の、鳴らし終わったぶんを引いて、残りのフレーム数を出す。`Reset` はソースボイスを作り直す。
- **検査**: `WindowsInputContract`（`RunWindowsInputTests.ps1`）に 46 項目を足し、111 項目に。音量 0 の実再生（再生・停止・再開・位置・ループ・ループ区間・ピッチ・範囲外の値・削除）、PCM ストリーム、音の登録、Media Foundation と `NWavFile` で同じ WAV を読んで一致、を確かめる。機器が無い環境では、再生の項目は SKIP し、「失敗を返す」だけを確かめる。位置・ピッチ・積んだ数の取り違えの変異で、別々の項目が失敗することを確かめた。
- **未確認（耳）**: パンの法則・ピッチの音質。実際の音の出方（DxLib との聞き比べ）。ogg。
- **R8 の残り**: 動画（`IMovieBackend`）、タッチ（`ITouchInput`）、排他的フルスクリーン。

## 2026-11-16 追記: DX12 の動画・タッチ・ビデオメモリ（R8 の仕上げ）
- **動画 `CWindowsMovieBackendSubsystem`（`IMovieBackend`、`Library/Platform/Windows/Video`）**: ムービーごとに `IMFSourceReader`（映像を RGB32 に変換させる）。映像は、画像（`ITextureBackend::CreateTextureFromPixels` で作り、`UpdateTexturePixels` で更新）。音声は float で先読み（0.5 秒）して XAudio2 のソースボイスへ積む（機器が無ければ映像だけ）。時計は `Tick` の `DeltaTime` x 速さ（止めれば止まる。音声の機器の時計には合わせていない）。1 回の `Tick` で読むコマは 8 まで。シークは、手前のキーフレームから読み直されるので、位置に追いつくまで（最大 600 コマ）読む。終わりまで来ると止まり、位置は長さ。終わった動画の `PlayMovie` は頭から。`SetMovieSpeed` 対応（音声は周波数比）。`ITextureBackend` と音声の出力は `Bind` で借りる。
- **タッチ `CWindowsTouchInputSubsystem`（`ITouchInput`）**: `RegisterTouchWindow` + `WM_TOUCH`（窓のメッセージの受け取り手）。画面座標 → クライアント → 論理座標。1 フレームの中で触れて離した指も、1 フレームは見える。`FTouchTracker` で、離された ID を出す。
- **ビデオメモリ `IGpuMemoryInfo`（新しい窓口、Core/Interfaces/Render）**: `CDX12PlatformSubsystem` が実装（`CDX12Device::QueryVideoMemory` = LUID から `IDXGIAdapter3::QueryVideoMemoryInfo`）。`CDX12AssetBackendSubsystem::GetMemoryInfo` が、`VideoMemoryUsed` / `VideoMemoryTotal`（予算）に入れる。Editor の Assets ツールに「Video memory 31.39 MB / 10.97 GB」と出ることを、DX12 で確認した。
- **検査**: `WindowsInputContract` に 28 項目を足し、139 項目に。動画は、Media Foundation で H.264 + AAC の mp4（128x96・30 コマ・1 秒。前半赤・後半青）を書いて、情報・画像の更新・RGBA の並び・一時停止・シーク・速さ・終わり・ループ・閉じるを確かめる（この機械が H.264 を書けなければ SKIP）。タッチは、指の動きの取り込み（`AddContact`）を確かめる（`WM_TOUCH` の読み取りは、タッチ機器が必要で未確認）。色の並び・シークの追いつき・離した指の報告を壊す変異で、失敗することを確かめた。
- **未確認**: 動画と音声の同期のずれ、大きな映像（1080p など）のデコードの重さ（今は、主スレッドで読む）、タッチ機器での `WM_TOUCH`、複数 GPU のビデオメモリ。
- **R8 の残り**: 排他的フルスクリーン（Borderless で代替の報告のまま）。非描画の窓口は、ひととおり DX12 にできた。次は R6 の残り（`MaterialRibbon3D`・アニメーションつきのモデル・`Invert` 系の合成・画像の mip）。

## 2026-11-16 追記: 動いているバックエンドを調べる・マクロで分ける
- `NBackendInfo`（`Library/Framework/Engine/BackendInfo.h`）: `Get()` / `GetName()` / `Is()` / `IsDX12()` / `IsDxLib()` / `IsNull()` / `TryParse()`。起動の直後から使える。起動のログに `Backend: DX12 (...)`、コンソールに `backend` コマンド。
- マクロ（`Library/Core/Types/BackendMacros.h`）: `MAZIGATI_BACKEND`（0 = 実行時に選ぶ・1 = DxLib・2 = DX12・3 = Null）、`MAZIGATI_BACKEND_IS_FIXED` / `_IS_DXLIB` / `_IS_DX12` / `_IS_NULL`（固定したビルドだけ 1）、`MAZIGATI_HAS_BACKEND_*`（組み込まれているか）。ビルドの設定は `Build/mazigatiDX.props` の `/p:MazigatiBackend=dxlib|dx12|null`、`/p:MazigatiWithoutDxLib=true`、`/p:MazigatiWithoutDX12=true`（`MAZIGATI_WITHOUT_*` も渡す）。固定したビルドでは `--backend` を無視する。固定したビルドで、ライブラリとゲームを同じ設定でビルドすること（props を共有していればそろう）。
- 検査: `WindowsInputContract` に、`NBackendInfo` と `NBackendSelection` の確認、固定した 5 通り（固定なし・DX12 なし・DxLib・DX12・Null）のマクロを `static_assert` と `#if` で確かめる翻訳単位（`BackendMacros*Check.cpp`）を足した。props の対応は `msbuild -getProperty:MazigatiBackendDefines` で確認した。固定したビルドの全体のビルドは、まだ通していない。
- 分けるのは、窓口で埋められない違いに限る。能力の確認は、名前より先に、能力の窓口（`IDrawExecutor::Supports...`）を使う。

## 2026-11-16 追記: アニメーションつきモデル（R5。CPU スキニング）
- **骨組み `ModelRig`（`Library/Platform/Render/Model/ModelRig.*`）**: 取り込んだ（左手系にした）`FImportedScene` から、骨の階層（親が先に来る並び）・メッシュの基準の姿とスキンの重み・クリップを持つ不変の `FModelRig` を作る。個体ごとの `FModelPose`（付けたアニメーション＝時間・割合・骨ごとの割合、骨の上書き、骨・メッシュの表示）。規約は DxLib（行ベクトル・時間は 30 = 1 秒）。クリップは名前で骨に結びつけるので、別のモデル（アニメーションだけの FBX）のクリップも付けられる。混ぜ方: 付けたアニメーションごとの割合（骨ごとに上書き可）を重みに、移動・拡大を線形、回転を向きを揃えた正規化つき線形補間。重みの合計が 1 未満の骨は元の姿へ寄せ、1 を超えたら割る。骨の上書きは、その骨の「親から見た行列」を置き換える。
- **ストア `CStaticModelStore`**: 個体ごとに `FInstanceRig`。動かない個体（スキン・アニメーション・上書き・非表示なし）は共有した頂点をそのまま貸す。動く個体は、ポーズの版が変わったときだけ CPU で頂点（位置・法線）を変形し、非表示のメッシュ・骨（祖先も）の範囲を除いた index・範囲を作る。すべて排他（`FMutex`）。`bAllowRig=false`（従来）は、骨組みなしの静的モデルのまま。`Duplicate` は上書きと表示を引き継ぎ、付けたアニメーションは引き継がない（DxLib の複製と同じ）。
- **取り込み**: `FStaticModelBuilder::Build` に `bAllowRig`（スキンつきのメッシュは基準の姿の頂点のまま・アニメーションのトラック・メッシュの無い（アニメーションだけの）シーンを許す）と、頂点ごとの元のメッシュ・頂点の番号（`FModelVertexSource`）の出力を足した。ブレンドシェイプ・頂点キャッシュ・線形でないスキンは、従来どおり拒否。DX12 の `LoadModel` は `bRequireStatic=false`・`bImportAnimations=true`。
- **描画命令**: `IStaticModelSource` に `SetStaticModelPlacement` / `SetStaticModelAnimTime` / `SetStaticModelAnimRate` / `SetStaticModelAnimFrameRate`（既定は false）。`Model3D` は、アニメーションの時間と配置を設定してから描く（以前は、アニメーションつきを拒否）。形が無い・すべて非表示のモデルは、何も描かず成功。`ModelAnim` / `ModelAnimFrame` は、状態を設定する（以前は、無視して報告）。
- **バックエンド窓口（`CDX12AssetBackendSubsystem`）**: `GetModelAnimCount` / `Name` / `Length` / `UnitsPerSecond`（30）、`AttachModelAnim` / `DetachModelAnim`、`SampleModelAnimFrameLocalMatrix`、`GetModelFramePosition`（描いた配置を掛けたワールド位置）、`GetModelFrameMatrix`（Local / World。以前は Local の基準値だけ）、骨の上書き（Get / Set / Clear）、骨・メッシュの表示（Get / Set）。
- **検査**: 検査用リポジトリの `RunModelRigTests.ps1`（独立した実行ファイル `ModelRigContract`。窓・描画なし）65 項目。骨 3 本 + スキンの帯 + 90 度に曲げるクリップを合成して、基準の姿・曲げ（剛体のまま・関節が動かない・2 本の骨の頂点）・補間・割合・2 つ付ける・骨ごとの割合（子も含める）・外す・上書き・複製・表示（祖先も）・配置を掛けた位置・別のモデルのクリップ・不正なシーン・混ぜ方を確かめる。スキンの掛け順・割合の元の姿への寄せ・祖先の非表示・子へ広げる割合・配置を壊す変異で、別々の項目が失敗することを確かめた。回転の向きを揃える処理は、`FromBasis` が符号をそろえるため、行列からは検査できない（防御）。
- **未確認**: 実素材（アニメーションつき FBX）での描画と DxLib との比較、割合の混ぜ方（DxLib の `MV1SetAttachAnimBlendRate` の仮定）、`ModelFramePose3D`・`ModelMaterial3D`・当たり判定・材質色・モーフ。

## 2026-11-16 追記: モデルの当たり判定と材質の色
- **当たり判定（`CStaticModelStore`）**: `PrepareCollision` で、いまのポーズの形（変形した頂点。非表示のメッシュも含む。モデルの手元の座標）を三角形の一覧に写し、`Raycast`（線分と三角形の交差。両面。最初に当たるもの）・`OverlapSphere`（三角形のいちばん近い点までの距離が半径以内のすべて。AABB で先に絞る）が、その形に問い合わせる。準備していなければ最初の問い合わせで準備し、準備し直すまで形は変わらない（DxLib と同じ）。当たった三角形の表の向きは、頂点の法線の側。`Frame` / `Material`（材質未指定は -1）/ フレームの中での三角形の番号を返す。
- **材質の色**: `SetMaterialColors` / `GetMaterialColors`（個体ごとの上書き）。描画に反映するのは拡散色だけ（頂点は範囲ごとに専用なので、その材質の範囲の頂点の色を変える）。骨組みの無い（従来の）モデルは拒否。
- **検査**: `ModelRigContract` を 90 項目に。四角形のモデルで、線の当たり・向き（巻き順が逆でも）・外れ・短い線分・球（半径が限界）・準備し直し・ポーズに追従・非表示に当たる、色の上書き（描いた頂点の色・別の個体は不変・不正な値）を確かめる。線分の端・法線の向き・球の半径・色の反映を壊す変異で、別々の項目が失敗することを確かめた。

## 2026-11-16 追記: ModelFramePose3D と、命令から頂点までの検査
- **`ModelFramePose3D`（描くときだけ骨を上書き）**: `CGeometryCommandExecutor` が、検証済みの命令（`NDrawModelFramePose`）から上書きを読み、`FModelFrameOverrideScope` で一時的に適用して、`DrawModel` で描き、元の上書き（あれば）へ戻す。`FModelFrameOverrideScope` は `IAssetBackend` でなく `IModelBackend` を取る形にした（DxLib 版は、そのまま使える）。`IStaticModelSource::GetStaticModelBackend` が、DX12 では `CDX12AssetBackendSubsystem` 自身を返す。`CDX12DrawExecutorSubsystem::SupportsModelFrameOverrides` は true。
- **`Model3D` の整理**: 本体を `CGeometryCommandExecutor::DrawModel` に切り出した（`Model3D` と `ModelFramePose3D` が共有）。`ModelAnim` / `ModelAnimFrame` の処理は、`CCanvasDrawExecutor` から `CGeometryCommandExecutor` へ移した（モデルの命令を 1 か所に）。
- **不具合の修正**: `CDX12AssetBackendSubsystem` が、`IStaticModelSource` の新しい窓口（配置・アニメーションの時間・割合・骨ごとの割合）を、ストアへ中継していなかった（基底の既定値 false のまま）。実行時にアニメーションが効かない状態だった。中継を足した。
- **検査**: `ModelRigContract` を 101 項目に。偽の 3D 実行先（頂点を記録）と、ストアへ中継する偽のモデルの窓口で、`FDrawContext` が記録した命令（`PushCamera`・`Model`・`ModelAnimLayer`・`ModelAnimFrameRate`・`ModelWithFrameOverrides`）を、`CGeometryCommandExecutor` に流して、描かれた頂点を確かめる: 基準の姿・アニメーションの時間と割合・割合 0・骨ごとの割合（子も含める）・骨の一時的な上書き（90 度のクリップと同じ姿・描いたあとに消える・元からある上書きは元の値へ戻る）・不正なモデルは拒否して続きを描く・すべて非表示は何も描かず成功・配置の記憶。割合を設定する処理を壊す変異で失敗することを確かめた。
- **未確認**: 実素材（アニメーションつき FBX）での、GPU を通した描画と DxLib との比較。

## 2026-11-16 追記: Invert を、混ぜ方として、全部の描画に
- **`EGeometryBlend::Invert`**（新）: 描く色を反転（1 - 色。アルファはそのまま）して、通常のアルファ合成で描く。`CCanvasDrawExecutor` が `EDrawBlend::Invert` をこれに写す。以前は、平らな図形だけ、CPU で色を反転していた（`bInvertBlend`。削除）。いまは、画像・文字・3D・図形のすべてが、同じ経路。
- **DX12**: `CDX12GeometryPipelineStore` が、ピクセルシェーダーを `INVERT` マクロつきでもう一度コンパイルした 4 つ（平ら・画像つき・マスクつき・マスク + 画像つき）を持ち、`Blend` が `Invert` のときに使う（`Blend` は PSO の鍵に入っているので、PSO も分かれる）。ブレンドの設定は、アルファと同じ。
- **確認**: DrawState の Invert の絵が、変更の前後で同じ（円は、時間でわずかに動くだけ）。一時的にサンプルへ「Invert Text」を足して、DxLib と並べた: 白い文字が黒く反転するところは一致（DX12 は、文字の縁のなめらかさが違う）。サンプルは元に戻した。
- **検査**: `RunDX12SampleImageTests.ps1` に、DrawState の Invert の円の中心（円の色 (230, 90, 60) の反転 (25, 165, 195)）の画素と、Multiply が反転でないことを足した（61 項目）。反転のシェーダーを使わない変異で失敗することを確かめた。画像の反転の画素は、サンプルが使わないので、検査していない（文字で確認）。

## 2026-11-16 追記: MultiplyAlpha と PmaInvert
- **DxLib の式を実測した**: 一時的に DrawState の 4 つの絵を `MultiplyAlpha`（128・255）と `PmaInvert`（128・255）に替え、円（230, 90, 60）を下地（230, 170, 60）に重ねた画素を読んだ（DxLib の撮影は、アルファが残るので、暗い地に合成した値から逆算）。`MultiplyAlpha` = 描画先 x (1 + (描く色 - 1) x アルファ)（アルファ 255 は Multiply と同じ）。`PmaInvert` は、描く色を乗算済みとして扱わず、通常の `Invert` と同じ結果。
- **実装**: `EGeometryBlend::MultiplyAlpha`（新）。ブレンドの設定は Multiply と同じで、ピクセルシェーダーの色を `lerp(1, 色, アルファ)` にする（`MULA` マクロでコンパイルした 4 つの版）。`PmaInvert` は `Invert` に写す。
- **確認**: DX12 で、同じ 4 つの絵の画素が、DxLib の式の値と 1 以内で一致（(218,115,37) / (207,60,14) / (127,167,128) / (25,165,195)）。確認のあと、サンプルは元に戻した。サンプルがこれらを使わないので、常設の画素検査は無い。

## 2026-11-16 追記: 材質付きの 3D 四角形・帯
- **新しい実行側**: `CMaterialEffectCommandExecutor`（`Library/Platform/Render/Shader/`）が、`MaterialQuad3D` / `DistortedMaterialQuad3D` / `MaterialRibbon3D` / `DistortedMaterialRibbon3D` を解釈する。四角形は `CGeometryCommandExecutor::BuildShaderQuad`（ShaderQuad3D と同じ向きの決め方）、帯は共通の展開（`NDrawRibbonMesh::Build`、画像の U はつなげたまま）でクリップ座標つきの頂点にして、組み込みのシェーダー（`EBuiltInVertexShader::EffectMaterial` / `EBuiltInPixelShader::EffectMaterial`）へ渡す。`CanvasDrawExecutor` が、シェーダーの命令と同じ並びで振り分ける。
- **頂点の詰め方**: `Diffuse` = 色（四角形は色、帯は点の色）、`Normal` = ( 消失, 発光, ゆがみの強さ )、`Tangent.xy` = 画像の移動、`Binormal.xy` = ゆがみ画像の移動。設定は、ピクセル側の定数 b7（80 バイト）。画像は、t0 = 表示、t1 = マスク（`g_ShadowMap` の名前を流用）、t2 = ゆがみ（`g_Aux`）。
- **シェーダー**: DxLib の `EffectMaterialRenderer`（HLSL）を、そのまま移した（組み込みの消失模様・消失の境界・ゆがみ・半テクセルの内側への制限）。DX12 は、画像ごとに別の資源なので、画像の矩形は全体（派生画像は未対応）。深度は、深度を比べない設定でも「いつも通す」で、書くだけにできる。両面描画、線形補間は固定。
- **合成**: Alpha と Add だけ（DxLib と同じ）。それ以外と、合成なし（Opaque）は、`ReportUnsupported` して、その命令だけ飛ばす。`DrawBuiltIn`（`CShaderCommandExecutor`）は、利用者のシェーダー・定数を使わず、描く前の準備（描画先のパス）は行う。
- **対応フラグ**: `CDX12DrawExecutorSubsystem` が `SupportsEffectMaterials` / `MaterialRibbons` / `UVDistortion` / `RibbonUVDistortion` を true にした（エディタの材質パネルが有効になる）。`MaterialMesh3D` / `ModelMaterial3D` は、まだ（`DX12_KNOWN_GAPS.md`）。
- **確認**: 一時的に Geometry3D サンプルへ、消失 0 / 0.25 / 0.5 / 0.75 の四角形、ゆがみ付き、4 点の帯を足して、DxLib と並べた。消失の模様・縁・ゆがみ・帯の継ぎ目が一致（四角形の中の平均差は 0.5〜2）。サンプルは元に戻した。
- **検査**: `Source/SelfTest/RunMaterialEffectTests.ps1`（`MaterialEffectContract`、42 項目）。偽のシェーダー実行先で、要求の中身（組み込みの種類・頂点・定数・画像の段・深度・合成）と、壊れた命令（記録のあとで書き換えた値）の拒否を調べる。変異で確かめた（`Saved` 下のログは消した）。

## 2026-11-16 追記: 材質付きメッシュ・モデル材質効果・メッシュデータ
- **MaterialMesh3D**: `CMaterialEffectCommandExecutor::DrawMesh`（命令の頂点を index 順に展開し、クリップ座標つきの頂点にして、組み込みの材質シェーダーへ）。World 行列を渡すと（モデル材質効果が使う）、頂点へ掛けて描く。`bTwoSided` が 0 のときは、いまの表裏の設定を使う。ゆがみは、強さが 0 のとき画像を使わない（DxLib と同じ）。Geometry3D サンプルへ一時的に足して、DxLib と並べた（消失・ゆがみ・半透明が一致）。
- **ModelMaterial3D**: 選んだ材質の形状（`FModelMaterialGeometry`。`NModelMaterialGeometry::Build` に、DxLib のバックエンドの作り方をそのまま出した。DxLib 側も、これを使うようにした）を、DX12 のバックエンドが番号で持つ（`CreateModelMaterialGeometry` / `DeleteModelMaterialGeometry` / `IStaticModelSource::FindStaticModelMaterialGeometry`）。描くときは、選んだメッシュを `SetModelMeshVisible` で隠して、残りを `CGeometryCommandExecutor::DrawModel` で描き、戻してから、選んだ面（フレームまで含めて「いま描かれる」メッシュだけ。`GetStaticModelMeshDrawn`）を命令の混ぜ方（Alpha / Add）・配置・不透明度で描く。骨組みの無いモデル（メッシュを隠せない）や、形状が無いときは、モデルだけを通常に描く。World サンプルの mesh_surface_showcase は、DxLib が最初の描画までシェーダーを作らない（`SupportsEffectMaterialMesh` が、生成時に false になる）ので、DxLib 側が描かず、比べられなかった。
- **GetModelMeshData**: `CStaticModelStore::GetMeshData`。三角形ごとに展開された頂点は、中身と変形元が同じものを共有に戻す。`DominantFrame` は、スキンの影響の重みが最大の骨（同じなら先のもの）。`bCurrentPose` は、いまの姿勢（アニメーション・骨の上書き）。モーフは含まない（取り込みが形状を読まない: `FImportedScene::bHasBlendShapes` だけ）。
- **検査**: `RunMaterialEffectTests.ps1` を 80 項目へ（メッシュ・モデル材質効果。偽のモデル窓口で、隠す・戻す・描かれないメッシュ・形状なし・隠せない・混ぜ方の復元・命令長・壊れた値）、`RunModelRigTests.ps1` を 115 項目へ（メッシュデータ）。変異で確かめた（生き残ったのは、等価な変異だけ）。

## 2026-11-16 追記: 固定バックエンドのビルド
- ライブラリ（`mazigatiDX.vcxproj`、Debug）を、実装を固定して、別の出力先（`/p:OutDir` / `/p:IntDir`）で、それぞれ作った: `MazigatiBackend=dx12` + `MazigatiWithoutDxLib=true`、`MazigatiBackend=dxlib` + `MazigatiWithoutDX12=true`、`MazigatiBackend=null` + 両方なし。3 通りとも、エラー 0・警告 0（`MAZIGATI_BACKEND` のマクロで、実装を切り分けたコードも通る）。
- DxLib を外した DX12 のライブラリへ、`MaterialEffectContract`（80 項目）と `ModelRigContract`（115 項目）をリンクして実行し、どちらも合格（DX12 だけで、リンクもできる）。確かめ方: 一時のルートに、`Library` と `x64\Debug`（外したライブラリの場所）への junction を作り、`-FrameworkRoot` へ渡す（終わったら、junction は `rmdir` で外す）。
- **Samples / Editor も、`MazigatiBackend=dx12` + `MazigatiWithoutDxLib=true` で作れた**（エラー 0）。作業コピーの外で確かめた: ソース（`Library` / `Build` / `Samples`（`x64` と `Assets` を除く）/ `Editor` / `Source`（`SelfTest` を除く）/ `Tools` / `VendorModules` / `EngineContent` とプロジェクトファイル）を、別のフォルダへ複写して、そこでビルドする（出力が重ならず、通常のビルドを壊さない。約 37 MB の複写 + ビルドで数 GB。ディスクの空きに注意）。`Samples.exe` は、`x64\Debug` へ置き直して `shot_dx12.ps1 -Root <複写先>` で撮影できる。
- **起動も確かめた**: DxLib を含まない `Samples.exe` が、Shapes / Geometry3D / StaticModel（FBX・OBJ の取り込みも通る）を、DX12 で正しく描いた。DxLib のみ・Null の固定は、ライブラリのビルドまで（実行ファイルは未確認）。

## 2026-11-16 追記: PreviewShadowMap と、PMX のモデル（Character サンプル）
- **PreviewShadowMap**: `CShaderCommandExecutor::DrawShadowPreview`（`ShaderRect` の矩形の処理を `DrawRect` に共通化して使う）で、組み込みのピクセルシェーダー `EBuiltInPixelShader::ShadowPreview`（t1 の影の絵の深度を、赤の濃さにする。最近傍）を、画面の矩形へ貼る。`CanvasDrawExecutor` が `PreviewShadowMap` を振り分ける。Shadows サンプルの確認表示（一時的に既定をオンにして）を DxLib と並べて、一致した（赤の平均差 2.3）。契約検査は `MaterialEffectContract` の `TestShadowPreview`（88 項目）。
- **PMX の取り込み**（ユーザーがモデル `Downloads\Faputa`（PMX 2.0）を指定した）: `NModelImport::ImportPmx`（`Library/Framework/Asset/Model/Import/PmxImporter.*`）。頂点（BDEF1 / BDEF2 / BDEF4 / SDEF / QDEF）・面・画像・材質・骨を読み、骨（親から見た移動だけ）とスキン（骨ごとのクラスター、移動だけのバインド）の `FImportedScene` にする。**PMX は左手系**なので、`FImportedScene::bLeftHanded` を true にして、`ConvertToLeftHanded` は何もしない（座標・向き・面の順は、そのまま）。`ImportFromMemory` が拡張子 `.pmx` で振り分け、DX12 のバックエンドは `.pmx` を取り込んで読む（DxLib は、PMX を直接読む）。読まないもの: モーフ・表示枠・剛体・IK・付与・VMD（モーションは別ファイル）。
- **Character サンプル**（`Samples/Source/Character`）: `Assets/Models/Faputa/Faputa.pmx` を読み、骨を**コードで**動かす（`CModelInstance::SetFrameLocalMatrixOverride`。基準の姿を `GetFrameMatrix( Local )` で読んで、回転を掛ける）。頭・首・体の呼吸・右腕（手を振る）・左腕・耳・しっぽ。骨は名前（`センター` / `頭` / `右腕` ...）で探すので、DxLib と DX12 で番号が違っても動く。**モデルは、リポジトリに入れない**（`.gitignore` の `/Samples/Assets/Models/`。無ければ、置き場所を画面に出す）。
- **確認**: 同じ姿勢（時計を固定）で、DxLib（PMX を直接読む）と DX12（取り込み）を並べた。形・色・画像・手を振る腕・頭の向き・耳・しっぽが一致。差: DxLib には口の歯が出て、DX12 には出ない（重ね材質の扱いか、モーフ。未確認）。
- **検査**: `ModelRigContract` に `TestPmxImport`（小さな PMX をメモリの上で作る。骨・頂点・重み 4 種・材質・画像・左手系・壊れたファイルの拒否・骨組みとして曲がること）。144 項目。変異で確かめた。

## 2026-11-16 追記: R2 の最初の縦の経路 — GPU が変換と光を計算する Forward（共有メッシュ）
- **目的**（ロードマップ R2: 「GPU の永続頂点／index、変換と逆転置法線、材質・ライトのバッファ」）: 共有モデル（`Model3D`）を、CPU で射影・照明して毎回頂点を送る代わりに、頂点と添字を **GPU に 1 度だけ置いて**（`MeshKey`）、個体ごとの行列・光・不透明度（定数）だけを替えて描く。同じメッシュの複数の個体は、バッファを共有する。
- **型**: `Core/Render/ForwardMeshDraw.h`（`FForwardMeshDraw`）、`IShaderDrawSource::DrawForwardMesh`（DxLib / Null は false）、`IRenderGeometry3D::GetColorFormat`（パイプラインを、描画先の形式に合わせる）、`FStaticModelDrawView::MeshKey / MeshVersion`（`CStaticModelStore` が、共有の形状の識別（`FResource::Id`、再利用しない）と版を渡す。**変形するモデル（スキン・骨の上書き・非表示）は、鍵 0 = CPU の経路**）、`FGraphicsPipelineDraw::Constants`（登録した頂点・添字のまま、描くたびに変わる定数だけを、転送領域から渡す。DX12 の `DrawGraphicsPipeline` を拡張）。
- **DX12**: `CDX12ForwardMeshRenderer`（`Platform/DX12/Pipeline/Forward/`）が、メッシュのキャッシュ（鍵と版。版が変わったら置き直す。使われない（8192 描画）か、512 MB を超えたら古いものから手放す）と、組み込みの HLSL（`ForwardVS`: クリップ座標への変換と、頂点ごとの光（画素ごとの光と同じ式。法線は、ワールドの逆行列の転置）、`ForwardPS` / `ForwardTexturedPS`）、状態（深度・表裏・合成・画像の外側・線形補間・描画先の形式）ごとのパイプラインを持つ。b0 = 個体（クリップ・ワールド・法線の行列と、光るか・不透明度）、b6 = 光・材質・カメラ（画素ごとの光と同じ並び）。
- **実行側**: `CGeometryCommandExecutor::DrawModel` が、`CanDrawForward`（影の絵への描画・影を受ける・フォグ・画像の変換・画素ごとの光・頂点が 65536 超・鍵なしは、不可）なら、材質の範囲ごとに `DrawForwardMesh` を呼ぶ。GPU が受け取らなかった範囲から先は、従来の CPU の経路で描く（二重に描かない）。光の定数は、`FillLightConstants`（画素ごとの光の経路と共通）。`--no-gpu-forward` で、従来の経路に切り替えられる（比べるため）。
- **GPU の経路で表せない状態**（`false` → CPU の経路）: マスク・異方性・深度の偏り・縦横で違う画像の外側・反転 / 乗算アルファの合成。
- **確認**: StaticModel を、時計を固定して（新しい `--model-time=<秒>`、`--model-lit`）、光なし・光ありで、GPU と CPU の経路で撮って比べた: 光なしは完全に一致（差 0）、光ありは最大 1（バイトの丸め）。Character（PMX・スキン）は、変形するので、CPU の経路のまま。
- **検査**: 契約 `ForwardMeshContract`（`RunForwardMeshTests.ps1`、64 項目: 実行側の振り分け・行列・光・不透明度・画面のずれ・フォールバック、レンダラーのキャッシュ・パイプライン・拒否・上限・定数の中身）。画像 `RunDX12SampleImageTests.ps1` に、GPU と CPU の経路の比較（光なし・光あり）を足した（67 項目）。変異（実行側 19、レンダラー 22）で確かめた。GPU の光の式を壊すと、画像の比較が失敗することも確かめた。
- **まだ（R2 の残り）**: （変形するモデルは、下の追記で対応した）基本図形・画像つき四角形・`Mesh3D` の GPU 経路、影・フォグ・画素ごとの光・画像の変換の GPU 経路、材質・ライトのバッファ（定数でなくバッファ）、不透明 / 切り抜き / 半透明の分類と透明の並べ替え（Span の順序を保つ契約の中で）、PBR（Unlit / DefaultLit のあと）。

## 2026-11-16 追記: 変形するモデルも GPU の Forward へ（頂点の溶接つき）
- **頂点の溶接**（`FStaticModelBuilder`）: 材質ごとに色を含める段で、これまで、三角形の角ごとに頂点を複製していた（Faputa: 245k 頂点）。いまは、（元の頂点, 材質）が同じものを 1 つにまとめる（同じ材質の三角形が共有する頂点は共有、材質が違えば、色が違うので別）。頂点数は、元のメッシュの数 + 材質をまたぐぶんだけになり、GPU の頂点の上限（65536）に収まる。`GetStaticModelDrawView` の頂点の並びは、元の頂点の並びになる（検査の期待値を直した）。
- **鍵と版**: 変形するモデル（スキン・骨の上書き・非表示）は、個体ごとの鍵（上位ビット + 個体の番号）と、ポーズの版（変わるたびに増える）を渡す。GPU は、版が変わったときだけ、頂点を置き直す（`CreateBuffer` は同期なので、動いている間は、毎フレーム 1 回の同期の転送がかかる。頂点は 55k 個ほど = 2.6 MB）。ポーズが変わらないあいだは、置いたものを使い回す。実行側の `CanDrawForward` から、全体の添字数の上限（196608）を外した（描く範囲ごとの上限は残る）。
- **確認**: Character サンプル（PMX・スキン・手を振る骨）を、時計を固定して（`--model-time`）、光あり・なしで、GPU と CPU の経路で撮って比べた: 平均差 0、8 を超える画素 0%（最大 66 の画素がごく少数: 重ねた材質の縁）。`RunDX12SampleImageTests` に、この比較を足した（モデルがあるときだけ）。
- **検査**: `ModelRigContract`（157 項目）に、溶接（同じ材質は共有、材質が違えば別・色）と、鍵・版（静的は共有の鍵で版が固定、変形は個体ごとの鍵で、ポーズが変われば版が変わる）。変異 11 件で確かめた。
- **まだ**: 毎フレームの同期の転送を避ける経路（転送領域から、頂点を、登録せずに渡す + 材質の範囲ごとに使い回す）。GPU スキニング（R5）。基本図形・`Mesh3D` の GPU 経路、影・フォグ・画素ごとの光・画像の変換、透明の並べ替え、PBR。

## 2026-11-16 追記: Forward キャッシュとシーン収集の前提
- **修正**: Forward のメッシュキャッシュは、期限切れを解放する前の使用量で 512 MiB の超過を判定していた。そのため、期限切れを解放するだけで収まる場合も、最近使った別のメッシュを余分に解放していた。期限切れを先に解放し、残りの使用量に対して上限を適用する。GPU 完了前の実資源の寿命管理は既存のバッファ資源側が担当する。
- **検査**: `ForwardMeshContract` に、期限切れと容量超過が同時に起こる場合を追加。修正前は最近使ったメッシュの保持と再利用が失敗し、修正後は Debug / Release とも 66 項目に合格した。
- **R2 の次の前提**: 既存の `FDrawSpan` は状態変更、描画先、2D と 3D を記録順に実行する。透明物の並べ替えは、明示的に並べ替え可能とされたシーン内に限り、状態・カメラ・描画先・Clear の境界を越えない設計が必要。状態の取り込み、同じ深度の安定順、範囲が不正な場合の挙動、DxLib / Null の意味も決めてから公開する。現在はシーン範囲も透明ソートも未実装。
- **並行実行で要確認**: `GetStaticModelDrawView` はロックを解放して借用ポインターを返し、実行側が後から読む。契約上は登録・更新・解放前に消費する必要があるため、ゲーム側が同時にモデルを変更・削除し得る場合の寿命は、このロックだけでは保証できない。シーンをフレームへ固定する前に、所有するスナップショットまたは完了までの寿命保証を設計し、更新・削除との競合を検査する。
- **参照**: [Unreal Engine の Threaded Rendering](https://dev.epicgames.com/documentation/unreal-engine/threaded-rendering-in-unreal-engine) の描画側への値のコピーと寿命、[Microsoft の Fence-Based Resource Management](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management) の GPU 完了後の再利用。

## 2026-11-17 追記: モデル描画ビューの寿命
- `FStaticModelDrawView::VertexOwner` は既存の `TSharedPtr` の別名参照。静的形状は元の資源、変形形状はポーズごとの不変コピーを保持する。ビューのコピーも所有を共有し、更新・Delete・Reset の後でも頂点・添字・材質範囲を保持する。元の資源も保持するので、モデル所有画像はビューの解放まで残る。バックエンド（画像の解放先）は全ビューより長く生存させる。
- 変形コピーは同じポーズでは共有し、再構築時に一度だけ作る。古いビューを保持した分だけ旧ポーズのメモリも残るため、消費後はビューを手放す。従来の変形用配列に加えて公開用コピーを持つ分、メモリとコピーの費用が増える。
- モデル契約検査は 160 項目。旧ビューを保持して次のポーズを構築し、全登録を削除してから旧形状を参照する場合を追加。Debug / Release のモデル・Forward・材質効果の契約検査で確認する。
- これは取得した形状の寿命保証。配置・アニメーション設定・ビュー取得を一括のフレーム状態として固定する処理、明示的シーン範囲と透明ソートは引き続き R2 の未完了項目。

## 2026-11-17 追記: 変形形状の二重保持を解消
- 前項の実装は変形用配列から公開用配列へコピーしていた。公開前のスナップショットへ直接頂点の変形・非表示の除外・材質色の設定を行い、完成後に共有する方式へ変更した。個体の作業用の頂点・添字・材質範囲を廃止し、公開時の追加コピーと二重保持を除いた。旧ビューが保持する旧ポーズは引き続き変更しない。
- 再構築の要否はポーズの版とスナップショットの有無で判定する。同じポーズを再取得したとき、頂点・添字・材質範囲の配列を共有する検査を追加（モデル契約 161 項目）。旧ポーズの保持・削除後の寿命も継続して検査する。
- 基準姿勢の頂点をコピーして変形する処理と、旧ビューを保持した場合の旧ポーズのメモリは残る。GPU 転送、シーン範囲、透明ソートの挙動は変更していない。所有と公開の設計は前項の Unreal Engine 公式資料と `THREADING.md` に従う。

## 2026-11-17 追記: Mesh3D の GPU Forward
- `Mesh3D` もモデルと同じ Forward 実行先へ接続した。登録したローカル頂点と添字を GPU に置き、指定した添字範囲・配置・逆転置法線・頂点照明を渡す。GPU へ渡せる場合は添字ごとの CPU 頂点展開を省略する。モデルと異なり、画像や頂点アルファを理由に合成を自動で有効にせず、従来の Mesh3D の合成指定を保つ。
- `IGeometryBufferSource::GetMeshCacheIdentity` は未対応なら false。共通の `CGeometryBufferStore` は頂点と添字の組の鍵を発行し、成功した書き込みのたびに所有者全体の版を増やす（他メッシュも保守的に再登録される）。鍵はプロセスで再利用しない番号に bit62 を付け、静的モデル（bit62 未満）・変形モデル（bit63）と区別する。組の表は 4096 個までで、新しい組を登録できない場合は CPU へ戻る。版の枯渇後は書き込みを拒否する。
- 影・フォグ・画素照明・画像変換などの制限、`--no-gpu-forward`、GPU 拒否時の CPU 経路を共有する。既存の借用契約どおり、バッファの登録・書き込み・削除と描画中の借用は並行させない。GPU バッファ解放は既存のフェンス管理を使う（参照: [Microsoft 公式資料](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management)）。
- Forward 契約は 81 項目。Mesh3D の振り分け・部分範囲・非一様拡大・合成・更新版・所有者間の鍵・不正入力・CPU へ一度だけ戻る動作を追加。Geometry3D に `--geometry-time=<秒>` を追加し、サンプル画像検査で GPU / CPU を同じ時刻で比較する。
- R2 のシーン範囲・透明ソート・PBR、他の基本図形の GPU 変換は未完了。
- **確認結果**: Debug / Release のライブラリと Samples のビルド成功。Forward 81 項目、モデル 161 項目、バッファ描画は実 GPU / WARP で各 188 項目、サンプル画像は各構成 74 項目（Geometry3D の固定時刻の GPU / CPU 比較を含む）に合格。互換台帳は 522 項目で整合し、書式検査も合格。性能の時間計測、完全なシーン収集、透明ソートの確認を完了したという意味ではない。

## 2026-11-17 追記: 基本図形のフレーム転送 Forward
- `FForwardMeshDraw::bTransient` で共有メッシュの登録を省き、既存の `DrawGraphicsPipelineTransient` からフレームの転送領域へ頂点・添字・定数を渡す。添字なしの連続した三角形にも対応した。GPU の完了と転送領域の再利用は既存基盤が担当する（[Microsoft のフェンス管理](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management)）。メッシュキャッシュの登録・同期転送を基本図形の描画ごとに行わない。
- 共通実行側の三角形をこの経路へ接続した。塗りつぶした箱・球・円錐・カプセル・三角形、画像付き四角形や三角形の列などが対象。ワールド・カメラ・逆転置法線・明るさ・合成を同じ命令位置で渡す。線・点、影・フォグ・画素照明など未対応状態は既存経路。GPU が受け取らなかった場合も CPU で一度だけ描く。モデル・Mesh3D の既存キャッシュ経路が失敗した際に、さらにこの経路へ再提出することはしない。
- 補助関数を用途に合わせて `DrawMeshForward` へ改名した。CPU の図形生成は残っており、GPU で形状を生成する実装ではない。変形モデルの材質間で転送領域を共有する最適化、シーン範囲・透明ソート・PBR は未完了。
- Forward 契約は 89 項目。登録なし・添字あり／なし・拒否、箱の GPU 選択、明るさ・合成、CPU へ一度だけ戻る動作を追加。Debug は契約とサンプル画像 74 項目が合格。Release の Samples は通常リンクで LNK1103（Forward モジュールのデバッグ情報が破損）を再現し、再コンパイルだけでは解消しなかった。
- **最終確認**: Release はビルドする PowerShell の `$env:_LINK_ = '/LTCG /INCREMENTAL:NO'` で完全リンクを指定すると成功した。増分情報が原因かどうかは未確定で、プロジェクトの恒久設定は変更していない。Debug / Release とも Forward 89、モデル 161、材質効果 88、画像 74 項目、実 GPU / WARP のバッファ描画各 188 項目が合格。書式と互換台帳 522 項目も整合。Release のリンク再発時はこの復旧手順と [LNK1103 の公式説明](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-error-lnk1103?view=msvc-170) を参照する。

## 2026-11-17 追記: Forward の UV 変換
- UV の移動・非一様拡大・中心付き回転を、既存の CPU 経路と同じ順序で頂点シェーダーへ接続した。`FForwardMeshDraw::UvTransform` に拡大・中心・移動・回転の sin/cos を格納する。無効時は恒等変換を毎回初期化し、前の描画へ状態を漏らさない。値は形状ではなく個体定数へ置くので、UV を変えてもメッシュのバッファを再登録しない。登録メッシュ・モデルと、基本図形のフレーム転送経路で共通に使う。
- 個体定数 b0 は 208 から 240 バイト（転送時の 256 バイト整合は同じ）になった。非有限な UV 定数は GPU の登録・描画前に拒否する。影・フォグ・画素照明など、他の未対応状態における CPU 経路は維持する。シーン範囲・透明ソート・PBR は引き続き未完了。
- `Geometry3D --geometry-time=1.7 --geometry-uv` で、Mesh3D と汎用バッファの三角形に同じ UV 変換を掛け、後続の描画前に解除する。画像検査では CPU / GPU を比べ、無変換の画像から模様が変わることも検査する。Debug / Release の Samples ビルド、Forward 93 項目、材質効果 88 項目、画像 78 項目、書式と台帳の確認が合格した。Release は前項の完全リンク指定で確認した。
- 参照: [Unreal Engine の UV 座標・Panner・Rotator](https://dev.epicgames.com/documentation/en-us/unreal-engine/coordinates-material-expressions-in-unreal-engine)、[HLSL のベクトル・行列演算](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-mul)。変換順と回転の意味は既存のプロジェクトの契約を優先する。

## 2026-11-17 追記: 画素照明・距離／高さフォグとシェーダーの正本
- Forward の共有モデル・編集メッシュ・基本図形へ、画素照明と距離／高さフォグ（線形・指数・指数二乗）をまとめて接続した。世界の位置と法線を補間して画素照明を行う。フォグは既存の頂点評価と合成順を保ち、無画像は頂点色へ、画像つきは係数を補間して画像のサンプル後に混ぜる。UV 変換も併用できる。画素照明とフォグを同時に指定したときは、従来と同じく頂点照明を使うが、GPU Forward は維持する。影など未対応状態と GPU 拒否時は既存経路。
- 個体定数 b0 は 320 バイト（転送時は 512 バイト整合）になった。光の b6 は従来と同じ。距離の計算にカメラの奥行きを渡し、距離・高さの設定と色を描画ごとに固定する。非有限値や範囲外のフォグ方式は GPU の記録前に拒否する。
- シェーダーの正本は `EngineContent/Shaders` に集約した。DX12 の Forward・図形・組み込み・クリア／撮影、DxLib の材質効果、共通の材質／Substrate テンプレートの計17個。`Build/EngineShaders.targets` で構成別の中間ヘッダーへ取り込み、実行時のファイル検索は増やさない。生成したヘッダーはコミットしない。以前の Forward 専用 cpp への分離は、今回の HLSL への移動で置き換えた。
- ソリューションは HLSL/HLSLI などを `None` 項目とフォルダーフィルターで表示する。生成器・ビルドへの接続・配布対象と17個の配置は [シェーダーの管理](SHADERS.md) を参照する。素材用の `EngineContent` にバックエンド専用シェーダーを置く方針を README に反映した。
- Forward 契約は105項目。フォグ各方式、解除後の画素照明への復帰、定数、拒否を追加。画像検査は126項目で、共有モデルと Geometry3D の各8条件（画素照明、距離3方式、高さ3方式、両フォグ＋画素照明指定）を、時刻を固定して GPU/CPU で比較する。Geometry3D は UV 変換とも併用する。`--forward-case=pixel|distance-linear|distance-exp|distance-exp2|height-linear|height-exp|height-exp2|combined` で再現できる。
- **確認結果**: Debug/Release のライブラリと Samples のビルドは警告・エラー0。移動後も画像126項目と Forward105項目が両構成で合格。描画先・撮影の実GPU/WARP/UIなし経路と、DxLib/Null の描画先検査も合格。シェーダー生成・一覧の検査36項目は Windows PowerShell でも合格し、全体検査へ追加した。HLSL だけを編集した再ビルドで実行結果が変わり、IDE が参照する tlog に入力が記録されることも確認した。assets は Debug 10,384 件／Release 10,381 件がワーカー0／1／4／15で一致して合格。配布物検査も合格し、17個すべてのシェーダーが元ファイルと同じハッシュで含まれることを確認した。Release は既存の完全リンク指定で確認した。
- `GraphBuilder.AddPass` 型の API と資源の読み書きから依存を検査するフレームグラフは、ロードマップ R3 の未実装項目。R2 のシーン範囲・透明ソート・共通 PBR も未完了であり、この変更では完了扱いにしない。

## 2026-11-17 追記: 明示的な3Dシーンの分類と透明順序
- BeginScene3D / EndScene3Dと、直後の描画に分類・有限な奥行きを付けるSceneDrawKey3Dを追加した。共通側で、不透明・切り抜きは各分類の記録順、半透明は奥から手前、同じ深度は元の順序に並べる。状態変更・2D・キーのない描画・未対応描画を越えない。DX12 / DxLib / Nullへ同じ順序処理を接続し、既存の命令番号は保った。
- 入れ子・範囲不正・不正キー・複製領域や項目数の上限超過では、Span全体を元の順序で処理する。順序処理はローカル出力だけを所有し、GPU・ImGui・subsystemに依存しない。モデルの姿勢を書き換える命令は境界に残す。
- 設計、利用方法、所有と並行実行、検査と未対応範囲は [3Dシーンと描画順](R2_SCENE_DRAW_ORDER.md) を参照する。フレーム固定のシーン収集・材質からの自動分類・共通PBRは残っており、R2全体やR3を完了とはしない。
- **確認結果**: Debug / Releaseの契約50項目と、実GPU・WARP・UIなし・CPU変換・DxLibの画像15項目が合格。通常順と逆順は手動基準との差0、誤順の比較は15,625点の差を検出。順序・境界・不正範囲・モデル姿勢を壊した6変異もすべて検出した。両構成の既存Forward105項目と描画先CPU45・実GPU/WARP各55項目も合格。Releaseの通常出力はCの容量不足（LNK1180）で失敗したため、完全リンク指定を維持しD:/CodexVerification/framework-scene-order/Releaseへ出力して成功した。画像検査のコピーは一組を使い回す方式へ修正した。
- **回帰確認**: 既存サンプル画像126項目もDebug / Releaseで合格。互換台帳525項目・DX12の役割54型・ソース書式2005ファイルが整合した。全体検査の一括実行を完了したという意味ではなく、今回に関係する検査を個別に実行した。検査用コピーの削除は自動審査で拒否されたためC側の初回画像検査コピーは残り、以後の画像と一時領域はDへ分けた。

## 2026-11-17 追記: 独立した3D描画一覧の所有と合流
- `CSceneDrawList` と不変の `FSceneDrawSnapshot` を追加した。カメラと項目ごとの状態・固定長3D命令を値で所有し、分類・固定ビュー方向の奥行き・同深度の合流順を保つ。各項目を独立したExecuteとして再生するので、合成や深度やライトの違いを含めて並べられる。従来Spanの途中を暗黙に並べ替えない。
- 各ジョブが別の一覧を確定し、所有者が決めた順でAppendする。同じ可変一覧を共有しない。元の記録バッファやカメラを変更・破棄しても確定済みの値は変わらず、KeepAliveは既存の共有所有を確定値の寿命まで保持する。新しいsubsystem・常駐スレッド・アセット管理は作っていない。
- Geometry3Dに `--scene-snapshot=reference|sorted|reverse|mutated` を追加し、既存IJobSystem::ParallelForで記録する。手動の正しい順を基準に、逆順と元データの変更を比較する。設計・上限・破棄スレッド・GPU完了との区別は [描画一覧の収集と確定](R2_SCENE_DRAW_SNAPSHOT.md) を参照する。
- モデルの姿勢や更新可能な資源の内容まで版として固定する仕組みではない。資源の更新・番号変更・解放に関する既存の借用契約は維持する。自動分類、共通PBR、R2全体とR3のフレームグラフは引き続き未完了。

- **最終確認**: Debug / ReleaseのライブラリとSamplesの最終ビルドは警告・エラー0。契約50項目、実GPU / WARP / UIなし / CPU変換 / DxLibの画像15項目が両構成で合格し、手動基準との差は0。ワーカー数は0 / 1 / 4 / 15で確認した。無変更の対照が合格し、6種類の所有・コピー・ビュー・順序の変異を検査の失敗としてすべて検出。従来のシーン順序50、Forward105、既存サンプル画像126項目も両構成で合格。互換台帳525項目、DX12の役割54型、ソース書式2014ファイルが整合した。全体検査の一括実行は行っていない。Releaseの出力と画像検査の一時領域は、容量を確保したD:/CodexVerification/framework-scene-snapshotへ分けた。

## 2026-11-17 追記: 材質を固定したSurface記録
- CMaterial::CreateSurfaceSnapshotと不変のFMaterialSurfaceSnapshotを追加した。準備と資源の固定はゲームスレッド、公開後のメッシュ記録は不変値だけを読む。定数・画像・完成済みシェーダーを保持し、元の材質の変更・画像差し替え・Reload・解放から独立させた。既存FMaterialProgramとUnlit / DefaultLit / Substrateの実装を再利用する。
- CSceneDrawListへ所有付きの原子的なAddを追加し、定数・画像・可変長シェーダー頂点列を検査して受け付ける。描画先・消去等の境界は保つ。材質のAddMeshは通常の不透明または半透明の深度と合成を選び、切り抜きは未対応として拒否する。
- Materialsに固定・逆順・元の変更とReload・固定しない変更後の比較を追加した。利用方法、資源内容の更新との区別、検査と残件は [Surface材質の固定](R2_MATERIAL_SURFACE_SNAPSHOT.md)。共有GPUメッシュ・自動分類・切り抜き・R2全体とR3のフレームグラフは未完了。

- **確認結果**: Debug / Releaseの契約93項目と、実GPU / WARP / UIなし / CPU変換指定 / DxLibの材質画像20項目が合格。固定経路は手動基準に対し各成分1を超える差のある画素0、固定しない変更後は12,628点の差を検出。ワーカー数0 / 1 / 4 / 15、元のパラメーター・画像の差し替えとReload、元の材質の解放を含む。長さ・頂点有限値・添字の4変異、所有・合流の6変異を、無変更の対照が合格する状態で検査の失敗として検出した。既存サンプル画像126項目も両構成で合格。最終ビルドは警告・エラー0、互換台帳525項目・役割54型・書式2023ファイルが整合した。全体検査の一括実行は行っていない。

## 2026-11-20 追記: シェーダー描画のGPUバッファ共有

- DX12で同じ頂点・添字が繰り返し使われる三角形は、二度目から既存のGPU頂点・添字バッファを共有する。個体ごとの行列、色、材質定数、画像は毎回設定する。初回、登録失敗、共有描画の拒否時はフレーム転送で描く。
- キャッシュはDX12シェーダーサービスが所有し、最大32件・32 MiB。比較用ハッシュの候補は全バイトで照合し、解放は既存のGPU完了管理に委ねる。キャッシュを外して比較するときは `--no-shader-mesh-cache` を使う。
- 記録側では引き続き頂点・添字をコピーする。記録を小さくする資源ID参照、材質からの自動分類、切り抜きはR2の残件。利用条件と検査は [Surface材質の固定](R2_MATERIAL_SURFACE_SNAPSHOT.md) に記載した。
- Debug / ReleaseともSamplesのビルド、形状共有57項目、描画一覧93項目、材質画像20項目が合格した。画像はキャッシュ無効の基準に対し、固定材質では許容差を超える画素が0だった。Releaseの既存サンプル画像113項目、互換台帳525項目、役割55型、ソース書式2028ファイルも確認した。

## 2026-11-21 追記: 描画記録の頂点・添字の共有

- `FShaderMeshSnapshot::Create` で既存頂点形式の添字付き三角形を一度検査して所有する。`FMaterialSurfaceSnapshot::AddMesh` に渡すと、一覧の各項目には1 wordの参照命令と材質・個体の設定が入り、形状のバイト列を共有する。合流・確定後は元の形状と材質を手放せる。
- DX12とDxLibは参照を既存のシェーダー描画へ解決し、Nullは参照を検査する。DX12では前項のGPUキャッシュも使う。1つの実行範囲が持てる形状は1個で、従来の頂点・添字の命令上限を適用する。自動分類、切り抜き、上限を超える形状はR2の残件。手順は [Surface材質の固定](R2_MATERIAL_SURFACE_SNAPSHOT.md) を参照する。
- Debug / ReleaseともSamplesビルド、描画一覧105項目、材質画像20項目が合格。実GPU・WARP・UIなし・CPU変換指定・DxLibの固定材質は基準に対する許容差超えが0画素。Debugの既存シーン順序50項目とGPUキャッシュ57項目、ソース書式2030ファイル、互換台帳526項目も確認した。

## 2026-11-21 追記: Surface材質の分類と切り抜き

- `.gxmaterial` の `SurfaceBlend` を固定材質に保持し、分類省略のAddMeshで順序・深度・合成を設定する。省略時の定義はManualで、従来の不透明・半透明の明示指定を受け付ける。定義と異なる明示指定は拒否する。
- MaskedのSurfaceグラフは `OpacityCutoff` を超える画素だけを色・深度へ書く。グラフの陰影を使い回し、切り抜き処理はEngineContentのSurface用HLSLで共通化した。HLSL直指定のMaskedと、独立した影生成パスでの切り抜きは未対応。詳細は [Surface材質の固定](R2_MATERIAL_SURFACE_SNAPSHOT.md)。
- Debug / ReleaseのSamplesビルド、描画一覧115項目、材質画像25項目、材質効果88項目が合格。実GPU・WARP・UIなし・CPU変換指定・DxLibで、しきい値と同値の画素では奥の赤が見え、しきい値を超えた画素では手前の緑が奥を遮った。互換台帳526項目とソース書式2031ファイルも確認した。

## R3の入口: 名前付きの描画パス

- `CRenderGraphBuilder` は既存画像の初期状態、パスの読み取り、色・深度の描画先を受け取る。全件を検査してから既存の描画先パスへ登録順で渡す。名前を指定してパスを前後に追加したり、同じ位置で差し替えたりできる。
- GPU検査ではMRTと深度の生成、挿入した追加描画、差し替えた合成パスを実行し、色と深度を読み戻す。使用法と次の接続先は [R3 描画パスの登録と依存検査](R3_RENDER_GRAPH.md)。
- Debug / ReleaseともCPU契約19項目、実GPU・WARP各107項目が合格。公開描画先を使うアプリ検査は実GPU・WARPのUIあり／なしで合格し、DxLib・Nullでは未対応の描画先を使わない既存経路も確認した。R2は未完了で、影の切り抜き・大きな形状・材質互換とフレーム固定の残作業を継続する。

## R2: Surfaceの切り抜きを影生成へ接続

- `PrepareMaskedShadowShader` と `CMaterialShadowMap::DrawMaskedMesh` を追加した。色と影が同じSurfaceグラフの不透明度と共通のしきい値判定を使う。影側の入口はEngineContentの既存深度用HLSLに置き、R3のパスからも再利用できるようにした。
- 色と同じ入力を呼び出し側が渡し、影の記録から再生まで材質・画像を保持する。従来の全形状の影も同じ影マップ内で使える。使用法は [Surfaceの影](MATERIAL.md)。
- Debug / ReleaseのSamplesビルド、描画一覧116項目、材質画像30項目が合格。実GPU・WARP・UIなし・CPU変換指定・DxLibで、同値と透明画像の穴は奥の深度0.7、残った手前の形状は深度0.5となった。

## R2: 一命令の上限を超えるSurface共有形状

- `FShaderMeshSnapshot::Create` に32bitの添字入力を追加し、最大262,144頂点・786,432添字に対応した。三角形の順序を保って一命令に収まる単位へ分け、各部分の頂点を16bitの添字へ置き直す。
- DX12とDxLibは材質状態を保って順に全部分を描く。ソートと所有は形状全体で、描画一覧の参照は1 wordのまま。先頭と末尾に可視三角形を持つ67,842頂点のサンプルを従来の球と画像比較する。
- Debug / ReleaseともSamplesビルド、契約検査120項目、材質画像35項目が合格。分割をまたいで元の全添字を復元し、末尾の不正な添字では形状全体を拒否した。実GPU・WARP・UIなし・CPU変換指定・DxLibで、大きい形状と従来の基準画像の許容差超えは0画素。

## R2: 評価済みモデル姿勢の捕捉と再生

- `FModelPoseSnapshot::Capture` でDX12の個体が持つ所有付きモデルビューを捕捉し、`CSceneDrawList::AddModel` で配置とともに記録する。合流・確定後は個体の更新や解放から独立する。再生は既存のGPU ForwardとCPU変換の経路を使う。
- ゲームスレッドで反映済みの骨・アニメーションと個体不透明度が対象。画像の内容や未実行のアニメーション命令までは固定しない。DxLibネイティブモデルでの捕捉は未対応。手順は [描画一覧](R2_SCENE_DRAW_SNAPSHOT.md)。
- Debug / ReleaseともSamplesビルド、モデル契約170項目、描画一覧120項目、Forward105項目、画像18項目が合格。実GPU・WARP・CPU変換で、元の個体を移動・解放した後に固定姿勢と変更前の基準を比較し、許容差超えは0画素。移動を反映した対照では17,143点の差を検出した。

## R2: CPUから登録した動的画像の固定

- `CDynamicTexture::CaptureCpuImageVersion` は作成・更新に成功したCPU画素を独立画像として保持する。同じ版は生存中だけ共有し、GPUの読み戻しは行わない。更新後も元のハンドルは維持する。
- `CMaterial::CreateSurfaceSnapshotWithCpuImageVersions` を選ぶと、固定材質は束縛する動的画像のCPU版を保持する。通常の固定材質は画像を共有参照し、描画先・フィルター・computeの結果を扱える。[固定材質と画像の版](R2_MATERIAL_SURFACE_SNAPSHOT.md)
- Debug / ReleaseともSamplesビルド、固定記録の契約128項目、材質画像45項目が合格。実GPU・WARP・UIなし・CPU変換指定・DxLibで、元画像を同じハンドルのまま更新した後も固定版の許容差超えは0画素。通常の参照方式で更新を反映した対照は12,628点に差が出た。

## R2: GPUで書いた描画先の色の固定

- `CRenderTarget::CaptureGpuColorVersion` はゲームスレッドでパス終了後に呼ぶ。既存のGPUコピーを使い、RGBA8、RGBA16F、対応GPUのR32Floatカラーを、元と同じ形式・寸法の独立画像へ複写する。材質や描画一覧は通常の画像参照と同じ所有方法で保持できる。
- コピーは完了待ちを伴う。深度・マルチサンプル・描画先以外のGPU更新資源、は今後の課題。描画グラフ上のコピー予約は次の「R3: 描画パス間の画像コピー」に記録した。[固定材質と画像の版](R2_MATERIAL_SURFACE_SNAPSHOT.md)
- Debug / Releaseでアプリをビルドし、描画先・描画グラフの検査を実GPU・WARPのUIあり／なしとDxLib・Nullで確認した。描画先の独立検査はCPU 45項目・実GPUとWARP各55項目、描画一覧の契約は128項目が合格。アプリ検査ではRGBA8の二つの版、HDRの範囲外の値、対応GPUのR32Float値、パスが開いている間の拒否を確認した。

## R3: 描画パス間の画像コピー

- `CRenderGraphBuilder::AddTextureCopyPass` / `InsertTextureCopyPassAfter` で、同じ寸法・色形式の画像全体を複写する名前付きのパスを登録できる。検査はコピー元が初期化済みか取り込み済みであること、コピー先が取り込み済みであることで、コピー後のコピー先は後続が読める。元と先が同じ、無効な画像、存在しない対象の名前、描画パスの処理の差し替えは拒否する。[描画グラフ](R3_RENDER_GRAPH.md)
- `IRenderSurface::RecordTextureCopy` を追加した。DX12では `CThreadedRenderSurface` が描画スレッドの先行する命令の後に記録し、`CDX12GameSurface` が `CDX12TextureStore::RecordTextureCopy` を通じて主画面と同じコマンド列へ積む。パスの外側（主画面や描画先パスの記録中を除く）で、パス間の定常状態（シェーダー資源）から複写し、定常状態へ戻す。GPUの完了は待たない。DxLibとNullは既定どおり未対応でfalse。
- `CDX12TextureStore::CopyTexture`（完了待ちあり）と共通の `FindTextureCopyPair` で、記録中の画像・寸法・色形式（深度は除外）を検査する。コピーの記録は `NDX12TextureCopy::RecordRegionCopy` に集約した。記録後に利用者が画像を解放しても、Storeの遅延解放がフレーム完了まで保持する。
- 失敗した場合はグラフがコピーの名前を付けて失敗を返し、後続のパスは実行しない。深度のコピー、マルチサンプル、描画先以外のGPU更新資源は今後の課題。
- 検査は、描画グラフの契約（CPU 27項目）に、コピーの元と先の向き・設定付きの差し替えの拒否・失敗後に後続を実行しないことを加え、アプリ経路（実GPU・WARP・UIなし）で、MRTの色をコピーして元を上書きしても合成が退避した色を読むこと、記録できない組（形式違い・同じ画像・無効な画像）がfalseになることを確かめる。深度同士のコピーの拒否は、HDRコピーの契約で確かめる。
- 変異検査は17件を実施した。生き残ったのは2件で、どちらも同じ結果になる変異だった（元と先が同じ資源・形式の違いは、`NDX12TextureCopy::RecordRegionCopy` も自分で拒否するため、`FindTextureCopyPair` の同じ条件を外しても観測できない）。残りは、検査を足して検出した。注意点として、契約の実行ファイルはソースではなく `x64/<構成>/mazigatiDX.lib` にリンクするので、変異ごとにライブラリを作り直さないと、変異が反映されずにすべて生き残ったように見える。
- 検査の落とし穴が2つあった。(1) シーン描画一覧・材質の画像検査は `Samples/x64/<構成>/Samples.exe` を読むが、`project.sln` 経由のビルドは `x64/<構成>/Samples.exe` へ出力する。前者は古いまま残るので、`Samples/Samples.vcxproj` を直接ビルドしてから走らせる（Releaseは `_LINK_=/LTCG /INCREMENTAL:NO` を付ける）。古い実行ファイルのままだと、Releaseだけ「元の基準と違う」と失敗し、差の数値が毎回変わる。(2) `ComputeContract.vcxproj` はDX12のソースを直接コンパイルするので、`DX12TextureStore.cpp` が新しい `DX12TextureCopy.cpp` を参照した時点で、一覧へ追加が必要だった。
- `RunAllChecks.ps1 -SkipBuild -Quick` は64ステップすべて合格（Debug / Release）。

## R3: G-buffer と Deferred 照明（2026-11-21）

- **何を作ったか**: `Library/Framework/Render/Deferred`（`CGBuffer`・`FGBufferView`・`NDeferredShaders`）、`EngineContent/Shaders/Deferred`（`GBufferWrite.hlsli`・`GBufferRead.hlsli`。製品の部品だけ）、サンプル `Deferred`（シェーダーはサンプルの素材 `Samples/Assets/Deferred`。製品に含まれないので、EngineContent へは置かない）。公開のシェーダー窓口（複数出力・浮動小数点の描画先・`ShaderTexture`）だけで作ったので、DxLib・DX12 で同じコード・同じ HLSL が動く。詳細は [R3 G-buffer と Deferred 照明](R3_DEFERRED.md)。
- **深度の取り決め**: 「カメラからの距離 / Far」ではなく「カメラの前向きの奥行き / Far」を、32 ビット浮動小数点の 1 チャンネルへ書く。距離だと Far の面の隅で 1 を超え（広角で Far の 4 倍）、何も描かれていない印（1）と区別できない。奥行きなら、再構成も `CameraPosition + (Far の面の点までのベクトル) * 深度` の正規化なしの一次式。頂点シェーダーがクリップ座標の w（= 奥行き）を渡して補間すれば、画素ごとの値になる。
- **位置の基準 `FGBufferView::Make` は、`FCamera::ScreenToWorld`（逆行列）を使わない**: Far が大きい広角（Near 0.2・Far 1000）で、奥行き 0.999 付近の点が 0.2 ずれた（float の逆行列の精度）。射影行列と同じ規則（縦横比 ÷ 横の拡大、レンズのずらし）から、Far の面の画面の端を直接求める。検査の基準も、逆行列を使わない `WorldToScreen` で点を画面へ進めて戻す形にした（距離に対する割合の誤差が 5e-8〜4e-7）。
- **検査**: `RunDeferredTests.ps1`（契約 63 項目 + `-Platform` で Forward と Deferred の画像比べ。半透明のガラスを含む）、`RunDeferredMutationTests.ps1`（C++ 側 14 件）。HLSL の計算は、WARP の D3D11 コンピュートシェーダーで実際に走らせて、C++ の再構成と `PackGBuffer` の 4 つの出力と比べる（`FGBufferView` のメモリをそのまま定数にするので、構造体の並びの取り違えも見つかる）。画像比べは、DX12・WARP・DxLib の 3 つで、見せるもの 6 種類ずつ、差が 3 を超える画素が 0（実測の最大差は 1）。手元の変異検査は、契約 37 件と、画像でだけ見える HLSL の 12 件（DX12 のみ。半透明 5 件を含む）のすべてを検出。`RunAllChecks.ps1` に 2 つの手順（変異と契約）を足した。
- **落とし穴**: (1) HLSL を C++ の文字列でなくファイルにする方針（[シェーダーの管理](SHADERS.md)）なので、テスト専用の HLSL も `Source/SelfTest/DeferredContract.hlsl` のファイルにして、実行ファイルへパスを引数で渡す。サンプルのシェーダーを最初は EngineContent に置いたが、EngineContent は製品に同梱されライブラリへ埋め込まれるため、サンプル専用のものが製品に混ざる。サンプルの素材へ移して、実行時に読む形にした。ファイルをそのまま読むと先頭に UTF-8 の BOM が付き、HLSL に連ねると `Illegal character` になる（生成器は BOM を外して埋め込む）。(2) サンプルの画像検査は `x64/<構成>/Samples.exe` を使う（`project.sln` の `Samples` ターゲットの出力）。(3) 契約が `mazigatiDX.lib` にリンクするので、HLSL・ライブラリの変異は、変異ごとにライブラリを作り直す。(4) 撮影の部屋は `%TEMP%` に作る（`move_windows_off_main.ps1` が移すのは `%TEMP%` のプロセスだけ）。
- **半透明（Forward 透明合成）**: 不透明の物だけ G-buffer へ書き、光を当てた場面の色へ、半透明の物を、奥から手前の順（物の中心で並べる簡単な方法）にアルファ合成で重ねる。Forward 経路はハードウェアの奥行きの比べ、Deferred 経路は G-buffer の深度をシェーダーで読んで `clip`（場面の色の描き先は、不透明の物の奥行きバッファを持たないため）。並べ替えの正しさは、両経路が同じ順を使うので、画像の比べでは確かめられない。
- **残り**: 標準の材質（Surface）の G-buffer 出力、深度だけのパス、描画グラフの名前付きパスとしての接続、半透明の `BeginScene3D` 分類との接続、マルチサンプル、光のカリング（R4）。
- **push はしていない**（Codex の利用上限が戻ったあと、または指示があったときに行う）。

## R4: タイルごとのライトカリング（2026-11-21）

- **何を作ったか**: `Library/Framework/Render/LightCulling`（`CTileLightGrid`・`NLightCullingShaders`）、`EngineContent/Shaders/LightCulling/TileLightList.hlsli`（製品の部品）、サンプル `Deferred` の動く光 64 個を、タイルごとの一覧で絞って足す（Forward+ / Deferred+）。詳細は [R4 ライトカリング](R4_LIGHT_CULLING.md)。一覧は CPU が作り、8 ビット RGBA の動的な画像（`CDynamicTexture::Update`）で毎フレーム渡す。DX12 の compute 版は、まだ（この実装が基準になる）。
- **欠落させない取り決め**: タイルは、カメラから Far の面まで伸びる四角錐。光の球と 4 つの側面・Near/Far を比べる保守的な判定で、触れるタイルには必ず入る。一覧は光の番号の昇順（足し算の順が同じ）で、光は半径の端で 0 になるので、絞った絵が全部の光の絵と**ビットまで一致**する（Forward+・Deferred+ とも差 0）。上限を超えたタイルは先頭の画素の R を 255 にして、シェーダーが全部の光を評価する。
- **検査**: `RunLightCullingTests.ps1`（契約。見落とし 0・近遠面・溢れ・画像の詰め方・作れない入力・WARP の D3D11 で HLSL の読み出しを C++ と比べる）、`RunLightCullingMutationTests.ps1`（C++ 側 12 件）、`RunDeferredTests.ps1 -Platform`（一覧を使った絵と全部の光の絵を、3 つの実行先で全画素比べる。溢れを起こす上限 3 の実行も）。`RunAllChecks.ps1` に 2 手順を足した。
- **落とし穴**: (1) 画面の文字（統計）が、一覧を使う / 使わないで変わると、絵の比べに文字の違いが混ざる（最初の失敗の原因）。統計をログへ出す `--deferred-tile-stats` のときは、画面の文字を描かない。(2) 動的な画像は、同じ大きさでしか `Update` できない。一覧の上限は起動のときに決める（`--deferred-tile-capacity`）。(3) DxLib は、シェーダーが宣言した画像の段が t0 から連続している必要があるので、Forward のシェーダーは一覧を t0、G-buffer を読むシェーダーは G-buffer を t0〜t3、一覧を t4 に置く（段は、シェーダーの先頭の `#define TILE_LIGHTS_REGISTER` で決める）。(4) 契約の見落としの検査は、タイルの中の点（4 隅と中心）と奥行きで当てるので、変異を足すときは、半径 0 の光のような端を、Near の判定で先に落ちない位置に置く（最初の変異検査で、半径 0 を受け入れる変異が生き残った）。
- **残り**: GPU の compute による一覧作成と交差判定、奥行き方向のクラスタ分割、スポットライト、Deferred の光の体積、GPU 時間の表示。
- **push はしていない**。

## R4: 奥行きの層（クラスタ）と、メニューの落下の修正（2026-11-21）

- **奥行きの層**: `CTileLightGrid` が、タイルに加えて奥行きの層（`NumDepthSlices`、既定 1、上限 64）を持つ。クラスタ = タイル x 層。層の始まりは `Near x (Far / Near)^(k / 層の数)`、光は球の奥行き（中心 ± 半径）が重なる層にだけ入る。画素の層は `GetSliceOfDepth`（HLSL は `SliceOfDepth`、同じ式）。層の範囲は前後に 0.1%（`SliceMargin`）広げて、GPU の対数の丸めで隣の層に入っても欠落しないようにした。画像の行 = 層 x タイルの行の数 + タイルの行。HLSL の部品は、`IsTileOverflowed` / `LoadTileTexel` を `IsClusterOverflowed` / `LoadClusterTexel` に改め、`SliceOfDepth` と `ClusterOfPixel` を足した。サンプルは `--deferred-slices=<1〜64>`（既定 16）、上限の既定は 48 から 24 に下げた（16 層で溢れは 0、画像は 320 x 320）。詳細は [R4 ライトカリング](R4_LIGHT_CULLING.md)。
- **検査**: `RunLightCullingTests.ps1`（147 件。層の式・見落とし 0・端・溢れ・画像の行・HLSL の層の式と対応を WARP で）、`RunLightCullingMutationTests.ps1`（C++ 21 件）、HLSL の変異（部品 12 件・サンプル 6 件。変異ごとにライブラリを作り直す手元の検査）、`RunDeferredTests.ps1 -Platform`（16 層・1 層・上限 3 の溢れを、全部の光の絵と全画素比べる）。
- **メニューの落下（修正済み）**: サンプルのメニューで、カーソルを説明文の長いサンプル（Shapes3D・PostEffects・Island・Deferred。約 500 文字より長い）に動かすだけで、DX12 のアプリが終了していた。原因は、メニューが説明文を 1 行の `Text` で描いていて、DX12 の文字の画像は 1 辺 4096 ピクセルまでなので（`CWindowsTextRasterizer::RasterizeText` が false を返す）、`CGeometryCanvas2D::Text` が `bFailed` を立て、そのフレームが捨てられ（`DX12 frame dropped: ... renderer failed`）、続いて `DX12 present failed` で `bExit` になるため。DxLib は、はみ出す分を切って描くので落ちない。直し方は、メニューが説明文を 130 文字で折り返して最大 5 行で描くこと。**DX12 の「描けない文字が 1 つでも、アプリが終了する」作りと、4096 ピクセルの上限そのものは、残っている**（[DX12 の未対応の事項](DX12_KNOWN_GAPS.md)）。メニューの検査: `RunSampleBrowserTests.ps1`（起動オプション `--browser-select=<サンプル名>` で、全サンプルを選んだ状態を DX12 で描き、フレームが捨てられないことを確かめる。折り返しを壊すと、4 件で失敗する）。キー入力は `GetAsyncKeyState` を読むので、PostMessage ではメニューを操作できない（起動オプションで選んだ状態を作る）。
- **push はしていない**。

## R4: スポットライト（2026-11-21）

- **何を作ったか**: `FCullLight`（カリングに使う光）に `Direction`・`ConeHalfAngle` を足した（半角 π 以上は点の光。`IsSpot` / `CanReach` / `FromLight( FLight )`）。光が届く範囲は、点の光は球、スポットライトは球を円錐で切り取った扇形。`CTileLightGrid::Build` の、タイルの 4 つの側面との比べと、奥行きの範囲は、球の半径の代わりに `GetExtent`（範囲が、面の法線の向きへ、光の位置からどこまで広がるか）を使う。点の光は、どの向きも半径なので、球のときと同じ結果（既存の検査と、一覧の画像の比べは、変わらず通る）。サンプル `Deferred` は、外側の輪の 18 個を、高い所（高さ 6）から内側の地面の動く点を照らすスポットライトにした（半径 22・外側の角 0.28 ラジアン・内側 0.15）。
- **サンプルの光の式**: `SampleCommon.hlsli` の `EvaluateLight`（旧 `PointLight`）。`g_LightSpots[k].w`（外側の角の cos。-1 以下なら点の光）が -1 より大きい光は、`smoothstep( cos 外側, cos 内側, cos 光から画素への角 )` で円錐の縁へ滑らかに 0 になる。**円錐の縁で 0 になることが、ライトカリングの前提**（円錐の外のクラスタの一覧から外しても、結果が同じ）。内側の角の cos は `g_LightColors[k].w`。スポットライトは遠くまで届くので、距離の減衰の係数を点の光（0.3）より小さく（0.02）した。
- **上限の既定を 24 から 36 にした**: スポットライトの円錐は長く伸びて、多くのクラスタを通るので、16 層で 150 クラスタが 24 で溢れた（36 なら、時間を変えても 0）。画像は 480 x 320（毎フレーム約 600KB）。`RunDeferredTests.ps1` も 36（層 1 は 48）。
- **検査**: `RunLightCullingTests.ps1`（226 件。スポットライトの `CanReach`・見落とし 0（6 種類のカメラ x 1 層・12 層）・横向き / 奥向き / カメラ向き / カメラの後ろ・向きの長さ・半角の単調さ・π 以上・0 以下・不正な入力・`FromLight` と `FLight::GetIntensityAt` の一致）、`RunLightCullingMutationTests.ps1`、`RunDeferredTests.ps1 -Platform`（スポットライトを含む絵で、一覧あり・なし、16 層・1 層・溢れの全画素一致、差 0。さらに、`--deferred-no-spots` の絵と約 21 万ピクセル違うこと = スポットライトが効いていること。向きを反転すると一覧の比べは通ってしまうので、この検査が必要）。
- **落とし穴**: (1) 円錐を球で包むと簡単だが絞れない。面の法線の向きへの広がりを式で出す（`半径 x cos( 軸との角 - 半角 )`、負なら 0）と、過不足がなく、点の光とも同じ式で扱える。(2) `FLight` の `OuterAngle` / `InnerAngle` は**全角**。`FCullLight::ConeHalfAngle` は半角。(3) サンプルの上限を決めるときは、時間を変えて溢れを見る（`--deferred-time=<秒>` は、先に書いた引数が勝つ）。
- **残り**: GPU の compute による一覧作成（公開の compute の窓口が必要）、Deferred の光の体積、GPU 時間の表示（描画命令に、タイムスタンプの窓口がまだない）、標準の材質（Surface）の G-buffer 出力。
- **push はしていない**。

## R4: GPU での光の一覧作成（2026-11-21）

- **方針**: 新しい compute の窓口は作らず（描画命令の中で compute を呼ぶ窓口は大きな作業で、R9 の GPU カリングなどと一緒に設計したい）、CPU の `CTileLightGrid::Build` と同じ判定を、**ピクセルシェーダー**で、一覧の画像の画素ごとに行う（`CTileLightBuilder`。`EngineContent/Shaders/LightCulling/TileLightBuild.hlsli` の `BuildTileLightTexel`）。公開のシェーダー窓口（`ShaderRect`・`ShaderConstants`・描画先）だけで動くので、DxLib でも DX12 でも同じコード。GPU の一覧は、CPU の一覧と同じか、境目で少し多い（足りなくならない）。サンプル `Deferred` の既定を GPU にした（`--deferred-cpu-lights` で CPU の経路）。詳細は [R4 ライトカリング](R4_LIGHT_CULLING.md)。
- **定数**: 1 つの定数バッファは 4096 バイトまで（`NDrawShader::MaxConstantBytes`）なので、3 つに分けた（b4: カメラ・クラスタ、b5: 光の位置と半径 254 個、b6: スポットライトの軸と半角の cos）。HLSL の cbuffer と C++ の `FTileLightBuildData` は同じ並びで、WARP の検査が構造体のメモリをそのまま定数にして確かめる。
- **落とし穴（実際に起きた）**: (1) 最初の `Record` は、描画先の中で `Depth( 深度の判定なし )` などを呼んで、戻さなかった。続く場面の描画の深度判定が切れて、光る玉が柱の手前に見えた。**GPU の一覧の絵と、基準（一覧なし）の絵は、同じ誤りで一致してしまった**。Forward と Deferred の比べと、CPU の経路（`--deferred-cpu-lights`）との比べで見つかった。`FDrawContext` の状態は、スコープで戻らないので、描画を記録する部品は、状態を変えないこと（`ShaderRect` は深度を使わず、画像を読まず、アルファ 1 なら、呼び出し側の状態のままで書ける）。(2) 端のタイルの範囲を、画面の端までに切る処理（`min( ( tileX + 1 ) * tileSize, width )`）を外す変異は、最初の検査（ランダムな光）では生き残った。画面の端のすぐ外の光を入れ、端が割り切れない画面（1000 x 600）を足して検出した。(3) CPU と GPU の一覧が、ほとんど一致するだけの検査では、一部の誤りが隠れる。基準の実行は、変異検査で、壊した HLSL を 2 つ目の引数で渡して、ライブラリを作り直さずに試す。
- **検査**: `RunLightCullingTests.ps1`（346 件。`BuildMain` を WARP で走らせ、4 種類の画面 x 5 通りの設定で、GPU の一覧が CPU と同じ）、`RunLightCullingMutationTests.ps1`（C++ 50 件と、GPU の HLSL 23 件。すべて検出）、`RunDeferredTests.ps1 -Platform`（GPU で作った一覧・CPU で作った一覧・全部の光の 3 つの絵が一致。`gpu=1/0` をログの統計で確かめる）。
- **残り**: compute シェーダー版（公開の compute の窓口が要る。光が数百以上、またはクラスタが非常に多い場面で、画素ごとの全部の光の判定が重くなる場合）、GPU 時間の表示、Deferred の光の体積。
- **push はしていない**。

## 文字の画像の大きさの上限と、1 フレームの数の上限（2026-11-22）

- **直したこと**: 描けない長さの 1 行（画像にして 4096 ピクセル超。既定の文字で約 450〜500 文字）や、行が多い文字列（4096 ピクセル・4M 画素超）が、**失敗でなく、はみ出す右・下を切った画像**になる（DxLib が画面の端で切るのと同じ。API の振る舞いの互換）。`CWindowsTextRasterizer::RasterizeText`（既定の文字）と `CWindowsGdiText::Rasterize`（フォントを指定した文字）。文字を描く処理は、画像の外の画素を書かない（境界を調べる）ので、画像を小さくするだけで済む。以前は、メニューで説明文の長いサンプルにカーソルを合わせるだけで、フレームが落ちてアプリが終了した（メニューの折り返しは、見た目のために残してある）。
- **既定の `Text` も画像のキャッシュを使う**: 以前は、1 フレームに 128 個を超える `Text`（既定の文字）があると、フレームごと失敗してアプリが終了した。フォントを指定した文字と同じ表（`TextCache`。鍵の先頭の 0x01 で区別）で、同じ文字列は画像を使い回し（使わなくなって 120 フレームで捨てる）、1 フレームに新しく作れる数は 512 個・合計 32MiB まで（フォント指定の文字と同じ）。毎フレームの画像の作成と GPU への転送が減る。
- **検査**: `CaptureContractMain.cpp`（`CheckTextRasterizer`: 600 文字の 1 行は幅 4096 で切れて、先頭と右端の両方に文字がある・5000 行は高さ 4096・長い行 100 行は 4096 x 1024。撮影の中で、違う文字列 200 個と 600 文字の行を描く）。旧い動作（上限で失敗）に戻すと、4 件失敗する（変異の検出）。
- **残り**: 1 フレームに新しく作る文字が 512 個・32MiB を超えると、そのフレームが失敗する（[未対応の台帳](DX12_KNOWN_GAPS.md)）。
- **落とし穴**: Bash のヒアドキュメントの中の Python は、バックスラッシュが半分になる。C++ の `"\n"` や `"\x01"` を書くと、本物の改行・制御文字がソースに入って、コンパイルが通らない（今回、テストのソースで起きた）。Python の補助スクリプトは、Write ツールでファイルにして書く。
- **push はしていない**。

## 見た目の水準と、画像のミップマップ（2026-11-22）

- **方針（ユーザーの指示）**: DX12 は、DxLib と同じ見た目にしない。バックエンドを替えるだけで、全体が格段に綺麗になるのが価値。ただし、公開 API の互換（形・意味・順序・状態・寿命）は保つ。そのために、見た目の水準を 2 つに分けた（`ERenderLook`: `Compat`・`Standard`。`Library/Core/Render/RenderLook.h`）。互換見た目は DxLib の式のまま（画素比較の検査の水準）、標準見た目は綺麗にする。**いまの既定は Compat**。DX12 の起動オプション `--dx12-look=compat|standard`。既定を Standard に替えるのは、機能が揃って、画素比較の検査が compat を明示して通るようになってから（R7b）。ロードマップの R7b は、この「撮る → 評価する → 直す」を繰り返す段階。
- **最初の機能: 画像のミップマップ**: [R7 ミップマップ](R7_MIPMAPS.md)。`NTextureMips`（CPU で、線形の光・アルファの重み・奇数の辺の面積で段を作る）、`IRenderTextureResources::CreateTextureRGBA8Mipped`（既定は通常の作成と同じ）、`CDX12TextureStore`（標準見た目のときだけ、全部の段を転送し、SRV に段の数）、`CTextureAssetStore::Create( ..., bMipmaps )`・`CDX12AssetBackendSubsystem`（画像のファイルから読んだ画像だけ段を持つ）。既存のサンプラー（160 通り）は、`MaxLOD` が最大で、段があれば三線形・異方性として効くので、サンプラーは変えていない。
- **検査**: 契約 `RunTextureMipsTests.ps1`（34 件）、変異 `RunTextureMipsMutationTests.ps1`（26 件）、GPU の絵 `CaptureContractMain.cpp` の `CheckMipmaps`（段なし = 縞が抜けて平均 0、段つき = 一様な 99。DX12・WARP）。全体検査に 3 手順を足した。
- **落とし穴**: (1) 透明な画素の割り戻しを守るガードを外す変異は、NaN が 0 に丸められて結果が同じになる（等価）。(2) 変異検査で、無限ループ・クラッシュになる変異は、「検査が終わらない・落ちる」ことで検出として数える（`FAIL:` の行が出ないため）。(3) 画像の縮小の見た目は、ほとんどのサンプルでは差が出ない（画像を縮小して描く場面が少ない）。見た目の変化は、専用の検査と、これから足す標準見た目の機能で見せる。
- **残り**: 動的な画像・生成した画像・描画先を縮小して描くときのミップ生成（GPU）。サンプルでの異方性の見た目の確認。標準見た目の機能（HDR の場面・トーンマップ・影・環境光・アンチエイリアス・ポスト処理）。
- **push はしていない**。

## 2026-11-22 追記: 影を受ける描画も GPU の Forward へ（Island 9.5 ms → 3.8 ms）
- **測定**: `--profile-frames=240` で測ると、Island の時間の約 6 割は、影を受ける 164 回の描画を CPU で頂点ごとに計算する `Geometry.SubmitMain`（6.1 ms）だった。影を受ける描画は、`CanDrawForward` が「影の絵への描画・影を受ける…は不可」としていたため、すべて CPU の経路だった。
- **変更**: 影を受ける光る描画も、`FForwardMeshDraw` に影の情報（`ShadowMap`・`ShadowMask`・`LightViewProjection`・`ShadowParams` = 影の絵の幅・高さ・補正の深さ）を載せて、GPU の Forward で描く。`ForwardMesh.hlsl`: 影を受けるライト（ライトの `ShadowMapSlots` が、受けるスロットの印と重なる。b6 のスポット角の組の y に、`ShadowMapSlots` を足した）の分は、頂点で `Shadowed` へ別に足し、画素で影の絵（t1。3x3 の平均）と比べた割合を掛けて足す（`ForwardShadowPS` / `ForwardShadowTexturedPS`。式は `BuiltIn.hlsl` の `ShadowFactor` と同じ）。CPU の経路と同じく、影を受ける描画は、画素ごとの光と距離・高さのフォグを使わない。影を受ける描画だけ、パイプラインを別にする（鍵に足した）。影の絵が使えない（`GetShadowPass` が false）スロットは、CPU の経路と同じく、影なしで描く。
- **注意**: 影用のシェーダーは、影の絵（t1）を使わないと、パイプラインの作成が断られ、CPU の経路へ落ちる。シェーダーを変異させて確かめるときは、t1 を使い続ける変異にすること（定数の色を返す変異は、何も検出できない）。
- **検査**: 契約 `ForwardMeshContract`（影を受ける描画が Forward のまま・影の情報が渡る・影の絵として使えない番号は受けない・光なしは受けない・フォグなし・オブジェクト定数 400 バイト）。画像 `RunDX12SampleImageTests.ps1` に、Island（`--island-fixed`: 時計・昼夜・歩き・カメラを止める）の GPU と CPU の経路の比較を足した（40 画素まで許す）。シェーダーの変異 5 種（影の反転・補正の深さなし・上下の反転・影を受けるライトの印を無視・3x3 を 1 行に）で、1.6 万〜19 万画素が違うことを確かめた。
- **残り**: 影を落とす描画（`Geometry.ShadowCaster` 1.2 ms / 99 回）は、まだ CPU の経路。

## 2026-11-22 追記: 影を落とす描画も GPU の Forward へ（Island 3.9 ms → 3.1 ms）
- **測定**: 影を受ける描画を GPU へ移したあと、Island で最も重かったのは、影の絵へ深度を書く 99 回の描画（`Geometry.ShadowCaster` 1.3 ms）だった。頂点を CPU で光のクリップ座標へ変換し、毎回、組み込みのシェーダー（`Quad3D` + `ShadowDepth`）を同期で呼んでいた。
- **変更**: `FForwardMeshDraw::bDepthOnly` を足した。true の描画は、`ClipMatrix`（= ワールド × 光の射影。画面のずれは掛けない）だけを使い、深度だけを書く（`ForwardMesh.hlsl` の `ForwardDepthVS` / `ForwardDepthPS`。`ShadowDepthMain` と同じ式）。パイプラインは、鍵の先頭を 2 にして別にする。b0（個体の定数）だけを使うので、パイプラインの枠も b0 の 1 つだけにする（宣言した枠は、すべて使われている必要がある）。深度だけの描画は、描画先が R32Float のときだけ受け付ける。`CGeometryForwardMesh::DrawShadowDepth` が、共有のモデルの範囲ごとに呼ぶ。画像つきの範囲・半透明（不透明度 250 未満）は、従来どおり、影を落とさない（何も描かず、描けた数に数える）。`CGeometryVertexSubmitter::SubmitShadowCaster` は、一度だけ描く三角形（地形・木など）も、GPU が受け取れば、フレームの転送領域から描く。断られたら、従来の CPU の経路。
- **不具合の修正**: 描画スレッドを使う面（`CThreadedRenderSurface`）は、`GetColorFormat()` を実装しておらず、いつも RGBA8 を返していた。そのため、Forward は、R32Float（影の絵）や RGBA16F（HDR）の描画先でも、RGBA8 のパイプラインを選び、断られて CPU の経路へ落ちていた。描画先のパスを始めるときに、描画スレッド側の実際の形式を覚えるようにした。
- **検査**: 契約 `ForwardMeshContract`（影を落とす描画が深度だけで GPU の経路・クリップ行列・状態・画像つきの範囲は落とさない・半透明は落とさない・GPU が断ったら CPU の経路）。Island の GPU と CPU の画像の比較（影を落とす側の変換が違えば、影の位置が変わる）。C++ の変異 3 種（光の行列なし・深度の書き込みなし・比較を反転）で確かめた。

## 2026-11-22 追記: 描画先パスの始まり・終わりを待たずに積む（Deferred 6.8 ms → 4.0 ms、PostEffects 4.2 ms → 3.5 ms）
- **測定**: Island 以外の Deferred（6.8 ms）・PostEffects（4.2 ms）は、描画スレッドの忙しさ（約 3 ms）に、ゲームスレッドの仕事（約 3 ms）が足されていた。`--dx12-frame-timings` に、**同期呼び出しの呼び出し元の名前ごとの集計**を足した（`ExecuteRender` と `CThreadedRenderSurface::Execute` が、`__builtin_FUNCTION()` で呼び出し元の関数名を渡す。`CRenderCommandThread::NotifyFrameEnd` が、待った時間の長い順に 8 件を出す）。Deferred は、毎フレーム 28 回の同期呼び出しで 3.1 ms 止まっていて、`BeginRenderTargetPass` が 9 回で 2.2 ms、`EndRenderTargetPass` が 9 回で 0.4 ms だった。`Begin` が重いのは、描画スレッドが、そのパスの準備（コマンドリスト・バリア・クリア）を、ゲームスレッドの待つ間にやっていたため。
- **変更（ゲームスレッドが待たない）**:
  1. `BeginRenderTargetPass`: 同期で成功した組み合わせ（描画先・深度の画像・倍率・深度の消去値）の「確かめ済み」（実寸・形式・窓口）を覚え（`FPassProof`）、同じ組み合わせの繰り返しは、待たずに積む（実寸・形式は、確かめたときの値）。実行先が断ったときは、描画スレッド側の印（`bRejectedPass`）で、**終わりまでの描画をすべて捨てる**（主画面へ流さない。`GetTargetCanvas` / `GetTargetGeometry` が nullptr を返し、`ExecuteBatch` と積んだ描画は何もしない）。断った回数は `GetNumRejectedPasses()`、ログは警告（回数が 2 の冪乗の回だけ）。
  2. `EndRenderTargetPass`: 待たずに積む（窓口は、フレームの始まりに調べた値へ戻す）。実行先で失敗したら、ログと `GetNumFailedPassEnds()`（フレームは落とさない。以前の同期の失敗も、フレームは落とさなかった）。**ただし**、パスの中で、同期の描画・コピー・GPU タイムスタンプの呼び出しが false を返した、または GPU タイムスタンプが閉じていないとき（`bPassTainted` / `NumOpenPassTimestamps`）は、終わりが失敗しうる（自己参照の競合など）ので、従来どおり、同期で実行して、本当の結果を返す（`graphics-pipeline-mrt` の自己参照のパスが、終わりの false を確かめている）。**注意**: 待たずに積んだ終わりの戻り値は、常に true。ゲームスレッドから分からない失敗（文字の描画の失敗・デバイスの異常）は、`CRenderGraphBuilder::Execute` の `end render target pass failed` ではなく、描画スレッドのログになる。`SetQueuedDrawsEnabled( false )` のときは、従来どおり、すべて同期で、本当の結果を返す。
  3. `SetTextureFilter`: 補間設定への対応の答えを、描画先ごとに覚える（`FFilterProof`）。以前は、パスが変わるたびの最初の 1 回が同期で、Deferred では 1 回に 2 ms の待ちが集まっていた（ほかの待ちを消すと、この待ちが、描画スレッドの遅れ全部を受けるため）。
  4. `CDX12PlatformSubsystem::BeginFrame / EndFrame` が、毎フレーム、待たずに積む描画の「確かめ済み」を捨てていた（`ExecuteRender` の既定が、捨てる）。フレームの境目は、資源の作成・解放ではない（番号が使い回される前には、必ず作成・解放の呼び出しがあり、そこで捨てる）ので、`bMayChangeTextureViews = false` で呼ぶ。これで、描画の「確かめ済み」（`CAsyncDrawGate`）も、設計どおり、フレームをまたいで使える（以前は、毎フレーム、最初の描画が同期だった。契約 `TestQueuedDraws` の「proofs survive into the next frame」は、フレームの境目だけを試していて、本番の経路が捨てていた）。
- **Deferred サンプル**: GPU で一覧を作る設定でも、CPU の一覧（統計の画面表示のため）を毎フレーム作っていた（`CTileLightGrid::Build` 1.05 ms + `WriteImage` 0.68 ms）。GPU の経路では、30 フレームに 1 回だけ作る（検査のログを出すとき・CPU の経路・まだ作っていないときは、従来どおり毎フレーム）。`LightCulling.GridBuild` / `LightCulling.GridWriteImage` の計測の範囲を足した。
- **残り**: Deferred の `Platform.EndFrame` の 2 ms は、描画スレッドの遅れ（忙しさ 2.9 ms）。次の候補は、`GetTextureResourceView`（UI で 46 回/フレーム・Village で 11 回。`TextureViews` を、フレームの境目・パスの終わりで捨てているが、成功した答えは、描画先のパスの始まり・資源の変更でしか変わらない）。
- **検査**: 契約 `RenderThreadContract` の `TestQueuedPasses`（始まり・補間設定・終わりが、実行を止めていても受け付けられる／順序／複数指定の描画先の複写／断られたパスの描画を捨てる・終わりを実行先へ渡さない／終わりの失敗の計数／倍率の違い・資源の変更・待たない設定は同期）。失敗した同期の描画・閉じていないタイムスタンプで、終わりが同期になること。C++ の変異 11 種（断られたパスの描画を捨てない・倍率を見ない・資源の変更で捨てない・計数 2 種・補間設定の描画先を見ない・複数指定の複写なし・寸法をフレームから取る・断られたパスの終わりを実行先へ渡す・失敗を印にしない・タイムスタンプを数えない）が、すべて検出された。全検査の 75 手順のうち、`graphics pipeline contracts`（`graphics-pipeline-mrt` が、自己参照のパスの終わりの false を期待）が、最初の版で落ちたので、上の「ただし」を足した。**注意**: この契約の実行ファイルは、ビルド済みの `mazigatiDX.lib` にリンクするので、変異のたびに `build_cfg.ps1 -Target mazigatiDX` が要る（再ビルドしないと、変異が効かず、通ってしまう）。

## 2026-11-22 追記: GPU 時間の自動計測（`--dx12-gpu-timings`）と、「GPU は空いている」という測定結果
- **目的**: R4 の「GPU 時間の表示」と、R2 から求められている「CPU / GPU の各パスの測定」。これまで、GPU が何ミリ秒働いているかは、フレームの間隔（と GPU の完了待ち）からの推測だった。
- **作り**: `CDX12GpuSegmentTimer`（`Library/Platform/DX12/Resource/Timestamp`）。既存の `CDX12TimestampStore`（R1 のタイムスタンプ）を借りて、**公開の窓口（`IRenderSurface::BeginGpuTimestamp` など）を通さず、描画スレッドの中で**、(1) 主画面の区間（フレームの始まり、または描画先パスの始まりで区切られた、次の区間。`CDX12Device::BeginFrameForBuffer` / `SubmitFrameSegment` / `SubmitFrame`）と、(2) 描画先パスごと（`CDX12RenderTargetPass::Begin` の最後 / `End` の最初）を、自動で測る。ゲームスレッドの同期呼び出しを増やさないので、前の節の「待たずに積む」を崩さない（公開の窓口の `BeginGpuTimestamp` は、閉じていないと終わりが同期になる）。付けないとき（既定）は、問い合わせも作らず、何もしない。
- **出力**: 120 フレームごとに、ログへ「合計（平均 / 最大）・主画面・パスごと（寸法・色の枚数・深度の有無）」。毎フレームの合計は、プロファイラーのカウンター `Gpu.Frame` / `Gpu.Main` / `Gpu.Passes`（ミリ秒）へ出す（プロファイラーのウィンドウの Counters タブ、Chrome トレース、`--profile-frames` の要約の最後の `counter Gpu.*: average ...` に出る。`ProfilerSubsystem::LogCounterSummary`。ほかのカウンターは数が多いので要約には出さない）。結果は、GPU の完了後に、数フレーム遅れて届く（`Pending` と 16 フレームの輪で、全部の区間がそろったフレームの合計を確定する。8 フレーム待っても届かない区間は捨てる）。パスは、そのフレームでの順番（0 から）で並ぶ。
- **測定**（Release・実 GPU・定常の 120 フレーム。`--dx12-frame-timings` と一緒に出した。GPU = 区間ごとの時間の合計 / フレームの間隔）:

|サンプル|GPU（主画面 + パス）|フレームの間隔|内訳|
|---|---|---|---|
|Deferred|0.50 ms（0.01 + 0.49）|3.6 ms|G-buffer（1280x640・4 色 + 深度）0.27、480x320 のパス 0.11、1 色のパス 0.05 と 0.03、ほかは 0.01 前後|
|Island|0.23 ms（0.18 + 0.04）|3.1 ms|影の絵（2048x2048）0.04、主画面 0.18|
|PostEffects|0.14 ms（0.02 + 0.12）|3.2 ms|2 色のパス 0.06、640x320 のパス 7 つが 0.01 ずつ|
|Village|0.07 ms|3.1 ms||
|UI|0.03 ms|3.1 ms||

- **読み取り**: GPU が働いている時間は、どのサンプルでも、フレームの 1〜14%（0.03〜0.5 ms）。残りは、CPU の記録（ゲームスレッドと描画スレッド）と、フレームの上限。つまり、**いまのサンプルの規模では、GPU の最適化（ライトカリングの compute 版・命令の並列記録など）は費用に見合わない**。効率の仕事は、CPU 側（前の節の「待たずに積む」・ゲームスレッドの仕事）を続ける。GPU 側の測定は、重い場面（多数のライト・大きな解像度・R7 の効果）を足したときに、ここで測り直す。
- **注意**: 合計は、区間ごとに測った時間の足し算で、区間のあいだの空き・バリアの遷移・表示は含まない（「GPU が何ミリ秒働いたか」）。compute の提出（`SubmitComputeWithTimestamp`）と、撮影のための描画は、測らない。区間の始まりは、パスの準備（色・深度のクリア）の後。
- **検査**: `RunGpuTimingsTests.ps1`（全体検査の `gpu pass timings`。Deferred を `--dx12-gpu-timings` で動かし、ログに合計・主画面・3 つ以上のパス・G-buffer のパス（4 色）が出ること、問い合わせの取りこぼしが 0 であること、`Gpu.Frame` / `Gpu.Passes` の平均が要約に出ること、付けないと何も出ないことを確かめる）。計測器の変異 5 種（パスを測らない・問い合わせを解放しない・`Gpu.Passes` を出さない・付けなくても有効・色の枚数の誤り）は、すべて検出された（問い合わせを解放しない変異は、256 件の上限で取りこぼしが出る）。
- **残り**: サンプルの画面への表示（Deferred の画面に、方式ごとの GPU 時間を重ねる）は、まだ。いまは、プロファイラーのウィンドウとログで見る。compute と撮影の区間の計測、パスの名前（いまは順番と寸法・色の枚数）も、必要になったら。

## 2026-11-22 追記: 画像の view の覚えを、フレームの境目・パスの終わりで捨てない（UI の同期呼び出し 49 → 4 回/フレーム）
- **測定**: `--dx12-frame-timings` の同期呼び出しの内訳で、UI は `GetTextureResourceView` が 45〜46 回/フレーム、Village は 8〜11 回/フレーム。ゲームスレッドの覚え（`CTextureViewCache`）が、毎フレーム、空になってから、同じ画像を全部、描画スレッドへ聞き直していた。
- **原因**: 覚えを捨てる場所が、`CDX12PlatformSubsystem` の `BeginFrame`・`EndFrame` の**2 か所**・`BeginRenderTargetPass`・`EndRenderTargetPass`・撮影の始まりと終わりにあった。前の節でフレームの境目の「確かめ済み」（`CAsyncDrawGate`）は捨てなくしたが、画像の view の覚えは、`BeginFrame` と、`EndFrame` の先頭（`ThreadedSurface->EndFrame()` の前。開いたままの描画先パスを閉じる所）で、まだ捨てていた。最初に 1 か所だけ直したときは、効果がほとんどなかった（UI 46 → 45）。一時のログ（`CTextureViewCache::Invalidate` に、中身の数を出す）で、もう 1 か所を見つけた。
- **view の答えが変わる条件**: `CDX12TextureStore::GetTextureResourceView` の答えは、(1) その画像の登録（作成・更新・解放。ゲームスレッドの窓口の呼び出しで、`ExecuteRender` の既定が全部捨てる）と、(2) 「いま書き込み中の描画先か」（`IsActiveTarget`）でしか変わらない。成功した答えだけを覚えるので、書き込み中の「ない」は覚えない。したがって、(a) フレームの境目と、(b) 描画先パスの終わりでは、何も捨てなくてよい。(c) パスの始まりでは、そのパスが書く描画先・深度（`Desc.ColorTarget`・`Desc.ColorTargets`・`Desc.DepthTarget`）の覚えだけを捨てる（`CTextureViewCache::Remove`、`ForgetPassTargetViews`）。ほかの画像の答えは、パスで変わらない。撮影の始まり・終わりは、全部捨てるまま。
- **結果**（Release・実 GPU）: 同期呼び出し（1 フレーム）は、UI 49 → 4（`GetTextureResourceView` 46 → 1）、Village 14 → 4（11 → 1）。残りの 1 回は、パスの描画先が、パスのあとで読まれるときの、最初の問い合わせ。次の候補は、パスの始まりで「隠す」だけにして、終わりで戻す（値は変わらない）こと。
- **注意（測り方）**: フレームの間隔は、UI・Village・Island で、いつも約 3.1 ms で、この変更で動かなかった（ゲームスレッドの `open` は UI で 1.43 → 0.79 ms に減ったのに）。最大 FPS は 0（無制限）で、人工の上限ではない。原因は VSync と環境の上限だった（次の節）。UI の `BeginFrame` の GPU 完了待ち 1.5〜1.7 ms は、前のフレームの表示の待ち。**CPU 側の効率の効果は、間隔ではなく、`open`・描画スレッドの忙しさ・同期呼び出しの回数で見る**。また、測定の途中で OBS が動いていて（GPU の取り込み）、Deferred・PostEffects の間隔が 3.2 → 4.4 ms、描画スレッドの忙しさが 2.7 → 3.9 ms に見かけ上悪化した（view を使わないサンプルなので、この変更の影響ではない）。測り比べは、外部の負荷（OBS・録画・ブラウザ）が無いときに行う。
- **検査**: 契約 `RenderTargetPlatformSelfTest`（パスの前に覚えさせた描画先の view が、パスの間は返らない・同じパスの間に、ほかの画像の view は同じものが返る・パスの終わりで、前と同じ view が返る）と、`GraphicsPipelinePlatformSelfTest`（複数指定の描画先の 2 枚とも、パスの間は返らない）。C++ の変異 3 種（パスの始まりで捨てない・複数指定の描画先を捨てない・単一の描画先を捨てない）は、すべて検出された。**注意**: `-Platform` の検査は、事前にビルドした `x64\<構成>\project.exe`（検査のソースを含むアプリ）を動かす。`build_cfg.ps1 -Target Samples` / `mazigatiDX` では、`project.exe` も、検査の変更も、反映されない（最初の変異の実行は、古い `project.exe` を使って、3 種とも「通った」）。変異のたびに `-Target Build` で作り直す（`RunAllChecks.ps1` は、`-SkipBuild` なしなら作り直す）。フレームの境目で捨てない効果は、結果が同じ（速度だけ）なので、契約では確かめられない（同期呼び出しの回数で測る）。

## 2026-11-23 追記: フレームの間隔 約 3.1 ms の正体（VSync と、環境の上限。DX12 の実装ではない）
- **疑問**: UI・Village・Island のフレームの間隔が、いつも約 3.1 ms で、CPU 側の改善（同期呼び出し 49 → 4、`open` 1.43 → 0.79 ms）で動かなかった。UI は GPU の仕事が 0.03 ms なのに、`BeginFrame` の GPU 完了待ちが 1.5〜2 ms あった。
- **1 つめの答え（VSync）**: Samples は VSync オン（`FWindowSettings::bVSync` の既定が `true`。起動ログに `vsync on`）。この機械の NVIDIA のディスプレイは 319 Hz（1 周期 3.13 ms）で、DX12 の `Present( 1, 0 )` が、その周期に揃える。前のフレームの表示の待ちが、次の `BeginFrame` の GPU 完了待ちに見えていた（UI で 2.0 ms）。
- **2 つめの答え（環境の上限）**: 起動オプション `--no-vsync`（`GameMain`。`bVSync` を切る。DxLib・DX12 の両方に効く）を足して測ると、VSync を切っても、間隔は動かない。UI は 2.94 ms（最大 3.0〜3.1。`Present` が 1.9 ms 止まり、`open` 0.78 + GPU 完了待ち 0.25 + `Present` 1.9 = 間隔）。同じ UI を DxLib（D3D11）で `--no-vsync` にしても、**2.93 ms（341 FPS。最小 2.83・最大 3.09）**。API の違う、DX12 を通らない経路でも同じ値なので、約 3 ms の下限は、DX12 の実装の外側（ディスプレイ・ドライバー・OS のどれか。原因の特定はしていない）にある。
- **効かなかった実験**（どれも間隔と `Present` の待ちが変わらず、戻した）: ティアリングを許す（`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` と `DXGI_PRESENT_ALLOW_TEARING`。対応は確認済みで、有効だった）、交換用画面を 2 枚 → 3 枚、窓を表示する（今までの測定は `-WindowStyle Hidden`）、枠なし全画面（`Viewport.WindowMode: Borderless`）。
- **測定の表**（Release・実 GPU・OBS が動いたまま。`--no-vsync`。ミリ秒）: UI 2.94（`open` 0.78）、Village 3.05（1.83）、Island 3.00（2.22）、Deferred 3.6〜3.7（3.3）、PostEffects 3.3〜3.5（3.25）。VSync オンは、UI 3.13・Village 3.12・Island 3.22・Deferred 3.9・PostEffects 3.3。
- **読み方（今後の測定の約束）**: (1) 間隔が約 3 ms のサンプル（UI・Village・Island）は、ゲームスレッドと描画スレッドの仕事が、間隔より短い（下限に隠れている）。CPU 側の効果は、間隔ではなく、`open`・描画スレッドの忙しさ・同期呼び出しの回数で見る。(2) 間隔が 3.3 ms を超えるサンプル（Deferred・PostEffects。`open` が 3.1〜3.4 ms）は、ゲームスレッドの仕事が間隔を決めている。ここは、CPU 側の改善が、間隔に出る。(3) 下限が約 3 ms なので、3 ms より速いフレームは、間隔では比べられない。
- **検査**: `RunGpuTimingsTests.ps1` が、`--no-vsync` を付けた起動で `vsync off`、付けない起動で `vsync on` が、起動ログに出ることを確かめる。

## 2026-11-23 追記: 描画先パスの開始を軽くする（描画スレッドの忙しさ 2.9 → 0.7 ms）
- **測定**: `--dx12-frame-timings` に、描画スレッドで**実行した仕事の名前ごとの時間**（`render task <名前>: N tasks/frame, ran X ms/frame`）を足した（`CRenderCommandThread::MakeTimed` が、仕事の名前と実行時間を集計する。同期呼び出しは、呼び出し元の関数名）。PostEffects は、描画スレッドの忙しさ 2.9 ms のうち、`Render.PassBegin` が 11 回/フレームで **2.2 ms（1 回あたり約 200 µs）**、Deferred は 9 回で 1.9 ms だった（GPU の仕事は 0.14〜0.5 ms なので、CPU の記録の側が重い）。
- **内訳**（一時の段ごとの計測。`CDX12RenderTargetPass::Begin` 1 回あたり）: 内部深度の用意（`PrepareFallbackDepth`）約 93 µs、検証・ストア・完了済みの提出レコードの破棄（`CollectCompleted`）約 60 µs、コマンドアロケーター + コマンドリストの作成 約 50〜65 µs、ほかは合わせて約 6 µs。
- **原因 1**: 内部深度（深度を指定しないパスが使う D32）を**1 枚だけ**持ち、パスの寸法が前のパスと違うと、毎回作り直していた（`CreateDescriptorHeap` + `CreateCommittedResource`）。PostEffects・Deferred は、寸法の違うパスを毎フレーム 9〜11 回繰り返す。**直し**: 寸法ごとに覚えて（`FallbackDepths`。数 32・合計 128 MB まで。古い順に手放す）、寸法が変わったら、覚えた深度へ切り替える。切り替えた直後は、作り直したときと同じく、`bClearDepth` が false でも、一度初期化する（内容が不定なため。`bFallbackInitialized = false`）。同じ寸法の続きでは、今までどおり、Load で深度が保たれる。
- **原因 2**: パスごとに、コマンドアロケーターとコマンドリストを作り、GPU 完了後に捨てていた（毎フレームの `CollectAfterGPUIdle` で、全部まとめて破棄）。**直し**: 完了した提出レコードから取り出して、置き場（`FreeRecordings`。16 組まで）へ移し、次のパスで、`Reset` して再利用する（デバイスの `BeginFrame` と同じ作り。`RecycleRecordings`・`AcquireRecording`）。完了の確認（フェンス）を通ったものだけを渡す。
- **結果**（Release・実 GPU・OBS が動いたまま。`--no-vsync`）: `Render.PassBegin` は 1 回あたり約 200 → 13 µs。描画スレッドの忙しさは、PostEffects 2.9 → 0.7 ms、Deferred 2.85 → 0.95 ms。ゲームスレッドの `open`（フレームを開いてから提出まで。描画スレッドの完了待ちを含む）は、PostEffects 3.1 → 0.75 ms、Deferred 3.3 → 1.35 ms。フレームの間隔は、両方とも、環境の下限（約 2.95 ms。前の節）に届いた（PostEffects 3.3 → 2.94、Deferred 3.5 → 2.96〜3.0）。Deferred の最初の 120 フレームの窓だけ、最大が約 570 ms になるのは、起動時の 1 回で、変更前のコードでも同じように出る（この変更の影響ではない）。
- **検査**: 契約 `RenderTargetPlatformSelfTest` に `CheckFallbackDepthSwitch`（8x8 で手前の面を描く → 4x4 のパスをはさむ → 8x8 へ戻って、Load の指定で奥の面を描くと見える（深度を持ち越さない）→ 同じ寸法の続きで、さらに奥の面は隠れる（Load で深度が保たれる））。この検査は、変更前のコードでも通る（今までの意味を守る）。C++ の変異 3 種（切り替えで初期化しない・同じ寸法の続きで初期化する・コマンドリストを Reset しない）が、すべて検出されることを確かめた（切り替えで初期化しない変異は、この検査が `after return 9,8,7 (expected 10,200,10)` で捉える。同じ寸法の続きで初期化する変異は、既存の契約の `implicit depth Load preserves prior near geometry` も捉える）。コマンドリストを、GPU 完了前に再利用する誤りは、この機械では速すぎて起きないので、検査では捉えられない（Debug のレイヤーで警告になる）。

## 2026-11-23 追記: 画像の更新を、完了を待たずに積む（PixelEffects の間隔 18.2 → 3.0 ms）
- **測定**: 全 36 サンプルを `--no-vsync` で測ると（`measure_all` 相当。間隔・`open`・描画スレッドの忙しさ）、ほとんどが環境の下限（約 2.94 ms）に張り付き、下限を超えたのは PixelEffects（**18.2 ms**・描画スレッドの忙しさ 15.9 ms）、Character（5.1）、Pixels（5.0）、Camera（4.2）、RenderTarget（3.2）だった。PixelEffects は、描画スレッドの仕事の名前ごとの時間で、`UpdateTextureRGBA8` が 20.3 回/フレームで **13.1 ms**（1 回あたり 0.64 ms）。ピクセル効果が、毎フレーム、CPU で作った画像を全部更新する。
- **内訳**（一時の段ごとの計測。1 回あたり）: テクスチャの committed resource の作成 約 215 µs、アップロード用の committed resource の作成 + Map 約 177 µs、画素のコピー 1.3 µs、命令の記録 15 µs、提出 + **GPU 完了待ち** 約 150 µs。GPU の仕事は、ほぼゼロで、資源の作成 2 回と完了待ちが、ほぼ全部。
- **直し**: 新しい役割のクラス `CDX12TextureUpdater`（`Resource/Texture/`）が、読み取り用の段なし RGBA8 画像の更新を、完了を待たずに積む。(1) 更新で外れた旧版（資源と記述子）を、GPU 完了後（フレームの境目の `CollectAfterGPUIdle`）に預かり（`Adopt`。128 枚・64 MB まで）、次の更新で寸法が同じものを再利用する（読み取り → 転送先 → 読み取りのバリアを記録）。なければ `CDX12TextureCreator::CreateEmptyRGBA8` で作る。(2) 転送領域（UPLOAD バッファ）は、開いたままマップして使い回す（128 個・64 MB まで）。(3) コピーは、専用のコマンドリスト（これも使い回す）に記録して、すぐ同じキューへ提出し、**待たない**。同じキューなので、そのあとに積む描画より先に実行される。提出済みの転送領域・命令列は、次の GPU 完了後に戻す。(4) 1 フレームの更新が 512 個・128 MB を超えたら、今までの、完了を待つ経路へ戻す（結果は同じ）。ミップマップつき・compute 画像は、今までの経路のまま。
- **更新の意味は変わらない**: 更新の前に記録した描画は、更新前の版を参照し続ける（旧版は、GPU 完了後にしか再利用しない）。`CDX12TextureStore` は、更新で外れた旧版を `RetiredVersions`（登録解除した画像の `Retired` とは別）に入れ、GPU 完了後に、再利用できなければ破棄する。
- **結果**（Release・実 GPU・OBS が動いたまま。`--no-vsync`）: PixelEffects の間隔 **18.2 → 2.97 ms**（約 6 倍。環境の下限）、描画スレッドの忙しさ 15.9 → 0.73 ms、`UpdateTextureRGBA8` 13.1 → 0.52 ms/フレーム（1 回約 26 µs）。Pixels は 5.0 → 4.1 ms（残りは `Render.CanvasBatch` 2.8 ms）。PixelEffects の `open` は 2.77 ms で、次は、ゲームスレッドの効果の計算（CPU）が支配する。
- **検査**: 契約 `RenderTargetPlatformSelfTest` に、更新を繰り返す画像（4x4 と、寸法の違う 8x2）の検査を足した。毎フレーム、描画 → 更新 → 描画 → 更新 → 描画と積み、各場所に、描いたときの版の色が出ること（更新の前に記録した描画は更新前の色。前のフレームから持ち越した版を含む）、2 フレーム目には 600 回の更新（予算超過の経路）を行って、結果が同じことを確かめる。`RunRenderTargetTests.ps1` の DX12 のモードに `--dx12-debug-layer=true` を足し、デバッグレイヤーの警告・エラーが 1 件でもあれば失敗にした（資源の状態の誤りを捉える。今までは、プラットフォームの検査でレイヤーを使っていなかった）。変異の確認（Release）: 常に完了を待つ経路（通る＝検査は経路に依存しない）、同じフレームでの旧版の再利用・コピーの省略・行のピッチの無視・読み取りの状態へ戻さない・転送先の状態へ移さない、の 5 種が、すべて検出された（最後の 2 種は、デバッグレイヤーの追加で初めて検出された）。
- **残り**: Pixels（`Render.CanvasBatch` 3 回で 2.8 ms）、Character（5.1 ms。描画スレッド 2.7 ms・23 タスク）、Camera（4.2 ms。408 タスクで 2.8 ms）、RenderTarget（3.2 ms。2.4 ms）。

## 2026-11-23 追記（夜）: Debug の速さ・シェーダー描画・文字・バッファ作成（Debug の Deferred 11.3 → 3.1 ms。残りの重いサンプルも下限へ）
- **きっかけ**: 利用者が「Deferred が、とても重かった」と記憶していた。VS の F5 で動かす **Debug 構成**を測ると、Deferred の間隔が 11.3 ms、ゲームスレッドの `open` が 11.1 ms（Release は 3.0 ms の下限）。全 36 サンプルを Release で測り直すと、下限を超えていたのは Character（5.1）・Pixels（4.1）・Camera（4.2）・RenderTarget（3.2）・Island（3.2）。
- **サンプリング・プロファイラ**（私用の道具。リポジトリには入れていない）: 別プロセスから、ゲームスレッドと描画スレッドのコールスタックを一定間隔で取り、PDB の関数名で集計する（`sampler.exe`。CPU を使っているスレッドだけを止める）。以降の調査は、これで関数ごとの割合を見て決めた。
- **1. Debug の設定**（`Build/mazigatiDX.props` の Debug）: `/JMC`（Just My Code）を切り（`SupportJustMyCode=false`）、`/Ob1`（`InlineFunctionExpansion=OnlyExplicitInline`）にした。Debug の Deferred のゲームスレッドの 23.8% が `__CheckForDebuggerJustMyCode`（全関数の入口の呼び出し）で、`/Od` のままだと `FORCEINLINE` も展開されず、`TArray` の添字・`IsFinite` などの小さな関数が、呼び出しのまま残っていた。**Debug の Deferred 11.27 → 5.02 ms、Island 8.67 → 6.16 ms**。ブレークポイントと変数の確認は、今までどおり使える。
- **2. シェーダー描画の CPU 処理**（`Platform | Scene3D` 285 回/フレーム。Debug で 1 回 28 µs）: (a) `CShaderCommandExecutor::Execute` の頂点・添字のループを、1 回の走査にした（有限判定は、浮動小数点の指数部のビット検査。添字は `Order` へ直接書き、`Expanded = MoveTemp( Order )` で複写しない）。(b) `CDX12ShaderService::DrawShader` が、物体ごとの定数（Deferred は光 64 個分で数 KB）を、1 バイトずつ `TArray` の添字で複写し、`TArray<TArray<uint8>>` を毎回確保していたのを、1 つの作業領域（`ConstantScratch`）へ並べて 1 回で複写するようにした。(c) `GetPipeline` が、描画のたびに、14 個の整数を `printf` で文字列にして鍵を作っていたのを、直前の描画と入力が同じなら前回の答えを返す覚え（`FPipelineInputs`）にした（シェーダーの削除・終了で捨てる）。**Debug の Deferred 5.02 → 3.11 ms（下限）**。
- **3. 文字**（Pixels。描画スレッドの 31% が GDI の `GetGlyphOutlineW`）: 毎フレーム内容が変わる文字列（`Canvas.Get( 64, 64 ) = ( R .., G .. )` のような値の表示）は、文字列全体のキャッシュに当たらず、毎フレーム、全文字を GDI でラスタライズし直していた。`CWindowsGdiText` に、**フォントごと・文字ごとのグリフキャッシュ**（被覆率と送り幅。16384 文字・32 MB まで）を足した。また、OS に描かせる文字が無いとき（標準のなめらかさで、UTF-16 で 2 つの文字が無い）は、`CreateDIBSection` と `GdiFlush` をしない（`NeedsSystemDraw`・`DrawSystemGlyphs`）。**Pixels 4.14 → 2.94 ms**。
- **4. バッファの作成**（Character。描画スレッドの 35% が `CDX12BufferStore::CreateBuffer`。CPU スキニングで毎フレーム変わる頂点を、毎フレーム新しい GPU バッファに作り直し、資源の作成 + 転送 + GPU 完了待ちをしていた）: 転送領域（UPLOAD バッファ）と命令列の使い回しと、完了を待たない提出を、共通のクラス `CDX12CopyStaging`（`Resource/Upload/`）に切り出し、テクスチャの更新（`CDX12TextureUpdater`）に続いて、バッファの作成（`CDX12BufferStore::CreateQueued`。用途なし・16 MB 以下）が使う。登録解除したバッファは、GPU 完了後に預かり、次の作成で、同じ大きさのものを使い回す（64 個・64 MB まで）。**Character 5.12 → 2.95 ms、Camera 4.15 → 3.02 ms**。
- **約束の確認**: `CollectAfterGPUIdle()` は、**GPU が空のときにだけ**呼ぶ（使い回しが、この約束に頼る）。`BufferGeometryContract` は、フレームの途中で呼んでいたので、回収の前に、小さなバッファの読み戻し（キューの完了待ち）を入れた。また、回収した資源の使い回しで「別々の資源」でなくなるため、ピンの数の検査は、小さなバッファの大きさを少しずつ変えるようにした。
- **全サンプルの実測**（Release・`--no-vsync`。間隔 ms。変更前 → 後）: PixelEffects 18.2 → 2.97、Character 5.1 → 2.95、Pixels 5.0 → 2.94、Camera 4.2 → 3.02、RenderTarget 3.2 → 2.97、Island 3.2 → 2.95、Deferred 3.5 → 2.94。ほかのサンプルは、もともと下限（2.94）。Debug は、Deferred 11.3 → 3.1、Island 8.7 → 6.2。
- **検査**: 契約 `BufferGeometryPlatformSelfTest` に、CPU で作る（完了を待たずに積まれる）頂点バッファを、同じフレームの中で「作る → 描く → 解放 → 同じ大きさで作る → 描く」と繰り返し、先に記録した描画が、後から作ったバッファの内容に化けないことを、画素で確かめる検査を足した。`RunBufferGeometryTests.ps1` の DX12 のモードにも、デバッグレイヤー（`--dx12-debug-layer=true`）を足した。変異（Release）: 解放した資源を同じフレームで再利用・コピーの省略・提出した命令列を完了前に戻す（デバッグレイヤーが「アロケーターのリセット」で捉える）の 3 種が検出された。転送後に COMMON へ戻すバリアの省略は、検出できなかった（バッファは、D3D12 の規則で暗黙に COMMON へ戻るため。同期の経路と同じ作りなので、バリアは残す）。
- **残り**: Character の CPU スキニング（`NModelRig::Deform`）、RenderTarget の描画スレッド（1.98 ms）、Island のテッセレーション（形状を毎フレーム CPU で作る）、Camera の 409 タスク。

## 2026-11-23 追記（夜、続き）: スキニングの並列化・転送描画のパイプラインの鍵・画像の新規作成（Debug の全サンプルを測り直した）
- **Debug の全サンプル**（`--no-vsync`。間隔 ms）: 下限（2.94）を超えたのは、Character 8.8・Adventure 8.7・Camera 7.5・PixelEffects 7.1・Island 6.0・World 5.1・Shapes3D 5.1・Platformer3D 4.2・Effects 3.8・RenderTarget 3.8。以降、サンプリングして、上から直した。
- **1. スキニングの並列化**（Character。Debug のゲームスレッドの 43% が `NModelRig::Deform`）: `NModelRig::Deform` に、任意の `IJobSystem*` を足した。頂点が 4096 個を超えるとき、頂点を範囲に分けて、`ParallelForRange` で並列に変形する（各頂点は自分の出力だけを書くので、結果は、ワーカーの数によらず、1 ビットも違わない）。`CStaticModelStore::SetJobSystem` を、`CDX12AssetBackendSubsystem::Initialize` が、`InitializeDependency<IJobSystem>()` で渡す。あわせて、`BuildDeformed` が、形の添字を 1 つずつ足していたのを、まとめて複写するようにした。**Character の Debug 8.76 → 4.36 ms、Release の `open` 2.76 → 1.87 ms**。
- **2. 転送描画のパイプラインの鍵**（Adventure。Debug のゲームスレッドの 34.6% が `CDX12ForwardMeshRenderer::GetPipeline` の `FString::Printf`）: 描画のたびに、18 個の整数を文字列にして鍵を作っていた。入力の値を覚え、直前の描画と同じなら、前回の答えを返す（シェーダー描画の `GetPipeline` と同じ作り）。**Adventure の Debug 8.70 → 6.17 ms、World 5.08 → 3.55 ms、Platformer3D 4.21 → 3.59 ms、Island 5.97 → 5.05 ms**。
- **3. 画像の新規作成**（Camera。描画スレッドの 25% が、毎フレーム新しい文字列の画像を作る `CDX12TextureStore::CreateTextureRGBA8` の、資源の作成 + 完了待ち）: 画像の更新（`CDX12TextureUpdater`）の、完了を待たずに積む作成を、新規作成（読み取り用・段なし）にも使い、解放した画像（`ReleaseTexture`）も、GPU 完了後に、次の作成・更新の使い回しへ預ける。**Camera（Release）の描画スレッド 1.24 → 0.67 ms、`open` 2.35 → 1.76 ms。Pixels の `open` 1.82 → 1.16 ms**。
- **検査**: 契約 `ModelRigContract` に `TestParallelDeform`（9000 頂点の骨組みを、本物のスレッドへ範囲を分ける検査用のジョブシステムで変形し、順番に変形した結果と 1 ビットも違わないこと、全頂点が独立に計算した位置・法線と合うこと、小さなモデルは呼んだスレッドだけで変形すること）。`RenderTargetPlatformSelfTest` の画像の更新の検査に、新規作成 → 描く → 解放 → 同じ大きさで作る → 描く（場所 5・6）を足した。変異（Release）: 範囲の最後の頂点を取りこぼす・解放した画像を同じフレームで再利用する、の 2 種が検出された。
- **残り（Debug）**: Camera・Island・Shapes3D は、形状（球・円錐など）を毎フレーム CPU でテッセレーションする（`CGeometryShapeBuilder`。`MakeVertex`・`cosf`）。形状ごとの結果の使い回しが次の候補。PixelEffects は、ピクセル効果の CPU 計算そのもの。

## 2026-11-23 追記（夜、その 3）: 球・円錐の頂点を 1 回ずつ作る・スレッド間の往復を眠らずに受ける（Debug の Camera 5.9 → 3.0 ms）
- **測り直し**（Debug、`--no-vsync`）: Camera は、間隔 5.89 ms のうち、同期呼び出し（描画スレッドへの往復）の待ちが 1.8 ms（1 フレーム 176 回。1 回約 10 µs。描画スレッドが実際に動くのは 1 回 2 µs ほど）、残りの約 4 ms は、ゲームスレッドの CPU。PixelEffects は、7.0 ms のうち待ちが 1.5 ms（55 回）で、残りは、ピクセル効果の合成そのもの（ゲームスレッドの CPU）。サンプラーは、待ちの多いスレッドで、待ちの割合を大きく見せる（止めて調べるため）ので、割合は、フレームの計測（`caller blocked in sync calls`）で確かめた。
- **1. 球・カプセル・円錐の頂点**（Camera のゲームスレッドの CPU の約 半分が `MakeVertex`・`cosf`・`BuildRounded`）: 球・カプセルは、面（四角形）ごとに 4 頂点を、三角関数つきで作り直していた（隣の面と同じ頂点を、4 回）。円錐も、分割の両端の向きを、隣と重ねて 2 回ずつ作っていた。円周の向きを境目ごとに 1 回、頂点を円ごと・境目ごとに 1 回だけ作って、面から共有する（出力の数も先に数えて、1 回で確保する）。**出力は、1 ビットも変わらない**（旧実装を残した検査用の比較で、乱数の入力（有限でない値・桁あふれ・軸が平行・半径 0・分割数の範囲外を含む）30 万ケース、約 15 億頂点が、Release 相当と Debug 相当の両方で、一致した）。**Camera の Debug 5.89 → 4.80 ms**。
- **2. 眠る前に短く待つ（`NSpinWait`、50 µs）**: 同期呼び出し 1 回の往復は、実行側の仕事が数 µs でも、スレッドを眠らせて起こす（2 回）ぶんが 10 µs ほどかかる。`COrderedTaskQueue::ExecuteOne(bWait)`（描画スレッドが次の仕事を待つ）と `FTaskFence::Wait`（ゲームスレッドが完了を待つ）が、眠る前に、最大 50 µs だけ、ロックなしの写し（`FTaskFenceState::IssuedHint` / `CompletedHint`）を見て待つ。写しは目安だけで、決めるのは、いつも、ロックの下の値（待ちの打ち切り・実行不能・自分の実行列の待ちの判定は、変えていない）。待ち時間 0 の `Wait( 0 )` は、眠らずに待たない。**Debug: Camera 4.80 → 3.04、Shapes3D 4.74 → 3.04、Adventure 6.17 → 5.63、World 3.55 → 3.29、Character 4.36 → 4.10、PixelEffects 6.9 → 6.5 ms。Release の `open`: Camera 1.76 → 1.29、PixelEffects 2.75 → 2.13 ms**（どれも間隔は下限 2.94 ms）。
- **検査**: `ThreadExecutionContract` に、`TestSpinWait`（条件が最初から真なら 1 回で返る・上限を過ぎるまで諦めない・ほかのスレッドが書いた値を受け取る）と、`TestRoundTrips`（結果を待って続けて積む往復を 2 万回、実行側が眠った後の往復を 20 回。起こし忘れ・書いた値の見え方）を足した。全体の `RunAllChecks -Quick` は、Release の `LNK1103`（`CommandThread.obj` のデバッグ情報。ファイルに触れて作り直すと通る既知のもの）を除いて、全部通った。
- **残り**: Island（Debug 4.6 ms。描画スレッドが 1 フレーム約 2 ms 動く）・Adventure（描画スレッド約 3 ms、1 フレーム約 1360 件）は、描画スレッド側の記録の CPU が次の候補。PixelEffects は、ピクセル効果の合成そのもの（Debug の最適化なしのコード）。

## 2026-11-23 追記（夜、その 4）: 登録したバッファの三角形を、共有のメッシュのまま描く・積む描画の入れ物を使い回す（Island の Release 2.06 → 1.15 ms、Debug 4.6 → 2.9 ms）
- **測り直し**（Island、サンプラー）: Release のゲームスレッドの CPU のうち、`FQueuedGraphicsDraw::Copy` と `SubmitPrimitive`（どちらも、約 2 割）が上位で、描画スレッドも、積んだ描画の破棄（`NtFreeVirtualMemory` を含む）と、転送領域への複写（`memcpy`）が上位だった。原因は、**地形（頂点バッファ + インデックスバッファ。約 3.8 万頂点）を、毎フレーム 2 回（影の絵と画面）、添字を解決して頂点を展開（1.8 MB）→ 全頂点の有限検査 → 積む描画へ複写（1.8 MB）→ 転送領域へ複写（1.8 MB）していたこと**。`Mesh3D` は、頂点とインデックスの組を、GPU の共有メッシュとして置き、使い回していたが、`PrimitiveBuffer3D` / `PrimitiveIndexedBuffer3D` は、使っていなかった。
- **1. 登録したバッファの三角形を、共有のメッシュのまま描く**（`CGeometryVertexSubmitter::SubmitCachedMesh`）: `PrimitiveBuffer3D` のうち、**三角形のリスト・インデックスあり・基準頂点なし・添字 512 個以上**のものは、`GetStableMeshCacheIdentity` で得た鍵と版で、`Mesh3D` と同じ共有メッシュの経路（`DrawMeshForward`、メッシュを置かない転送ではない方）へ渡す。登録のときに、全頂点の有限を検査済みなので、描くたびの検査は要らない。**描けなかったとき（画像が使えない・GPU が受け取らない・添字が範囲外・頂点が 65536 個を超える）は、何も描かずに戻り、これまでと同じ展開の経路で描く**（結果は同じ）。512 個未満は、頂点を展開して送るほうが、同じか速いので、これまでどおり（Shapes3D の木 1 本は、約 100 頂点）。
- **2. 同じ版を 8 回描いてから、共有メッシュへ置く**（`GetStableMeshCacheIdentity`）: 描くたびに頂点を書き換えるバッファ（Shapes3D の池の水面）を共有メッシュへ置くと、毎フレーム、GPU のバッファを作り直すことになり、これまでより遅い（Shapes3D 1.29 → 1.51 ms を実測）。組ごとに、同じ版を描いた回数を数え、8 回を超えてから使う。毎フレーム書き換えるものは、いつまでも、これまでの経路。書き換えない、地形・木のような静的なものは、数フレームで切り替わる。`IGeometryBufferSource` に追加し、既定は false（`CDX12AssetBackendSubsystem` は、共通の所有者へ転送する）。
- **3. 版を、バッファごとにする**: `CGeometryBufferStore` の版は、書き込みのたびに、全メッシュで 1 つ増える、全体の通し番号だった（どれか 1 つへ書くと、全部の共有メッシュを置き直す）。頂点バッファ・インデックスバッファごとに、最後に書いたときの通し番号を持ち、組の版は、その大きいほう（どちらかへ書くと、必ず変わる）にした。池の水面を毎フレーム書いても、木のメッシュは置き直さない。
- **4. 積む描画の入れ物を使い回す**（`FQueuedGraphicsDraw`）: 積む描画は、頂点・添字・定数・画像・バッファの配列を、別々の確保で持ち（1 回に 4〜6 回）、確保するゲームスレッドと、解放する描画スレッドが別で、解放のたびに、ヒープが大きなブロックを OS へ返していた。**1 つの入れ物（`FQueuedDrawStorage`）の 1 つの領域へ、全部の配列を並べて置き、使い終わった入れ物は、置き場（上限 1024 個。64 KB を超えた領域は手放す）へ返して、次の描画で使い回す**。持つのは、入れ物へのポインターだけ（8 バイト）なので、積む仕事（`TCallable`、24 バイトまでは、中に置く）も、確保しない。実行のときの定数の見え方は、8 個までスタックへ置く。置き場は、プロセスの終わりに、先に消えても、あとから返された入れ物を、そのまま解放する（`bPoolAlive`）。
- **5. そのほか**: `Cube3D`・`Line3D` などの頂点配列、`SubmitPrimitive` の帯・扇の展開は、足しながら広げず、先に数えて 1 回で確保する。`BuildCone` は、`AddUninitialized` で、1 回で確保して、直接書く（`Add` は、頂点ごとに、容量の確かめと、再確保が入る）。出力は同じ。
- **実測**（Release の `open`、ゲームスレッドの 1 フレーム）: **Island 2.06 → 1.15 ms**。そのほか（Camera 1.30、Shapes3D 1.29、Deferred 1.25、Adventure 2.15、World 1.49、Character 1.71、PixelEffects 2.60、Village 1.57）は、変わらない。Debug（`--no-vsync`、間隔）: **Island 4.6 → 2.94（下限）、Adventure 5.63 → 4.66、World 3.29 → 2.95**、Camera 3.02・Shapes3D 3.03・Character 4.14 は、変わらない。
- **検査**: `RenderThreadContract` に `TestQueuedDrawStorageReuse` を足した（形の違う 7 種類 — 頂点 6〜6000 個（96 KB で、取っておく上限を超える）・添字あり／なし・定数 0〜10 個（スタックの上限を超える）・画像 0〜2 枚 — の描画を、実行を止めて溜めてから流す、走らせたまま積む、の 2 通りで 8 フレーム、約 1000 回。実行時点の頂点・添字・定数・画像の要約が、積んだときと一致するか。積んだあとの元データは壊す）。

## 2026-11-23 追記（夜、その 5）: モデルのモーフ（形状。`SetModelShapeRate` / `GetModelShapeRate`）を DX12 で動かす
- **ロードマップの順（R5）**: 互換台帳で R5 に Missing として残っていたのは、この 2 つだけだった（`SetModelShapeRate` / `GetModelShapeRate`）。DxLib は、PMX の**頂点モーフだけ**を形状にする（Faputa は、モーフ 55 個のうち、頂点 47 個。材質 7 個・骨 1 個は形状にならない）。形状の番号と名前は、ファイルの並びのまま、DX12 も同じにした（47 個の名前と順番が、DxLib の `MV1GetShapeName` と一致）。
- **取り込み**（`PmxImporter`）: 骨の次のモーフの節を読み、頂点のモーフを `FImportedShape`（名前と、メッシュごとの「頂点の番号と位置のずれ」）にする。ほかの種類（グループ・骨・UV・材質、2.1 のフリップ・インパルス）は、読み飛ばす（番号の大きさ `MorphIndexSize` / `RigidIndexSize` も、検査する）。壊れた節（頂点の番号が範囲の外・知らない種類・途中で終わる）は、取り込みの失敗。`ConvertToLeftHanded` は、右手系から変換するとき、位置のずれの Z も反転する。
- **骨組みと変形**（`ModelRig`）: `FModelRig::Shapes`（疎なずれ）、`FModelPose::ShapeRate`（個体ごとの割合）。`NModelRig::EvaluateShapes` が、割合が 0 でない形状に触られるメッシュだけ、基準の位置へ「割合 × ずれ」を足した位置を作り（形状の番号の順。同じ頂点を 2 つの形状が動かすと、足し合わせる）、`NModelRig::Deform` が、その位置から骨で変形する。**形状は骨の変形の前に足す**（形状で動いた頂点が、骨と一緒に動く）。法線は基準のまま（PMX の頂点モーフは、位置のずれだけを持つ。DxLib が法線も変えるかは、確かめていない。サンプルは光なしで描くので、画像には出ない）。
- **ストア**（`CStaticModelStore`）: `GetShapeRate` / `SetShapeRate`。**同じ割合を設定し直しても、ポーズの版を上げない**（毎フレーム同じ値を設定しても、頂点を作り直さない・GPU のメッシュを置き直さない）。複製は、割合を引き継ぎ、あとからは別々。骨組みの無いモデル・範囲の外の番号・NaN は false（値は変えない）。
- **割合の範囲は、DxLib に合わせた**: 実測（Character サンプルを、DxLib と DX12 で、割合 -1・0.5・1・1.5・2・4・100 にして、変化した画素の範囲を比べた）で、DxLib は、**負の割合を 0 にし、1 を超える割合は、位置のずれを延ばして足す**（上限なし。100 では、形が画面いっぱいに壊れる）。DX12 も同じ（負は 0、1 超は延長）にして、安全のために、**上限は 100**（`CStaticModelStore::MaxShapeRate`。非有限の頂点を作らない）。
- **実素材での一致**（Character サンプルに、表情を足した: まばたきと、にこり。時刻で動かす。`--character-expression=<0 から 1>` で固定できる）: DxLib と DX12 で、「表情なし」と「表情あり」の画像の差（変化した画素）の範囲が、重なる。割合 1: 516 / 511 画素、重なり 509、**IoU 0.983**。割合 0.5: 0.963、1.5: 0.959、2: 0.964、4: 0.915、-1: 変化なし（両方）。骨の違う時刻 2.3 秒: 0.974、4.7 秒: 0.958。割合 0.5 で表情を付けても、DxLib との全体の差の画素数は増えない（6430 → 6369）。
- **検査**: `ModelRigContract`（Release / Debug とも 211 項目）に、PMX の取り込み（頂点のモーフだけが形状になる・並び・位置のずれ・2.1 のフリップ／インパルスを読み飛ばす・壊れたモーフの拒否）と、形状（割合に比例・クランプ・NaN・2 つの形状の足し合わせ・骨の変形と合わせる・同じ割合の再設定で版が変わらない・複製・スキンの無いメッシュ・0 に戻すと共有の形に戻る・壊れた形状の拒否・右手系の変換）を足した。**変異で確かめた**: 割合を無視する変更と、剛体のメッシュで変形を飛ばす変更を入れると、期待した 3 項目だけが失敗した。
- **未対応（残り）**: FBX のブレンドシェイプ（`bHasBlendShapes` の FBX は、これまでどおり、骨組みとして断る）、PMX のグループ・骨・UV・材質のモーフ（DxLib も形状にしない）。形状の動きに合わせた、法線の再計算はしない（DxLib が法線を変えるかは、光ありの画像で、確かめていない）。

## 2026-11-24 追記: モデルのスキニングを GPU へ（R5。Forward の `Draw.Model`。第 1 段）
- **なぜ**: Character（PMX の Faputa）のゲームスレッドの約 70% が、頂点のデータの移動だった（CPU で毎フレーム全頂点を変形し、GPU のメッシュを置き直す）。頂点は変形の前の姿で 1 回だけ置き、変形は頂点シェーダーに任せる。骨が動いても、頂点のバッファは作り直さない（メッシュの版は、骨の動きでは上がらない）。
- **GPU へ渡すもの**: ① 変形の前の頂点（共有のメッシュ。鍵は、リソースごと `(3<<62)|(Resource.Id<<1)`、個体ごと `(3<<62)|(Handle<<1)|1`）、② 頂点ごとの骨と重み（`FStaticModelSkinVertex` 24 バイト = 骨 4 つ（16 ビット）+ 重み 4 つ。構造化バッファ `t2`。頂点の入力は float32 しか持てないので、整数の骨の番号は入力の並びに入れず、`SV_VertexID` で読む）、③ 骨の行列（描画ごとの一時的な定数 `b1`。`float4 g_Bones[3 * 256]` = 12288 バイト。行列 1 つ = 3 つの float4（列 0〜2）。`x = dot(float4(p, 1), col0)`、行ベクトルの規約）。骨の行列は、メッシュごとに「クラスターごとの行列（`ClusterOffset * Globals[骨]`）+ メッシュの節（ノード）の行列 1 つ」。
- **パイプライン**: 頂点シェーダーは `ForwardMesh.hlsl` に `#ifdef SKINNED`（`ForwardVS` / `ForwardDepthVS`。`D3DCompile` のマクロで 2 種類ずつ）。スキニングの有無で、パイプラインの鍵が分かれる（`NumPipelineInputs` 19）。枠の並びは、定数が先（個体 b0 → 光 b6（深度だけの描画は無し）→ 骨 b1）、そのあとに登録したバッファ（`t2`）。宣言した枠は、シェーダーがすべて使う。定数は、反射した最小サイズ以上を渡す（骨の行列は、常に 256 個ぶんに、0 で埋めて渡す）。
- **ストア**（`CStaticModelStore::GetStaticModelSkinnedDrawView`）: 骨組みのある個体だけ。次の個体は false（呼び側が、これまでの CPU で変形した頂点で描く）: 頂点に骨の影響が 5 つ以上、骨の行列が 256 個を超えるメッシュ、材質の色を上書きした個体（頂点の色を作り直すため）。重みは、合計 1 に正規化する（`Deform` と同じく、合計で割る）。何も影響しない頂点は、メッシュの節の行列に付ける。初期状態（形状・非表示なし）の個体は、同じリソースのメッシュを共有する（`Resource.DefaultSkinGeometry`。大量の同じモデルが、1 つの GPU メッシュで済む）。形状の割合が変わると、メッシュだけ作り直す（`ShapeVersion` / `GeometryVersion`）。骨だけが動くときは、行列だけを毎フレーム渡す。
- **実行側**（`CGeometryModelCommands::DrawModel`）: Forward が使えるとき、まず GPU スキニングのビューを頼む（`DrawSkinnedView`）。範囲ごとに GPU へ渡し、GPU が受け取らなかった範囲から先は、変形済みのビュー（`GetStaticModelDrawView`）で、CPU の経路が描く（同じ範囲を 2 回描かない）。骨の行列が有限でない・範囲が不正なら、何も描かない（誤ったポーズに落とさない）。影の絵（深度だけ）も、同じ経路。
- **まだ CPU で変形するもの（第 2 段）**: 場面の描画の並び・スナップショット（`FModelPoseSnapshot`）・Deferred の G-buffer。`--no-gpu-forward`。65,536 頂点を超えるメッシュ。
- **確認**: Character を、GPU スキニングと `--no-gpu-forward`（CPU の変形）で、同じ時刻に撮った画像は、差が 6/255 を超える画素が 0（Release / Debug）。Character の間隔と `open`（描画命令を開く時間）: Release は `open` 1.71 → 0.54 ms、間隔 3.12 ms（下限）。Debug は間隔 3.75〜4.14 → 3.12 ms（下限）、`open` 1.71 ms。`--no-gpu-forward` の CPU の経路は、間隔 10.2 ms（Release）/ 33.9 ms（Debug）で、比較の基準だけに使う。
- **検査**: `ModelRigContract`（Release / Debug とも 229 項目）に `TestGpuSkin`（シェーダーと同じ式を CPU で行い、`Deform` と頂点ごとに一致: 基準の姿・骨を動かした姿・アニメーション中・形状つき。骨が動いても版が変わらない・非表示・個体間の共有・材質の色の上書き / 骨 5 つ以上 / 未知の個体で false）。`ForwardMeshContract`（162 項目）に `TestSkinnedRenderer`（3 つのバッファ・定数 3 つの並びと大きさ・パイプラインの枠・骨の行列だけが変わるときは置き直さない・版が変わると 3 つとも置き直す・範囲外の骨の番号 / 有限でない重みと行列 / 骨が 0 個または 256 個より多い / 一時的な描画は断って何も置かない・解放）と `TestSkinnedForward`（GPU が全範囲を描くと変形済みの頂点を頼まない・断った個体・範囲の不正・有限でない骨・途中で GPU が断ると、残りを CPU が描く（2 回描かない）・影の絵・シェーダーの実行先が無いと頼まない）。変異テスト（1 件ごとにライブラリを作り直して、検査が落ちることを確認。契約検査は、作ってあるライブラリにリンクするので、ライブラリを作り直さないと、変異が検査に届かない）: 12 件（重みの正規化を外す・クラスター行列の順序・行列の転置・メッシュの節の行列・骨番号の範囲・光の定数の抜け・スキニング用バッファの解放漏れ・保持バイト数・再利用の条件・CPU への引き継ぎで全範囲を描き直す・変形済みの頂点を常に取る・有限でない骨の行列）すべて検査が落ちる。最初は 3 件（重みの正規化・節の行列・再利用の条件）が生き残ったので、検査を足した（重みの合計が 1 でない頂点、骨の影響が無い小物のメッシュ、同じ鍵と版で骨の数やスキニングの有無が変わる描画）。`ModelRigContract` は 233 項目、`ForwardMeshContract` は 164 項目（Release / Debug）。
- **検査が見つけた不具合**: 骨の影響が無いメッシュ（FBX の小物など）で、GPU へ渡す「変形前の頂点」が、ビルダーが作った頂点（節の行列が、すでにかかっている）だったため、GPU で節の行列が二重にかかった。`RestVertices` を、骨組みの元の位置と法線（`Deform` と同じ入力）から作るように直した（`CStaticModelStore::Create`）。全頂点に骨がある PMX の Character では、元の位置と基準の姿が一致するので、表に出ていなかった。
- **互換台帳**: `API.IStaticModelSource.GetStaticModelSkinnedDrawView`（Implemented, R5）を足し、`Draw.Model3D` の注記を更新した。`DX12_KNOWN_GAPS.md` のアニメーションつきモデルの行も更新した。
- **次**: 第 2 段（スナップショット・場面の描画の並び・Deferred の G-buffer も GPU でスキニング）。そのあと、R5 の残り（FBX のブレンドシェイプ、PMX の頂点以外のモーフ）。

## 2026-11-24 追記（続き）: 固定した姿勢を GPU スキニングのまま保持する・文字の上限で落とさない
- **全体チェック**（`RunAllChecks.ps1 -SkipBuild -Quick`）: SDL の差し替え・GPU スキニング（第 1 段）の入ったあとで、**77 ステップすべて通った**（約 55 分。変異テスト・Debug / Release の契約・サンプル画像）。
- **固定した姿勢（`FModelPoseSnapshot`）**: GPU でスキニングできる個体は、変形済みの頂点のコピーではなく、基準の頂点（共有）・骨と重み（共有）・骨の行列（その姿勢のぶん。最大 12 KB）を保持する（`Capture` が、`GetStaticModelSkinnedDrawView` を先に頼み、断られたら、これまでの変形済みのビュー）。元の個体を削除しても、ビューが持つ所有（`VertexOwner` が資源を、`SkinOwner` が骨の行列を保持）で、そのまま描ける。
- **描画**（`CGeometryModelCommands::DrawModelView`）: ビューに `SkinVertices` があれば `DrawSkinnedView( View, -1, ... )`。GPU の経路（Forward）が使えれば、骨の行列つきで GPU が描く。GPU が使えない（実行先が無い・`--no-gpu-forward`・65536 頂点超・GPU が途中で断る）ときは、**ビュー自身の骨と重みから、CPU で変形して**（`CGeometryModelCommands::SkinViewOnCpu`。シェーダーと同じ式、重みが 0 の枠は飛ばし、法線は正規化）、続きを CPU の経路が描く（同じ範囲を 2 回描かない）。個体を引ける呼び出し（`Handle >= 0`。`Draw.Model` の経路）は、これまでどおり、個体の変形済みの頂点で続きを描く。
- **Deferred の G-buffer**: モデルを G-buffer へ描く経路が、まだ無い（Deferred サンプルは、球・立方体・地面の固定メッシュだけ）。モデルを G-buffer へ描く機能を足すときに、`SKINNED=1` の頂点シェーダーを使う。第 2 段は、これで完了とする。
- **検査**: `ModelRigContract`（246 項目）: `TestPoseSnapshot` を、骨と重みのビューを貸さない源と、貸す源の両方で実行（アニメーションの途中で捕捉 → 個体を動かして削除 → 保存した姿勢で再生した頂点が、捕捉したときの頂点と一致・不透明度の保持・所有元の違う源は拒否・Null の実行側が受ける）。`SkinViewOnCpu` が、変形済みのビュー（`Deform`）と、位置・法線・色・UV の全頂点で一致（骨を動かした姿）。`ForwardMeshContract`（176 項目）: 固定した GPU スキニングの姿勢を、GPU が全範囲描く（個体も変形済みの頂点も頼まない）／途中で GPU が断ると、残りをビューから CPU が描く（2 回描かない・個体を引かない）／GPU の経路が無いと CPU が描く。変異テスト（ライブラリを作り直して試す）: `SkinViewOnCpu` の重み・行の取り違え・法線の正規化・移動の欠け、固定した姿勢でスキニングしない分岐、スナップショットが骨と重みを持たない、CPU の引き継ぎで全範囲を描き直す、の 7 件を検出。最初、「スキニングしない分岐」が生き残ったので、基準の姿ではなく、アニメーションの途中で捕捉するようにした。
- **文字の上限で、アプリが終了しない**（`CGeometryCanvas2D`。`Text` / `TextFont`）: 1 フレームに新しく画像にできる文字列は 512 個・合計 32 MiB まで。**超えたら、フレームを失敗にして（アプリが終了していた）、ではなく、そのフレームは描かずに飛ばす**。作った画像は、キャッシュに残るので、同じ文字列を描き続ければ、次のフレームで続きを作る（600 個の新しい文字列は、2 フレームで全部出る）。飛ばした数を、120 フレームに 1 回、`LogRender` の警告に出す。検査（`ForwardMeshContract` の `TestTextLimits`、偽の画像の所有先とラスタライザー）: 600 個で 512 個を描いてフレームは成功／次のフレームで 600 個全部／`TextFont` も同じ／4 MiB の画像は 8 個まで（32 MiB の上限）で成功。変異（上限を失敗に戻す・バイトの上限を失敗に戻す・数の境目を 1 つずらす）の 3 件を検出。`DX12_KNOWN_GAPS.md` の「文字の数の上限」を更新した。
- **次**: R5 の残り（FBX のブレンドシェイプは、DxLib の形状の番号の付け方が分からず、手がかりになる実素材も無いので、保留）。

## 2026-11-24 追記（続き 2）: 描画の内容が失敗したフレームで、アプリを終了させない
- **問題**: `CDX12PlatformSubsystem::EndFrame` は、ゲームの描画命令・描画の実行・UI のどれか 1 つでも失敗すると、`DX12 frame dropped` の警告のあと、そのフレームを提出せずに、表示の失敗（`DX12 present failed`）と同じ扱いで、**アプリを終了していた**（`bExit = true`）。内容の失敗（画像を作れない・入力が不正）と、デバイスの失敗（提出・表示）を、区別していなかった。DxLib は、描画の問題でアプリを終了しない。
- **変更**: 内容の失敗は、**記録した命令を提出して（資源の予約を確定し、記述子・画像の寿命は、通常のフレームと同じ）、表示だけをしない**（`CDX12Device::DiscardPendingPresent`。前の絵が残る。戻す色の遷移は、提出した命令の中で済んでいるので、次のフレームは、そのまま始まる）。連続して 120 フレーム失敗したときだけ、`DX12 frames keep failing` のエラーを出して終了する（一時的な超過ではなく、描画の誤り）。提出・表示の失敗は、これまでどおり、すぐ終了。以前は、提出しないフレームが、コマンドリストを開いたまま（`bRecording`）にして、次のフレームを始められなかった（そのため、終了するしかなかった）。
- **故障の注入**（検査用）: `--dx12-drop-frame=<番号>`（そのフレームだけ）・`--dx12-drop-frames-from=<番号>`（そのフレームから先）・`--dx12-drop-frame-every=<N>`（N フレームに 1 回）。フレームの番号は `--capture-frame` と同じ。ログに `(injected for a test)` と付く。
- **確認**: デバッグレイヤーを有効にして、Hello・RenderTarget（フレームの途中で命令列を区切る）・PostEffects（MRT）・Deferred・Shadows・Character で、5 フレーム目を落としても、続きを描き、30 フレーム目を撮れる（エラー 0）。Release・Debug の両方。`RunDX12SampleImageTests.ps1`（139 項目）に、1 フレームだけ落とす 4 サンプル・落とし続けて理由つきで終了する・飛び飛びの失敗（2 フレームに 1 回を 300 フレーム）が積み重なって終了にならない、の検査を足した。変異（Samples を作り直す）: 表示待ちから外さない（次のフレームが始まらない）・連続の上限が効かない・1 回で終了（以前の動き）・連続の数を戻さない、の 4 件を検出（最後の 1 件は、最初に生き残ったので、飛び飛びの失敗の検査を足した）。
- **残り**: 確認したのは、上の 6 サンプルだけ（ほかのサンプルは、全体の検査で、落としたフレームの無い経路だけを通る）。外部窓（ImGui の別窓）の描画は、別の経路（`Viewports.Render`）で、この変更の対象ではない。

## 2026-11-24 追記（続き 3）: 登録した共有メッシュの頂点・添字の上限を引き上げる（65,536 → 1,048,576 頂点）
- **問題**: 頂点が 65,536 個（添字が 196,608 個）を超える、登録したバッファのメッシュは、**GPU でも CPU でも拒否され、描かれなかった**（フォールバックが効かない）。実測（Island の地形を、マス数 256 = 頂点 66,049・添字 393,216 にした）: `Draw command ... rejected` の警告が出て、地形が丸ごと消える。MMD 系の PMX は 7 万頂点前後が普通なので、実際のモデルで、GPU スキニングの効果が出ない恐れもあった。
- **原因**: 同じ値が、4 つの層に別々に書かれていた（命令を積む門 `AsyncDrawGate.cpp`、DX12 の描画部品 `DX12GraphicsPipelineDrawer.cpp`、Forward の描画部品 `DX12ForwardMeshRenderer.cpp`、実行側の `CanDrawForward` と `GeometryShapeCommands.cpp`）。
- **変更**: `Library/Core/Render/GraphicsPipelineDraw.h` の **`NGraphicsPipelineLimits`**（`MaxTransientVertices` 65536 / `MaxTransientIndices` 196608 / `MaxRegisteredVertices` `1 << 20` / `MaxRegisteredIndices` `6 << 20`）へ、値を 1 か所にまとめた。登録しない描画（毎回、転送領域へ複写）は、小さいまま。登録した描画は、GPU のバッファ 1 つの上限（64 MiB）に収まる大きさ（頂点 48 バイトで 48 MiB・添字 4 バイトで 24 MiB）。`PrimitiveBuffer3D` の実行側は、GPU の共有メッシュの候補（三角形のリスト・添字つき・基準頂点なし・512 個以上）の 1 回の数の上限を、大きくした。
- **CPU の経路（保険）**: 共有メッシュで描けなかった大きな三角形のリストは、頂点の上限以内の塊（65535 = 3 の倍数）へ分けて描く（以前は、拒否）。24 倍遅い（Island の 66k 頂点の地形で、74.6 ms 対 3.12 ms）ので、**1 度だけ警告を出す**（`The DX12 backend does not support: A registered mesh of more than 65536 indices on the GPU shared-mesh path`）。三角形のリスト以外は、65536 を超えると、これまでどおり拒否。
- **実機の確認**: マス数 256 の Island が、地形つきで描かれる（Release・Debug。D3D12 のデバッグレイヤーのエラー 0）。GPU の経路は、Release の間隔 3.12 ms（下限）、`open` 0.90 ms。自動の検査として、Island サンプルに `--island-cells=<N>`（16〜1024。既定 80）を足し、`RunDX12SampleImageTests.ps1`（141 項目）が、256 マスで、地形の緑の画素・「未対応」の警告が無いこと・落としたフレームとデバッグレイヤーのエラーが無いことを確かめる。
- **検査**: `ForwardMeshContract`（193 項目）: 登録するメッシュの 65537 頂点・ちょうど上限・上限 + 1、添字の上限・上限 + 3、登録しない描画の 65537 頂点拒否・65536 受理・196608 / 196611 添字、実行側（65537 頂点のモデルが GPU へ・上限を超えると送らない）、`TestLargePrimitiveBuffer`（70000 頂点・210000 添字。最初は 65535 頂点の塊 3 つと残り・繰り返すと 1 つの共有メッシュ・GPU が無いと CPU で 4 つの塊・GPU が受け取らないと警告 1 回・三角形の帯は拒否）。`RenderThreadContract`（113 項目）: 門の登録した描画（70000 頂点・210000 添字を受け付ける・上限を超えると同期）、登録しない描画の境目。変異テスト 12 件（各層の上限を戻す・登録しない上限を上げる・塊の大きさ 65536・塊に分けない、など）、**全件検出**。
- **落とし穴**: フォールバック（塊に分ける CPU の経路）が、よくできていて、GPU の共有メッシュの上限を戻しても、**黙って代わりに描いてしまい**、実機の検査が反応しなかった。そこで、遅い経路に落ちたとき、1 度だけ警告を出し、検査が、その警告を見る。
- **残り**: 骨の行列の定数が、骨の数によらず 12 KB（256 個ぶん）で、描画ごとに送られる（群衆では、転送が重い）。DxLib の経路（`Library/Platform/DxLib`）の大きなメッシュの扱いは、確かめていない。

## 2026-11-24 追記（続き 4）: 群衆を測って直す（骨つきのキャラクターが、142 体 → 約 1300 体まで、GPU の経路で描ける）
- **道具**: Character サンプルに `--character-copies=<N>`（同じモデルを、基準の姿のまま、N 体。1〜2000）。`--dx12-frame-timings` と、サンプリング・プロファイラ（`sample_run.ps1` / `sampler.exe`）で測った。以降、Faputa（骨 79 個・頂点 50,243・範囲 7 つ）の Release の `open`（ゲームスレッドの CPU）。
- **1. 描画ごとの、メッシュ全体の添字の検査**（`CDX12ForwardMeshRenderer::Draw`）: ゲームスレッドの 84% が、`Draw` 自身の中のループだった。**描画のたびに、置いてあるメッシュの全部の添字（24 万個）を検査し直していた**。置くとき（`AcquireMesh`）だけへ移した（登録しない描画は、毎回中身が違うので、毎回）。**1 体 0.55 → 0.22 ms、50 体 18.3 → 1.69 ms、200 体 46 → 3.96 ms**。単体のキャラクターも、速くなった。
- **2. 個体数の上限 128**（`CStaticModelStore::Create` / `Duplicate`）: 同じモデルの個体が 128 体までしか作れなかった。`CStaticModelStore::MaxInstances = 4096`（個体は形状と画像を共有する）。
- **3. 拒否のログ**: GPU の経路が描画を拒否するたびに警告を 1 行出していた（1 フレーム 1674 行）。1 度だけ（`NBackendSupport::ReportUnsupported`）。
- **4. 1 フレームの描画回数の上限 1024**（`NGraphicsPipelineLimits::MaxDrawsPerFrame`）: DX12 の描画部品と、命令を積む門の両方が、カスタムグラフィックスパイプラインの描画を 1 フレーム 1024 回（門は 1000 回）に制限していた。**1 フレームに 1024 個を超える描画があるシーンは、すべて、GPU の経路から、遅い CPU の経路へ落ちた**（Faputa は 142 体で。200 体で `open` 194 ms、400 体で 714 ms）。上限は、防御用で、描画ごとの領域は取らない。16384 へ（門は 24 回少なく。実際の制約は、転送領域の予算）。
- **5. 骨の行列の定数の段階分け**: 次の制約は、1 フレームの転送領域の予算（門 40 MiB・DX12 64 MiB）。骨の行列の定数が、骨の数によらず 12 KB で、範囲ごとに複写されていた。骨の数を 32 個ずつの段階（`SkinBoneTierSize`。8 段階）に切り上げた大きさで渡す（Faputa は 79 → 96 個ぶん = 4.5 KB）。段階ごとに別の頂点シェーダー（HLSL の `MAX_SKIN_BONES`）= 別のパイプライン。シェーダーは、初めて使うときに、使う分だけ作る。**群衆の上限: 142 体 → 約 1300 体**。
- **実測**（Release・Faputa 7 範囲・基準の姿。体数: `open` / 間隔）: 1: 0.22 ms。200: 6.1 / 14.9 ms。400: 12.2 / 29.4 ms。1000: 35.5 / 76.6 ms（警告 0）。1500: 64 MiB の予算を超えて、また崖（350 ms）。1 体あたりの CPU は、約 30 µs（7 回の描画 = 約 4.3 µs / 回）。GPU は、1 体あたり約 40 µs（5 万頂点のスキニング）で、約 400 体から、GPU が支配的。
- **検査**: `ForwardMeshContract`（213 項目）: 段階の計算・骨の数ごとの定数の大きさ・同じ段階はパイプラインを共有・先頭に行列で残りは 0・前の描画の残りが混ざらない・置いてあるメッシュを描くとき、全体の添字を検査し直さない（300 万個の添字を 2000 回描いて 1 秒未満）。`ModelRigContract`（251 項目）: 個体数 4096。`RunDX12SampleImageTests.ps1`（143 項目）: Faputa があるとき、Character を 300 体（2100 回の描画）で、警告・落としたフレーム・デバッグレイヤーのエラーが無い。変異テスト 9 件（毎回調べ直す・段階の切り捨て・最大の大きさ・末尾を埋めない・1 つのパイプライン・描画回数を 1024 に戻す、など）、すべて検出。
- **残り・次**: ①初めて骨つきの描画をするときの、シェーダーを作る時間（約 440 ms。1 度だけ）: シェーダーの保存（R9 の shader / PSO キャッシュ）で消せる。②**1500 体を超える群衆**: 転送領域の予算が上限。1 体の 7 回の描画が、同じ個体の定数（個体・光・骨の行列。約 6.9 KB）を、毎回複写している。描画部品の側で、同じ内容の定数の複写を除く設計（1 フレームの定数を 1 度だけ上げて、描画から参照する）が要る。インスタンシング・VAT（R9）と一緒に設計する。③GPU 側: 大量のスキニングの頂点は、GPU が支配的（400 体以上）。距離による簡略化（LOD）や、視野外の除外（R9）が効く。

## 静的モデルの階層鏡映と面の向き（2026-11-15）

- `FStaticModelBuilder` はフレーム階層を頂点へ焼き込むが、負の行列式を持つ変換では三角形の表裏だけが反転し、逆転置で求めた法線と逆を向いていた。親を含む線形変換の行列式をfloat64で求め、鏡映の場合のみ各三角形の後ろ2頂点を入れ替える。UV・頂点色は頂点と共に移動し、三角形・材質範囲の順序は保持する。非有限・特異な変換は理由を返し、出力を保持する。
- これは取り込み時の静的フレームに限る。描画時のモデル個体の負の拡大や、スキン・モーフ・R2全体の完成を意味しない。[Unreal Engineの座標系](https://dev.epicgames.com/documentation/en-us/unreal-engine/coordinate-space-terminology?application_version=4.27)と[Microsoftのカリング説明](https://learn.microsoft.com/en-us/windows/win32/direct3d9/culling-state)を参照。
- 検査側[9d9c183](https://github.com/mynameisGaku/framework-test/commit/9d9c1839b768d64b4c222c882ada24f239fbfaf7)は、親子それぞれの負の拡大・二重鏡映・回転と非一様拡大・正負混在・材質範囲とUV・特異行列・FBX取り込みを検査する。独立した同一検査では修正前に19条件中6失敗し、修正後は追加した混在を含む21条件がDebug／Releaseで成功。続く[f482f34](https://github.com/mynameisGaku/framework-test/commit/f482f3435c187d91b286ef78cc06df79f5744571)では各三角形の材質色も検査する。
- 同じ検査側のGraphicsPipelineは、モデルの通常・鏡映三角形に表面／裏面除去を適用し、GPUから画素を読み戻す。修正前の既定GPUは対応する条件で失敗し、修正後はDebug／Releaseの既定GPU・WARP各110条件、CPU各3条件が成功。SDKデバッグ層は有効。独立コピーでライブラリとprojectを両構成ビルドし、アセット検査は最終的にDebug（ワーカー0）9989条件・4省略、Release（ワーカー4）9986条件・4省略が成功した。全ソリューション・全サンプルや実モデルの全画素比較は今回の検査範囲に含まない。
- Debugアセット検査は最初の2回、`text-input-real` で6件のイベントの並びと編集結果（期待`ba`、実際`b`）が不一致になった。修正前のモデル実装では成功し、その後、診断を外した最終版でも成功したため、原因とモデル変更との関係は未確定。入力実装はShiftの状態を参照するが、その影響と断定していない。比較ログは検証コピーの `Saved/StaticModelMirror-20261115/` に保持する。書式監査は1831ファイル、互換台帳は488項目が成功。
- Claude側は確認時点でローカル `Dx12` の `d057f7e`、描画・ライティングに未コミット変更がある。今回そのファイルは変更していない。後の統合では静的形状の表裏と描画時の法線・カリング処理が重複しないか確認する。

## 2026-11-24: 鏡映モデル修正の統合

- backend-dx12 の 6ebe9e0 と検査側 9d9c183・f482f34 を Dx12 に統合した。スキン付きメッシュの変換を維持し、静的な小物は親を含む行列式で頂点順を補正する。
- Debug / Release でライブラリのビルドとモデル契約 251 項目が成功。鏡映親を持つ骨の重みのない小物を CPU 変形と GPU スキニング相当の計算で比較した。グラフィックスの独立検査は両構成の実 GPU / WARP 各 110 項目が成功。書式と互換台帳 534 項目、DX12 の役割 76 型も確認した。
- 全体 77 ステップ、実モデルの GPU スキニング画像比較、アプリ全体の Debug / Release ビルドは、この統合後には未実行。

## 2026-11-24: 鏡映モデル統合後のサンプル検証

- Debug / Release の Samples をビルドし、DX12 のサンプル画像検査が両構成で 143 項目ずつ成功した。Faputa の GPU と CPU の Forward 比較、および 300 体の描画検査を含む。
- この比較は DX12 内の二つの経路を比べたもの。GPU スキニングと DxLib の画像比較、および統合後の全体 77 ステップは未実行。
