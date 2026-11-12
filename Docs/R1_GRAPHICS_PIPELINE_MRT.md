# カスタムグラフィックスパイプライン契約とMRT複数描画先

2026-11-12（日本時間）。R1の既存描画基盤と描画先資源へ、API非依存のカスタムグラフィックスパイプライン所有契約（`IGraphicsPipelineResources`）、複数描画先（MRT：1〜8枚の `RGBA8` 色ターゲット＋任意の読み取れる `D32` 深度）へのパス記録、汎用バッファ・画像入力を伴う `IRenderGeometry3D::DrawGraphicsPipeline`、同一 `DIRECT` キューへの実GPU提出、およびcompute／通常描画／撮影への受け渡しを追加しました。既存の `CDX12TextureStore`・`CDX12BufferStore`・`CDX12RenderTargetPass`・`CDX12GeometryRenderer`・`CDX12GameRenderer`・`CDX12Device`・`CDX12PlatformSubsystem`・`CThreadedRenderSurface` を拡張して接続し、新しいSubsystemやImGui依存は増やしていません。

## 共通の値・窓口とDX12実装の配置

|配置・型|役割|
|---|---|
|`Library/Core/Render/GraphicsPipelineHandle.h`|再利用しない単調増加番号を包む `FGraphicsPipelineHandle`（0は無効）|
|`Library/Core/Render/GraphicsBufferLayout.h`|`EGraphicsBufferLayout`（`Constant`／`Raw`／`Structured`、R1は読み取り専用）|
|`Library/Core/Render/GraphicsBufferSlot.h`|`FGraphicsBufferSlot`（`Binding`、`Layout`、`ElementStride`）|
|`Library/Core/Render/GraphicsVertexAttributeFormat.h`|`EGraphicsVertexAttributeFormat`（`Float1`／`Float2`／`Float3`／`Float4`／`UByte4Norm`）|
|`Library/Core/Render/GraphicsInputElement.h`|`FGraphicsInputElement`（`SemanticName`、`SemanticIndex`、`Format`、`AlignedByteOffset`）|
|`Library/Core/Render/GraphicsTextureSlot.h`|`FGraphicsTextureSlot`（`register(tN)` の番号と `RGBA8`／`R32Float` の区別）|
|`Library/Core/Render/RenderTargetFormat.h`|`ERenderTargetColorFormat::RGBA8` と `FRenderTargetColorAttachment`（Framework側の既存 `ERenderTargetFormat` との名前衝突を避けるため `ColorFormat` として分離）|
|`Library/Core/Render/GraphicsPipelineDesc.h`|`EGraphicsDepthFormat`（`None`／`D32Float`）と `FGraphicsPipelineDesc`（`MaxRenderTargets = 8`）|
|`Library/Core/Render/GraphicsPipelineDraw.h`|`FGraphicsPipelineDraw`（パイプライン番号・頂点／indexバッファ・範囲・読み取り専用 `Buffers`／`Textures` 列）|
|`Library/Core/Render/RenderTargetCapabilities.h`|`FRenderTargetCapabilities` へ `MaxColorTargets` と `SupportsMultipleRenderTargets` を追加|
|`Library/Core/Render/RenderTargetPassDesc.h`|`FRenderTargetPassDesc` へMRT指定用の `ColorTargets` 列（1〜8枚）を追加|
|`Library/Core/Interfaces/Render/IGraphicsPipelineResources.h`|`CreateGraphicsPipeline` と `ReleaseGraphicsPipeline` を公開する作成・所有・遅延破棄窓口|
|`Library/Core/Interfaces/Render/IRenderGeometry3D.h`|記録中のパスや主画面へカスタムパイプライン描画を積む `DrawGraphicsPipeline`（既定実装は `false`）|
|`Library/Framework/Render/Thread/ThreadedRenderSurface.*`|ゲームスレッドの専用描画命令内から `DrawGraphicsPipeline` を転送|
|`Library/Platform/DX12/Pipeline/Graphics/DX12GraphicsPipelineBinding.h`|GPU完了まで保持する `FDX12GraphicsPipelineBinding`（PSO・RootSignature・宣言スロット・最小CB幅等）|
|`Library/Platform/DX12/Pipeline/Graphics/DX12GraphicsPipelineStore.*`|DXBC SM5.0／5.1のreflection照合・PSO所有・`Retired` 遅延破棄を行う `CDX12GraphicsPipelineStore`|
|`Library/Platform/DX12/Resource/Texture/DX12TextureFormat.cpp`|`bRGBA8RenderTarget` 有効時に `MaxColorTargets = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT`（8）を返す|
|`Library/Platform/DX12/Resource/Texture/DX12TextureStore.*`|パス中MRT全色ターゲット・深度のハザード追跡（`ActiveColorTargets`／`IsActiveTarget`）と用途・形式照合|
|`Library/Platform/DX12/Render/Target/DX12RenderTargetPass.*`|1〜8枚MRT＋任意D32深度へのパス実行・任意色クリアPSO・提出時寿命保持（`Colors`／`Depth`／`Buffers`／`Uploads`／`Textures`／`Pipelines`）|
|`Library/Platform/DX12/Render/Geometry/DX12GeometryRenderer.*`|MRT複数RTV束縛、`DrawGraphicsPipeline` の検証・遷移・ピン留め・実行、ハザード検出|
|`Library/Platform/DX12/Render/Game/DX12GameRenderer.*`|MRTパス開始時の `Clip` 初期化（`Canvas` 無効時も描画先全体を有効領域とする）|
|`Library/Platform/DX12/Device/DX12Device.*` / `DX12PlatformSubsystem.*`|`IGraphicsPipelineResources` の公開・主画面／オフスクリーンでの実行・フレーム／Wait時回収|

役割分類（`Tools/CheckDX12Architecture.ps1`）では `CDX12GraphicsPipelineStore` を機能・所有者（`C`、49型）、`FDX12GraphicsPipelineBinding` 等をデータ（`F`）へ統一し、公開層とDX12実装の間でD3D12固有型やImGui依存を持ち込みません（`Tools/CheckDX12DeviceDependencies.ps1`、`Tools/CheckGameRenderDependencies.ps1` で検証済み）。

## パイプライン作成・Reflection照合・RootSignature契約

`CDX12GraphicsPipelineStore::CreateGraphicsPipeline(const FGraphicsPipelineDesc &Desc)` は有効な `ID3D12Device`（device removed でないこと）と保持上限（`MaxPipelines = 64`、解放待ち含む）を確認した上で、以下を厳密に検査し、いずれかが不適合なら所有状態を変えずに無効ハンドルを返します。

1. **DXBCバイナリとステージ照合**:
   - VS／PS各4MiB上限・DXBCコンテナ正当性・SM5.0／5.1の頂点／ピクセルステージ一致を `D3DReflect`（`ID3D12ShaderReflection`）で確認します。
2. **頂点入力レイアウト照合（`ValidateVertexInputs`）**:
   - `VertexStride` は4〜2048の4の倍数、`InputElements` は1〜16要素、重複なし、`SV_` 開始名の禁止、`AlignedByteOffset` は4の倍数かつ `Stride` 内。
   - VSの非システム入力（`SV_VertexID` は許可）とセマンティック名・番号・32bit浮動小数成分マスクが1対1で一致することを要求します。
3. **ステージ間・複数 `SV_Target` 出力照合（`ValidateStageOutputs`）**:
   - VSは `SV_Position` を必須とし、PSの各非システム入力（`SV_Position`・`SV_IsFrontFace` 以外のシステム値は拒否）がVS出力から互換供給されること。
   - `NumRenderTargets` は1〜8、`RenderTargetFormats[0..N-1]` はすべて `ERenderTargetColorFormat::RGBA8`、PS出力は `SV_Target0..N-1` が過不足なくすべて `float4`（`ComponentType == D3D_REGISTER_COMPONENT_FLOAT32`、`Mask == 0xF`）で宣言されていることを要求します。
4. **バッファ・画像・静的サンプラスロット照合とRootSignature**:
   - `BufferSlots.Num() + TextureSlots.Num() <= 16`、スロット番号重複や `t` スロット間でのバッファ／画像番号衝突を禁止。
   - `Constant`（`b0..b15`、Root CBV）は定数バッファ最小サイズ `MinimumBytes` をreflectionから取得します。`Raw`（`t0..t127`）は4byteアライン、`Structured`（`t0..t127`）は `ElementStride`（4〜2048の4の倍数）とreflectionの `NumSamples` 一致を要求します。
   - `TextureSlots`（`t0..t127`、各1記述子のSRV DescriptorTable）は `EComputeTextureFormat::RGBA8`（4成分 `Texture2D<float4>`）または `R32Float`（1成分スカラー `Texture2D<float>`）を `D3D_SIF_TEXTURE_COMPONENTS` と `ReturnType` で照合します。
   - サンプラは `register(s0)` の静的サンプラ1本（Linear/Point・AddressMode設定）のみを許可し、動的サンプラ記述子ヒープを増やしません。
5. **深度とBlend／Rasterizer状態・遅延解放**:
   - `DepthFormat == None` のとき `bDepthTest == false && bDepthWrite == false` が必須、また `!bDepthTest && bDepthWrite` は拒否。
   - `ReleaseGraphicsPipeline` は番号を直ちに無効化して `Retired` へ移し、`CollectAfterGPUIdle()` でGPU完了後にのみPSO／RootSignatureを解放します。

## MRTパス・DrawGraphicsPipeline・ハザード・寿命保証

- **MRT能力照会とパス開始（`BeginRenderTargetPass`）**:
  - `FRenderTargetCapabilities::SupportsMultipleRenderTargets(Count)` で `bRGBA8RenderTarget && bReadableDepthD32 && Count >= 1 && Count <= MaxColorTargets`（DX12は8）を確認できます。
  - `FRenderTargetPassDesc` では `ColorTarget`（単一）と `ColorTargets`（1〜8枚MRT）の同時指定を拒否します。MRTではすべての色描画先が同じ `CDX12TextureStore` 所有の `CreateRenderTargetRGBA8` 資源であり、重複ハンドルがなく、互いに同一幅・同一高さ（任意 `DepthTarget` を含む）であることを要求します。
  - 各色アタッチメントは個別に `bClear`／`ClearColor` を指定でき、生成時の最適化クリア色と異なる色が渡されても全画面クリアPSOでD3D12デバッグ層警告なしにクリアします。`bClear == false` で既存内容を保持した描画（前パスの深度・色を引き継ぐ増分描画など）にも対応します。
  - MRT（`ColorViews.Num() > 1`）では2Dキャンバスの自動バッチは無効化しますが、`CDX12GameRenderer::BeginFrame` は先に描画先寸法で `Clip` を初期化するため、MRT時でも `DrawGraphicsPipeline` のviewport／scissorが有効に保たれます。
- **汎用描画命令の検証と一括拒否（`DrawGraphicsPipeline`）**:
  - 束縛中ターゲット数（主画面では1）とパイプラインの `NumRenderTargets` の一致、主画面（D32）またはオフスクリーン深度の有無と `DepthFormat == D32Float` の整合を確認します（主画面で `DepthFormat == None` と `D32Float` の双方に対応）。
  - 頂点・index範囲、バッファ用途（VS定数・Raw・Structuredに対する `MinimumBytes`／アラインメント／余りバイト）、画像・読み取り深度の種別と `EComputeTextureFormat`（`RGBA8`／`R32Float`）、1フレーム上限（1024描画）とピン留め上限を事前に全検証し、1つでも不正ならGPUコマンド・状態変更・ピン留めを一切行わず `false` を返します。
- **自己参照ハザード検出と未完了提出の寿命**:
  - パス内で現在書き込み中の色ターゲット（`ActiveColorTargets`）または深度ターゲット（`ActiveDepthTarget`）を `DrawGraphicsPipeline` の `Textures` へ渡す自己参照は拒否し、`bHazardRejected` によりそのパスの `EndRenderTargetPass()` も `false` で失敗通知します。
  - パスを閉じた `EndRenderTargetPass()` は `ColorTargets`・`DepthTarget` を `PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE` へ戻し、通常画面やcomputeと同じ `DIRECT` キューへ直ちに `ExecuteCommandLists` で提出します。提出後は同一キュー上で後続パス（MRT色2枚＋D32深度を同時に読む合成パイプライン描画等）や `SubmitCompute` 読み取り、主画面・撮影への描画が安全に繋がります。
  - パス中に使用したパイプライン・バッファ・色／深度ターゲットを提出直後に `Release*` しても、`CDX12RenderTargetPass::FSubmissionRecord` と `CDX12GeometryRenderer` のフレーム完了保持によりGPU完了まで実体を生存させます。

## 参照した公式オンライン文書（2026-11-12確認）

- [`ID3D12ShaderReflection::GetInputParameterDesc`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nf-d3d12shader-id3d12shaderreflection-getinputparameterdesc)
- [`ID3D12ShaderReflection::GetOutputParameterDesc`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nf-d3d12shader-id3d12shaderreflection-getoutputparameterdesc)
- [`D3D12_SHADER_DESC`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/ns-d3d12shader-d3d12_shader_desc)
- [`D3D12_SHADER_INPUT_BIND_DESC`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/ns-d3d12shader-d3d12_shader_input_bind_desc)
- [`ID3D12ShaderReflectionConstantBuffer`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nn-d3d12shader-id3d12shaderreflectionconstantbuffer)
- [`D3D12_GRAPHICS_PIPELINE_STATE_DESC`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_graphics_pipeline_state_desc)
- [`D3D12_INPUT_ELEMENT_DESC`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_input_element_desc)
- [`D3D12_ROOT_PARAMETER_TYPE`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_root_parameter_type)
- [`ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootconstantbufferview)
- [`ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootshaderresourceview)
- [`ID3D12GraphicsCommandList::OMSetRenderTargets`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetrendertargets)
- [Direct3D 12 Constants (`D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT`, `D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT`)](https://learn.microsoft.com/en-us/windows/win32/direct3d12/constants)

## 検査と結果

自己検査リポジトリ `framework-dx12-selftests`（`framework-test`）に `GraphicsPipelineTestData.h`、`GraphicsPipelineContract.vcxproj`、`GraphicsPipelineContractMain.cpp`、`GraphicsPipelinePlatformSelfTest.cpp`、`RunGraphicsPipelineTests.ps1` を追加し、`RunGameRenderDependencyCheckTest.ps1` の依存検査フィクスチャと `Tools/RunAllChecks.ps1` へ接続しました。

- 検査側の対応コミット: `a8ad0b810bc1059237dfed51f844b013486b9ec2`（tree `00dc9638fbf05eabc9baf37af8253a8c93f9d42e`、ローカル。公開はCodexまたは手動運用）。
- `GraphicsPipelineContract.exe`（Debug／Release ともに ImGui シンボル未リンクを確認）：
  - `mode=cpu`: 3項目（CPU期待値・MRTモデル参照検証）、0失敗。
  - `mode=default`（SDKデバッグ層有効）／`mode=warp`: 各66項目、0失敗（VS／PS reflection不一致・入力レイアウト・深度組み合わせ・スロット型違いの拒否、MRT 2色・8色同時書き込みとD32深度テスト・`bClear=false` 保持遷移、色2枚＋R32Float深度を読む合成描画パス、共有DIRECTキューのcompute読み戻しとCPU期待値比較、撮影ターゲットへの直接受け渡し、自己参照ハザードとパス失敗通知、不正引数の一括拒否、提出直後解放の未完了生存）。
- 通常アプリ（`--selftest=graphics-pipeline`、Debug／Release）：
  - `dx12`、`warp`、`dx12-no-ui`、`warp-no-ui` で全6フレーム（MRT 2色＋D32深度描画、色2枚＋R32Float深度の合成、`EFrameCaptureStage::Game` 撮影（2フレーム目）、提出直後 `ReleaseGraphicsPipeline`（3フレーム目）、再作成（4フレーム目）、主画面左右描画）が画素一致で通過。主画面判定領域は `CDebugOverlay` の `(16, 16)` ステータス表示と重ならない領域（Y=48..80）を使用し、UI有無の双方で独立検証。
  - `dxlib` と `null` で `IGraphicsPipelineResources` 非公開・`SupportsMultipleRenderTargets()` 非対応・`DrawGraphicsPipeline` 拒否を確認。
- 回帰・構成検証：
  - `project.sln` Debug／Release x64 ビルド：各警告0件・エラー0件。
  - `Tools/RunAllChecks.ps1 -SkipBuild -Quick`：全36ステップ合格（`Saved/ArgonCoordination/run_all_checks_r1_mrt.log`）。互換台帳は472項目（API 328、Draw 100、Sample 33、Editor 11；Implemented 186、Verified 0、Unreviewed 0 新規レビュー待ちなし）。

## 今回含めない範囲（明示的境界）

本段階はR1の汎用カスタムグラフィックスパイプライン契約と `RGBA8` MRT（最大8枚）＋ `D32_FLOAT` 深度の縦経路であり、以下は意図的にスコープ外としています。

- `RGBA16F`／HDR浮動小数レンダーターゲット、およびRTVの混在形式対応
- MSAA描画先、複数mip／配列テクスチャ、キューブマップ描画先
- PSによる深度出力（`SV_Depth`）および同一パス内での読み取り専用DSV同時サンプリング
- Shader Model 6／DXCコンパイラ経路、バイナリシェーダーキャッシュの永続化
- 非同期compute／copy複数キュー同期、indirect draw
- GPU timestamp計測・PIX領域・全R1資源横断のデバイス喪失自動復旧
- R2の永続GPUメッシュ・材質・Forward描画、およびR3のフレームグラフ・Deferred G-buffer／照明パス
