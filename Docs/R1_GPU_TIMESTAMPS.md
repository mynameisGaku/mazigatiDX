# GPUタイムスタンプ計測契約（R1）

2026-11-12（日本時間）。[ロードマップ](ROADMAP.md) R1 GPU基盤の未着手項目であった `timestamp` として、API非依存の能力照会・計測ハンドル作成・遅延スロット回収・Graphics範囲計測（主画面および `BeginRenderTargetPass`〜`EndRenderTargetPass` MRT/D32パス）・Compute一括提出計測（`SubmitComputeWithTimestamp`）・Fence連動非同期解決と読み取りを実装しました。既存の `CDX12Device`・`CDX12ComputeQueue`・`CDX12RenderTargetPass`・`CDX12GameSurface`・`CThreadedRenderSurface`・`CDX12PlatformSubsystem` へ接続し、新しいSubsystemやImGui依存は増やしていません。

## 共通の値・窓口とDX12実装の配置

|配置・型|役割|
|---|---|
|`Library/Core/Render/GpuTimestampHandle.h`|`NRenderResourceSerial` による非再利用 `uint64 Value` を保持する `FGpuTimestampHandle`（0は無効）|
|`Library/Core/Render/GpuTimestampCapabilities.h`|`bSupported`・キュー周波数 `FrequencyHz`・同時保持上限 `MaxActiveQueries` を返す `FGpuTimestampCapabilities`|
|`Library/Core/Render/GpuTimestampResult.h`|`BeginTicks`・`EndTicks`・`FrequencyHz`・`ElapsedMs` を返す `FGpuTimestampResult`|
|`Library/Core/Interfaces/Render/IGpuTimestampResources.h`|`GetGpuTimestampCapabilities` / `CreateGpuTimestampQuery` / `ReleaseGpuTimestampQuery` / `IsGpuTimestampReady` / `WaitGpuTimestamp` / `GetGpuTimestampResult` を公開|
|`Library/Core/Interfaces/Render/IRenderSurface.h`|記録中の主画面またはオフスクリーン描画パス内で範囲計測を積む `BeginGpuTimestamp` / `EndGpuTimestamp`（既定実装 `false`）|
|`Library/Core/Interfaces/Render/IComputeQueue.h`|既存の `FComputeDispatch` 構造体初期化を壊さず提出前後の計測を積む `SubmitComputeWithTimestamp`（別名virtualメソッド・既定無効返却）|
|`Library/Framework/Render/Thread/ThreadedRenderSurface.*`|専用描画スレッド上で溜まった2Dバッチを `Execute` で反映した直後に `BeginGpuTimestamp` / `EndGpuTimestamp` を実行|
|`Library/Platform/DX12/Resource/Timestamp/DX12TimestampStore.*`|`D3D12_QUERY_HEAP_TYPE_TIMESTAMP`（512スロット = 256ペア）・`D3D12_HEAP_TYPE_READBACK` 4KiBバッファ・専用 `ID3D12Fence`・遅延スロット回収を所有する `CDX12TimestampStore`|
|`Library/Platform/DX12/Command/Compute/DX12ComputeQueue.*`|共通 `SubmitComputeInternal` で事前全検証を通過した場合のみcomputeコマンド列の前後へ `EndQuery` / `ResolveQueryData` とFenceを結合|
|`Library/Platform/DX12/Render/Target/DX12RenderTargetPass.*`|オフスクリーン描画パス用バッチでの開始/終了記録、および未閉じスコープが残った `End()` 時のバッチ破棄＋`false` 返却|
|`Library/Platform/DX12/Device/DX12Device.*`|`CDX12TimestampStore` の所有・終了回収、主画面タイムスタンプバッチ管理、`SubmitFrameSegment` 時の未閉じスコープ拒否、`SubmitFrame` 時の安全破棄＋提出Fence確定|
|`Library/Platform/DX12/Render/Game/DX12GameSurface.*`|現在のターゲットに応じて `DX12RenderTargetPass` または `DX12Device` へ転送し、オフスクリーン撮影中（`Capture.IsActive()`）は `false` で拒否|
|`Library/Platform/DX12/Window/DX12PlatformSubsystem.*`|`IGpuTimestampResources` の公開（`GetAppSubsystem<IGpuTimestampResources>()`）・全操作の `GAME_THREAD_ONLY` 強制・`SubmitComputeWithTimestamp` 前の主画面待ちFlush|

役割分類（`Tools/CheckDX12Architecture.ps1`）では `CDX12TimestampStore` を機能・所有者（`C`、53型へ登録）、`FGpuTimestampCapabilities` / `FGpuTimestampHandle` / `FGpuTimestampResult` をデータ（`F`）へ統一し、公開層・`DX12TimestampStore` へのImGui／DxLib／`Framework/` 依存混入を `Tools/CheckDX12DeviceDependencies.ps1` および `Tools/CheckGameRenderDependencies.ps1` で禁止・検証しています。

## スロット状態遷移・DIRECTキュー同期・安全保証

1. **同一 `DIRECT` キューとReadback配置**:
   - `CDX12TimestampStore` は描画・computeと同一の `ID3D12CommandQueue`（`D3D12_COMMAND_LIST_TYPE_DIRECT`）から `GetTimestampFrequency()` を取得し、`D3D12_QUERY_HEAP_TYPE_TIMESTAMP`（`MaxActiveQueries * 2 = 512` 個）と `4096` バイトの `D3D12_HEAP_TYPE_READBACK`（初期状態 `D3D12_RESOURCE_STATE_COPY_DEST`、1値8バイト・256ペアで計4096バイト）を作成します。
   - `ElapsedMs` は `((EndTicks - BeginTicks) * 1000.0) / FrequencyHz` で算出し、ラップ・異常値時もアンダーフローしません。
2. **状態遷移と物理スロットの遅延解放**:
   - 各ハンドルは `Idle` → `Recording`（`BeginQuery` の代わりに `EndQuery(TIMESTAMP, Slot*2)`）→ `PendingSubmit`（`EndQuery(TIMESTAMP, Slot*2+1)` ＋ `ResolveQueryData`）→ `Submitted(FenceValue)`（`ExecuteCommandLists` ＋ `Queue->Signal` 後）→ `Completed`（Fence完了後に1度だけMapして `CachedResult` へ読出し）へ遷移します。`Completed` 後の再記録（`BeginGpuTimestamp` / `SubmitComputeWithTimestamp`）も許可します。
   - `ReleaseGpuTimestampQuery` は呼び出し直後に `FGpuTimestampHandle` を `LiveQueries` から削除して無効化しますが、`PendingSubmit`（`BatchId` 保有中）または `Submitted`（`FenceValue` 保有中）または `Recording`（コマンドリストへ `EndQuery` 済み）のスロットは `RetiredSlots` へ移し、`AbortBatch` または対応GPU Fenceの完了後まで `FreeSlots` へ戻しません。これにより上限 256 件超過時は新規作成が失敗し、完了後に別シリアル番号で安全に空きを回復します。
3. **不整合・競合・デバイス喪失への防御**:
   - 主画面（`CDX12Device`）で未閉じの `Recording` スコープを開いたまま `SubmitFrameSegment()`（`BeginRenderTargetPass` / `BeginOffscreenCapture` / `SubmitCompute` 切替）へ進もうとすると拒否します。
   - 主画面のフレーム終了（`SubmitFrame`）で万一閉じ忘れが残っていても `AbortUnclosedInBatch` により不正な `ResolveQueryData` を書かず安全に `Idle` へ戻します。逆に `CDX12RenderTargetPass::End()` では未閉じスコープがあれば `AbortBatch` してパス自体を提出せず `false` を返します。
   - `WaitGpuTimestamp` は `TimeoutMs > 60000` を拒否し、`TimeoutMs == 0` は未完了なら即 `false` を返します。`Fence->GetCompletedValue() == UINT64_MAX` や `ID3D12Device::GetDeviceRemovedReason()` 失敗時は `bHealthy = false` として後続の全計測APIを安全失敗させます。
4. **今回含めない範囲**:
   - Copyキュー専用（`D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP`）、Pipeline Statistics／Occlusion Query、CPU/GPUクロック較正（`GetClockCalibration`）と `CProfilerSubsystem` 自動集計（R9で接続予定）、同期撮影 `BeginOffscreenCapture` 内の計測、複数コマンドリストに跨ぐ同一スコープ。

## 参照した公式オンライン文書（2026-11-12確認）

- [`ID3D12Device::CreateQueryHeap`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createqueryheap)
- [`D3D12_QUERY_HEAP_TYPE`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_query_heap_type)
- [`D3D12_QUERY_TYPE`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_query_type)
- [`ID3D12GraphicsCommandList::EndQuery`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-endquery)
- [`ID3D12GraphicsCommandList::ResolveQueryData`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resolvequerydata)
- [`ID3D12CommandQueue::GetTimestampFrequency`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-gettimestampfrequency)
- [Timing (Direct3D 12)](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing)
