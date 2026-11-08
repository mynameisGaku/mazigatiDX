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
		Frame.TextEvents = nullptr;
		Frame.Ime = nullptr;
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
	if ( bUpdating )
	{
		FPendingChange Change;
		Change.bPush = true;
		Change.Screen = Screen;
		Pending.Add( Change );
	}
	else
	{
		Screens.Add( Screen );
	}
	return Screen.Id;
}

bool CUIScreenStack::IsKnown( int32 Id ) const
{
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		if ( Screens[Index].Id == Id )
		{
			return true;
		}
	}
	for ( int32 Index = 0; Index < Pending.Num(); ++Index )
	{
		if ( Pending[Index].bPush && Pending[Index].Screen.Id == Id )
		{
			return true;
		}
	}
	return false;
}

bool CUIScreenStack::Pop()
{
	if ( !bUpdating )
	{
		return Screens.IsEmpty() ? false : Remove( Screens.Last().Id );
	}
	// 最中: 反映されたあとの、いちばん上（あとから積まれる画面があれば、それ。外す予定の画面は、数えない）
	int32 TopId = 0;
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		TopId = Screens[Index].Id;
	}
	for ( int32 Index = 0; Index < Pending.Num(); ++Index )
	{
		if ( Pending[Index].bPush )
		{
			TopId = Pending[Index].Screen.Id;
		}
	}
	for ( int32 Index = 0; Index < Pending.Num(); ++Index )
	{
		if ( !Pending[Index].bPush && Pending[Index].RemoveId == TopId )
		{
			// すでに外す予定。その下の画面を、探す
			TopId = 0;
			for ( int32 Search = Screens.Num() - 1; Search >= 0 && TopId == 0; --Search )
			{
				bool bPendingRemoval = false;
				for ( int32 Check = 0; Check < Pending.Num(); ++Check )
				{
					bPendingRemoval = bPendingRemoval || ( !Pending[Check].bPush && Pending[Check].RemoveId == Screens[Search].Id );
				}
				TopId = bPendingRemoval ? 0 : Screens[Search].Id;
			}
			break;
		}
	}
	return TopId != 0 && Remove( TopId );
}

bool CUIScreenStack::Remove( int32 Id )
{
	if ( bUpdating )
	{
		if ( !IsKnown( Id ) )
		{
			return false;
		}
		FPendingChange Change;
		Change.RemoveId = Id;
		Pending.Add( Change );
		return true;
	}
	return RemoveNow( Id );
}

void CUIScreenStack::ApplyPending()
{
	// 反映の最中に、イベントの受け手が、また積む・外すを呼んでも、混ざらないよう、いまの分を取り出してから反映する
	const TArray<FPendingChange> Changes = Pending;
	Pending.Empty();
	for ( int32 Index = 0; Index < Changes.Num(); ++Index )
	{
		if ( Changes[Index].bPush )
		{
			Screens.Add( Changes[Index].Screen );
		}
		else
		{
			RemoveNow( Changes[Index].RemoveId );
		}
	}
}

bool CUIScreenStack::RemoveNow( int32 Id )
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
	if ( bUpdating )
	{
		// 最中: いまある画面と、これから積まれる画面を、すべて外す予定にする
		TArray<int32> Ids;
		for ( int32 Index = 0; Index < Screens.Num(); ++Index )
		{
			Ids.Add( Screens[Index].Id );
		}
		for ( int32 Index = 0; Index < Pending.Num(); ++Index )
		{
			if ( Pending[Index].bPush )
			{
				Ids.Add( Pending[Index].Screen.Id );
			}
		}
		for ( int32 Index = Ids.Num() - 1; Index >= 0; --Index )
		{
			Remove( Ids[Index] );
		}
		return;
	}
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
	// これから積まれる画面（Update の最中に Push した画面）も探す
	for ( int32 Index = 0; Index < Pending.Num(); ++Index )
	{
		if ( Pending[Index].bPush && Pending[Index].Screen.Id == Id )
		{
			return Pending[Index].Screen.Context.Get();
		}
	}
	return nullptr;
}

FUIScreenInputResult CUIScreenStack::Update( const FUIInputFrame& Frame )
{
	FUIScreenInputResult Result;
	bUpdating = true;
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
		Result.bWantsTextInput = Result.bWantsTextInput || ScreenResult.bWantsTextInput;
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

	// 最中に呼ばれた、積む・外すを反映する。そのあとで、戻るで外す
	bUpdating = false;
	ApplyPending();
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
