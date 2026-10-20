// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_CORE_INTERFACES_RENDER_DRAWMATERIALRIBBON_H
#define LIBRARY_CORE_INTERFACES_RENDER_DRAWMATERIALRIBBON_H

#include <type_traits>

#include "DrawCommand.h"
#include "DrawRibbon.h"

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

#endif
