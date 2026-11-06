// SPDX-License-Identifier: MIT
#include "DxLibInputSubsystem.h"

#include "Library/Platform/DxLib/DxLibInclude.h"

//=============================================================================
// CDxLibInputSubsystem のうち、文字入力（ITextInput）
//   DxLib の文字コードのバッファ（GetInputChar）と、IME の状態（GetIMEInputData）を、バックエンドに依らない形（FTextInputEvent /
//   FImeComposition）へ写す。DxLib の制御コード（CTRL_CODE_*）と、1 バイトずつ届く文字は、ここで吸収する。
//   文字コードは UTF-8（DxLibBackend が SetUseCharCodeFormat で設定している）。
//=============================================================================
namespace
{
	// 1 フレームに読む回数の上限（バッファが壊れても、止まらなくなることがないように）
	constexpr int32 MaxReadsPerFrame = 8192;

	// DxLib の制御コードを、編集の操作にする。対応しない制御コードは false
	bool TryMapControlCode( uint8 Code, ETextEditCommand& OutCommand )
	{
		switch ( Code )
		{
		case CTRL_CODE_BS:
			OutCommand = ETextEditCommand::Backspace;
			return true;
		case CTRL_CODE_DEL:
			OutCommand = ETextEditCommand::Delete;
			return true;
		case CTRL_CODE_CR:
			OutCommand = ETextEditCommand::Enter;
			return true;
		case CTRL_CODE_TAB:
			OutCommand = ETextEditCommand::Tab;
			return true;
		case CTRL_CODE_ESC:
			OutCommand = ETextEditCommand::Escape;
			return true;
		case CTRL_CODE_LEFT:
			OutCommand = ETextEditCommand::Left;
			return true;
		case CTRL_CODE_RIGHT:
			OutCommand = ETextEditCommand::Right;
			return true;
		case CTRL_CODE_UP:
			OutCommand = ETextEditCommand::Up;
			return true;
		case CTRL_CODE_DOWN:
			OutCommand = ETextEditCommand::Down;
			return true;
		case CTRL_CODE_HOME:
			OutCommand = ETextEditCommand::Home;
			return true;
		case CTRL_CODE_END:
			OutCommand = ETextEditCommand::End;
			return true;
		case CTRL_CODE_PAGE_UP:
			OutCommand = ETextEditCommand::PageUp;
			return true;
		case CTRL_CODE_PAGE_DOWN:
			OutCommand = ETextEditCommand::PageDown;
			return true;
		case CTRL_CODE_COPY:
			OutCommand = ETextEditCommand::Copy;
			return true;
		case CTRL_CODE_CUT:
			OutCommand = ETextEditCommand::Cut;
			return true;
		case CTRL_CODE_PASTE:
			OutCommand = ETextEditCommand::Paste;
			return true;
		case CTRL_CODE_ALL:
			OutCommand = ETextEditCommand::SelectAll;
			return true;
		default:
			return false;
		}
	}
}

void CDxLibInputSubsystem::SetTextInputActive( bool bActive )
{
	if ( bActive == bTextInputActive )
	{
		return;
	}
	bTextInputActive = bActive;
	// 受けない間は IME を切って、ゲームの操作に変換が割り込まないようにする。切り替えたときに、古い入力は捨てる
	SetUseIMEFlag( bActive ? TRUE : FALSE );
	ClearInputCharBuf();
	TextEvents.Reset();
	ImeComposition = FImeComposition();
	ByteAssembler.Reset();
}

bool CDxLibInputSubsystem::IsTextInputActive() const
{
	return bTextInputActive;
}

const TArray<FTextInputEvent>& CDxLibInputSubsystem::GetTextInputEvents() const
{
	return TextEvents;
}

const FImeComposition& CDxLibInputSubsystem::GetImeComposition() const
{
	return ImeComposition;
}

void CDxLibInputSubsystem::UpdateText()
{
	TextEvents.Reset();
	if ( !bTextInputActive )
	{
		return;
	}

	// 文字コードのバッファを空になるまで読む。制御コードは編集の操作に、残りは UTF-8 の 1 バイトずつ。
	// DxLib は、1 文字（全角は 3 バイトまとめて）の後ろに区切りの 0 を返すので、0 が 2 回続いたら空とする
	int32 ConsecutiveZeros = 0;
	for ( int32 Guard = 0; Guard < MaxReadsPerFrame; ++Guard )
	{
		const uint8 Code = static_cast<uint8>( GetInputChar( TRUE ) );
		if ( Code == 0 )
		{
			++ConsecutiveZeros;
			if ( ConsecutiveZeros >= 2 )
			{
				break;
			}
			continue;
		}
		ConsecutiveZeros = 0;
		if ( Code < 0x20 )
		{
			ByteAssembler.Reset();
			ETextEditCommand Command = ETextEditCommand::Backspace;
			if ( TryMapControlCode( Code, Command ) )
			{
				TextEvents.Add( FTextInputEvent::MakeCommand( Command ) );
			}
			continue;
		}
		FString Character;
		if ( ByteAssembler.Push( Code, Character ) )
		{
			TextEvents.Add( FTextInputEvent::MakeText( Character ) );
		}
	}

	// IME で入力している途中の状態
	FImeComposition Next;
	const IMEINPUTDATA* Ime = GetIMEInputData();
	if ( Ime != nullptr && Ime->InputString != nullptr && Ime->InputString[0] != '\0' )
	{
		Next.bActive = true;
		Next.Text = FString( Ime->InputString ).ToValidUtf8();
		Next.CursorByteOffset = Ime->CursorPosition < 0 ? 0 : ( Ime->CursorPosition > Next.Text.Len() ? Next.Text.Len() : Ime->CursorPosition );
		if ( Ime->CandidateNum > 0 && Ime->CandidateList != nullptr )
		{
			for ( int32 Index = 0; Index < Ime->CandidateNum; ++Index )
			{
				Next.Candidates.Add( Ime->CandidateList[Index] != nullptr ? FString( Ime->CandidateList[Index] ).ToValidUtf8() : FString() );
			}
			Next.SelectedCandidate = Ime->SelectCandidate >= 0 && Ime->SelectCandidate < Next.Candidates.Num() ? Ime->SelectCandidate : -1;
		}
	}
	ImeComposition = MoveTemp( Next );
}
