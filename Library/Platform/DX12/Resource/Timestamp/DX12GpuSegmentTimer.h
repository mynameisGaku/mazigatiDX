// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Render/GpuTimestampHandle.h"
#include "Library/Core/Types/TypeDefinition.h"
#include <d3d12.h>

class CDX12TimestampStore;

// 診断用（--dx12-gpu-timings）。主画面の区間と、描画先パスごとのGPU時間を、公開の窓口を通さずに自動で計測して、
// 一定フレームごとにログへ、毎フレームの合計をプロファイラーのカウンター（Gpu.Frame / Gpu.Main / Gpu.Passes）へ出す。
// 描画スレッドだけから呼ぶ。付けないとき（既定）は、何もしない。
class CDX12GpuSegmentTimer
{
public:
	// 計測する区間の種類。同じ種類の区間は、同時には1つしか開けない。
	enum class ESegment : uint8
	{
		// 主画面（交換用画面）へ記録する区間。描画先パスの始まりで区切られる。
		Main = 0,
		// 描画先パス（BeginRenderTargetPass から EndRenderTargetPass まで）。
		Pass = 1,
	};

	// 区間の見出し（ログに出す）。
	struct FLabel
	{
		// 描画先の寸法（主画面は0）。
		int32 Width = 0;
		int32 Height = 0;
		// 色の描画先の枚数（主画面は0）。
		int32 NumColors = 0;
		// 深度の描画先があるか。
		bool bDepth = false;
	};

	// 使うタイムスタンプの所有者と、計測するかを決める。bInEnabled が false なら、以降の呼び出しは何もしない。
	void Initialize( CDX12TimestampStore *InStore, bool bInEnabled );
	// 計測しているか。
	bool IsEnabled() const;
	// 区間の開始点を、記録中のコマンド列へ積む。計測しない・問い合わせを作れない・記録できないときは、false（描画には影響しない）。
	bool OpenSegment( ESegment Segment, ID3D12GraphicsCommandList *Commands, uint64 BatchId, const FLabel &Label );
	// 開いている区間を閉じて、結果の回収待ちへ入れる。開いていなければ、何もしない。
	void CloseSegment( ESegment Segment, ID3D12GraphicsCommandList *Commands, uint64 BatchId );
	// 開いている区間を、計測せずに捨てる（記録の失敗・バッチの破棄のとき）。
	void DiscardSegment( ESegment Segment );
	// フレームの境目。完了した区間を回収して、終わったフレームの合計を集計し、一定フレームごとにログへ出す。
	void NotifyFrame();
	// GPUの完了後に、タイムスタンプの所有者の状態が初期化されたとき、回収待ちを捨てる（問い合わせの解放は、所有者が済ませている）。
	void Reset();

private:
	// 区間の行の数。行0が主画面、行1以降が、1フレーム内の描画先パスの順番（あふれたら最後の行へ足す）。
	static constexpr int32 MaxRows = 16;
	// 集計中のフレームを持つ輪の大きさ（GPUの完了待ちで、結果が数フレーム遅れて届く）。
	static constexpr int32 FrameRing = 16;
	// 何フレームごとにログへ出すか（--dx12-frame-timings と同じ）。
	static constexpr int32 LogFrames = 120;
	// 結果が回収できないまま、捨てるまでのフレーム数（バッチを破棄した区間の掃除）。
	static constexpr uint32 MaxPendingFrames = 8;

	// 開いている区間。
	struct FOpenSegment
	{
		FGpuTimestampHandle Query;
		int32 Row = 0;
		FLabel Label;
		bool bOpen = false;
	};
	// 閉じて、結果の回収を待っている区間。
	struct FPendingSegment
	{
		FGpuTimestampHandle Query;
		int32 Row = 0;
		uint32 FrameId = 0;
		FLabel Label;
	};
	// 集計中の1フレーム（全部の区間の結果がそろったら、合計を確定する）。
	struct FFrameSlot
	{
		uint32 FrameId = 0;
		int32 Outstanding = 0;
		float64 MainMs = 0;
		float64 PassMs = 0;
		bool bUsed = false;
		bool bClosed = false;
	};
	// 行ごとの集計（ログの区切りの間）。
	struct FRowStats
	{
		FLabel Label;
		float64 SumMs = 0;
		float64 MaxMs = 0;
		int32 Count = 0;
	};

	// 結果が届いた区間を回収する。
	void Poll();
	// 回収待ちの区間を1つ片付ける（bMeasured が false なら、時間は足さない）。
	void RetireSegment( int32 Index, bool bMeasured, float64 ElapsedMs );
	// そろったフレームの合計を確定して、カウンターへ出し、区切りのたびにログへ出す。
	void FinalizeFrame( FFrameSlot &Slot );
	// 区切りの間の集計を、ログへ出して、初期化する。
	void LogWindow();

	CDX12TimestampStore *Store = nullptr;
	bool bEnabled = false;
	FOpenSegment Opened[2];
	TArray<FPendingSegment> Pending;
	FFrameSlot Frames[FrameRing];
	uint32 CurrentFrameId = 0;
	int32 PassOrdinal = 0;
	FRowStats Rows[MaxRows];
	int32 WindowFrames = 0;
	float64 WindowTotalMs = 0;
	float64 WindowMaxTotalMs = 0;
	float64 WindowMainMs = 0;
	float64 WindowPassMs = 0;
	int32 NumDropped = 0;
};
