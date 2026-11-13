# DX12開発の引き継ぎ

最終更新: 2026-11-13（日本時間）。この文書は作業中も更新します。実装済み、未接続、検証済みを分け、予定を完了扱いにしません。

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
  - `CDX12Device`先行解放（`TryShutdown()`や`ResizeBuffers`/`GetBuffer`失敗時の`ReleaseAfterGPUStopped()`）後に`CDX12ImGuiFrameRenderer::TryShutdown()`を呼ぶと、`Device->Wait()`と`Device->IsDeviceRemoved()`がともに`false`を返して`Shutdown()`が無限待機する問題を、`DeviceLifecycleMain.cpp`（`ImGuiFrameLifecycle`）で再現しました。`CDX12Device::IsInitialized() const`を追加し、`CDX12ImGuiFrameRenderer::TryShutdown()`が`Device->IsInitialized()`のときのみ`Wait()`・`IsDeviceRemoved()`を待機するよう修正しました（`PrepareShutdown()`タイムアウト中は`bInitialized == true`のため待機を継続）。
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

- **調査対象**: `C:/Users/g0190/.claude/worktrees/framework-stl`（`Asset` ブランチ、HEAD `19e7170`）。Claude側はpushできておらず、コミット済み履歴だけでなく、このローカル作業コピーの未コミット変更・未追跡ファイルを読み取り専用で参照する。作業中のため、統合時に必ず再確認する。
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
  - `MaterialShadowMap.cpp` は `ERenderTargetFormat::Float32`・`Channels = 1` のカラー描画先＋`bDepth = true` で深度を色へ出力しており、`NumRenderTargets == 0` 必須とは読めない。一方、DX12 側の `R32Float` 単一成分カラー描画先未対応と、`Texture2D g_ShadowMap`（`Texture2D<float4>`）対 `R32Float` 制約は R2 の課題として残る。
- **検証結果**:
  - `framework-dx12-verify` の製品コードおよび保護 7 ファイルは変更なし（共通 `Library/Framework/Material` も DX12/ImGui 非依存のまま）。
  - `framework-dx12-selftests/GraphicsPipelineContractMain.cpp` に実型 `FShaderVertex3D`（112B）を用いた Substrate Surface 互換の縦経路検査（検証用 HLSL、`b4`/`b5`/`b6`、`t0`/`t1`/`s0`、`RGBA16F` + `D32Float`、HDR readback）を追加し、`RunGraphicsPipelineTests.ps1` の Debug / Release 双方で `--cpu`（3 checks, 0 failures）・`--default`（71 checks, 0 failures）・`--warp`（71 checks, 0 failures）が全件成功した。
