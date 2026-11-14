# compute画像の能力照会と読み書き・描画連携

2026-11-11（日本時間）。R1の既存buffer・compute・描画経路へ、RGBA8画像のcompute能力照会、読み取り／読み書き作成、プログラムへの画像slot宣言、compute提出と状態遷移、2D／3D描画への直接受け渡しを追加しました。画像所有・プログラム作成・compute提出はそれぞれ既存の `CDX12TextureStore`・`CDX12ComputePipelineStore`・`CDX12ComputeQueue` へ接続し、新しいSubsystemやImGui・アセット・ファイル入出力への依存は追加しません。

## 現在の対応（2026-11-14）

以下の初期実装記録に加え、現在はRGBA16Fの読み取り・読み書き、およびR32Floatの読み取りに対応しています。`FComputeTextureCapabilities` の対応する能力を形式と用途ごとに確認してください。R32Floatはカラー描画先またはD32深度を `Texture2D<float>` で読み取り、computeからの書き込みには対応しません。RGBA8／RGBA16Fは4成分として照合します。検査と制限は[描画先と深度](R1_RENDER_TARGET_RESOURCES.md)、[引き継ぎ](DX12_HANDOFF.md)も参照してください。

以下の能力条件と検査結果は、記載当時のRGBA8対応の記録です。

## 共通の値と窓口

|配置・型|役割|
|---|---|
|`Library/Core/Render/ComputeTextureAccess.h`|`EComputeTextureAccess::Read`（SRV、初期DX12の `t0`〜`t127`）と `ReadWrite`（UAV、初期DX12の `u0`〜`u63`）|
|`Library/Core/Render/ComputeTextureCapabilities.h`|`FComputeTextureCapabilities`（RGBA8／RGBA16FのRead・ReadWriteとR32FloatのRead）|
|`Library/Core/Render/ComputeTextureSlot.h`|`FComputeTextureSlot`（`Binding`、`Access`、`Format`）|
|`Library/Core/Interfaces/Render/IRenderTextureResources.h`|`GetComputeTextureCapabilities` と `CreateComputeTextureRGBA8` を追加。既定実装は出力を保持して `false`／無効ハンドルを返す|
|`Library/Core/Interfaces/Render/IComputePipelineResources.h`|`CreateComputePipelineWithTextures` を追加。既定実装は無効ハンドルを返す|
|`Library/Core/Render/ComputeDispatch.h`|`FComputeDispatch` の末尾へ `Textures` を追加し、既存の5項目初期化とのソース互換を維持|
|`Library/Platform/DX12/Resource/Texture/DX12TextureFormat.*`|`NDX12TextureFormat::GetComputeCapabilities` による `DXGI_FORMAT_R8G8B8A8_UNORM` のSRV・typed UAV load／store能力照会|
|`Library/Platform/DX12/Resource/Texture/DX12TextureBinding.h`|`FDX12TextureBinding`（提出へ渡すGPU画像実体 `Resource` と寸法 `Width`／`Height`）|

画像番号は `NRenderResourceSerial::Allocate()` から単調増加で発行し、解除後も再利用しません。作成時の書き込み可否 `bWritable` は `CDX12TextureStore::FTextureRecord` が所有し、`AcquireComputeTexture` で要求用途と照合したうえで `FDX12TextureBinding` を返します。別の `CDX12TextureStore` が発行した番号は照会・更新・提出で拒否します。

## 能力照会とプログラム照合

`NDX12TextureFormat::GetComputeCapabilities` は `DXGI_FORMAT_R8G8B8A8_UNORM` に対して以下を照会します。

- `bRGBA8Read`: `D3D12_FEATURE_DATA_FORMAT_SUPPORT::Support1` に `D3D12_FORMAT_SUPPORT1_TEXTURE2D` と `D3D12_FORMAT_SUPPORT1_SHADER_LOAD` の両方があること。
- `bRGBA8ReadWrite`: `bRGBA8Read` に加え、`D3D12_FEATURE_DATA_D3D12_OPTIONS::TypedUAVLoadAdditionalFormats`、個別形式の `D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW`、および `D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD` と `D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE` の**全て**を満たすこと。
- 未初期化時や照会失敗時は出力の `FComputeTextureCapabilities` を書き換えずに `false` を返します。

`CDX12ComputePipelineStore::CreateComputePipelineWithTextures` は、バッファと画像のslot合計が1〜16個であること、同じ `t` または `u` レジスターをバッファと画像、あるいは画像同士で重複宣言していないこと、そしてコンパイル済みCS5.0／5.1のreflectionが宣言と過不足なく一致することを検査します。

- 画像のReadは `D3D_SIT_TEXTURE`、ReadWriteは `D3D_SIT_UAV_RWTYPED`、次元は `D3D_SRV_DIMENSION_TEXTURE2D`、`ReturnType` は `D3D_RETURN_TYPE_FLOAT` または `D3D_RETURN_TYPE_UNORM`、要素数はRGBA8／RGBA16Fでは4成分、R32Float Readでは `FLOAT` の1成分、`BindCount == 1`、`Space == 0` を要求します。
- 1D／2D配列／3D、整数要素、2〜3成分、sampler（`s` レジスター）を含むシェーダーは拒否します。1成分はR32FloatのReadだけに対応します。
- 画像だけのプログラム（バッファslot 0個）も作成できます。

## 提出・状態遷移・寿命・スレッド契約

`CDX12ComputeQueue::SubmitCompute` は提出前にバッチ全体のプログラム・バッファ・画像を検証し、後続のdispatchに1つでも誤りがあれば先行dispatchも含めて記録・提出しません。

- `CreateTextureRGBA8` で作った通常の読み取り画像は `EComputeTextureAccess::Read` でだけ使え、`ReadWrite` へ渡した場合は提出前に拒否します。
- `CreateComputeTextureRGBA8` で作った画像は `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` を持ち、`Read` と `ReadWrite` の両方に使えて、そのまま `GetTextureResourceView` から2D／3D描画にも借りられます。
- 同一dispatch内で同じ画像番号を複数slotへ束縛する呼び出し（Read同士を含む）は拒否します。
- 画像の定常状態は `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` です。`ReadWrite` で使うdispatchの入口で `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` へ遷移し、dispatch後にUAV barrierと定常状態への復帰を記録するため、同一提出内の後続dispatchで直ちにSRVとして `Load` したり、compute提出直後の2D／3D描画でそのまま参照したりできます。
- 公開の `FComputeSubmission` 自体はキュー番号とチケット番号の値であり、キュー内部の未完了提出記録（`FSubmissionRecord`）がプログラムの `ComPtr<ID3D12PipelineState>` と `ComPtr<ID3D12RootSignature>`、バッファと画像の `ComPtr<ID3D12Resource>`、提出専用のshader-visible descriptor heap、およびcommand allocator／command listをGPU完了まで保持します。提出直後に `ReleaseTexture` で番号を無効化しても、また `UpdateTextureRGBA8` で同じ番号を新しいGPU画像実体へ差し替えても、先行提出は束縛時点の版を安全に読み書きします。`UpdateTextureRGBA8` は `CreateComputeTextureRGBA8` で作られた画像のUAV用途フラグを維持します。

`CDX12PlatformSubsystem` の公開窓口（`GetComputeTextureCapabilities`、`CreateComputeTextureRGBA8`、`CreateComputePipelineWithTextures` 等）はすべて `GAME_THREAD_ONLY` であり、ゲームスレッドから呼び出します。GPU操作は `ExecuteRender` を通じて専用描画スレッド（`CRenderCommandThread`）へ同期転送して実行し、Presentや外部窓更新で専用側をdrainして窓側へ排他的に貸している期間（`bWindowRenderPhase`）の再入はその窓側で実行します。

## 検査

別リポジトリ `framework-test` の `RunComputeTextureTests.ps1` を追加し、`Tools/RunAllChecks.ps1` へ接続しました。独立した実行ファイルの `.map` でImGui未リンクを確認し、SDKデバッグ層が有効な環境では警告以上を失敗とします。また、既存の `ComputeContract.vcxproj` へ今回の画像資源・同期の実装ファイルを追加し、Releaseで `/GL` 付きの `mazigatiDX.lib` をリンクする独立検査プロジェクトでは増分リンクを無効化して `/LTCG` へ統一しました。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunComputeTextureTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Debug -Platform
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunComputeTextureTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Release -Platform
```

- `ComputeTextureContract.exe`: CPU参照実装（`CReferenceComputeTextures`、27条件・失敗0）、既定GPU（67条件・失敗0）、WARP（67条件・失敗0）で能力照会、未対応・読み取り専用環境での拒否、寸法・null・別所有先・非書き込み画像・重複束縛・後半dispatch不正によるバッチ全体拒否、reflection負例（次元・型・要素数・sampler・レジスター衝突）、CS5.0／5.1×`float4`／`unorm float4` の定数・構造体・RAW・読み取り画像・読み書き画像の同時実行、提出直後の番号解除、`UpdateTextureRGBA8` 前後の版分離と書き込み用途維持、compute生成画像の3D描画・2Dキャンバス描画とCPU参照描画の全画素一致を検査します。
- 通常アプリ（`--selftest=compute-texture`）: `dx12`、`warp`、`dx12-no-ui`、`warp-no-ui` の4起動で6フレーム実行し、毎フレーム定数を変えたcompute画像生成、2D描画と主画面画素通知、2フレーム目のオフスクリーン撮影、3フレーム目の描画直後の解除、4フレーム目の別世代再作成、5フレーム目の `UpdateTextureRGBA8` を確認します。`dxlib` と `null` ではcompute画像の能力が公開されないことを確認します。

Debug／Releaseの4プロジェクトは警告・エラー0件でビルドでき、`RunComputeTextureTests.ps1 -Platform`（Debug／Release）および `Tools/RunAllChecks.ps1 -SkipBuild -Quick` の全32項目が成功しました（検査ログは検証用コピーの `Saved/ComputeTextureContract/<構成>` および `Saved/ArgonCoordination/run_all_checks.log`）。検査側の公開コミットは [`45c2635ce7fd854eec7de99e44f68ad0f69b1451`](https://github.com/mynameisGaku/framework-test/commit/45c2635ce7fd854eec7de99e44f68ad0f69b1451)（tree `e1df9788f48c9abcf37eb79024f9f5a79daf5a43`、検証済みローカル `5a609de7307934626f9eb37e66eabd0016ca4e4b` と同一treeでGitHub連携により公開）です。

## 残る範囲

初期実装はRGBA8 2D画像の1 mipを対象とし、その後RGBA16FとR32Float Readを追加しました。複数mip、配列・3D・キューブ画像、R32Floatのcompute書き込み、compute内のsampler、DXC／SM6、複数キュー、GPU障害回復全体、R2の永続GPUメッシュ・材質・Forward描画は残ります。
