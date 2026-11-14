// SPDX-License-Identifier: MIT
#include "RenderCommandThread.h"

#include <exception>

#include "Library/Core/Thread/RenderThread.h"

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

FTaskFence CRenderCommandThread::Enqueue( TCallable<void()> Task, const char *Name )
{
	return bReady ? Thread.Enqueue( MoveTemp( Task ), Name ) : FTaskFence();
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
	for ( ;; )
	{
		// 同期呼出しは受付を落とさない。拒否された場合も元の関数を再登録できるよう保持する。
		const FTaskFence Accepted = Thread.Enqueue( Task, Name );
		if ( Accepted.IsValid() )
		{
			return WaitAccepted( Accepted );
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
