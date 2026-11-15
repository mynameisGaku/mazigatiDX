// SPDX-License-Identifier: MIT
#include "WindowsTextInputSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Core/Memory/Memory.h"

#include <imm.h>
#pragma comment( lib, "imm32.lib" )

REGISTER_SUBSYSTEM( CWindowsTextInputSubsystem )

namespace
{
	// 終端の 0 つきの UTF-16 の並びを、UTF-8 の文字列にする
	FString ToUtf8( const TArray<wchar_t>& Wide )
	{
		return Wide.Num() > 0 ? FString( Wide.GetData() ).ToValidUtf8() : FString();
	}

	// UTF-16 の先頭から Units 個（終端の 0 は含まない）を、UTF-8 にしたときのバイト数
	int32 Utf8BytesOfPrefix( const TArray<wchar_t>& Wide, int32 Units )
	{
		TArray<wchar_t> Prefix;
		const int32 Count = Units < 0 ? 0 : ( Units > Wide.Num() - 1 ? Wide.Num() - 1 : Units );
		Prefix.SetNum( Count + 1, L'\0' );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			Prefix[Index] = Wide[Index];
		}
		return FString( Prefix.GetData() ).Len();
	}

	// IME の文字列（GCS_COMPSTR・GCS_RESULTSTR）を読む。終端の 0 つき。無ければ空
	TArray<wchar_t> ReadImeString( HIMC Context, DWORD Kind )
	{
		TArray<wchar_t> Result;
		const LONG Bytes = ImmGetCompositionStringW( Context, Kind, nullptr, 0 );
		if ( Bytes <= 0 )
		{
			return Result;
		}
		const int32 Units = static_cast<int32>( Bytes / sizeof( wchar_t ) );
		Result.SetNum( Units + 1, L'\0' );
		if ( ImmGetCompositionStringW( Context, Kind, Result.GetData(), static_cast<DWORD>( Units * sizeof( wchar_t ) ) ) < 0 )
		{
			Result.Reset();
		}
		return Result;
	}

	// キーを、編集の操作にする。Shift を押していれば、範囲を広げる操作にする。対応しないキーは false
	bool TryMapKey( uint32 VirtualKey, bool bShift, bool bControl, ETextEditCommand& OutCommand )
	{
		if ( bControl )
		{
			switch ( VirtualKey )
			{
			case 'C':
				OutCommand = ETextEditCommand::Copy;
				return true;
			case 'X':
				OutCommand = ETextEditCommand::Cut;
				return true;
			case 'V':
				OutCommand = ETextEditCommand::Paste;
				return true;
			case 'A':
				OutCommand = ETextEditCommand::SelectAll;
				return true;
			default:
				break; // 矢印・Home・End などは、下で、Ctrl なしと同じ扱い
			}
		}
		switch ( VirtualKey )
		{
		case VK_BACK:
			OutCommand = ETextEditCommand::Backspace;
			return true;
		case VK_DELETE:
			OutCommand = ETextEditCommand::Delete;
			return true;
		case VK_RETURN:
			OutCommand = ETextEditCommand::Enter;
			return true;
		case VK_TAB:
			OutCommand = ETextEditCommand::Tab;
			return true;
		case VK_ESCAPE:
			OutCommand = ETextEditCommand::Escape;
			return true;
		case VK_LEFT:
			OutCommand = bShift ? ETextEditCommand::SelectLeft : ETextEditCommand::Left;
			return true;
		case VK_RIGHT:
			OutCommand = bShift ? ETextEditCommand::SelectRight : ETextEditCommand::Right;
			return true;
		case VK_UP:
			OutCommand = bShift ? ETextEditCommand::SelectUp : ETextEditCommand::Up;
			return true;
		case VK_DOWN:
			OutCommand = bShift ? ETextEditCommand::SelectDown : ETextEditCommand::Down;
			return true;
		case VK_HOME:
			OutCommand = bShift ? ETextEditCommand::SelectHome : ETextEditCommand::Home;
			return true;
		case VK_END:
			OutCommand = bShift ? ETextEditCommand::SelectEnd : ETextEditCommand::End;
			return true;
		case VK_PRIOR:
			OutCommand = ETextEditCommand::PageUp;
			return true;
		case VK_NEXT:
			OutCommand = ETextEditCommand::PageDown;
			return true;
		default:
			return false;
		}
	}
}

bool CWindowsTextInputSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsTextInputSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Window = Collection.InitializeDependency<INativeWindow>();
	if ( Window != nullptr )
	{
		Window->AddMessageObserver( this );
	}
}

void CWindowsTextInputSubsystem::Deinitialize()
{
	if ( Window != nullptr )
	{
		Window->RemoveMessageObserver( this );
	}
	Window = nullptr;
	bActive = false;
	Pending.Reset();
	Events.Reset();
	PendingIme = FImeComposition();
	Ime = FImeComposition();
	HighSurrogate = 0;
}

void CWindowsTextInputSubsystem::Tick( float32 )
{
	Events = MoveTemp( Pending );
	Pending.Reset();
	Ime = PendingIme;
	if ( !bActive )
	{
		Events.Reset();
		Ime = FImeComposition();
	}
}

void CWindowsTextInputSubsystem::SetTextInputActive( bool bNewActive )
{
	if ( bNewActive == bActive )
	{
		return;
	}
	bActive = bNewActive;
	// 切り替えたとき、古い入力と、途中の変換は捨てる
	Pending.Reset();
	Events.Reset();
	PendingIme = FImeComposition();
	Ime = FImeComposition();
	HighSurrogate = 0;
	if ( Window != nullptr )
	{
		if ( HWND Handle = static_cast<HWND>( Window->GetNativeWindow() ) )
		{
			if ( HIMC Context = ImmGetContext( Handle ) )
			{
				if ( !bActive )
				{
					ImmNotifyIME( Context, NI_COMPOSITIONSTR, CPS_CANCEL, 0 ); // 途中の変換が残ったまま、ゲームの操作へ戻らないように
				}
				ImmReleaseContext( Handle, Context );
			}
		}
	}
}

bool CWindowsTextInputSubsystem::IsTextInputActive() const
{
	return bActive;
}

const TArray<FTextInputEvent>& CWindowsTextInputSubsystem::GetTextInputEvents() const
{
	return Events;
}

const FImeComposition& CWindowsTextInputSubsystem::GetImeComposition() const
{
	return Ime;
}

bool CWindowsTextInputSubsystem::OnNativeWindowMessage( void* WindowHandle, uint32 Message, uint64 WParam, int64 LParam, int64& OutResult )
{
	if ( !bActive )
	{
		return false;
	}
	switch ( Message )
	{
	case WM_CHAR:
		HandleChar( static_cast<uint16>( WParam ) );
		return false; // ImGui も、続けて受け取る
	case WM_KEYDOWN:
		HandleKeyDown( static_cast<uint32>( WParam ) );
		return false;
	case WM_IME_SETCONTEXT:
		// OS の変換の窓・候補の窓を出さない（途中の文字列と候補は、UI が描く）
		OutResult = DefWindowProcW( static_cast<HWND>( WindowHandle ), WM_IME_SETCONTEXT, static_cast<WPARAM>( WParam ), static_cast<LPARAM>( static_cast<LPARAM>( LParam ) & ~static_cast<LPARAM>( ISC_SHOWUICOMPOSITIONWINDOW | ISC_SHOWUIALLCANDIDATEWINDOW ) ) );
		return true;
	case WM_IME_STARTCOMPOSITION:
		OutResult = 0;
		return true;
	case WM_IME_COMPOSITION:
		HandleImeComposition( WindowHandle, LParam );
		OutResult = 0;
		return true;
	case WM_IME_ENDCOMPOSITION:
		PendingIme = FImeComposition();
		OutResult = 0;
		return true;
	case WM_IME_NOTIFY:
		HandleImeNotify( WindowHandle, WParam );
		OutResult = 0;
		return true;
	default:
		return false;
	}
}

void CWindowsTextInputSubsystem::HandleChar( uint16 Unit )
{
	if ( Unit >= 0xD800 && Unit <= 0xDBFF )
	{
		HighSurrogate = Unit; // 後半が届くまで持つ
		return;
	}
	wchar_t Buffer[3] = {};
	if ( Unit >= 0xDC00 && Unit <= 0xDFFF )
	{
		if ( HighSurrogate == 0 )
		{
			return; // 前半の無い後半は捨てる
		}
		Buffer[0] = static_cast<wchar_t>( HighSurrogate );
		Buffer[1] = static_cast<wchar_t>( Unit );
		HighSurrogate = 0;
	}
	else
	{
		HighSurrogate = 0;
		if ( Unit < 0x20 || Unit == 0x7F )
		{
			return; // 制御文字は、WM_KEYDOWN の編集の操作で受ける
		}
		Buffer[0] = static_cast<wchar_t>( Unit );
	}
	const FString Text = FString( Buffer ).ToValidUtf8();
	if ( !Text.IsEmpty() )
	{
		Pending.Add( FTextInputEvent::MakeText( Text ) );
	}
}

void CWindowsTextInputSubsystem::HandleKeyDown( uint32 VirtualKey )
{
	const bool bShift = ( GetKeyState( VK_SHIFT ) & 0x8000 ) != 0;
	const bool bControl = ( GetKeyState( VK_CONTROL ) & 0x8000 ) != 0;
	const bool bAlt = ( GetKeyState( VK_MENU ) & 0x8000 ) != 0;
	if ( bControl && bAlt )
	{
		return; // AltGr（Ctrl + Alt）は、文字を打つ操作
	}
	ETextEditCommand Command = ETextEditCommand::Backspace;
	if ( TryMapKey( VirtualKey, bShift, bControl, Command ) )
	{
		Pending.Add( FTextInputEvent::MakeCommand( Command ) );
	}
}

void CWindowsTextInputSubsystem::HandleImeComposition( void* WindowHandle, int64 LParam )
{
	const HWND Handle = static_cast<HWND>( WindowHandle );
	HIMC Context = ImmGetContext( Handle );
	if ( Context == nullptr )
	{
		return;
	}
	const DWORD Flags = static_cast<DWORD>( static_cast<LPARAM>( LParam ) );
	if ( Flags & GCS_RESULTSTR )
	{
		const FString Result = ToUtf8( ReadImeString( Context, GCS_RESULTSTR ) );
		if ( !Result.IsEmpty() )
		{
			Pending.Add( FTextInputEvent::MakeText( Result ) );
		}
	}
	if ( Flags & GCS_COMPSTR )
	{
		const TArray<wchar_t> Wide = ReadImeString( Context, GCS_COMPSTR );
		const FString Text = ToUtf8( Wide );
		if ( Text.IsEmpty() )
		{
			PendingIme = FImeComposition();
		}
		else
		{
			const LONG CursorUnits = ImmGetCompositionStringW( Context, GCS_CURSORPOS, nullptr, 0 ); // 先頭からの UTF-16 の単位数
			PendingIme.bActive = true;
			PendingIme.Text = Text;
			PendingIme.CursorByteOffset = Utf8BytesOfPrefix( Wide, static_cast<int32>( CursorUnits < 0 ? 0 : CursorUnits ) );
		}
	}
	else if ( Flags & GCS_RESULTSTR )
	{
		PendingIme = FImeComposition(); // 確定して、途中の文字列は無くなった
	}
	ImmReleaseContext( Handle, Context );
}

void CWindowsTextInputSubsystem::HandleImeNotify( void* WindowHandle, uint64 Command )
{
	if ( Command == IMN_CLOSECANDIDATE )
	{
		PendingIme.Candidates.Reset();
		PendingIme.SelectedCandidate = -1;
		return;
	}
	if ( Command != IMN_OPENCANDIDATE && Command != IMN_CHANGECANDIDATE )
	{
		return;
	}
	const HWND Handle = static_cast<HWND>( WindowHandle );
	HIMC Context = ImmGetContext( Handle );
	if ( Context == nullptr )
	{
		return;
	}
	const DWORD Bytes = ImmGetCandidateListW( Context, 0, nullptr, 0 );
	if ( Bytes >= sizeof( CANDIDATELIST ) )
	{
		TArray<uint8> Storage;
		Storage.SetNum( static_cast<int32>( Bytes ), 0 );
		if ( ImmGetCandidateListW( Context, 0, reinterpret_cast<CANDIDATELIST*>( Storage.GetData() ), Bytes ) != 0 )
		{
			const CANDIDATELIST* List = reinterpret_cast<const CANDIDATELIST*>( Storage.GetData() );
			// 候補の窓が見せている 1 ページぶん（ページの大きさが無ければ、全部）
			const DWORD First = List->dwPageSize > 0 && List->dwPageStart < List->dwCount ? List->dwPageStart : 0;
			const DWORD Last = List->dwPageSize > 0 ? ( First + List->dwPageSize < List->dwCount ? First + List->dwPageSize : List->dwCount ) : List->dwCount;
			PendingIme.Candidates.Reset();
			for ( DWORD Index = First; Index < Last; ++Index )
			{
				const DWORD Offset = List->dwOffset[Index];
				if ( Offset >= Bytes )
				{
					break; // 壊れた並びは、ここまで
				}
				PendingIme.Candidates.Add( FString( reinterpret_cast<const wchar_t*>( Storage.GetData() + Offset ) ).ToValidUtf8() );
			}
			PendingIme.SelectedCandidate = List->dwSelection >= First && List->dwSelection < Last ? static_cast<int32>( List->dwSelection - First ) : -1;
		}
	}
	ImmReleaseContext( Handle, Context );
}
