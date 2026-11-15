// SPDX-License-Identifier: MIT
#include "WindowsClipboardSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Core/Collection/Array.h"

REGISTER_SUBSYSTEM( CWindowsClipboardSubsystem )

namespace
{
	// 別のアプリが、クリップボードを開いている間は、少し待って開き直す
	bool OpenClipboardWithRetry()
	{
		for ( int32 Attempt = 0; Attempt < 10; ++Attempt )
		{
			if ( OpenClipboard( nullptr ) )
			{
				return true;
			}
			Sleep( 5 );
		}
		return false;
	}

	// 開いたクリップボードを、必ず閉じる
	struct FClipboardScope
	{
		bool bOpened = false;
		FClipboardScope() : bOpened( OpenClipboardWithRetry() ) {}
		~FClipboardScope()
		{
			if ( bOpened )
			{
				CloseClipboard();
			}
		}
	};
}

bool CWindowsClipboardSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

bool CWindowsClipboardSubsystem::GetText( FString& OutText ) const
{
	OutText = FString();
	FClipboardScope Scope;
	if ( !Scope.bOpened || !IsClipboardFormatAvailable( CF_UNICODETEXT ) )
	{
		return false;
	}
	HANDLE Handle = GetClipboardData( CF_UNICODETEXT );
	if ( Handle == nullptr )
	{
		return false;
	}
	const wchar_t* Wide = static_cast<const wchar_t*>( GlobalLock( Handle ) );
	if ( Wide == nullptr )
	{
		return false;
	}
	// 終端の 0 が無い壊れた中身でも、確保した大きさの中だけを読む
	const size_t MaxChars = GlobalSize( Handle ) / sizeof( wchar_t );
	size_t Length = 0;
	while ( Length < MaxChars && Wide[Length] != L'\0' )
	{
		++Length;
	}
	TArray<wchar_t> Buffer;
	Buffer.SetNum( static_cast<int32>( Length ) + 1, L'\0' );
	for ( size_t Index = 0; Index < Length; ++Index )
	{
		Buffer[static_cast<int32>( Index )] = Wide[Index];
	}
	GlobalUnlock( Handle );
	OutText = FString( Buffer.GetData() ).ToValidUtf8();
	return !OutText.IsEmpty();
}

bool CWindowsClipboardSubsystem::SetText( const FString& Text )
{
	const TArray<wchar_t> Wide = Text.ToWide(); // 終端の 0 つき
	const SIZE_T Bytes = static_cast<SIZE_T>( Wide.Num() ) * sizeof( wchar_t );
	HGLOBAL Memory = GlobalAlloc( GMEM_MOVEABLE, Bytes );
	if ( Memory == nullptr )
	{
		return false;
	}
	void* Locked = GlobalLock( Memory );
	if ( Locked == nullptr )
	{
		GlobalFree( Memory );
		return false;
	}
	memcpy( Locked, Wide.GetData(), Bytes );
	GlobalUnlock( Memory );

	FClipboardScope Scope;
	if ( !Scope.bOpened || !EmptyClipboard() || SetClipboardData( CF_UNICODETEXT, Memory ) == nullptr )
	{
		GlobalFree( Memory ); // クリップボードが受け取らなかったときだけ、自分で解放する
		return false;
	}
	return true; // 受け取ったメモリは、OS のものになった
}
