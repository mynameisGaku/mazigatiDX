// SPDX-License-Identifier: MIT
#include "UIDialog.h"

#include "Library/Core/Types/Numeric.h"

CUIDialog::CUIDialog( const FUIDialogStyle& InStyle, const FString& InTitle, const FString& InMessage, const TArray<FUIDialogButton>& InButtons, int32 InDefaultId, int32 InCancelId )
	: CancelId( InCancelId )
{
	// 暗幕: 画面全体（クリックを止める）
	SetBackground( InStyle.Dim );
	FUILayoutProperties RootLayout;
	RootLayout.AnchorMin = FVector2( 0.0f, 0.0f );
	RootLayout.AnchorMax = FVector2( 1.0f, 1.0f );
	SetLayout( RootLayout );

	// 窓: 中央
	Window = MakeShared<CUIWidget>();
	Window->SetBackground( InStyle.Window );
	FUILayoutProperties WindowLayout;
	WindowLayout.AnchorMin = FVector2( 0.5f, 0.5f );
	WindowLayout.AnchorMax = FVector2( 0.5f, 0.5f );
	WindowLayout.Pivot = FVector2( 0.5f, 0.5f );
	WindowLayout.Offsets = FUIMargin( 0.0f, 0.0f, InStyle.WindowSize.x, InStyle.WindowSize.y );
	WindowLayout.Padding = InStyle.Padding;
	Window->SetLayout( WindowLayout );
	AddChild( Window );

	TSharedPtr<CUIStackPanel> Column = MakeShared<CUIStackPanel>();
	Column->SetOrientation( EUIOrientation::Vertical );
	Column->SetSpacing( 14.0f );
	Window->AddChild( Column );

	TitleLabel = MakeShared<CUILabel>();
	TitleLabel->SetFont( InStyle.TitleFont );
	TitleLabel->SetText( InTitle );
	TitleLabel->SetColor( InStyle.TitleColor );
	FUILayoutProperties TitleLayout;
	TitleLayout.HorizontalAlignment = EUIAlignment::Start;
	TitleLayout.VerticalAlignment = EUIAlignment::Start;
	TitleLabel->SetLayout( TitleLayout );
	Column->AddChild( TitleLabel );

	// メッセージ: 窓の幅で折り返し、残りの高さを取る
	MessageLabel = MakeShared<CUILabel>();
	MessageLabel->SetFont( InStyle.BodyFont );
	MessageLabel->SetText( InMessage );
	MessageLabel->SetColor( InStyle.MessageColor );
	MessageLabel->SetMaxWidth( NMath::Max( 1.0f, InStyle.WindowSize.x - InStyle.Padding.GetHorizontal() ) );
	FUILayoutProperties MessageLayout;
	MessageLayout.Weight = 1.0f;
	MessageLayout.HorizontalAlignment = EUIAlignment::Start;
	MessageLayout.VerticalAlignment = EUIAlignment::Start;
	MessageLabel->SetLayout( MessageLayout );
	Column->AddChild( MessageLabel );

	// ボタン: 窓の下に、中央寄せで並べる
	TSharedPtr<CUIStackPanel> Row = MakeShared<CUIStackPanel>();
	Row->SetOrientation( EUIOrientation::Horizontal );
	Row->SetSpacing( InStyle.ButtonSpacing );
	FUILayoutProperties RowLayout;
	RowLayout.FixedHeight = InStyle.ButtonSize.y;
	RowLayout.HorizontalAlignment = EUIAlignment::Center;
	Row->SetLayout( RowLayout );
	Column->AddChild( Row );

	for ( int32 Index = 0; Index < InButtons.Num(); ++Index )
	{
		TSharedPtr<CUIButton> Button = MakeShared<CUIButton>();
		Button->SetStyle( InStyle.Button );
		FUILayoutProperties ButtonLayout;
		ButtonLayout.FixedWidth = InStyle.ButtonSize.x;
		ButtonLayout.FixedHeight = InStyle.ButtonSize.y;
		ButtonLayout.HorizontalAlignment = EUIAlignment::Start;
		Button->SetLayout( ButtonLayout );

		TSharedPtr<CUILabel> Caption = MakeShared<CUILabel>();
		Caption->SetFont( InStyle.BodyFont );
		Caption->SetText( InButtons[Index].Text );
		Caption->SetColor( InStyle.MessageColor );
		FUILayoutProperties CaptionLayout;
		CaptionLayout.HorizontalAlignment = EUIAlignment::Center;
		CaptionLayout.VerticalAlignment = EUIAlignment::Center;
		Caption->SetLayout( CaptionLayout );
		Caption->SetHitTestable( false );
		Button->AddChild( Caption );

		const int32 Id = InButtons[Index].Id;
		Button->OnClicked.Add( [this, Id]( CUIWidget& ) { OnResult.Broadcast( *this, Id ); } );
		Row->AddChild( Button );

		FButtonEntry Entry;
		Entry.Id = Id;
		Entry.Button = Button;
		Buttons.Add( Entry );
		// 既定の番号のボタン（なければ、先頭のボタン）
		if ( DefaultButton == nullptr || Id == InDefaultId )
		{
			DefaultButton = Button.Get();
		}
	}
}

CUIButton* CUIDialog::FindButton( int32 Id ) const
{
	for ( int32 Index = 0; Index < Buttons.Num(); ++Index )
	{
		if ( Buttons[Index].Id == Id )
		{
			return Buttons[Index].Button.Get();
		}
	}
	return nullptr;
}

void CUIDialog::Cancel()
{
	if ( CancelId >= 0 )
	{
		OnResult.Broadcast( *this, CancelId );
	}
}

int32 NUIDialog::Show( CUIScreenStack& Screens, const TSharedPtr<CUIDialog>& Dialog )
{
	return Show( Screens, Dialog, FUIScreenTransition() );
}

int32 NUIDialog::Show( CUIScreenStack& Screens, const TSharedPtr<CUIDialog>& Dialog, const FUIScreenTransition& Transition )
{
	if ( !Dialog )
	{
		return 0;
	}
	FUIScreenOptions Options;
	Options.bModal = true;
	Options.bBlocksGameInput = true;
	Options.bPopOnCancel = false; // 戻るは、ダイアログ自身が、結果として扱う
	Options.Transition = Transition;
	const int32 Id = Screens.Push( Dialog, Options );
	if ( Id == 0 )
	{
		return 0;
	}

	// 結果が決まったら、画面を外す（ボタンのイベントの中から呼ばれても安全）
	CUIScreenStack* Stack = &Screens;
	Dialog->OnResult.Add( [Stack, Id]( CUIWidget&, int32 ) { Stack->Remove( Id ); } );

	if ( CUIContext* Context = Screens.FindContext( Id ) )
	{
		// 戻る: ダイアログは、戻るを使う（CancelId が無ければ、何も起こさない）
		CUIDialog* Raw = Dialog.Get();
		Context->OnCancel.Add( [Raw]() { Raw->Cancel(); } );
		Context->SetFocus( Dialog->GetDefaultButton() );
	}
	return Id;
}
