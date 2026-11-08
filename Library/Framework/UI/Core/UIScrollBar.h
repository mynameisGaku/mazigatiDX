// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/UI/Core/UITypes.h"

//=============================================================================
// スクロールバーの計算（CUIScrollBox と CUIListView が共有する。描画や入力の部品は知らない）
//   溝（Track）の矩形、つまみ（Thumb）の矩形、つまみのつかみ・ドラッグ・溝のクリック（1 ページずつ送る）。
//   つまみの長さは、見えている割合（短すぎると見えないので、最小 24 画素）。
//
//   NUIScrollBar::FDrag Drag;
//   // 押したとき
//   float32 Offset = ScrollOffset;
//   if ( NUIScrollBar::Press( Track, Thumb, Orientation, Position, ScrollOffset, Viewport, MaxOffset, Drag, Offset ) ) { SetScrollOffset( Offset ); }
//   // 押している間
//   if ( Drag.bActive ) { SetScrollOffset( NUIScrollBar::DragOffset( Track, Thumb, Orientation, Position, Drag, MaxOffset ) ); }
//=============================================================================
namespace NUIScrollBar
{
	// つまみをつかんでいる間の状態
	struct FDrag
	{
		bool bActive = false;
		float32 Grab = 0.0f;   // つまみの先頭から、つかんだ位置までの距離（主軸）
	};

	inline bool IsVertical( EUIOrientation Orientation )
	{
		return Orientation == EUIOrientation::Vertical;
	}

	// 溝の矩形。View は、バーを置く範囲（内側の余白を引いたもの）。出さない（bShown が false）なら、大きさ 0
	inline FUIRect MakeTrack( const FUIRect& View, EUIOrientation Orientation, float32 Width, bool bShown )
	{
		if ( !bShown || Width <= 0.0f )
		{
			return FUIRect( View.Position, FVector2( 0.0f, 0.0f ) );
		}
		if ( IsVertical( Orientation ) )
		{
			return FUIRect( View.Right() - Width, View.Top(), Width, View.Size.y );
		}
		return FUIRect( View.Left(), View.Bottom() - Width, View.Size.x, Width );
	}

	// つまみの矩形。Content は中身の主軸の大きさ、Viewport は見える範囲の主軸の大きさ
	inline FUIRect MakeThumb( const FUIRect& Track, EUIOrientation Orientation, float32 Content, float32 Viewport, float32 Offset, float32 MaxOffset )
	{
		if ( Track.Size.x <= 0.0f || Track.Size.y <= 0.0f || Content <= 0.0f )
		{
			return FUIRect( Track.Position, FVector2( 0.0f, 0.0f ) );
		}
		const float32 TrackLength = IsVertical( Orientation ) ? Track.Size.y : Track.Size.x;
		const float32 ThumbLength = NMath::Min( TrackLength, NMath::Max( 24.0f, TrackLength * Viewport / Content ) );
		const float32 Travel = TrackLength - ThumbLength;
		const float32 Ratio = MaxOffset > 0.0f ? NMath::Clamp( Offset / MaxOffset, 0.0f, 1.0f ) : 0.0f;
		if ( IsVertical( Orientation ) )
		{
			return FUIRect( Track.Left(), Track.Top() + Travel * Ratio, Track.Size.x, ThumbLength );
		}
		return FUIRect( Track.Left() + Travel * Ratio, Track.Top(), ThumbLength, Track.Size.y );
	}

	// 溝の上を押した。つまみの上なら、つかむ（Drag が有効になる。スクロール量は変わらない）。つまみの外なら、押した側へ 1 ページ送る。
	// 溝の外なら false（何もしない）。true のとき、新しいスクロール量が OutOffset に入る
	inline bool Press( const FUIRect& Track, const FUIRect& Thumb, EUIOrientation Orientation, const FVector2& Position, float32 Offset, float32 Viewport, float32 MaxOffset, FDrag& Drag, float32& OutOffset )
	{
		OutOffset = Offset;
		Drag = FDrag();
		if ( Track.Size.x <= 0.0f || Track.Size.y <= 0.0f || !Track.Contains( Position ) )
		{
			return false;
		}
		const bool bVertical = IsVertical( Orientation );
		const float32 Along = bVertical ? Position.y : Position.x;
		const float32 ThumbStart = bVertical ? Thumb.Top() : Thumb.Left();
		const float32 ThumbLength = bVertical ? Thumb.Size.y : Thumb.Size.x;
		if ( Along >= ThumbStart && Along < ThumbStart + ThumbLength )
		{
			Drag.bActive = true;
			Drag.Grab = Along - ThumbStart;
			return true;
		}
		OutOffset = NMath::Clamp( Along < ThumbStart ? Offset - Viewport : Offset + Viewport, 0.0f, MaxOffset );
		return true;
	}

	// つまみをつかんでいる間、ポインターの位置から、スクロール量を求める
	inline float32 DragOffset( const FUIRect& Track, const FUIRect& Thumb, EUIOrientation Orientation, const FVector2& Position, const FDrag& Drag, float32 MaxOffset )
	{
		const bool bVertical = IsVertical( Orientation );
		const float32 TrackStart = bVertical ? Track.Top() : Track.Left();
		const float32 TrackLength = bVertical ? Track.Size.y : Track.Size.x;
		const float32 ThumbLength = bVertical ? Thumb.Size.y : Thumb.Size.x;
		const float32 Travel = TrackLength - ThumbLength;
		if ( Travel <= 0.0f )
		{
			return 0.0f;
		}
		const float32 Along = bVertical ? Position.y : Position.x;
		return NMath::Clamp( ( Along - Drag.Grab - TrackStart ) / Travel, 0.0f, 1.0f ) * MaxOffset;
	}
}
