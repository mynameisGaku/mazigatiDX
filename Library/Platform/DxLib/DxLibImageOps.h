// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"

//=============================================================================
// NDxLibImageOps
//   画像の処理（フィルター・混ぜる）の命令を、DxLib の GraphFilter / GraphBlend へ流す。CDxLibDrawExecutor が使う。
//   メインスレッドだけから呼ぶ。DxLib の描画先・描画の状態（クリップ・ブレンドなど）は、読みも変えもしない
//   （GraphFilter 系は、それらに影響されない。転送先のブレンドの設定 SetGraphFilterBltBlendMode だけは、呼ぶたびに決めて、終わりに元（ブレンドなし）へ戻す）。
//=============================================================================
namespace NDxLibImageOps
{
	// FDrawImageFilterData の通りに、画像へフィルターを掛ける（その場なら GraphFilter、矩形なしなら GraphFilterBlt、矩形ありなら GraphFilterRectBlt）。
	// DxLib が受け付けたら true。画像のハンドルが無効・種類が範囲の外・DxLib が拒んだときは false（何もしない）
	bool ApplyFilter( const FDrawImageFilterData& Data );

	// FDrawImageBlendData の通りに、2 枚の画像を混ぜる（Dest が Base と同じなら GraphBlend、違えば GraphBlendBlt）。DxLib が受け付けたら true
	bool BlendImages( const FDrawImageBlendData& Data );

	// DxLib の種類（DX_GRAPH_FILTER_*）への変換。種類が範囲の外、または、乗算済みアルファ版が無い種類を bPremultiplied で呼んだときは、乗算されていない版。範囲の外は -1
	int ToDxFilterType( int32 Type, bool bPremultiplied );

	// DxLib の種類（DX_GRAPH_BLEND_*）への変換。範囲の外は -1
	int ToDxBlendType( int32 Type, bool bPremultiplied );

	// DxLib のマスクの成分（DX_MASKGRAPH_CH_*）への変換。範囲の外は、アルファ
	int ToDxMaskChannel( int32 Channel );
}
