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
		AddScreen( Screen );
	}
	return Screen.Id;
}

void CUIScreenStack::AddScreen( const FScreen& InScreen )
{
	FScreen Screen = InScreen;
	const bool bAnimated = Screen.Options.Transition.Kind != EUIScreenTransitionKind::None && Screen.Options.Transition.Duration > 0.0f;
	Screen.TransitionProgress = bAnimated ? 0.0f : 1.0f;
	if ( bAnimated )
	{
		ApplyTransition( Screen );
	}
	Screens.Add( Screen );
	// 最初にフォーカスを持たせる部品（この画面の木の中のものだけ）
	if ( Screen.Options.InitialFocus != nullptr )
	{
		Screen.Context->SetFocus( Screen.Options.InitialFocus );
	}
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
		const int32 TopIndex = FindTopOpenIndex();
		return TopIndex < 0 ? false : Remove( Screens[TopIndex].Id );
	}
	// 最中: 反映されたあとの、いちばん上（あとから積まれる画面があれば、それ。外す予定の画面・消える途中の画面は、数えない）
	int32 TopId = 0;
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		if ( !Screens[Index].bClosing )
		{
			TopId = Screens[Index].Id;
		}
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
				TopId = bPendingRemoval || Screens[Search].bClosing ? 0 : Screens[Search].Id;
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
			AddScreen( Changes[Index].Screen );
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
		if ( Screens[Index].Id != Id )
		{
			continue;
		}
		FScreen& Screen = Screens[Index];
		if ( Screen.bClosing )
		{
			// すでに、消える途中
			return false;
		}
		if ( Screen.Options.Transition.Kind == EUIScreenTransitionKind::None || Screen.Options.Transition.Duration <= 0.0f )
		{
			return RemoveImmediately( Id );
		}
		// 消える演出を始める。入力は、もう受けない（ホバー・フォーカスの印を外す）。描くのは、演出が終わるまで続ける
		Screen.bClosing = true;
		Screen.Context->SetRoot( TSharedPtr<CUIWidget>() );
		return true;
	}
	return false;
}

bool CUIScreenStack::RemoveImmediately( int32 Id )
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
		RemoveImmediately( Screens.Last().Id );
	}
}

int32 CUIScreenStack::FindTopOpenIndex() const
{
	for ( int32 Index = Screens.Num() - 1; Index >= 0; --Index )
	{
		if ( !Screens[Index].bClosing )
		{
			return Index;
		}
	}
	return -1;
}

int32 CUIScreenStack::NumOpen() const
{
	int32 Count = 0;
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		Count += Screens[Index].bClosing ? 0 : 1;
	}
	return Count;
}

bool CUIScreenStack::IsClosing( int32 Id ) const
{
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		if ( Screens[Index].Id == Id )
		{
			return Screens[Index].bClosing;
		}
	}
	return false;
}

void CUIScreenStack::ApplyTransition( FScreen& Screen )
{
	const FUIScreenTransition& Transition = Screen.Options.Transition;
	const float32 Amount = NUIEasing::Evaluate( Transition.Easing, Screen.TransitionProgress );
	// 行き過ぎるイージング（Back・Elastic）でも、不透明度は 0〜1（SetOpacity が収める）
	Screen.Root->SetOpacity( Amount );
	FUIRenderTransform Pose;
	const float32 Remaining = ( 1.0f - Amount ) * Transition.Distance;
	switch ( Transition.Kind )
	{
	case EUIScreenTransitionKind::SlideUp:
		Pose.Translation = FVector2( 0.0f, Remaining );
		break;
	case EUIScreenTransitionKind::SlideDown:
		Pose.Translation = FVector2( 0.0f, -Remaining );
		break;
	case EUIScreenTransitionKind::SlideLeft:
		Pose.Translation = FVector2( Remaining, 0.0f );
		break;
	case EUIScreenTransitionKind::SlideRight:
		Pose.Translation = FVector2( -Remaining, 0.0f );
		break;
	case EUIScreenTransitionKind::Scale:
	{
		const float32 Scale = Transition.StartScale + ( 1.0f - Transition.StartScale ) * Amount;
		Pose.Scale = FVector2( Scale, Scale );
		break;
	}
	default:
		break;
	}
	Screen.Root->SetRenderTransform( Pose );
}

void CUIScreenStack::AdvanceTransitions( float32 DeltaTime )
{
	TArray<int32> Finished;
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		FScreen& Screen = Screens[Index];
		const FUIScreenTransition& Transition = Screen.Options.Transition;
		if ( Transition.Kind == EUIScreenTransitionKind::None || Transition.Duration <= 0.0f )
		{
			continue;
		}
		const float32 Step = NMath::Max( 0.0f, DeltaTime ) / Transition.Duration;
		if ( Screen.bClosing )
		{
			Screen.TransitionProgress = NMath::Max( 0.0f, Screen.TransitionProgress - Step );
			ApplyTransition( Screen );
			if ( Screen.TransitionProgress <= 0.0f )
			{
				Finished.Add( Screen.Id );
			}
		}
		else if ( Screen.TransitionProgress < 1.0f )
		{
			Screen.TransitionProgress = NMath::Min( 1.0f, Screen.TransitionProgress + Step );
			ApplyTransition( Screen );
		}
	}
	for ( int32 Index = 0; Index < Finished.Num(); ++Index )
	{
		RemoveImmediately( Finished[Index] );
	}
}

CUIWidget* CUIScreenStack::GetTopRoot() const
{
	const int32 TopIndex = FindTopOpenIndex();
	return TopIndex < 0 ? nullptr : Screens[TopIndex].Root.Get();
}

CUIContext* CUIScreenStack::GetTopContext() const
{
	const int32 TopIndex = FindTopOpenIndex();
	return TopIndex < 0 ? nullptr : Screens[TopIndex].Context.Get();
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
	// 出入りの演出を進める（消えきった画面は、ここで外れる）
	AdvanceTransitions( Frame.DeltaTime );
	bUpdating = true;
	bool bPointerTaken = false;
	bool bNavigationTaken = false;
	int32 CancelledId = 0;
	const int32 TopOpenIndex = FindTopOpenIndex();

	for ( int32 Index = Screens.Num() - 1; Index >= 0; --Index )
	{
		FScreen& Screen = Screens[Index];
		Screen.Root->UpdateLayout( Area );
		if ( Screen.bClosing )
		{
			// 消える途中の画面は、描くだけ（入力を受けず、下の画面へ、そのまま渡す）
			continue;
		}

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
		Screen.Context->SetTooltipDelay( TooltipStyle.Delay );
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
		if ( Index == TopOpenIndex && ScreenResult.bCancelRequested && Screen.Options.bPopOnCancel )
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
	UpdateTooltip();
	return Result;
}

void CUIScreenStack::SetTooltipStyle( const FUITooltipStyle& InStyle )
{
	TooltipStyle = InStyle;
	// 文字のツールチップの部品を、作り直す
	TextTooltip.Reset();
	TextTooltipLabel.Reset();
}

void CUIScreenStack::UpdateTooltip()
{
	ShownTooltip.Reset();
	ShownTooltipRect = FUIRect();
	// いちばん上の画面から順に、ツールチップを出す依頼を探す（下の画面は、上の画面がポインターを使うと、ポインターを受けない）
	for ( int32 Index = Screens.Num() - 1; Index >= 0; --Index )
	{
		if ( Screens[Index].bClosing )
		{
			continue;
		}
		const FUITooltipRequest Request = Screens[Index].Context->GetTooltip();
		if ( !Request.bVisible || Request.Source == nullptr )
		{
			continue;
		}
		if ( Request.Source->GetTooltipWidget() )
		{
			ShownTooltip = Request.Source->GetTooltipWidget();
		}
		else if ( TooltipStyle.Font && !Request.Source->GetTooltip().IsEmpty() )
		{
			if ( !TextTooltip )
			{
				TextTooltip = MakeShared<CUIWidget>();
				TextTooltip->SetHitTestable( false );
				TextTooltip->SetBackground( TooltipStyle.Background );
				FUILayoutProperties PanelLayout;
				PanelLayout.Padding = TooltipStyle.Padding;
				TextTooltip->SetLayout( PanelLayout );
				TextTooltipLabel = MakeShared<CUILabel>();
				TextTooltipLabel->SetFont( TooltipStyle.Font );
				TextTooltipLabel->SetColor( TooltipStyle.TextColor );
				TextTooltipLabel->SetMaxWidth( TooltipStyle.MaxWidth );
				TextTooltipLabel->SetHitTestable( false );
				TextTooltip->AddChild( TextTooltipLabel );
			}
			TextTooltipLabel->SetText( Request.Source->GetTooltip() );
			ShownTooltip = TextTooltip;
		}
		if ( ShownTooltip )
		{
			ShownTooltipAnchor = Request.Anchor;
			return;
		}
	}
}

void CUIScreenStack::Paint( FDrawContext& CommandList )
{
	for ( int32 Index = 0; Index < Screens.Num(); ++Index )
	{
		Screens[Index].Root->Paint( CommandList );
	}

	// ツールチップは、いちばん上に。ポインターの右下へ出し、画面からはみ出すなら、反対側へ寄せて、画面の中に収める
	ShownTooltipRect = FUIRect();
	if ( ShownTooltip )
	{
		ShownTooltip->Measure();
		const FVector2 Size = ShownTooltip->GetDesiredSize();
		float32 X = ShownTooltipAnchor.x + TooltipStyle.Offset.x;
		float32 Y = ShownTooltipAnchor.y + TooltipStyle.Offset.y;
		if ( X + Size.x > Area.Right() )
		{
			X = ShownTooltipAnchor.x - TooltipStyle.Offset.x - Size.x;
		}
		if ( Y + Size.y > Area.Bottom() )
		{
			Y = ShownTooltipAnchor.y - 6.0f - Size.y;
		}
		X = NMath::Max( Area.Left(), NMath::Min( X, Area.Right() - Size.x ) );
		Y = NMath::Max( Area.Top(), NMath::Min( Y, Area.Bottom() - Size.y ) );
		ShownTooltipRect = FUIRect( X, Y, Size.x, Size.y );
		ShownTooltip->ArrangeExact( ShownTooltipRect );
		ShownTooltip->Paint( CommandList );
	}
}
