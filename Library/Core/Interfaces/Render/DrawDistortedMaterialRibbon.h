// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_CORE_INTERFACES_RENDER_DRAWDISTORTEDMATERIALRIBBON_H
#define LIBRARY_CORE_INTERFACES_RENDER_DRAWDISTORTEDMATERIALRIBBON_H

#include <cmath>

#include "DrawMaterialRibbon.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/TypeTraits.h"

// 既存材質帯へ、共通ノイズ画像と点ごとの変位量を加えて記録する値。
struct FDrawDistortedMaterialRibbonData
{
	// 既存帯の形・表示画像・マスク・境界発光を保つ固定設定。
	FDrawMaterialRibbonData Material;
	// 表示画像のUVだけを曲げるノイズ画像。
	int32 DistortionTexture = -1;
	// ノイズ画像へ掛けるU/V倍率。
	float32 UVScale[2] = { 1.0f, 1.0f };

	// 形・既存材質点に、補間可能なノイズ設定をまとめた点。
	struct FPoint
	{
		// 帯の位置・幅・累積距離・RGBA・消失・発光・既存UV移動。
		FDrawMaterialRibbonData::FPoint Material;
		// この点でノイズから読むずれ量。0から1の範囲。
		float32 Strength = 0.0f;
		// ノイズ画像へ加える未折返しのU/V移動量。
		float32 UVOffset[2] = {};
	};
};

namespace NDrawDistortedMaterialRibbon
{
	// Dataの材質・画像・倍率を検査し、いずれかが無効ならfalseを返す。
	inline bool IsValidHeader( const FDrawDistortedMaterialRibbonData& Data )
	{
		return NDrawMaterialRibbon::IsValidHeader( Data.Material ) && Data.DistortionTexture >= 0 && NMath::IsFinite( Data.UVScale[0] ) && NMath::IsFinite( Data.UVScale[1] ) && Data.UVScale[0] > 0.0f && Data.UVScale[1] > 0.0f && Data.UVScale[0] <= 128.0f && Data.UVScale[1] <= 128.0f;
	}

	// 固定設定と点列が描画可能か調べ、無効値ならfalseを返す。
	// Data: 共有設定と必要な点数。
	// Points: Ribbon.NumPoints個以上の有効な型付き配列。nullまたは無効値ならfalse、byte payloadの完全長は呼出側で先に確認する。
	inline bool IsValid( const FDrawDistortedMaterialRibbonData& Data, const FDrawDistortedMaterialRibbonData::FPoint* Points )
	{
		if ( Points == nullptr || !IsValidHeader( Data ) ) return false;
		// 共有される帯形状設定。
		const FDrawRibbonData& Ribbon = Data.Material.Ribbon;
		// 接線だけに使う補助区間を除いた画像反復数。
		int32 TextureSections = 0;
		// 全pointをコピー前提で順に調べる添字。
		for ( int32 Index = 0; Index < Ribbon.NumPoints; ++Index )
		{
			// 既存材質値とノイズ変位値を持つ現在点。
			const FDrawDistortedMaterialRibbonData::FPoint& Point = Points[Index];
			if ( !NDrawMaterialRibbon::IsValidPoint( Data.Material, Point.Material, Index > 0, Index > 0 ? Points[Index - 1].Material.Path.U : 0.0f ) || !NMath::IsFinite( Point.Strength ) || Point.Strength < 0.0f || Point.Strength > 1.0f || !NMath::IsFinite( Point.UVOffset[0] ) || !NMath::IsFinite( Point.UVOffset[1] ) || NMath::Abs( Point.UVOffset[0] ) > 1.0e6f || NMath::Abs( Point.UVOffset[1] ) > 1.0e6f ) return false;
			// 連続経路距離へ画像反復を掛けた横座標。
			const float32 BaseU = Point.Material.Path.U * Ribbon.UVRepeat;
			if ( !NMath::IsFinite( BaseU * Data.UVScale[0] + Point.UVOffset[0] ) ) return false;
			// 接線用補助点が作る区間を除いた実描画区間。
			const bool bVisibleSegment = Index > 0 && !( Index == 1 && ( Ribbon.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) && !( Index + 1 == Ribbon.NumPoints && ( Ribbon.JoinFlags & FDrawRibbonData::NextPoint ) != 0 );
			if ( bVisibleSegment && !NDrawMaterialRibbon::AddTextureSections( Data.Material, Points[Index - 1].Material.Path.U, Point.Material.Path.U, TextureSections ) ) return false;
		}
		return true;
	}
}

static_assert( sizeof( FDrawDistortedMaterialRibbonData::FPoint ) == 52 && bIsTriviallyCopyable<FDrawDistortedMaterialRibbonData::FPoint>, "distorted material ribbon point must occupy thirteen words" );
static_assert( sizeof( FDrawDistortedMaterialRibbonData ) == 68 && sizeof( FDrawDistortedMaterialRibbonData ) % 4 == 0 && bIsTriviallyCopyable<FDrawDistortedMaterialRibbonData>, "distorted material ribbon header must occupy seventeen words" );

#endif
