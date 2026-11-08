// SPDX-License-Identifier: MIT
#include "UIScreenStack.h"

namespace
{
	// ポインターに関する入力を、取り除く（下の画面が、上の画面の入力を見ないように）
	void RemovePointer( FUIInputFrame& Frame )
	{
		Frame.bPointerValid = false;
		Frame.bPointerDown = false;
		Frame.bPointerSecondaryDown = false;
		Frame.WheelDelta = 0.0f;
	}

	// ナビゲーション・決定・戻るを、取り除く
	void RemoveNavigation( FUIInputFrame& Frame )
	{
		Frame.bNavigateUp = false;
		Frame.bNavigateDown = false;
		Frame.bNavigateLeft = false;
		Frame.bNavigateRight = false;
		Frame.bConfirm = false;
		Frame.bCancel = false;
		Frame.bFocusNext = false;
		Frame.bFocusPrevious = false;
	}
}

int32 CUIScreenStack::Push( const TSharedPtr<CUIWidget>& Root, const FUIScreenOptions& Options )
{
	if ( !Root )
	{
		return 0;
	}
	FScreen Screen;
	Screen.Id = NextId++;
	Screen.Root = Root;
	Screen.Options = Options;
	Screen.Context = MakeShared<CUIContext>();
	Screen.Context->SetRoot( Root );
	Screens.Add( Screen );
	return Screen.Id;
}

bool CUIScreenStack::Pop()
{
	if ( Screens.IsEmpty() )
	{
		return false;
	}
	return Remove( Screens.Last().Id );
}

bool CUIScreenStack::Remove( int32 Id )
{
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		if ( Screens[Index].Id == Id )
		{
			// 状態を外してから、取り除く（部品の側の、ホバー・フォーカスの印を残さない）
			Screens[Index].Context->SetRoot( TSharedPtr<CUIWidget>() );
			Screens.RemoveAt( Index );
			OnPopped.Broadcast( Id );
			return true;
		}
	}
	return false;
}

void CUIScreenStack::Clear()
{
	while ( !Screens.IsEmpty() )
	{
		Pop();
	}
}

CUIWidget* CUIScreenStack::GetTopRoot() const
{
	return Screens.IsEmpty() ? nullptr : Screens.Last().Root.Get();
}

CUIContext* CUIScreenStack::GetTopContext() const
{
	return Screens.IsEmpty() ? nullptr : Screens.Last().Context.Get();
}

CUIContext* CUIScreenStack::FindContext( int32 Id ) const
{
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		if ( Screens[Index].Id == Id )
		{
			return Screens[Index].Context.Get();
		}
	}
	return nullptr;
}

FUIScreenInputResult CUIScreenStack::Update( const FUIInputFrame& Frame )
{
	FUIScreenInputResult Result;
	bool bPointerTaken = false;
	bool bNavigationTaken = false;
	int32 CancelledId = 0;

	for ( int32 Index = Screens.Num() - 1; Index >= 0; --Index )
	{
		FScreen& Screen = Screens[Index];
		Screen.Root->UpdateLayout( Area );

		// 上の画面が使った入力は、この画面には届かない（ナビゲーションは、いちばん上の画面だけ）
		FUIInputFrame Local = Frame;
		if ( bPointerTaken )
		{
			RemovePointer( Local );
		}
		if ( bNavigationTaken )
		{
			RemoveNavigation( Local );
		}
		const FUIInputResult ScreenResult = Screen.Context->Update( Local );

		Result.bPointerOverUI = Result.bPointerOverUI || ScreenResult.bPointerOverUI;
		Result.bPointerCaptured = Result.bPointerCaptured || ScreenResult.bPointerCaptured;
		Result.bNavigationConsumed = Result.bNavigationConsumed || ScreenResult.bNavigationConsumed;
		Result.bWheelConsumed = Result.bWheelConsumed || ScreenResult.bWheelConsumed;
		if ( Screen.Options.bBlocksGameInput )
		{
			Result.bGameInputBlocked = true;
		}
		if ( Index == Screens.Num() - 1 && ScreenResult.bCancelRequested && Screen.Options.bPopOnCancel )
		{
			CancelledId = Screen.Id;
		}

		// 下の画面へ渡すか: ポインターは、この画面が使った（または、モーダル）なら止める。ナビゲーションは、必ず止める
		if ( ScreenResult.ConsumesPointer() || Screen.Options.bModal )
		{
			bPointerTaken = true;
		}
		bNavigationTaken = true;
	}

	// 戻るで外す（更新の最中に外すと、画面の一覧が変わるので、最後にまとめて）
	if ( CancelledId != 0 )
	{
		Remove( CancelledId );
		Result.bNavigationConsumed = true;
	}

	Result.bGameInputBlocked = Result.bGameInputBlocked || Result.ConsumesPointer() || Result.bNavigationConsumed || Result.bWheelConsumed;
	return Result;
}

void CUIScreenStack::Paint( FDrawContext& CommandList )
{
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		Screens[Index].Root->Paint( CommandList );
	}
}
