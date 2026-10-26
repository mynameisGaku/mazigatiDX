// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_CORE_INTERFACES_RENDER_DRAWMATERIALRIBBON_H
#define LIBRARY_CORE_INTERFACES_RENDER_DRAWMATERIALRIBBON_H

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "DrawCommand.h"
#include "DrawRibbon.h"

#include "Library/Core/Types/Numeric.h"

// 既存の帯の形状に、画像マスク・発光・点ごとの値を加えて記録する材質設定。
struct FDrawMaterialRibbonData
{
	// 既存の帯の画像・点数・反復数・補助点指定。既存Ribbonと同じ意味を持つ。
	FDrawRibbonData Ribbon;

	// 帯全体で共有するマスク画像・UV倍率・境界色・深度の設定。
	struct FMaterialSettings
	{
		// 消失判定に使う画像。-1なら組み込みノイズを使う。
		int32 MaskTexture = -1;
		// 表示画像へ掛けるU/V倍率。有限の正数を指定する。
		float32 UVScale[2] = { 1.0f, 1.0f };
		// マスク画像へ掛けるUV倍率。有限の正数を指定する。
		float32 MaskScale = 1.0f;
		// 消失境界の幅。0なら境界発光を行わない。
		float32 EdgeWidth = 0.08f;
		// 消失境界へ加える発光倍率。
		float32 EdgeEmission = 2.0f;
		// 消失境界へ加える色。下位から赤・緑・青・不透明度の各8bit。
		FDrawRgba EdgeColor = { 255, 128, 32, 255 };
		// 0なら点ごとの消失値と境界発光を無効にする。
		int32 bDissolve = 0;
		// 深度を比較して手前の物体に隠されるか。比較方法はLessEqual。
		int32 DepthTest = 1;
		// 深度を書き込んで後続描画を隠すか。半透明描画では通常0。
		int32 DepthWrite = 0;
	};

	// 形状点と補間する消失・発光・連続UV移動量をまとめた値。
	struct FPoint
	{
		// 中心位置・全幅・累積距離・生のRGBA色。UVRepeatはRibbon設定で掛ける。
		FDrawRibbonData::FPoint Path;
		// この点の消失しきい値。0から1の範囲。
		float32 Dissolve = 0.0f;
		// この点の自己発光倍率。0から32の範囲。
		float32 Emission = 1.0f;
		// 表示画像へ加える未折返しのU/V移動量。点間で補間してから画像参照時に折り返す。
		float32 UVOffset[2] = {};
	};

	// 表示画像と帯の形状設定に続く、材質共有設定。
	FMaterialSettings Material;
};

static_assert( sizeof( FDrawMaterialRibbonData::FMaterialSettings ) == 40 && std::is_trivially_copyable_v<FDrawMaterialRibbonData::FMaterialSettings>, "material ribbon settings must occupy ten words" );
static_assert( sizeof( FDrawMaterialRibbonData::FPoint ) == 40 && std::is_trivially_copyable_v<FDrawMaterialRibbonData::FPoint>, "material ribbon point must occupy ten words" );
static_assert( sizeof( FDrawMaterialRibbonData ) == 56 && sizeof( FDrawMaterialRibbonData ) % 4 == 0 && std::is_trivially_copyable_v<FDrawMaterialRibbonData>, "material ribbon header must occupy fourteen words" );

namespace NDrawMaterialRibbon
{
	// 帯材質の固定設定が記録・実行可能な値かを調べる。
	inline bool IsValidHeader( const FDrawMaterialRibbonData& Data )
	{
		// 帯形状の記録設定。
		const FDrawRibbonData& Ribbon = Data.Ribbon;
		// 既存の材質描画設定。
		const FDrawMaterialRibbonData::FMaterialSettings& Material = Data.Material;
		if ( Ribbon.NumPoints < 2 || Ribbon.NumPoints > FDrawRibbonData::MaxPoints || Ribbon.Texture < 0 || Material.MaskTexture < -1 || !NMath::IsFinite( Ribbon.UVRepeat ) || Ribbon.UVRepeat <= 0.0f || Ribbon.JoinFlags < 0 || ( Ribbon.JoinFlags & ~( FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint ) ) != 0 || Ribbon.NumPoints - static_cast<int32>( ( Ribbon.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) - static_cast<int32>( ( Ribbon.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) < 2 ) return false;
		return NMath::IsFinite( Material.UVScale[0] ) && NMath::IsFinite( Material.UVScale[1] ) && Material.UVScale[0] > 0.0f && Material.UVScale[1] > 0.0f && Material.UVScale[0] <= 128.0f && Material.UVScale[1] <= 128.0f && NMath::IsFinite( Material.MaskScale ) && Material.MaskScale > 0.0f && Material.MaskScale <= 128.0f && NMath::IsFinite( Material.EdgeWidth ) && Material.EdgeWidth >= 0.0f && Material.EdgeWidth <= 1.0f && NMath::IsFinite( Material.EdgeEmission ) && Material.EdgeEmission >= 0.0f && Material.EdgeEmission <= 32.0f && ( Material.bDissolve == 0 || Material.bDissolve == 1 ) && ( Material.DepthTest == 0 || Material.DepthTest == 1 ) && ( Material.DepthWrite == 0 || Material.DepthWrite == 1 );
	}

	// 位置・材質・連続UVを調べ、前点より距離が戻らないことを確認する。
	inline bool IsValidPoint( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint& Point, bool bHasPrevious, float32 PreviousU )
	{
		// この点列が共有する帯設定。
		const FDrawRibbonData& Ribbon = Data.Ribbon;
		// 元画像とmaskの倍率設定。
		const FDrawMaterialRibbonData::FMaterialSettings& Material = Data.Material;
		// 元画像の横座標へ反復倍率を適用した値。
		const float32 BaseU = Point.Path.U * Ribbon.UVRepeat;
		return NMath::IsFinite( Point.Path.Pos[0] ) && NMath::IsFinite( Point.Path.Pos[1] ) && NMath::IsFinite( Point.Path.Pos[2] ) && NMath::IsFinite( Point.Path.Width ) && Point.Path.Width >= 0.0f && NMath::IsFinite( Point.Path.U ) && Point.Path.U >= 0.0f && NMath::IsFinite( BaseU ) && NMath::IsFinite( BaseU * Material.UVScale[0] + Point.UVOffset[0] ) && NMath::IsFinite( BaseU * Material.MaskScale ) && NMath::IsFinite( Point.Dissolve ) && Point.Dissolve >= 0.0f && Point.Dissolve <= 1.0f && NMath::IsFinite( Point.Emission ) && Point.Emission >= 0.0f && Point.Emission <= 32.0f && NMath::IsFinite( Point.UVOffset[0] ) && NMath::IsFinite( Point.UVOffset[1] ) && NMath::Abs( Point.UVOffset[0] ) <= 1.0e6f && NMath::Abs( Point.UVOffset[1] ) <= 1.0e6f && ( !bHasPrevious || Point.Path.U >= PreviousU );
	}

	// 表示区間だけの画像反復数を加算し、8192区間の上限を守る。
	inline bool AddTextureSections( const FDrawMaterialRibbonData& Data, float32 FirstU, float32 LastU, int32& InOutSections )
	{
		// 区間の先頭が属する画像反復番号。
		const float64 FirstRepeat = std::floor( static_cast<float64>( FirstU * Data.Ribbon.UVRepeat ) );
		// 終端までに必要な整数境界区間数。
		const float64 Sections = NMath::Max( 1.0, std::ceil( static_cast<float64>( LastU * Data.Ribbon.UVRepeat ) ) - FirstRepeat );
		if ( !NMath::IsFinite( Sections ) || Sections > static_cast<float64>( FDrawRibbonData::MaxTextureSections - InOutSections ) ) return false;
		InOutSections += static_cast<int32>( Sections );
		return true;
	}

	// 固定設定と点列全体を確認し、記録途中で不正値が残らないことを保証する。
	inline bool IsValid( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points )
	{
		if ( Points == nullptr || !IsValidHeader( Data ) ) return false;
		// 点列全体で使用する画像区間数。
		int32 TextureSections = 0;
		// 固定配列上限内の全経路点を順に検査する。
		for ( int32 Index = 0; Index < Data.Ribbon.NumPoints; ++Index )
		{
			// 直前点と比較する現在点。
			const FDrawMaterialRibbonData::FPoint& Point = Points[Index];
			if ( !IsValidPoint( Data, Point, Index > 0, Index > 0 ? Points[Index - 1].Path.U : 0.0f ) ) return false;
			// 接線用補助点が作る区間を除いた実描画区間。
			const bool bVisibleSegment = Index > 0 && !( Index == 1 && ( Data.Ribbon.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) && !( Index + 1 == Data.Ribbon.NumPoints && ( Data.Ribbon.JoinFlags & FDrawRibbonData::NextPoint ) != 0 );
			if ( bVisibleSegment && !AddTextureSections( Data, Points[Index - 1].Path.U, Point.Path.U, TextureSections ) ) return false;
		}
		return true;
	}
}

#endif
