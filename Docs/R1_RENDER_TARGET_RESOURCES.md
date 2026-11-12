# オフスクリーン描画先資源と読み取れる深度・compute連携

2026-11-11（日本時間）。R1の既存画像・描画・compute経路へ、単一のRGBA8色描画先（RTV＋SRV）と読み取れるD32深度（DSV＋SRV）の能力照会・資源作成、共通窓口からのオフスクリーン描画と同一 `DIRECT` キューへの実GPU提出、色（`Texture2D<float4>`）とスカラー深度（`Texture2D<float>`）を同時に読むcompute、ならびに通常画面・オフスクリーン撮影への受け渡しを追加しました。新しい資源番号体系や別インターフェースは増やさず、既存の `IRenderTextureResources`・`FTextureResourceHandle`・`NRenderResourceSerial`・`CDX12TextureStore`・`CDX12DescriptorPool`・`IRenderSurface`・`CDX12ComputePipelineStore`・`CDX12ComputeQueue` を拡張して接続しています。

## 共通の値と窓口

|配置・型|役割|
|---|---|
|`Library/Core/Render/ComputeTextureFormat.h`|`EComputeTextureFormat::RGBA8`（4成分浮動小数、既定値）と `R32Float`（1成分スカラー浮動小数、読み取り専用）|
|`Library/Core/Render/ComputeTextureCapabilities.h`|`FComputeTextureCapabilities` に `bR32FloatRead` を追加|
|`Library/Core/Render/ComputeTextureSlot.h`|`FComputeTextureSlot` の末尾へ `Format = EComputeTextureFormat::RGBA8` を追加し、既存の2要素初期化とのソース互換を維持|
|`Library/Core/Render/RenderTargetCapabilities.h`|`FRenderTargetCapabilities`（`bRGBA8RenderTarget`、`bReadableDepthD32`、`IsSupported()`）|
|`Library/Core/Render/RenderTargetPassDesc.h`|`FRenderTargetPassDesc`（`ColorTarget`、`DepthTarget`、`Scale`、`ClearColor`、`ClearDepth`、`bClearColor`、`bClearDepth`）|
|`Library/Core/Interfaces/Render/IRenderTextureResources.h`|`GetRenderTargetCapabilities`、`CreateRenderTargetRGBA8`、`CreateDepthTextureD32` を追加。既定実装は出力を保持して `false`／無効ハンドルを返す|
|`Library/Core/Interfaces/Render/IRenderSurface.h`|`BeginRenderTargetPass` と `EndRenderTargetPass` を追加。既定実装は `false` を返す|
|`Library/Platform/DX12/Resource/Texture/DX12TextureFormat.*`|`NDX12TextureFormat::GetComputeCapabilities`（`R32_FLOAT` 読み取りを含む）と `GetRenderTargetCapabilities` による形式能力照会|
|`Library/Platform/DX12/Resource/Texture/DX12TextureBinding.h`|`FDX12TextureBinding` に資源参照、寸法、`TargetHeap`、`SrvGpuDescriptor`、`InitialClearColor`、`InitialClearDepth`、`Format` を保持|
|`Library/Platform/DX12/Render/Target/DX12RenderTargetPass.*`|`CDX12RenderTargetPass`（オフスクリーン描画パスのコマンドリスト・フェンス・未完了提出記録・省略時の内部深度を所有し、GPUコマンドの開始・遷移・提出を受け持つ）|

機能やGPU資源・descriptor heap・進行中状態を所有・管理する型は `C`（`CDX12TextureStore`、`CDX12RenderTargetPass`、`CDX12GameRenderer`、`CDX12ComputePipelineStore`、`CDX12ComputeQueue`、検査の `CReferenceRenderTargets`・`CRenderTargetPlatformScene`）、設定・能力・ハンドル・提出番号の値型は `F`（`FRenderTargetCapabilities`、`FRenderTargetPassDesc`、`FComputeTextureCapabilities`、`FComputeTextureSlot`、`FTextureResourceHandle`、`FComputeSubmission`）、列挙は `E`（`EComputeTextureFormat`）、抽象窓口は `I`（`IRenderTextureResources`、`IRenderSurface`）で統一しています。

## 能力照会と資源作成・用途制約

### 形式能力の照会条件

`NDX12TextureFormat::GetComputeCapabilities` および `NDX12TextureFormat::GetRenderTargetCapabilities` は `ID3D12Device::CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, ...)` を用いて以下を照会します。未初期化時や照会失敗時は出力構造体を書き換えずに `false` を返します。

- `FComputeTextureCapabilities::bR32FloatRead`: `DXGI_FORMAT_R32_FLOAT` の `Support1` に `D3D12_FORMAT_SUPPORT1_TEXTURE2D` と `D3D12_FORMAT_SUPPORT1_SHADER_LOAD` の**両方**（AND）があること。
- `FRenderTargetCapabilities::bRGBA8RenderTarget`: `DXGI_FORMAT_R8G8B8A8_UNORM` の `Support1` に `D3D12_FORMAT_SUPPORT1_TEXTURE2D`、`D3D12_FORMAT_SUPPORT1_RENDER_TARGET`、`D3D12_FORMAT_SUPPORT1_SHADER_LOAD`、`D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE`、`D3D12_FORMAT_SUPPORT1_BLENDABLE` の**全て**（AND）があること。
- `FRenderTargetCapabilities::bReadableDepthD32`: `DXGI_FORMAT_D32_FLOAT` の `Support1` に `D3D12_FORMAT_SUPPORT1_TEXTURE2D` と `D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL` の**両方**（AND）があり、**かつ** `DXGI_FORMAT_R32_FLOAT` の `Support1` に `D3D12_FORMAT_SUPPORT1_TEXTURE2D` と `D3D12_FORMAT_SUPPORT1_SHADER_LOAD` の**両方**（AND）があること。
- `FRenderTargetCapabilities::IsSupported()`: `bRGBA8RenderTarget && bReadableDepthD32`（両方のAND）を返します。

### 色描画先と読み取れる深度の所有

すべての画像番号は `NRenderResourceSerial::Allocate()` から単調増加で発行し、解除後も再利用しません。`CDX12TextureStore` が内部の `ETextureKind` で通常画像・compute画像・色描画先・D32深度を区別し、用途と画像形式の要求を照合します。

1. `CreateRenderTargetRGBA8(Width, Height, InitialClearColor)`:
   - 資源形式 `DXGI_FORMAT_R8G8B8A8_UNORM`、`D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET`で作成します。
   - 最適化クリア値に `InitialClearColor` を設定し、初期状態 `D3D12_RESOURCE_STATE_RENDER_TARGET` で作成直後に1回クリアして定常状態 `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` へ遷移・GPU完了待機します。そのため、一度も描画パスを開かずに `GetTextureResourceView` やcompute読み取りへ渡しても未初期化になりません。
   - 共有SRVプール（`CDX12DescriptorPool`）の記述子を持つため、描画後にそのまま `GetTextureResourceView` で通常画面や撮影の2D／3D描画へ貼ることができます。また `AcquireComputeTexture` で `EComputeTextureAccess::Read`（`EComputeTextureFormat::RGBA8`）として使えます。色描画先へのUAV要求は拒否し、UAV出力は既存の `CreateComputeTextureRGBA8` で作成します。
2. `CreateDepthTextureD32(Width, Height, InitialClearDepth)`:
   - 同一GPU資源に対してDSVとSRVの両方を作るため、資源本体は `DXGI_FORMAT_R32_TYPELESS`、`D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL` で作成します。
   - 最適化クリア値には `DXGI_FORMAT_D32_FLOAT` と `InitialClearDepth`（`0.0f`〜`1.0f` の有限値、範囲外やNaN／Infは作成拒否）を設定し、初期状態 `D3D12_RESOURCE_STATE_DEPTH_WRITE` でクリア後に定常状態 `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` へ遷移します。
   - 描画パス中は `DXGI_FORMAT_D32_FLOAT` のDSVとして束縛し、compute読み取り時は `DXGI_FORMAT_R32_FLOAT` のSRVとして束縛します。
   - 既存の2D／3Dピクセルシェーダーは `float4` RGBAサンプリング前提であるため、深度番号に対する `GetTextureResourceView` は出力を保って `false` を返し、また `UpdateTextureRGBA8` や `EComputeTextureAccess::ReadWrite`（UAV）、`EComputeTextureFormat::RGBA8` 指定も拒否します。深度の画面可視化は、computeで `EComputeTextureFormat::R32Float` として読み出し、RGBA8画像へ変換してから通常描画へ渡します。

## オフスクリーン描画パスと実GPU提出・compute連携

### 描画パスの記録と同一DIRECTキューへのGPU提出

`IRenderSurface::BeginRenderTargetPass(const FRenderTargetPassDesc &)` と `EndRenderTargetPass()` は、`CDX12GameSurface`（および専用描画スレッド転送層の `CThreadedRenderSurface`）から `CDX12Device`・`CDX12RenderTargetPass`・`CDX12GameRenderer` へ接続されています。

1. **開始検査（`BeginRenderTargetPass`）**:
   - オフスクリーン描画パスの二重開始、またはオフスクリーン撮影（`BeginOffscreenCapture`）中の開始を拒否します。
   - `ColorTarget` は同じ `CDX12TextureStore` が現在有効に所有する `CreateRenderTargetRGBA8` の資源であることを要求します（無効番号、解放済み、別所有先、通常の `CreateTextureRGBA8`／`CreateComputeTextureRGBA8`、深度資源はすべて拒否）。
   - `DepthTarget.IsValid()` の場合は、同じ `CDX12TextureStore` が所有する `CreateDepthTextureD32` の資源であり、かつ `ColorTarget` と**同一の幅・高さ**であることを要求します。`bClearDepth` が有効な場合は `ClearDepth` が `0.0f`〜`1.0f` の有限値であることも検査します。
   - `DepthTarget` が無効番号の場合は、オフスクリーン幅・高さに合わせた内部の `DXGI_FORMAT_D32_FLOAT` 深度バッファを用意して3D描画のZテストを維持します。初回の内部深度は `bClearDepth == false` でも1で初期化し、同じ寸法の後続パスでは既存内容を保ちます。初期化は命令の記録時点でなく、実際に提出した時点で確定します。
2. **コマンドリスト切り替えと状態遷移**:
   - 主画面フレーム中は、CPUの2Dバッチを描画スレッドへ渡した後、`CDX12Device::SubmitFrameSegment` でそこまでの主画面命令を同じDIRECTキューへ実際に提出します。命令列だけをResetし、allocatorは次のフレームでGPU完了を確認するまでResetしません。主画面の色・深度・キャンバスのクリップや補間状態を消さず、オフスクリーン専用の命令列へ切り替えます。区切りの追加によるCPUのGPU待機はありません。
   - `ColorTarget` を定常SRV状態から `D3D12_RESOURCE_STATE_RENDER_TARGET` へ、`DepthTarget`（指定時）を定常SRV状態から `D3D12_RESOURCE_STATE_DEPTH_WRITE` へ遷移し、RTV／DSVの設定、必要に応じた `ClearRenderTargetView`／`ClearDepthStencilView`、およびオフスクリーン寸法のviewport／scissorを設定します。
   - パス中は既存の `GetCanvas2D()`・`GetGeometry3D()`・`ExecuteDrawSpan` をそのまま使って2D／3D図形やGPUバッファ図形を描画できます。
3. **競合（ハザード）拒否**:
   - パス開始から終了までの間、描画先として束縛されている `ColorTarget` と `DepthTarget` には `CDX12TextureStore` が `ActiveColorTarget`／`ActiveDepthTarget` に番号を記録します。
   - この状態の資源に対して、同じパス内から `GetTextureResourceView`（自己参照描画）や `SubmitCompute`（SRV／UAV束縛）を行うと拒否します。描画先・深度に対する `UpdateTextureRGBA8` は用途違いとして拒否し、`ReleaseTexture` は番号を直ちに無効化してGPU実体の回収を延期します。他の無関係なテクスチャをパス内の描画でサンプリングすることは可能です。
4. **終了と実GPU提出（`EndRenderTargetPass`）**:
   - `CDX12GameRenderer::EndFrame()` でオフスクリーンコマンドリストへ残りの描画バッチを記録した後、`ColorTarget` と `DepthTarget` を定常状態 `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` へ戻すバリアを記録します。
   - **重要**: CPU上のコマンドリストへ積むだけで終わらせず、直ちにコマンドリストを `Close()` して、主画面・computeと共有する同じ `DIRECT` キュー（`CommandQueue`）へ `ExecuteCommandLists` で**実際にGPU提出**し、パス所有者のfenceに提出番号を付けます。GPU完了は待たず、未完了提出記録が実体を保持します。
   - これにより、`EndRenderTargetPass()` の直後に `SubmitCompute` を呼んだ場合でも、同じ `DIRECT` キュー上で必ず「オフスクリーン描画コマンドの実行とSRV遷移」が「computeコマンドの実行」より先にGPUへ到達し、`ColorTarget` と `DepthTarget` の描画結果を安全に `Load` できます。主画面へ戻った次の描画命令は、その主画面のRTV／DSV・viewport／scissor・PSO・記述子を設定し直して記録を継続します。主画面の深度・クリップは保持し、切り替え前に借りた窓口は再取得します。

### スカラー深度を読むcompute reflectionとSRV生成

- `CDX12ComputePipelineStore::CreateComputePipelineWithTextures` は、各 `FComputeTextureSlot` の `Format` を検査します。
  - `EComputeTextureFormat::RGBA8`: `Read`／`ReadWrite` ともに対応し、シェーダーのreflectionが4成分（`(uFlags & D3D_SIF_TEXTURE_COMPONENTS) == D3D_SIF_TEXTURE_COMPONENTS`、すなわち `Texture2D<float4>`／`RWTexture2D<float4>`／`RWTexture2D<unorm float4>`）であることを要求します。
  - `EComputeTextureFormat::R32Float`: `EComputeTextureAccess::Read` のみ許可し（`ReadWrite` はパイプライン作成時に拒否）、reflectionが `D3D_SIT_TEXTURE`、`D3D_SRV_DIMENSION_TEXTURE2D`、`ReturnType == D3D_RETURN_TYPE_FLOAT`、かつ1成分スカラー（`(uFlags & D3D_SIF_TEXTURE_COMPONENTS) == 0`、すなわち `Texture2D<float>`）であることを要求します。
  - スロット宣言が `RGBA8` なのにシェーダー側が `Texture2D<float>` である場合や、スロット宣言が `R32Float` なのにシェーダー側が `Texture2D<float4>` である場合は不一致としてパイプライン作成を拒否します。
- `CDX12ComputeQueue::SubmitCompute` は、各画像slotについて `CDX12TextureStore::AcquireComputeTexture(Handle, Slot.Access, OutBinding, Slot.Format)` を呼び出し、要求された `Format` と資源の作成種別が一致することを検証したうえで、提出専用のshader-visible descriptor heap上に照合済みの `DXGI_FORMAT_R8G8B8A8_UNORM` または `DXGI_FORMAT_R32_FLOAT` のSRVを生成します。

### 寿命管理とスレッド契約

- `CDX12RenderTargetPass` はパス実行中の `ActiveColor` と `ActiveDepth` の `FDX12TextureBinding` を保持し、`EndRenderTargetPass()` でGPU提出する際に、これらとallocator・命令列・頂点・バッファ参照を未完了提出記録へ移します。パスfenceの実完了値を確認してから回収します。
- `CDX12ComputeQueue` の未完了提出記録（`FSubmissionRecord`）は、`ColorTarget` や `DepthTarget` の `ComPtr<ID3D12Resource>` をGPU fence完了まで保持します。compute提出直後に `ReleaseTexture` で色描画先や深度の番号を解除しても、先行提出は束縛時点のGPU資源を安全に読み取ります。
- `CDX12TextureStore` の更新前資源や解放済み資源・SRV記述子は `Retired` に残し、同じキューのGPU完了を所有側が確認して `CollectAfterGPUIdle` を呼んだときに回収します。未提出の描画が参照している間は回収しません。
- 公開窓口（`GetRenderTargetCapabilities`、`CreateRenderTargetRGBA8`、`CreateDepthTextureD32`、`BeginRenderTargetPass`、`EndRenderTargetPass` 等）はすべて `GAME_THREAD_ONLY` であり、ゲームスレッドから呼び出します。`CThreadedRenderSurface` と `CDX12PlatformSubsystem::ExecuteRender` が専用描画スレッド（`CRenderCommandThread`）へ同期転送して順序どおりに実行します。

## 参照した公式オンライン文書（2026-11-11確認）

- [`ID3D12Device::CheckFeatureSupport`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-checkfeaturesupport)
- [`D3D12_FORMAT_SUPPORT1`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_format_support1)（`D3D12_FORMAT_SUPPORT1_RENDER_TARGET`、`D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL`、`D3D12_FORMAT_SUPPORT1_SHADER_LOAD`、`D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE`）
- [`D3D12_RESOURCE_FLAGS`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_flags)（`D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET`、`D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL`、`D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`）
- [`DXGI_FORMAT`](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format)（同一の深さ資源でDSVとSRVを共存させるための `DXGI_FORMAT_R32_TYPELESS`、`DXGI_FORMAT_D32_FLOAT`、`DXGI_FORMAT_R32_FLOAT` の対応関係）
- [Using Resource Barriers to Synchronize Resource States in Direct3D 12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)（`D3D12_RESOURCE_STATE_RENDER_TARGET`／`D3D12_RESOURCE_STATE_DEPTH_WRITE` と `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` の遷移）
- [Executing and Synchronizing Command Lists](https://learn.microsoft.com/en-us/windows/win32/direct3d12/executing-command-lists)（同一 `ID3D12CommandQueue` への `ExecuteCommandLists` 提出順序とGPU実行順序の保証）
- [`D3D_SHADER_INPUT_FLAGS`](https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_shader_input_flags)（`D3D_SIF_TEXTURE_COMPONENTS` による `Texture2D<float>` と `Texture2D<float4>` の成分数識別）

## 検査

撮影の独立検査では、検査側の `CStandaloneCaptureSurface` が既存の撮影先とゲーム描画器を組み合わせます。製品の `CDX12RenderTargetPass` は撮影型・撮影分岐を持ちません。

別リポジトリ `framework-test` に `RenderTargetTestData.h`、`RenderTargetContract.vcxproj`、`RenderTargetContractMain.cpp`、`RenderTargetPlatformSelfTest.cpp`、`RunRenderTargetTests.ps1` を追加し、`Tools/RunAllChecks.ps1` へ接続しました。独立した実行ファイルの `.map` でImGui未リンクを確認し、SDKデバッグ層が有効な環境では警告以上を失敗とします。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunRenderTargetTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Debug -Platform
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunRenderTargetTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Release -Platform
```

- `RenderTargetContract.exe`: CPU参照実装（`CReferenceRenderTargets`）、既定GPU、WARPのそれぞれで、能力照会、未対応環境の拒否、寸法・無効深度値・別所有先・非描画先画像・寸法不一致の拒否、パス中の自己参照描画・更新・compute競合と二重開始の拒否、番号解除後の実体保持、`Texture2D<float>`／`Texture2D<float4>` のreflection照合と `R32Float` UAV拒否、オフスクリーン3D描画での前後2枚のquadのZ遮蔽（前方が勝つ領域と後方のみが見える領域）、同一 `DIRECT` キューへの提出順序に基づくcomputeでの色＋スカラー深度同時読み取りと可視化出力、オフスクリーン色描画先と深度可視化画像の通常画面およびオフスクリーン撮影への2D／3D描画と全画素一致、ならびにcompute提出直後の `ReleaseTexture` と別寸法再作成を検査します。
- 通常アプリ（`--selftest=render-target`）: `dx12`、`warp`、`dx12-no-ui`、`warp-no-ui` の4起動で6フレーム実行し、毎フレームのオフスクリーン3D描画（色＋D32深度）→compute合成（色＋スカラー深度読み取り）→主画面への左右描画（左：オフスクリーン色描画先、右：深度＋色compute可視化）と画素検証、2フレーム目のオフスクリーン撮影、3フレーム目のcompute提出直後の解放、4フレーム目の8×8資源再作成、更新前の色・compute画像を先に主画面へ描いた場合の旧画素保持、主画面の深度・クリップを保ったパス切り替えと復帰、撮影を閉じる前の別compute提出の拒否を確認します。`dxlib` と `null` ではオフスクリーン描画先と `bR32FloatRead` の能力が公開されないことを確認します。

検査側の対応コミット: `633d2edc7f25ebe181c7f0e78031454c2dafd9e4`（tree `ab58ca0b8e0a749ea7db6fcc9d8da8042654b94a`、ローカル。公開はCodex担当）。Debug／Release各4プロジェクトのビルド0警告・0エラー、`RunRenderTargetTests.ps1 -Platform`（CPU 45・既定GPU 51・WARP 51条件、通常アプリ6起動）、`RunAllChecks.ps1 -SkipBuild -Quick` 全34項目成功。

## 残る範囲

今回は単一のRGBA8色描画先と読み取れるD32スカラー深度の垂直スライスです。複数描画先（MRT）と複数 `SV_Target` を出力する描画PSO・シェーダー経路、HDR浮動小数描画先（RGBA16F等）、ステンシル併用形式、MSAA、複数mip／配列描画先、GPU timestamp計測、複数キュー間の非同期実行、R2の永続GPUメッシュ・材質・Forward、R3のフレームグラフとG-buffer／Deferredは後続段階で実装します。


## 公開前レビューの継続

Argon側の全34項目成功（Saved/ArgonCoordination/run_all_checks_r1_render_target.log）後、撮影の検査接続を検査リポジトリへ移し、内部深度の初回Loadを実GPUで再現して修正しています。修正前後の記録はSaved/ArgonCoordination/fallback-regression-before.logとcodex-render-target-Debug.logへ分けます。前の34項目成功を修正後の検証と混同しません。

主画面の旧画素が後続の描画先更新・compute書き込み後の値へ変わる不具合を6フレームすべてで再現しました（Saved/ArgonCoordination/codex-ordering-before.log）。提出を区切る修正と、更新前後の画素・主画面の深度とクリップの保持を確認する回帰検査を追加しました。修正後の記録はcodex-ordering-Debug.log／codex-ordering-Release.logへ保存します。R1全体は未完了で、MRT・timestamp・障害回復等の残件は別に完了条件へ照合します。

### 通常画面と別提出の順序

ゲームスレッド向けの `SubmitCompute` は、先行するCPU描画バッチを確定し、通常画面の未提出GPU命令も同じキューへ提出してからcomputeを提出します。同じ画像やGPUバッファを「通常描画で読む→computeで書く→通常描画で読む」と使っても、先の描画が更新後の値へ変わりません。画像の版と頂点・indexの参照は、従来どおりフレーム全体のGPU完了まで保持します。

現在のゲーム用接続では、オフスクリーン描画先パス・撮影の記録中に別のcompute提出を挟むことは拒否します。`EndRenderTargetPass` または `EndOffscreenCapture` の後に提出してください。ネイティブの `CDX12ComputeQueue` 単体はパスのスケジューラーを持たないため、直接組み合わせる所有者が提出順を決め、描画中の資源との束縛競合はTextureStoreが拒否します。

根拠はMicrosoftの [命令列の記録と再利用](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles) と [実行・同期](https://learn.microsoft.com/en-us/windows/win32/direct3d12/executing-and-synchronizing-command-lists)（2026-11-11確認）です。命令列のResetとallocatorのResetを区別し、GPU待機や回収をCPUの受付完了で代用しません。
