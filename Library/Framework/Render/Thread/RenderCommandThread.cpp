// SPDX-License-Identifier: MIT
#include "RenderCommandThread.h"

#include <exception>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/RenderThread.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/CString.h"

DEFINE_LOG_CATEGORY_STATIC( LogRenderThread, Log )

CRenderCommandThread::CRenderCommandThread() : Thread( "Render Thread" )
{
	// 同期呼出しの参照は、受付済みの処理が必ず戻るまで保持する。
	auto Bind = [this]()
	{ bReady = NRenderThread::Bind(); };
	WaitAccepted( Thread.Enqueue( MoveTemp( Bind ), "Render.Bind" ) );
}

CRenderCommandThread::~CRenderCommandThread()
{
	if ( IsInRenderThread() )
	{
		// 自分を待つ破棄は成立しない。登録を残したまま待ち続けず、寿命の契約違反として終了する。
		std::terminate();
	}
	if ( bReady )
	{
		WaitAccepted( Thread.GetFence() );
		auto Unbind = []()
		{ NRenderThread::Unbind(); };
		WaitAccepted( Thread.Enqueue( MoveTemp( Unbind ), "Render.Unbind" ) );
	}
	Thread.StopAndJoin();
}

bool CRenderCommandThread::IsReady() const { return bReady; }
FTaskFence CRenderCommandThread::GetFence() const { return bReady ? Thread.GetFence() : FTaskFence(); }

void CRenderCommandThread::EnableTimings()
{
	bTimings = true;
}

TCallable<void()> CRenderCommandThread::MakeTimed( TCallable<void()> Task, const char *Name )
{
	if ( !bTimings || !Task )
	{
		return Task;
	}
	// 実行時間を数える包み。描画スレッドが、仕事の前後で時刻を取る。
	return [this, Name, Inner = MoveTemp( Task )]() mutable
	{
		const int64 Start = NPlatformTime::GetTicks();
		Inner();
		const int64 Elapsed = NPlatformTime::GetTicks() - Start; // 実行にかかった時間
		BusyTicks.FetchAdd( Elapsed, EMemoryOrder::Relaxed );
		Tasks.FetchAdd( 1, EMemoryOrder::Relaxed );
		AddNamedTime( TaskTimes, Name, Elapsed );
	};
}

void CRenderCommandThread::NotifyFrameEnd()
{
	constexpr int32 LogFrames = 120; // DX12 のフレーム時間の内訳（--dx12-frame-timings）と同じ間隔
	if ( !bTimings || ++TimingFrames < LogFrames )
	{
		return;
	}
	const float64 Frames = static_cast<float64>( TimingFrames );
	const float64 BusyMs = NPlatformTime::TicksToMs( BusyTicks.Exchange( 0, EMemoryOrder::Relaxed ) );
	const float64 WaitMs = NPlatformTime::TicksToMs( SyncWaitTicks.Exchange( 0, EMemoryOrder::Relaxed ) );
	const int64 Calls = SyncCalls.Exchange( 0, EMemoryOrder::Relaxed );
	const int64 NumTasks = Tasks.Exchange( 0, EMemoryOrder::Relaxed );
	LOG( LogRenderThread, Log, "render thread (per frame, %d frames): busy %.2f ms, tasks %.0f, sync calls %.0f, caller blocked in sync calls %.2f ms", TimingFrames, BusyMs / Frames, static_cast<float64>( NumTasks ) / Frames, static_cast<float64>( Calls ) / Frames, WaitMs / Frames );

	// 待った時間の長い呼び出し元（どの同期呼び出しが、ゲームスレッドを止めているか）と、実行に時間のかかった仕事を、上位から並べる
	TArray<FNamedTime> SortedCallers;
	TArray<FNamedTime> SortedTasks;
	{
		FScopedLock Lock( CallerMutex );
		SortedCallers = Callers;
		SortedTasks = TaskTimes;
		Callers.Empty();
		TaskTimes.Empty();
	}
	LogLongest( MoveTemp( SortedCallers ), Frames, true );
	LogLongest( MoveTemp( SortedTasks ), Frames, false );
	TimingFrames = 0;
}

void CRenderCommandThread::LogLongest( TArray<FNamedTime> Sorted, float64 Frames, bool bSyncCallers )
{
	constexpr int32 NumShown = 8;
	for ( int32 Rank = 0; Rank < NumShown; ++Rank )
	{
		int32 Longest = INDEX_NONE;
		for ( int32 Index = 0; Index < Sorted.Num(); ++Index )
		{
			if ( Sorted[Index].Calls > 0 && ( Longest == INDEX_NONE || Sorted[Index].Ticks > Sorted[Longest].Ticks ) )
			{
				Longest = Index;
			}
		}
		if ( Longest == INDEX_NONE )
		{
			break;
		}
		const float64 CallsPerFrame = static_cast<float64>( Sorted[Longest].Calls ) / Frames; // 1 フレームあたりの回数
		const float64 MsPerFrame = NPlatformTime::TicksToMs( Sorted[Longest].Ticks ) / Frames; // 1 フレームあたりの時間
		if ( bSyncCallers )
		{
			LOG( LogRenderThread, Log, "  sync caller %s: %.1f calls/frame, blocked %.3f ms/frame", Sorted[Longest].Name, CallsPerFrame, MsPerFrame );
		}
		else
		{
			LOG( LogRenderThread, Log, "  render task %s: %.1f tasks/frame, ran %.3f ms/frame", Sorted[Longest].Name, CallsPerFrame, MsPerFrame );
		}
		Sorted[Longest].Calls = 0;
	}
}

void CRenderCommandThread::AddNamedTime( TArray<FNamedTime> &Times, const char *Name, int64 Ticks )
{
	FScopedLock Lock( CallerMutex );
	for ( FNamedTime &Entry : Times )
	{
		if ( Entry.Name == Name || NCString::Compare( Entry.Name, Name ) == 0 )
		{
			++Entry.Calls;
			Entry.Ticks += Ticks;
			return;
		}
	}
	FNamedTime Added;
	Added.Name = Name;
	Added.Calls = 1;
	Added.Ticks = Ticks;
	Times.Add( Added );
}

FTaskFence CRenderCommandThread::Enqueue( TCallable<void()> Task, const char *Name )
{
	return bReady ? Thread.Enqueue( MakeTimed( MoveTemp( Task ), Name ), Name ) : FTaskFence();
}

bool CRenderCommandThread::Execute( TCallable<void()> Task, const char *Name )
{
	if ( !bReady || !Task )
	{
		return false;
	}
	if ( IsInRenderThread() )
	{
		Task();
		return true;
	}
	const int64 CallStart = bTimings ? NPlatformTime::GetTicks() : 0; // 呼び出し側が待つ時間の計測
	const TCallable<void()> Timed = MakeTimed( Task, Name );
	for ( ;; )
	{
		// 同期呼出しは受付を落とさない。拒否された場合も元の関数を再登録できるよう保持する。
		const FTaskFence Accepted = Thread.Enqueue( Timed, Name );
		if ( Accepted.IsValid() )
		{
			const bool bDone = WaitAccepted( Accepted );
			if ( bTimings )
			{
				const int64 WaitTicks = NPlatformTime::GetTicks() - CallStart;
				SyncWaitTicks.FetchAdd( WaitTicks, EMemoryOrder::Relaxed );
				SyncCalls.FetchAdd( 1, EMemoryOrder::Relaxed );
				AddNamedTime( Callers, Name, WaitTicks );
			}
			return bDone;
		}
		if ( !WaitAccepted( Thread.GetFence() ) )
		{
			return false;
		}
	}
}

bool CRenderCommandThread::WaitForCommands() const
{
	return bReady && !IsInRenderThread() && WaitAccepted( Thread.GetFence() );
}

bool CRenderCommandThread::WaitAccepted( const FTaskFence &Fence )
{
	if ( !Fence.IsValid() )
	{
		return false;
	}
	while ( !Fence.Wait( 60000 ) )
	{
		// 寿命を借りた処理を残して戻らない。GPU操作そのものには既存の期限を適用する。
	}
	return true;
}
