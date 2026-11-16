# DX12開発の引き継ぎ

最終更新: 2026-11-15（日本時間）。この文書は作業中も更新します。実装済み、未接続、検証済みを分け、予定を完了扱いにしません。

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
