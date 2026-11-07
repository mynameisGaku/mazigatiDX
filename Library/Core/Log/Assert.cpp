// SPDX-License-Identifier: MIT
#include "Assert.h"

#include <cstdarg>   // va_list（ほかの Core と同じ。中身の書式化は NCString）

#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Process.h"

DEFINE_LOG_CATEGORY_STATIC( LogAssert, Log )

namespace
{
	NAssert::FCallStackProvider GCallStackProvider = nullptr;
	NAssert::FFailureHandler GFailureHandler = nullptr;
	int64 GNumEnsureFailures = 0;

	// 式・理由・場所を 1 つの文章にする
	FString MakeMessage( const char* Label, const char* Expression, const char* File, int32 Line, const char* Function, const char* Format, va_list Arguments )
	{
		char Reason[1024];
		Reason[0] = '\0';
		if ( Format != nullptr )
		{
			NCString::FormatV( Reason, sizeof( Reason ), Format, Arguments );
		}
		char Buffer[2048];
		NCString::Format( Buffer, sizeof( Buffer ), "%s: ( %s )%s%s [%s:%d %s]", Label, Expression, Reason[0] != '\0' ? " - " : "", Reason, File, Line, Function );
		return FString( Buffer );
	}
}

void NAssert::SetCallStackProvider( FCallStackProvider Provider )
{
	GCallStackProvider = Provider;
}

FString NAssert::CaptureCallStack( int32 SkipFrames, int32 MaxDepth )
{
	return GCallStackProvider != nullptr ? GCallStackProvider( SkipFrames + 1, MaxDepth ) : FString();
}

void NAssert::SetFailureHandler( FFailureHandler Handler )
{
	GFailureHandler = Handler;
}

int64 NAssert::GetNumEnsureFailures()
{
	return GNumEnsureFailures;
}

void NAssert::ReportCheckFailed( const char* Expression, const char* File, int32 Line, const char* Function, const char* Format, ... )
{
	va_list Arguments;
	va_start( Arguments, Format );
	const FString Message = MakeMessage( "Check failed", Expression, File, Line, Function, Format, Arguments );
	va_end( Arguments );
	if ( GFailureHandler != nullptr && GFailureHandler( EKind::Check, *Message ) )
	{
		return;
	}
	const FString Stack = CaptureCallStack( 1, 32 );
	// Fatal: ログに出したあと、止まる（止まる前に、Platform が登録したフックが、クラッシュ報告を書く）
	LOG( LogAssert, Fatal, "%s%s%s", *Message, Stack.IsEmpty() ? "" : "\nCall stack:\n", *Stack );
	NProcess::Abort();
}

void NAssert::ReportEnsureFailed( bool& bAlreadyReported, const char* Expression, const char* File, int32 Line, const char* Function, const char* Format, ... )
{
	++GNumEnsureFailures;
	va_list Arguments;
	va_start( Arguments, Format );
	const FString Message = MakeMessage( "Ensure failed", Expression, File, Line, Function, Format, Arguments );
	va_end( Arguments );
	if ( GFailureHandler != nullptr && GFailureHandler( EKind::Ensure, *Message ) )
	{
		return;
	}
	if ( bAlreadyReported )
	{
		return;
	}
	bAlreadyReported = true;
	const FString Stack = CaptureCallStack( 1, 32 );
	LOG( LogAssert, Error, "%s (reported once per place)%s%s", *Message, Stack.IsEmpty() ? "" : "\nCall stack:\n", *Stack );
}
