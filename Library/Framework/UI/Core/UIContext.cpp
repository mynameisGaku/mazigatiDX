// SPDX-License-Identifier: MIT
#include "UIContext.h"

#include "Library/Core/Types/Numeric.h"

namespace
{
	// 木の中に、そのポインターの部品があるか（部品を参照せず、ポインターの比較だけで探す。壊れたポインターでも安全）
	bool Contains( const CUIWidget& Parent, const CUIWidget* Target )
	{
		const TArray<TSharedPtr<CUIWidget>>& Children = Parent.GetChildren();
		for ( int32 Index = 0; Index < Children.Num(); ++Index )
		{
			if ( Children[Index].Get() == Target || Contains( *Children[Index], Target ) )
			{
				return true;
			}
		}
		return false;
	}

	FVector2 DirectionVector( EUINavDirection Direction )
	{
		switch ( Direction )
		{
		case EUINavDirection::Up:
			return FVector2( 0.0f, -1.0f );
		case EUINavDirection::Down:
			return FVector2( 0.0f, 1.0f );
		case EUINavDirection::Left:
			return FVector2( -1.0f, 0.0f );
		case EUINavDirection::Right:
		default:
			return FVector2( 1.0f, 0.0f );
		}
	}
}

void CUIContext::SetRoot( const TSharedPtr<CUIWidget>& InRoot )
{
	// 古い木の状態を、イベントなしで外す（木ごと入れ替わるので、外れた通知は起こさない）
	if ( Root )
	{
		if ( Hovered != nullptr && ( Hovered == Root.Get() || Contains( *Root, Hovered ) ) )
		{
			Hovered->bHovered = false;
		}
		if ( Pressed != nullptr && ( Pressed == Root.Get() || Contains( *Root, Pressed ) ) )
		{
			Pressed->bPressed = false;
		}
		if ( Focused != nullptr && ( Focused == Root.Get() || Contains( *Root, Focused ) ) )
		{
			Focused->bFocused = false;
		}
	}
	Hovered = nullptr;
	Pressed = nullptr;
	Focused = nullptr;
	bPreviousPointerDown = false;
	Root = InRoot;
}

bool CUIContext::IsInTree( const CUIWidget* Widget ) const
{
	return Root && Widget != nullptr && ( Widget == Root.Get() || Contains( *Root, Widget ) );
}

bool CUIContext::IsLive( const CUIWidget* Widget ) const
{
	return IsInTree( Widget ) && Widget->IsEffectivelyVisible();
}

void CUIContext::CollectFocusable( CUIWidget& Widget, TArray<CUIWidget*>& OutList ) const
{
	if ( Widget.GetVisibility() != EUIVisibility::Visible )
	{
		return;
	}
	if ( Widget.IsFocusable() && Widget.IsEffectivelyEnabled() )
	{
		OutList.Add( &Widget );
	}
	const TArray<TSharedPtr<CUIWidget>>& Children = Widget.GetChildren();
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		CollectFocusable( *Children[Index], OutList );
	}
}

bool CUIContext::SetFocus( CUIWidget* Widget )
{
	if ( Widget == Focused )
	{
		return true;
	}
	if ( Widget != nullptr && ( !IsLive( Widget ) || !Widget->IsFocusable() || !Widget->IsEffectivelyEnabled() ) )
	{
		return false;
	}
	CUIWidget* Old = Focused;
	Focused = Widget;
	if ( Old != nullptr )
	{
		Old->bFocused = false;
		Old->OnFocusLost.Broadcast( *Old );
	}
	if ( Focused != nullptr )
	{
		Focused->bFocused = true;
		Focused->OnFocusGained.Broadcast( *Focused );
		// 祖先に伝える（スクロール枠が、フォーカスした部品を見える位置へ動かす）
		for ( CUIWidget* Ancestor = Focused->GetParent(); Ancestor != nullptr; Ancestor = Ancestor->GetParent() )
		{
			Ancestor->OnChildFocused( *Focused );
		}
	}
	return true;
}

CUIWidget* CUIContext::FindNearestInDirection( CUIWidget& From, EUINavDirection Direction ) const
{
	TArray<CUIWidget*> Candidates;
	CollectFocusable( *Root, Candidates );

	const FVector2 Axis = DirectionVector( Direction );
	const FVector2 Origin = From.GetRect().GetCenter();
	CUIWidget* Best = nullptr;
	float32 BestScore = 0.0f;
	for ( int32 Index = 0; Index < Candidates.Num(); ++Index )
	{
		CUIWidget* Candidate = Candidates[Index];
		if ( Candidate == &From )
		{
			continue;
		}
		const FVector2 Delta = Candidate->GetRect().GetCenter() - Origin;
		const float32 Along = Delta.x * Axis.x + Delta.y * Axis.y;
		if ( Along <= 0.0f )
		{
			continue; // 向きの半平面の外
		}
		const float32 Across = NMath::Abs( Delta.x * Axis.y - Delta.y * Axis.x );
		// 向きに近く、横にずれていないものを選ぶ（横のずれは、2 倍の重み）
		const float32 Score = Along + Across * 2.0f;
		if ( Best == nullptr || Score < BestScore )
		{
			Best = Candidate;
			BestScore = Score;
		}
	}
	return Best;
}

CUIWidget* CUIContext::FindNavigationTarget( CUIWidget& From, EUINavDirection Direction ) const
{
	// 名前の指定が先。見つからない・使えない名前なら、位置から選ぶ
	const FString& TargetName = From.GetNavigationTarget( Direction );
	if ( !TargetName.IsEmpty() )
	{
		CUIWidget* Named = Root->FindByName( TargetName );
		if ( Named != nullptr && Named != &From && Named->IsFocusable() && Named->IsEffectivelyEnabled() && Named->IsEffectivelyVisible() )
		{
			return Named;
		}
	}
	return FindNearestInDirection( From, Direction );
}

void CUIContext::Press( CUIWidget& Target, const FVector2& Position )
{
	Pressed = &Target;
	Target.bPressed = true;
	if ( Target.IsFocusable() )
	{
		SetFocus( &Target );
	}
	Target.OnPointerPressed( Position );
	Target.OnPressed.Broadcast( Target );
}

void CUIContext::Release( CUIWidget* Over, const FVector2& Position )
{
	CUIWidget* Released = Pressed;
	Pressed = nullptr;
	if ( Released == nullptr )
	{
		return;
	}
	Released->bPressed = false;
	Released->OnPointerReleased( Position );
	Released->OnReleased.Broadcast( *Released );
	if ( Released == Over )
	{
		Released->OnClicked.Broadcast( *Released );
	}
}

void CUIContext::Activate( CUIWidget& Target )
{
	Target.OnPressed.Broadcast( Target );
	Target.OnReleased.Broadcast( Target );
	Target.OnClicked.Broadcast( Target );
}

FUIInputResult CUIContext::Update( const FUIInputFrame& InputFrame )
{
	// 入力欄にフォーカスがあるときは、一部の入力を、取り除いて使うので、手元の複写で扱う
	FUIInputFrame Frame = InputFrame;
	FUIInputResult Result;
	if ( !Root )
	{
		return Result;
	}

	// 木から外された・見えなくなった部品は、イベントなしで、状態から外す
	if ( Hovered != nullptr && !IsLive( Hovered ) )
	{
		if ( IsInTree( Hovered ) )
		{
			Hovered->bHovered = false;
		}
		Hovered = nullptr;
	}
	if ( Pressed != nullptr && !IsLive( Pressed ) )
	{
		if ( IsInTree( Pressed ) )
		{
			Pressed->bPressed = false;
		}
		Pressed = nullptr;
	}
	if ( Focused != nullptr && ( !IsLive( Focused ) || !Focused->IsFocusable() || !Focused->IsEffectivelyEnabled() ) )
	{
		if ( IsInTree( Focused ) )
		{
			Focused->bFocused = false;
			CUIWidget* Lost = Focused;
			Focused = nullptr;
			Lost->OnFocusLost.Broadcast( *Lost );
		}
		else
		{
			Focused = nullptr;
		}
	}

	// ポインターの対象: 当たった部品から親へたどって、最初の Interactive な部品
	CUIWidget* Hit = Frame.bPointerValid ? Root->HitTest( Frame.PointerPosition ) : nullptr;
	CUIWidget* Target = nullptr;
	for ( CUIWidget* Walk = Hit; Walk != nullptr; Walk = Walk->GetParent() )
	{
		if ( Walk->IsInteractive() )
		{
			Target = Walk;
			break;
		}
	}
	const bool bTargetBlocked = Target != nullptr && !Target->IsEffectivelyEnabled();
	CUIWidget* EventTarget = ( Target != nullptr && !bTargetBlocked ) ? Target : nullptr;
	Result.bPointerOverUI = Hit != nullptr;

	// ホイール: 当たった部品から親へたどって、最初に使う部品へ（使えない部品は飛ばす）
	if ( Hit != nullptr && Frame.WheelDelta != 0.0f )
	{
		for ( CUIWidget* Walk = Hit; Walk != nullptr; Walk = Walk->GetParent() )
		{
			if ( Walk->IsEffectivelyEnabled() && Walk->OnWheel( Frame.PointerPosition, Frame.WheelDelta ) )
			{
				Result.bWheelConsumed = true;
				break;
			}
		}
	}

	// ホバーの出入り
	if ( EventTarget != Hovered )
	{
		CUIWidget* Old = Hovered;
		Hovered = EventTarget;
		if ( Old != nullptr )
		{
			Old->bHovered = false;
			Old->OnHoverEnd.Broadcast( *Old );
		}
		if ( Hovered != nullptr )
		{
			Hovered->bHovered = true;
			Hovered->OnHoverBegin.Broadcast( *Hovered );
		}
	}

	// 押す・離す（主ボタン）
	const bool bPressEdge = Frame.bPointerDown && !bPreviousPointerDown;
	const bool bReleaseEdge = !Frame.bPointerDown && bPreviousPointerDown;
	bPreviousPointerDown = Frame.bPointerDown;
	if ( bPressEdge )
	{
		if ( EventTarget != nullptr )
		{
			Press( *EventTarget, Frame.PointerPosition );
		}
		else if ( !bTargetBlocked )
		{
			// 何もない所をクリックすると、フォーカスを外す
			SetFocus( nullptr );
		}
	}
	// 押している間は、ポインターが外へ出ても、押した部品へ位置を届ける（スライダーのドラッグ）
	if ( Pressed != nullptr && Frame.bPointerValid && Frame.bPointerDown && !bPressEdge )
	{
		Pressed->OnPointerDragged( Frame.PointerPosition );
	}
	if ( bReleaseEdge && Pressed != nullptr )
	{
		Release( EventTarget, Frame.PointerPosition );
	}
	if ( Frame.bPointerSecondaryDown && Hit != nullptr )
	{
		Result.bPointerOverUI = true;
	}
	Result.bPointerCaptured = Pressed != nullptr;

	// 文字入力: 入力欄にフォーカスがあれば、文字と編集の操作を渡す。左右の矢印・決定・戻るは、入力欄が（文字入力として）使うので、ここでは使わない
	if ( Focused != nullptr && Focused->WantsTextInput() && Focused->IsEffectivelyEnabled() )
	{
		Result.bWantsTextInput = true;
		Frame.bNavigateLeft = false;
		Frame.bNavigateRight = false;
		Frame.bConfirm = false;
		Frame.bCancel = false;
		static const TArray<FTextInputEvent> NoEvents;
		CUIWidget* TextWidget = Focused;
		if ( TextWidget->OnTextInput( Frame.TextEvents != nullptr ? *Frame.TextEvents : NoEvents, Frame.Ime ) )
		{
			SetFocus( nullptr );
			Result.bWantsTextInput = false;
		}
	}

	// フォーカスの移動
	TArray<CUIWidget*> Order;
	const bool bNavigationPressed = Frame.bNavigateUp || Frame.bNavigateDown || Frame.bNavigateLeft || Frame.bNavigateRight || Frame.bFocusNext || Frame.bFocusPrevious;
	if ( bNavigationPressed )
	{
		CollectFocusable( *Root, Order );
		if ( !Order.IsEmpty() )
		{
			CUIWidget* Next = nullptr;
			if ( Focused == nullptr )
			{
				// フォーカスが無いときの最初の操作は、先頭（前へなら末尾）へ
				Next = Frame.bFocusPrevious ? Order.Last() : Order[0];
			}
			else if ( Frame.bFocusNext || Frame.bFocusPrevious )
			{
				int32 Index = 0;
				for ( int32 Search = 0; Search < Order.Num(); ++Search )
				{
					if ( Order[Search] == Focused )
					{
						Index = Search;
						break;
					}
				}
				const int32 Count = Order.Num();
				Index = Frame.bFocusNext ? ( Index + 1 ) % Count : ( Index + Count - 1 ) % Count;
				Next = Order[Index];
			}
			else
			{
				const EUINavDirection Direction = Frame.bNavigateUp ? EUINavDirection::Up : ( Frame.bNavigateDown ? EUINavDirection::Down : ( Frame.bNavigateLeft ? EUINavDirection::Left : EUINavDirection::Right ) );
				// フォーカス中の部品が矢印を使うなら（スライダーの値の変更など）、フォーカスは動かさない
				if ( !Focused->OnNavigate( Direction ) )
				{
					Next = FindNavigationTarget( *Focused, Direction );
				}
			}
			if ( Next != nullptr && Next != Focused )
			{
				SetFocus( Next );
			}
			Result.bNavigationConsumed = true;
		}
	}

	// 決定: フォーカス中の部品を、クリックしたことにする
	if ( Frame.bConfirm && Focused != nullptr )
	{
		Activate( *Focused );
		Result.bNavigationConsumed = true;
	}

	// 戻る
	if ( Frame.bCancel )
	{
		Result.bCancelRequested = true;
		if ( OnCancel.IsBound() )
		{
			OnCancel.Broadcast();
			Result.bNavigationConsumed = true;
		}
	}
	// 最後に、いまのフォーカスで決める（このフレームの中で、矢印・Tab で入力欄へ動いたときも、ホストに、文字入力を始めさせる）
	Result.bWantsTextInput = Focused != nullptr && Focused->WantsTextInput() && Focused->IsEffectivelyEnabled();
	return Result;
}
