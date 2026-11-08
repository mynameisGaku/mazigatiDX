// SPDX-License-Identifier: MIT
#include "DxLibInputSubsystem.h"

#include "Library/Platform/DxLib/DxLibInclude.h"

//=============================================================================
// CDxLibInputSubsystem のうち、クリップボード（IClipboard）
//   DxLib の GetClipboardText / SetClipboardText を使う。文字コードは UTF-8（DxLibBackend が SetUseCharCodeFormat で設定している）。
//=============================================================================
bool CDxLibInputSubsystem::GetText( FString& OutText ) const
{
	OutText = FString();
	// 戻り値は、終端の 0 を除いたバイト数（無ければ -1）
	const int32 Size = static_cast<int32>( GetClipboardText( nullptr ) );
	if ( Size <= 0 )
	{
		return false;
	}
	TArray<char> Buffer;
	Buffer.SetNum( Size + 1, '\0' );
	if ( GetClipboardText( Buffer.GetData(), Size + 1 ) < 0 )
	{
		return false;
	}
	OutText = FString( Buffer.GetData() ).ToValidUtf8();
	return true;
}

bool CDxLibInputSubsystem::SetText( const FString& Text )
{
	return SetClipboardText( *Text ) == 0;
}
