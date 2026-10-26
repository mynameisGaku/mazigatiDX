// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_CORE_INTERFACES_RENDER_DRAW_RIBBON_H
#define FRAMEWORK_CORE_INTERFACES_RENDER_DRAW_RIBBON_H


#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Types/TypeTraits.h"

// カメラへ面を向ける帯の記録。直後にNumPoints個のFPointが続く。
struct FDrawRibbonData
{
	// 1命令で記録できる点の上限。添字と命令長が安全な範囲に収まる。
	static constexpr int32 MaxPoints = 1024;
	// 画像を反復するために分割できる区間の上限。展開後も16bitの頂点番号に収まる。
	static constexpr int32 MaxTextureSections = 8192;
	// 前の一点を接線だけに使い、最初の区間を描かない指定。
	static constexpr int32 PreviousPoint = 1;
	// 後ろの一点を接線だけに使い、最後の区間を描かない指定。
	static constexpr int32 NextPoint = 2;

	// 経路上の位置・全幅・累積距離・色。位置と幅はワールド座標で指定する。
	struct FPoint
	{
		// 帯の中心になるワールド位置。
		float32 Pos[3] = {};
		// この点での帯の全幅。0は先端を細くする指定。
		float32 Width = 0.0f;
		// 経路の先頭からの累積距離。UVRepeatを掛けて画像の横座標にする。
		float32 U = 0.0f;
		// 下位から赤・緑・青・不透明度の各8bit。0xFFFFFFFFは不透明な白。
		uint32 Color = 0xFFFFFFFFu;
	};

	// 画像のハンドル。-1なら画像を使わず、点の色だけで描く。
	int32 Texture = -1;
	// 直後に続く経路の点数。2以上MaxPoints以下。
	int32 NumPoints = 0;
	// 経路1単位あたりの画像の繰り返し数。有限の正の値。
	float32 UVRepeat = 1.0f;
	// 端の補助点を接線計算だけへ使う指定。0..3以外は無効。
	int32 JoinFlags = 0;
};

static_assert( sizeof( FDrawRibbonData ) == 16 && bIsTriviallyCopyable<FDrawRibbonData>, "ribbon header must occupy four words" );
static_assert( sizeof( FDrawRibbonData::FPoint ) == 24 && bIsTriviallyCopyable<FDrawRibbonData::FPoint>, "ribbon point must occupy six words" );

#endif
