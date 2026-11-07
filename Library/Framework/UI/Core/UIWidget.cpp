// SPDX-License-Identifier: MIT
#include "UIWidget.h"

#include <cmath>

#include "Library/Core/Types/Numeric.h"

namespace
{
	// 置き場所の中での、寄せ方による位置（Stretch は、広げたあとなので Start と同じ）
	float32 PlaceOnAxis( EUIAlignment Alignment, float32 AvailableStart, float32 AvailableSize, float32 Size )
	{
		switch ( Alignment )
		{
		case EUIAlignment::Center:
			return AvailableStart + ( AvailableSize - Size ) * 0.5f;
		case EUIAlignment::End:
			return AvailableStart + AvailableSize - Size;
		case EUIAlignment::Start:
		case EUIAlignment::Stretch:
		default:
			return AvailableStart;
		}
	}
}

CUIWidget::~CUIWidget()
{
	// 子が先に残っても、壊れた親を指さないようにする
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		Children[Index]->Parent = nullptr;
	}
}

bool CUIWidget::AddChild( const TSharedPtr<CUIWidget>& Child )
{
	return InsertChild( Child, Children.Num() );
}

bool CUIWidget::InsertChild( const TSharedPtr<CUIWidget>& Child, int32 Index )
{
	if ( !Child )
	{
		return false;
	}
	// 自分自身や、自分の祖先を子にすると、木が輪になる
	for ( CUIWidget* Ancestor = this; Ancestor != nullptr; Ancestor = Ancestor->Parent )
	{
		if ( Ancestor == Child.Get() )
		{
			return false;
		}
	}

	// 引数が、外す元の配列の中の要素のことがあるので、先に手元へ持つ
	const TSharedPtr<CUIWidget> Keep = Child;
	if ( Keep->Parent != nullptr )
	{
		Keep->Parent->RemoveChild( Keep.Get() );
	}

	const int32 SafeIndex = ( Index < 0 || Index > Children.Num() ) ? Children.Num() : Index;
	Children.Insert( Keep, SafeIndex );
	Keep->Parent = this;
	Keep->bLayoutDirty = true;
	MarkLayoutDirty();
	return true;
}

bool CUIWidget::RemoveChild( const CUIWidget* Child )
{
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		if ( Children[Index].Get() == Child )
		{
			// 外した子は、あとで別の木に付けたとき、計算し直されるようにする
			const TSharedPtr<CUIWidget> Removed = Children[Index];
			Removed->Parent = nullptr;
			Removed->bLayoutDirty = true;
			Children.RemoveAt( Index );
			MarkLayoutDirty();
			return true;
		}
	}
	return false;
}

void CUIWidget::RemoveAllChildren()
{
	if ( Children.IsEmpty() )
	{
		return;
	}
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		Children[Index]->Parent = nullptr;
		Children[Index]->bLayoutDirty = true;
	}
	Children.Empty();
	MarkLayoutDirty();
}

CUIWidget* CUIWidget::FindByName( const FString& InName )
{
	if ( Name == InName )
	{
		return this;
	}
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		if ( CUIWidget* Found = Children[Index]->FindByName( InName ) )
		{
			return Found;
		}
	}
	return nullptr;
}

void CUIWidget::SetVisibility( EUIVisibility InVisibility )
{
	if ( Visibility == InVisibility )
	{
		return;
	}
	Visibility = InVisibility;
	// Collapsed かどうかで、場所の取り方が変わる（Hidden と Visible は、レイアウトが同じ）
	MarkLayoutDirty();
}

CUIWidget* CUIWidget::HitTest( const FVector2& Point )
{
	if ( Visibility != EUIVisibility::Visible )
	{
		return nullptr;
	}

	const bool bInside = Rect.Contains( Point );
	// 子は、あとのものほど手前なので、後ろから調べる。クリップするなら、自分の矩形の外は、子にも当たらない
	if ( bInside || !bClipChildren )
	{
		for ( int32 Index = Children.Num() - 1; Index >= 0; --Index )
		{
			if ( CUIWidget* Hit = Children[Index]->HitTest( Point ) )
			{
				return Hit;
			}
		}
	}
	return ( bHitTestable && bInside ) ? this : nullptr;
}

void CUIWidget::OnPaint( FDrawContext&, float32 )
{
}

void CUIWidget::Paint( FDrawContext& CommandList )
{
	PaintRecursive( CommandList, 1.0f );
	// ブラシが設定したブレンド（アルファ）と画像に掛ける色を、元（不透明・白）に戻す
	CommandList.Blend( EDrawBlend::Opaque );
	CommandList.Tint( FColor32( 255, 255, 255 ) );
}

void CUIWidget::PaintRecursive( FDrawContext& CommandList, float32 ParentOpacity )
{
	if ( Visibility != EUIVisibility::Visible )
	{
		return;
	}
	const float32 EffectiveOpacity = ParentOpacity * Opacity;
	if ( EffectiveOpacity <= 0.0f )
	{
		return;
	}

	NUIBrush::Draw( CommandList, Background, Rect, EffectiveOpacity );
	OnPaint( CommandList, EffectiveOpacity );

	// クリップは整数の範囲。矩形を含む最小の整数の範囲にする
	bool bPushedClip = false;
	if ( bClipChildren )
	{
		bPushedClip = CommandList.PushClip( static_cast<int32>( std::floor( Rect.Left() ) ), static_cast<int32>( std::floor( Rect.Top() ) ),
											static_cast<int32>( std::ceil( Rect.Right() ) ), static_cast<int32>( std::ceil( Rect.Bottom() ) ) );
	}
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		Children[Index]->PaintRecursive( CommandList, EffectiveOpacity );
	}
	if ( bPushedClip )
	{
		CommandList.PopClip();
	}
}

bool CUIWidget::IsEffectivelyEnabled() const
{
	for ( const CUIWidget* Widget = this; Widget != nullptr; Widget = Widget->Parent )
	{
		if ( !Widget->bEnabled )
		{
			return false;
		}
	}
	return true;
}

bool CUIWidget::IsEffectivelyVisible() const
{
	for ( const CUIWidget* Widget = this; Widget != nullptr; Widget = Widget->Parent )
	{
		if ( Widget->Visibility != EUIVisibility::Visible )
		{
			return false;
		}
	}
	return true;
}

void CUIWidget::SetLayout( const FUILayoutProperties& InLayout )
{
	Layout = InLayout;
	MarkLayoutDirty();
}

void CUIWidget::MarkLayoutDirty()
{
	for ( CUIWidget* Widget = this; Widget != nullptr; Widget = Widget->Parent )
	{
		Widget->bLayoutDirty = true;
	}
}

void CUIWidget::ClearLayoutDirty()
{
	bLayoutDirty = false;
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		Children[Index]->ClearLayoutDirty();
	}
}

bool CUIWidget::UpdateLayout( const FUIRect& Area )
{
	if ( !bLayoutDirty && bHasArranged && LastArea == Area )
	{
		return false;
	}
	Measure();
	Arrange( Area );
	LastArea = Area;
	bHasArranged = true;
	ClearLayoutDirty();
	return true;
}

FVector2 CUIWidget::ComputeContentSize() const
{
	FVector2 Largest( 0.0f, 0.0f );
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		const CUIWidget& Child = *Children[Index];
		if ( Child.GetVisibility() == EUIVisibility::Collapsed )
		{
			continue;
		}
		Largest.x = NMath::Max( Largest.x, Child.GetDesiredSize().x );
		Largest.y = NMath::Max( Largest.y, Child.GetDesiredSize().y );
	}
	return Largest;
}

void CUIWidget::ArrangeChildren( const FUIRect& ContentRect )
{
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		CUIWidget& Child = *Children[Index];
		if ( Child.GetVisibility() != EUIVisibility::Collapsed )
		{
			Child.Arrange( ContentRect );
		}
	}
}

void CUIWidget::Measure()
{
	if ( Visibility == EUIVisibility::Collapsed )
	{
		DesiredSize = FVector2( 0.0f, 0.0f );
		return;
	}

	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		Children[Index]->Measure();
	}

	FVector2 Content = ComputeContentSize() + Layout.Padding.GetSize();
	if ( Layout.FixedWidth >= 0.0f )
	{
		Content.x = Layout.FixedWidth;
	}
	if ( Layout.FixedHeight >= 0.0f )
	{
		Content.y = Layout.FixedHeight;
	}
	Content.x = NMath::Clamp( Content.x, Layout.MinSize.x, Layout.MaxSize.x );
	Content.y = NMath::Clamp( Content.y, Layout.MinSize.y, Layout.MaxSize.y );
	DesiredSize = Content + Layout.Margin.GetSize();
}

void CUIWidget::Arrange( const FUIRect& Slot )
{
	if ( Visibility == EUIVisibility::Collapsed )
	{
		Rect = FUIRect( Slot.Position, FVector2( 0.0f, 0.0f ) );
		return;
	}

	const FUIRect Available = Slot.Deflate( Layout.Margin );
	const FVector2 ContentDesired = DesiredSize - Layout.Margin.GetSize();

	// 広げる軸は、置き場所いっぱいに。固定の大きさがあれば、広げない。広げない軸は、ほしい大きさ（置き場所からはみ出てもよい）
	FVector2 Size;
	Size.x = ( Layout.HorizontalAlignment == EUIAlignment::Stretch && Layout.FixedWidth < 0.0f ) ? Available.Size.x : ContentDesired.x;
	Size.y = ( Layout.VerticalAlignment == EUIAlignment::Stretch && Layout.FixedHeight < 0.0f ) ? Available.Size.y : ContentDesired.y;
	Size.x = NMath::Clamp( Size.x, Layout.MinSize.x, Layout.MaxSize.x );
	Size.y = NMath::Clamp( Size.y, Layout.MinSize.y, Layout.MaxSize.y );

	const FVector2 Position( PlaceOnAxis( Layout.HorizontalAlignment, Available.Position.x, Available.Size.x, Size.x ),
							 PlaceOnAxis( Layout.VerticalAlignment, Available.Position.y, Available.Size.y, Size.y ) );
	ArrangeExact( FUIRect( Position, Size ) );
}

void CUIWidget::ArrangeExact( const FUIRect& Final )
{
	if ( Visibility == EUIVisibility::Collapsed )
	{
		Rect = FUIRect( Final.Position, FVector2( 0.0f, 0.0f ) );
		return;
	}
	Rect = Final;
	ArrangeChildren( Rect.Deflate( Layout.Padding ) );

	// 場所を取らない子（Collapsed）は、パネルが置かないので、前の矩形が残らないよう、大きさ 0 にする
	for ( int32 Index = 0; Index < Children.Num(); ++Index )
	{
		if ( Children[Index]->Visibility == EUIVisibility::Collapsed )
		{
			Children[Index]->Rect = FUIRect( Rect.Position, FVector2( 0.0f, 0.0f ) );
		}
	}
}
