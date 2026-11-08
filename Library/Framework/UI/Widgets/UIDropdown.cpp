// SPDX-License-Identifier: MIT
#include "UIDropdown.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/UI/Core/UIPanels.h"

CUIDropdown::CUIDropdown()
{
	// 文字と矢印を、中に置く（どちらも、ポインターを受けない）
	Label = MakeShared<CUILabel>();
	Label->SetHitTestable( false );
	FUILayoutProperties LabelLayout;
	LabelLayout.HorizontalAlignment = EUIAlignment::Start;
	LabelLayout.VerticalAlignment = EUIAlignment::Center;
	LabelLayout.Padding = DropdownStyle.TextPadding;
	Label->SetLayout( LabelLayout );
	AddChild( Label );

	ArrowWidget = MakeShared<CUIWidget>();
	ArrowWidget->SetHitTestable( false );
	FUILayoutProperties ArrowLayout;
	ArrowLayout.HorizontalAlignment = EUIAlignment::End;
	ArrowLayout.VerticalAlignment = EUIAlignment::Center;
	ArrowLayout.FixedWidth = DropdownStyle.ArrowSize.x;
	ArrowLayout.FixedHeight = DropdownStyle.ArrowSize.y;
	ArrowLayout.Margin = FUIMargin( 0.0f, 0.0f, 10.0f, 0.0f );
	ArrowWidget->SetLayout( ArrowLayout );
	AddChild( ArrowWidget );

	// 押す（クリック・決定）と、開く（開いていれば、閉じる）
	OnClicked.Add( [this]( CUIWidget& )
	{
		if ( IsOpen() )
		{
			Close();
		}
		else
		{
			Open();
		}
	} );
	RefreshLabel();
}

void CUIDropdown::SetFont( const TSharedPtr<CFont>& InFont )
{
	Font = InFont;
	Label->SetFont( InFont );
	MarkLayoutDirty();
}

void CUIDropdown::SetDropdownStyle( const FUIDropdownStyle& InStyle )
{
	DropdownStyle = InStyle;
	SetStyle( InStyle.Button );
	FUILayoutProperties LabelLayout = Label->GetLayout();
	LabelLayout.Padding = InStyle.TextPadding;
	Label->SetLayout( LabelLayout );
	FUILayoutProperties ArrowLayout = ArrowWidget->GetLayout();
	ArrowLayout.FixedWidth = InStyle.ArrowSize.x;
	ArrowLayout.FixedHeight = InStyle.ArrowSize.y;
	ArrowWidget->SetLayout( ArrowLayout );
	ArrowWidget->SetBackground( InStyle.Arrow );
	RefreshLabel();
}

void CUIDropdown::SetItems( const TArray<FString>& InItems )
{
	Items = InItems;
	const int32 OldSelection = SelectedIndex;
	if ( SelectedIndex >= Items.Num() )
	{
		SelectedIndex = Items.Num() - 1;
	}
	RefreshLabel();
	if ( SelectedIndex != OldSelection )
	{
		OnSelectionChanged.Broadcast( *this, SelectedIndex );
	}
}

void CUIDropdown::SetSelectedIndex( int32 InIndex, bool bNotify )
{
	const int32 Next = Items.IsEmpty() ? -1 : NMath::Clamp( InIndex, -1, Items.Num() - 1 );
	if ( Next == SelectedIndex )
	{
		return;
	}
	SelectedIndex = Next;
	RefreshLabel();
	if ( bNotify )
	{
		OnSelectionChanged.Broadcast( *this, SelectedIndex );
	}
}

FString CUIDropdown::GetSelectedText() const
{
	return Items.IsValidIndex( SelectedIndex ) ? Items[SelectedIndex] : FString();
}

void CUIDropdown::SetPlaceholder( const FString& InPlaceholder )
{
	Placeholder = InPlaceholder;
	RefreshLabel();
}

void CUIDropdown::RefreshLabel()
{
	const bool bHasSelection = Items.IsValidIndex( SelectedIndex );
	Label->SetText( bHasSelection ? Items[SelectedIndex] : Placeholder );
	Label->SetColor( bHasSelection ? DropdownStyle.TextColor : DropdownStyle.PlaceholderColor );
}

bool CUIDropdown::IsOpen() const
{
	return PopupId != 0 && PopupHost != nullptr && PopupHost->FindContext( PopupId ) != nullptr;
}

void CUIDropdown::Close()
{
	if ( IsOpen() )
	{
		PopupHost->Remove( PopupId );
	}
	PopupId = 0;
}

void CUIDropdown::Open()
{
	if ( IsOpen() || PopupHost == nullptr || Items.IsEmpty() || !IsEffectivelyEnabled() )
	{
		return;
	}
	const TWeakPtr<bool> AliveToken = Alive;

	// 一覧: 項目の部品は、文字だけのラベル。選んでいる項目は、背景で見せる
	TSharedPtr<CUIListView> List = MakeShared<CUIListView>();
	const TArray<FString> Snapshot = Items;
	const FUIDropdownStyle StyleCopy = DropdownStyle;
	const TSharedPtr<CFont> FontCopy = Font;
	List->SetItemFactory(
		[FontCopy, StyleCopy]() -> TSharedPtr<CUIWidget>
		{
			TSharedPtr<CUILabel> Item = MakeShared<CUILabel>();
			Item->SetFont( FontCopy );
			Item->SetColor( StyleCopy.ItemTextColor );
			FUILayoutProperties ItemLayout;
			ItemLayout.HorizontalAlignment = EUIAlignment::Stretch;
			ItemLayout.VerticalAlignment = EUIAlignment::Center;
			ItemLayout.Padding = StyleCopy.ItemPadding;
			Item->SetLayout( ItemLayout );
			return Item;
		},
		[Snapshot, StyleCopy]( CUIWidget& Item, int32 Index, bool bSelected )
		{
			CUILabel& Row = static_cast<CUILabel&>( Item );
			Row.SetText( Snapshot.IsValidIndex( Index ) ? Snapshot[Index] : FString() );
			Row.SetBackground( bSelected ? StyleCopy.ItemSelected : FUIBrush() );
		} );
	List->SetItemHeight( StyleCopy.ItemHeight );
	List->SetScrollBar( StyleCopy.ScrollTrack, StyleCopy.ScrollThumb, StyleCopy.ScrollBarWidth );
	List->SetItemCount( Items.Num() );
	List->SetSelectedIndex( SelectedIndex, false );
	// クリック・決定: その項目に決めて、閉じる
	List->OnItemClicked.Add( [this, AliveToken]( CUIWidget&, int32 Index )
	{
		if ( !AliveToken.IsValid() || Index < 0 )
		{
			return;
		}
		SetSelectedIndex( Index );
		Close();
	} );

	// 一覧の枠: 背景と余白。ドロップダウンの下（入りきらなければ上）に置く
	const int32 Visible = NMath::Max( 1, NMath::Min( Items.Num(), StyleCopy.MaxVisibleItems ) );
	const float32 Width = NMath::Max( 80.0f, GetRect().Size.x );
	const float32 Height = static_cast<float32>( Visible ) * StyleCopy.ItemHeight + StyleCopy.PopupPadding.GetVertical();
	const FUIRect Area = PopupHost->GetArea();
	float32 X = GetRect().Left();
	float32 Y = GetRect().Bottom();
	if ( Y + Height > Area.Bottom() )
	{
		Y = GetRect().Top() - Height;
	}
	X = NMath::Max( Area.Left(), NMath::Min( X, Area.Right() - Width ) );
	Y = NMath::Max( Area.Top(), Y );

	TSharedPtr<CUIWidget> Frame = MakeShared<CUIWidget>();
	Frame->SetBackground( StyleCopy.PopupBackground );
	FUILayoutProperties FrameLayout;
	FrameLayout.Padding = StyleCopy.PopupPadding;
	FrameLayout.Offsets = FUIMargin( X, Y, Width, Height );
	Frame->SetLayout( FrameLayout );
	Frame->AddChild( List );

	// 全体を覆う、見えない受け皿: 一覧の外をクリックすると、選ばずに閉じる
	TSharedPtr<CUICanvasPanel> PopupRoot = MakeShared<CUICanvasPanel>();
	PopupRoot->SetInteractive( true );
	PopupRoot->OnClicked.Add( [this, AliveToken]( CUIWidget& )
	{
		if ( AliveToken.IsValid() )
		{
			Close();
		}
	} );
	PopupRoot->AddChild( Frame );

	FUIScreenOptions Options;
	Options.bModal = true;
	Options.bBlocksGameInput = true;
	Options.bPopOnCancel = true;
	Options.InitialFocus = List.Get();
	PopupId = PopupHost->Push( PopupRoot, Options );
}
