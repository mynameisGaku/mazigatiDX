// SPDX-License-Identifier: MIT
#include "DX12GpuSegmentTimer.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/DX12/DX12Log.h"
#include "Library/Platform/DX12/Resource/Timestamp/DX12TimestampStore.h"

void CDX12GpuSegmentTimer::Initialize( CDX12TimestampStore *InStore, bool bInEnabled )
{
	Reset();
	Store = InStore;
	bEnabled = bInEnabled && InStore != nullptr;
}

bool CDX12GpuSegmentTimer::IsEnabled() const
{
	return bEnabled;
}

bool CDX12GpuSegmentTimer::OpenSegment( ESegment Segment, ID3D12GraphicsCommandList *Commands, uint64 BatchId, const FLabel &Label )
{
	if ( !bEnabled )
	{
		return false;
	}
	DiscardSegment( Segment ); // 閉じ忘れの区間は捨てる。
	const FGpuTimestampHandle Query = Store->CreateGpuTimestampQuery();
	if ( !Query.IsValid() )
	{
		++NumDropped;
		return false;
	}
	if ( !Store->BeginRecording( Commands, BatchId, Query ) )
	{
		Store->ReleaseGpuTimestampQuery( Query );
		++NumDropped;
		return false;
	}
	FOpenSegment &Open = Opened[static_cast<int32>( Segment )];
	Open.Query = Query;
	Open.Label = Label;
	Open.bOpen = true;
	Open.Row = 0;
	if ( Segment == ESegment::Pass )
	{
		Open.Row = 1 + NMath::Min( PassOrdinal, MaxRows - 2 );
		++PassOrdinal;
	}
	return true;
}

void CDX12GpuSegmentTimer::CloseSegment( ESegment Segment, ID3D12GraphicsCommandList *Commands, uint64 BatchId )
{
	FOpenSegment &Open = Opened[static_cast<int32>( Segment )];
	if ( !bEnabled || !Open.bOpen )
	{
		return;
	}
	Open.bOpen = false;
	if ( !Store->EndRecording( Commands, BatchId, Open.Query ) )
	{
		Store->ReleaseGpuTimestampQuery( Open.Query );
		++NumDropped;
		return;
	}
	FPendingSegment Added;
	Added.Query = Open.Query;
	Added.Row = Open.Row;
	Added.FrameId = CurrentFrameId;
	Added.Label = Open.Label;
	Pending.Add( Added );
	++Frames[CurrentFrameId % FrameRing].Outstanding;
}

void CDX12GpuSegmentTimer::DiscardSegment( ESegment Segment )
{
	FOpenSegment &Open = Opened[static_cast<int32>( Segment )];
	if ( !Open.bOpen )
	{
		return;
	}
	Open.bOpen = false;
	if ( Store )
	{
		Store->ReleaseGpuTimestampQuery( Open.Query );
	}
}

void CDX12GpuSegmentTimer::NotifyFrame()
{
	if ( !bEnabled )
	{
		return;
	}
	Poll();
	FFrameSlot &Closing = Frames[CurrentFrameId % FrameRing];
	Closing.bClosed = true;
	if ( Closing.bUsed && Closing.Outstanding <= 0 )
	{
		FinalizeFrame( Closing );
	}
	++CurrentFrameId;
	PassOrdinal = 0;
	FFrameSlot &Next = Frames[CurrentFrameId % FrameRing];
	if ( Next.bUsed ) // 輪が一周しても、結果が届かなかった古いフレーム。届いた分だけで確定する。
	{
		FinalizeFrame( Next );
	}
	Next = FFrameSlot();
	Next.FrameId = CurrentFrameId;
	Next.bUsed = true;
	for ( int32 Index = Pending.Num() - 1; Index >= 0; --Index ) // バッチを破棄した区間は、結果が届かない。
	{
		if ( CurrentFrameId - Pending[Index].FrameId > MaxPendingFrames )
		{
			RetireSegment( Index, false, 0.0 );
		}
	}
}

void CDX12GpuSegmentTimer::Reset()
{
	Pending.Reset();
	for ( FOpenSegment &Open : Opened )
	{
		Open = FOpenSegment();
	}
	for ( FFrameSlot &Slot : Frames )
	{
		Slot = FFrameSlot();
	}
	for ( FRowStats &Row : Rows )
	{
		Row = FRowStats();
	}
	CurrentFrameId = 0;
	PassOrdinal = 0;
	Frames[0].bUsed = true;
	WindowFrames = 0;
	WindowTotalMs = WindowMaxTotalMs = WindowMainMs = WindowPassMs = 0.0;
	NumDropped = 0;
}

void CDX12GpuSegmentTimer::Poll()
{
	for ( int32 Index = Pending.Num() - 1; Index >= 0; --Index )
	{
		bool bReady = false;
		if ( !Store->IsGpuTimestampReady( Pending[Index].Query, bReady ) )
		{
			RetireSegment( Index, false, 0.0 );
			continue;
		}
		if ( !bReady )
		{
			continue;
		}
		FGpuTimestampResult Result;
		if ( Store->GetGpuTimestampResult( Pending[Index].Query, Result ) )
		{
			RetireSegment( Index, true, Result.ElapsedMs );
		}
		else
		{
			RetireSegment( Index, false, 0.0 );
		}
	}
}

void CDX12GpuSegmentTimer::RetireSegment( int32 Index, bool bMeasured, float64 ElapsedMs )
{
	const FPendingSegment Segment = Pending[Index];
	Pending.RemoveAt( Index );
	Store->ReleaseGpuTimestampQuery( Segment.Query );
	FFrameSlot &Slot = Frames[Segment.FrameId % FrameRing];
	const bool bSlotValid = Slot.bUsed && Slot.FrameId == Segment.FrameId;
	if ( bMeasured )
	{
		FRowStats &Row = Rows[Segment.Row];
		Row.Label = Segment.Label;
		Row.SumMs += ElapsedMs;
		Row.MaxMs = NMath::Max( Row.MaxMs, ElapsedMs );
		++Row.Count;
		if ( bSlotValid )
		{
			( Segment.Row == 0 ? Slot.MainMs : Slot.PassMs ) += ElapsedMs;
		}
	}
	if ( bSlotValid )
	{
		--Slot.Outstanding;
		if ( Slot.bClosed && Slot.Outstanding <= 0 )
		{
			FinalizeFrame( Slot );
		}
	}
}

void CDX12GpuSegmentTimer::FinalizeFrame( FFrameSlot &Slot )
{
	const float64 TotalMs = Slot.MainMs + Slot.PassMs;
	PROFILE_COUNTER_MS( "Gpu.Frame", TotalMs );
	PROFILE_COUNTER_MS( "Gpu.Main", Slot.MainMs );
	PROFILE_COUNTER_MS( "Gpu.Passes", Slot.PassMs );
	WindowTotalMs += TotalMs;
	WindowMaxTotalMs = NMath::Max( WindowMaxTotalMs, TotalMs );
	WindowMainMs += Slot.MainMs;
	WindowPassMs += Slot.PassMs;
	Slot.bUsed = false;
	if ( ++WindowFrames >= LogFrames )
	{
		LogWindow();
	}
}

void CDX12GpuSegmentTimer::LogWindow()
{
	const float64 NumFrames = static_cast<float64>( NMath::Max( WindowFrames, 1 ) );
	LOG( LogDX12, Log, "DX12 gpu timings (%d frames, ms per frame, average / max): total %.2f / %.2f (main screen %.2f, passes %.2f), %d dropped", WindowFrames, WindowTotalMs / NumFrames, WindowMaxTotalMs, WindowMainMs / NumFrames, WindowPassMs / NumFrames, NumDropped );
	for ( int32 Index = 0; Index < MaxRows; ++Index )
	{
		const FRowStats &Row = Rows[Index];
		if ( Row.Count == 0 )
		{
			continue;
		}
		if ( Index == 0 )
		{
			LOG( LogDX12, Log, "  gpu main screen: %.2f average, %.2f longest segment", Row.SumMs / NumFrames, Row.MaxMs );
		}
		else
		{
			LOG( LogDX12, Log, "  gpu pass %d (%dx%d, %d color%s%s): %.2f average, %.2f longest", Index - 1, Row.Label.Width, Row.Label.Height, Row.Label.NumColors, Row.Label.NumColors == 1 ? "" : "s", Row.Label.bDepth ? " + depth" : "", Row.SumMs / NumFrames, Row.MaxMs );
		}
	}
	for ( FRowStats &Row : Rows )
	{
		Row = FRowStats();
	}
	WindowFrames = 0;
	WindowTotalMs = WindowMaxTotalMs = WindowMainMs = WindowPassMs = 0.0;
	NumDropped = 0;
}
