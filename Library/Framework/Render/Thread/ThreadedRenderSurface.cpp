// SPDX-License-Identifier: MIT
#include "ThreadedRenderSurface.h"

#include "RenderCommandThread.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Thread/RenderThread.h"

CThreadedRenderSurface::CThreadedRenderSurface( IRenderSurface &InTarget, CRenderCommandThread &InThread ) : Target( InTarget ), Thread( InThread ) {}
CThreadedRenderSurface::~CThreadedRenderSurface() { EndFrame(); }

bool CThreadedRenderSurface::BeginFrame( int32 InWidth, int32 InHeight )
{
	GAME_THREAD_ONLY( false );
	if ( bActive || !Thread.IsReady() || InWidth <= 0 || InHeight <= 0 || InWidth > 16384 || InHeight > 16384 )
	{
		return false;
	}
	// 実行先の生きた状態をゲーム側から直接読まない。
	auto Probe = [this]()
	{ bHasCanvas = Target.GetCanvas2D() != nullptr; bHasGeometry = Target.GetGeometry3D() != nullptr; };
	if ( !Thread.Execute( MoveTemp( Probe ), "Render.SurfaceBegin" ) )
	{
		return false;
	}
	Width = FrameWidth = InWidth;
	Height = FrameHeight = InHeight;
	NumCommands = TextBytes = 0;
	bFailed.Store( false );
	bCapture = false;
	bRenderTargetPass = false;
	bActive = true;
	return true;
}

bool CThreadedRenderSurface::EndFrame()
{
	GAME_THREAD_ONLY( false );
	if ( !bActive )
	{
		return true;
	}
	if ( bRenderTargetPass )
	{
		EndRenderTargetPass();
		bFailed.Store( true );
	}
	// 空の区切りも同じ実行列へ積み、既に渡した全件が終わるまで所有者を生存させる。
	const bool bFlushed = Flush();
	const bool bDone = Thread.WaitForCommands();
	bActive = false;
	return bFlushed && bDone && !bFailed.Load();
}

bool CThreadedRenderSurface::Flush()
{
	GAME_THREAD_ONLY( false );
	if ( Pending.IsEmpty() )
	{
		return true;
	}
	// 配列ごと所有権を移し、記録側が次のバッチを変更しても実行側へ影響させない。
	auto Batch = [this, Commands = MoveTemp( Pending )]()
	{ ExecuteBatch( Commands ); };
	if ( !Thread.Enqueue( MoveTemp( Batch ), "Render.CanvasBatch" ).IsValid() )
	{
		bFailed.Store( true );
		return false;
	}
	return true;
}

void CThreadedRenderSurface::Queue( FCommand Command )
{
	GAME_THREAD_ONLY();
	if ( !bActive || NumCommands >= 65536 )
	{
		bFailed.Store( true );
		return;
	}
	++NumCommands;
	Pending.Add( MoveTemp( Command ) );
	if ( Pending.Num() >= 64 )
	{
		Flush();
	}
}

bool CThreadedRenderSurface::Execute( TCallable<void()> Command )
{
	GAME_THREAD_ONLY( false );
	return Flush() && Thread.Execute( MoveTemp( Command ) );
}

IRenderCanvas2D *CThreadedRenderSurface::GetCanvas2D()
{
	GAME_THREAD_ONLY( nullptr );
	return bActive && bHasCanvas ? this : nullptr;
}
IRenderGeometry3D *CThreadedRenderSurface::GetGeometry3D()
{
	GAME_THREAD_ONLY( nullptr );
	return bActive && bHasGeometry ? this : nullptr;
}
int32 CThreadedRenderSurface::GetWidth() const
{
	GAME_THREAD_ONLY( 0 );
	return Width;
}
int32 CThreadedRenderSurface::GetHeight() const
{
	GAME_THREAD_ONLY( 0 );
	return Height;
}

bool CThreadedRenderSurface::WaitForGPU()
{
	bool bResult = false;
	auto Wait = [&]()
	{ bResult = Target.WaitForGPU(); };
	return Execute( MoveTemp( Wait ) ) && bResult;
}

bool CThreadedRenderSurface::BeginOffscreenCapture( const FOffscreenCaptureDesc &Desc )
{
	GAME_THREAD_ONLY( false );
	if ( !bActive || bCapture || bRenderTargetPass )
	{
		return false;
	}
	bool bResult = false;
	auto Begin = [&]()
	{ bResult = Target.BeginOffscreenCapture( Desc ); };
	if ( !Execute( MoveTemp( Begin ) ) || !bResult )
	{
		return false;
	}
	Width = Desc.Width;
	Height = Desc.Height;
	bCapture = true;
	return true;
}

bool CThreadedRenderSurface::EndOffscreenCapture( TArray<FColor32> &Out )
{
	GAME_THREAD_ONLY( false );
	if ( !bActive || !bCapture || bRenderTargetPass )
	{
		return false;
	}
	bool bResult = false;
	auto End = [&]()
	{ bResult = Target.EndOffscreenCapture( Out ); };
	const bool bExecuted = Execute( MoveTemp( End ) );
	Width = FrameWidth;
	Height = FrameHeight;
	bCapture = false;
	return bExecuted && bResult;
}

bool CThreadedRenderSurface::BeginRenderTargetPass( const FRenderTargetPassDesc &Desc )
{
	GAME_THREAD_ONLY( false );
	if ( !bActive || bCapture || bRenderTargetPass )
	{
		return false;
	}
	bool bResult = false;
	int32 TargetWidth = 0, TargetHeight = 0; // 描画先の実寸を描画スレッド側で取得する。
	auto Begin = [&]()
	{
		bResult = Target.BeginRenderTargetPass( Desc );
		if ( bResult )
		{
			if ( IRenderGeometry3D *Geometry = Target.GetGeometry3D() )
			{
				TargetWidth = Geometry->GetWidth();
				TargetHeight = Geometry->GetHeight();
			}
		}
	};
	if ( !Execute( MoveTemp( Begin ) ) || !bResult || TargetWidth <= 0 || TargetHeight <= 0 )
	{
		return false;
	}
	Width = TargetWidth;
	Height = TargetHeight;
	bRenderTargetPass = true;
	return true;
}

bool CThreadedRenderSurface::EndRenderTargetPass()
{
	GAME_THREAD_ONLY( false );
	if ( !bActive || bCapture || !bRenderTargetPass )
	{
		return false;
	}
	bool bResult = false;
	auto End = [&]()
	{ bResult = Target.EndRenderTargetPass(); };
	const bool bExecuted = Execute( MoveTemp( End ) );
	Width = FrameWidth;
	Height = FrameHeight;
	bRenderTargetPass = false;
	return bExecuted && bResult;
}

bool CThreadedRenderSurface::DrawGeometry( const FGeometryVertex *Vertices, int32 Count, const FGeometryDrawState &State )
{
	GAME_THREAD_ONLY( false );
	if ( !bActive )
	{
		return false;
	}
	bool bResult = false;
	auto Draw = [&]()
	{ if ( IRenderGeometry3D* Geometry = Target.GetGeometry3D() ) { bResult = Geometry->DrawGeometry( Vertices, Count, State ); } };
	return Execute( MoveTemp( Draw ) ) && bResult;
}

bool CThreadedRenderSurface::DrawTexturedGeometry( const FGeometryVertex *Vertices, int32 Count, const FGeometryDrawState &State, const FTextureDrawView &Texture )
{
	GAME_THREAD_ONLY( false );
	if ( !bActive )
	{
		return false;
	}
	bool bResult = false;
	auto Draw = [&]()
	{ if ( IRenderGeometry3D* Geometry = Target.GetGeometry3D() ) { bResult = Geometry->DrawTexturedGeometry( Vertices, Count, State, Texture ); } };
	return Execute( MoveTemp( Draw ) ) && bResult;
}

bool CThreadedRenderSurface::DrawBufferGeometry( const FGeometryBufferDraw &Draw, const FGeometryDrawState &State, const FTextureDrawView *Texture )
{
	GAME_THREAD_ONLY( false );
	if ( !bActive )
	{
		return false;
	}
	bool bResult = false;
	auto Work = [&]()
	{ if ( IRenderGeometry3D* Geometry = Target.GetGeometry3D() ) { bResult = Geometry->DrawBufferGeometry( Draw, State, Texture ); } };
	return Execute( MoveTemp( Work ) ) && bResult;
}

void CThreadedRenderSurface::Box( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, bool bFill, float32 Thickness )
{
	GAME_THREAD_ONLY();
	// この操作だけの値を保持し、呼出し元の変数を参照しない。
	FCommand Command;
	Command.Type = ECommand::Box;
	Command.Values[0] = X0;
	Command.Values[1] = Y0;
	Command.Values[2] = X1;
	Command.Values[3] = Y1;
	Command.Values[4] = Thickness;
	Command.Color = Color;
	Command.bEnabled = bFill;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::Line( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, float32 Thickness )
{
	GAME_THREAD_ONLY();
	// この操作だけの値を保持し、呼出し元の変数を参照しない。
	FCommand Command;
	Command.Type = ECommand::Line;
	Command.Values[0] = X0;
	Command.Values[1] = Y0;
	Command.Values[2] = X1;
	Command.Values[3] = Y1;
	Command.Values[4] = Thickness;
	Command.Color = Color;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::Circle( float32 X, float32 Y, float32 Radius, FColor32 Color, bool bFill, float32 Thickness, int32 Segments )
{
	GAME_THREAD_ONLY();
	// この操作だけの値を保持し、呼出し元の変数を参照しない。
	FCommand Command;
	Command.Type = ECommand::Circle;
	Command.Values[0] = X;
	Command.Values[1] = Y;
	Command.Values[2] = Radius;
	Command.Values[3] = Thickness;
	Command.Color = Color;
	Command.bEnabled = bFill;
	Command.Integers[0] = Segments;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 )
{
	GAME_THREAD_ONLY();
	// この操作だけの値を保持し、呼出し元の変数を参照しない。
	FCommand Command;
	Command.Type = ECommand::PushClip;
	Command.Integers[0] = X0;
	Command.Integers[1] = Y0;
	Command.Integers[2] = X1;
	Command.Integers[3] = Y1;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::PopClip()
{
	GAME_THREAD_ONLY();
	// この操作だけの値を保持し、呼出し元の変数を参照しない。
	FCommand Command;
	Command.Type = ECommand::PopClip;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::SetAntiAlias( bool bEnabled )
{
	GAME_THREAD_ONLY();
	// この操作だけの値を保持し、呼出し元の変数を参照しない。
	FCommand Command;
	Command.Type = ECommand::AntiAlias;
	Command.bEnabled = bEnabled;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::RecordClear( FColor32 Color )
{
	GAME_THREAD_ONLY();
	// この操作だけの値を保持し、呼出し元の変数を参照しない。
	FCommand Command;
	Command.Type = ECommand::ClearColor;
	Command.Color = Color;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::ClearDepth()
{
	GAME_THREAD_ONLY();
	// この操作だけの値を保持し、呼出し元の変数を参照しない。
	FCommand Command;
	Command.Type = ECommand::ClearDepth;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::Triangle( const float32 ( &Points )[6], FColor32 Color, bool bFill, float32 Thickness )
{
	GAME_THREAD_ONLY();
	// 参照された点列を固定長の命令へ所有コピーする。
	FCommand Command;
	Command.Type = ECommand::Triangle;
	for ( int32 Index = 0; Index < 6; ++Index )
	{
		Command.Values[Index] = Points[Index];
	}
	Command.Values[6] = Thickness;
	Command.Color = Color;
	Command.bEnabled = bFill;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::Quad( const float32 ( &Points )[8], FColor32 Color, bool bFill, float32 Thickness )
{
	GAME_THREAD_ONLY();
	// 参照された点列を固定長の命令へ所有コピーする。
	FCommand Command;
	Command.Type = ECommand::Quad;
	for ( int32 Index = 0; Index < 8; ++Index )
	{
		Command.Values[Index] = Points[Index];
	}
	Command.Values[8] = Thickness;
	Command.Color = Color;
	Command.bEnabled = bFill;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::Text( float32 X, float32 Y, FColor32 Color, const char *Begin, const char *End )
{
	GAME_THREAD_ONLY();
	if ( !bActive || !Begin || Begin == End || Begin[0] == 0 || Color.a == 0 )
	{
		return;
	}
	// ネイティブの64KiB制限と、終端省略時の読み方を保つ。
	int32 Length = 0;
	if ( End )
	{
		if ( End < Begin || End - Begin > 65536 )
		{
			return;
		}
		Length = static_cast<int32>( End - Begin );
	}
	else
	{
		while ( Length < 65536 && Begin[Length] )
		{
			++Length;
		}
		if ( Length == 65536 )
		{
			return;
		}
	}
	if ( Length > 8 * 1024 * 1024 - TextBytes )
	{
		bFailed.Store( true );
		return;
	}
	// 範囲の内容をこの操作だけが所有し、呼出し元が書き換えても使わない。
	FCommand Command;
	Command.Type = ECommand::Text;
	Command.Values[0] = X;
	Command.Values[1] = Y;
	Command.Color = Color;
	Command.String = FString( Begin, Length );
	TextBytes += Length;
	Queue( MoveTemp( Command ) );
}

void CThreadedRenderSurface::Image( const FTextureDrawView &Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color )
{
	GAME_THREAD_ONLY();
	// bindingは借用値、点列とUVはこの命令が保持する。
	FCommand Command;
	Command.Type = ECommand::Image;
	Command.Texture = Texture;
	Command.Color = Color;
	for ( int32 Index = 0; Index < 8; ++Index )
	{
		Command.Values[Index] = Points[Index];
	}
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		Command.Values[8 + Index] = UV[Index];
	}
	Queue( MoveTemp( Command ) );
}

bool CThreadedRenderSurface::SetTextureFilter( bool bLinear )
{
	GAME_THREAD_ONLY( false );
	if ( !bActive )
	{
		return false;
	}
	bool bResult = false;
	auto Set = [&]()
	{ if ( IRenderCanvas2D* Canvas = Target.GetCanvas2D() ) { bResult = Canvas->SetTextureFilter( bLinear ); } };
	return Execute( MoveTemp( Set ) ) && bResult;
}

void CThreadedRenderSurface::ExecuteBatch( const TArray<FCommand> &Commands )
{
	RENDER_THREAD_ONLY();
	for ( const FCommand &Command : Commands )
	{
		if ( Command.Type == ECommand::ClearColor )
		{
			Target.RecordClear( Command.Color );
			continue;
		}
		if ( Command.Type == ECommand::ClearDepth )
		{
			if ( IRenderGeometry3D *Geometry = Target.GetGeometry3D() )
			{
				Geometry->ClearDepth();
			}
			else
			{
				bFailed.Store( true );
			}
			continue;
		}
		// 実行先は描画スレッド上でだけ借り、ゲーム側へ公開しない。
		IRenderCanvas2D *Canvas = Target.GetCanvas2D();
		if ( !Canvas )
		{
			bFailed.Store( true );
			continue;
		}
		switch ( Command.Type )
		{
		case ECommand::Box:
			Canvas->Box( Command.Values[0], Command.Values[1], Command.Values[2], Command.Values[3], Command.Color, Command.bEnabled, Command.Values[4] );
			break;
		case ECommand::Line:
			Canvas->Line( Command.Values[0], Command.Values[1], Command.Values[2], Command.Values[3], Command.Color, Command.Values[4] );
			break;
		case ECommand::Circle:
			Canvas->Circle( Command.Values[0], Command.Values[1], Command.Values[2], Command.Color, Command.bEnabled, Command.Values[3], Command.Integers[0] );
			break;
		case ECommand::Triangle:
		{
			const float32 Points[6] = { Command.Values[0], Command.Values[1], Command.Values[2], Command.Values[3], Command.Values[4], Command.Values[5] };
			Canvas->Triangle( Points, Command.Color, Command.bEnabled, Command.Values[6] );
			break;
		}
		case ECommand::Quad:
		{
			const float32 Points[8] = { Command.Values[0], Command.Values[1], Command.Values[2], Command.Values[3], Command.Values[4], Command.Values[5], Command.Values[6], Command.Values[7] };
			Canvas->Quad( Points, Command.Color, Command.bEnabled, Command.Values[8] );
			break;
		}
		case ECommand::Text:
			Canvas->Text( Command.Values[0], Command.Values[1], Command.Color, *Command.String, *Command.String + Command.String.Len() );
			break;
		case ECommand::Image:
		{
			const float32 Points[8] = { Command.Values[0], Command.Values[1], Command.Values[2], Command.Values[3], Command.Values[4], Command.Values[5], Command.Values[6], Command.Values[7] };
			const float32 UV[4] = { Command.Values[8], Command.Values[9], Command.Values[10], Command.Values[11] };
			Canvas->Image( Command.Texture, Points, UV, Command.Color );
			break;
		}
		case ECommand::PushClip:
			Canvas->PushClip( Command.Integers[0], Command.Integers[1], Command.Integers[2], Command.Integers[3] );
			break;
		case ECommand::PopClip:
			Canvas->PopClip();
			break;
		case ECommand::AntiAlias:
			Canvas->SetAntiAlias( Command.bEnabled );
			break;
		default:
			bFailed.Store( true );
			break;
		}
	}
}
