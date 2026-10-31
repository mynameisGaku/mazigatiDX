// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Render/Lighting.h"
#include "Library/Framework/Render/ShadowMapView.h"
#include "Source/Shadows/ShadowsDemoState.h"

class CAssetManager;

//=============================================================================
// NShadowsScenery
//   サンプル Shadows の舞台: 昼の中庭。芝生の上に、家・木・木箱・柵・歩き回る旅人を置き、動く太陽（方向ライト）の影を落とす。
//     家・木・木箱・旅人  不透明な図形が、そのまま影を落とす（BeginShadowMap〜EndShadowMap の間に、同じ図形を描く）
//     柵                  すき間のある画像（アルファ 0 の画素）。影にも、すき間ができる
//     空の明かり・補助光・ランタン  影を受けないライト（FLight::ShadowMapSlots = 0）。太陽の影の中も照らす（影が真っ黒にならない）
//   画像を作る関数と、シャドウマップを作る関数、ライトと影の絵の決め方、舞台を描く（記録する）関数、名札の置き場所を作る関数。
//=============================================================================
namespace NShadowsScenery
{
	// 名札 1 つ（世界の位置と、文字）
	struct FStageLabel
	{
		FString Text;
		FVector3 Position;
	};

	// 舞台で使う画像を作って、State に入れる。作れなければ false
	bool MakeTextures( CAssetManager& Assets, FShadowsDemoState& State );

	// State のシャドウマップを、State.SizeLevel の大きさにする（もう同じ大きさなら、何もしない）。作れなければ false。メインスレッドで呼ぶ
	bool UpdateShadowMap( FShadowsDemoState& State );

	// 太陽の光が進む向き（長さ 1。下向き）
	FVector3 GetSunDirection( const FShadowsDemoState& State );

	// 太陽（暖かい白の方向ライト）。影を受ける
	FLight MakeSun( const FShadowsDemoState& State );

	// 空の明かり（真上からの、青みがかった弱い方向ライト）。影を受けない
	FLight MakeSkyLight();

	// ランタン（橙色の点光源）。影を受けない
	FLight MakeLantern();

	// 影の絵の決め方: 太陽の向き・State の範囲の段階・補正の段階
	FShadowMapView MakeShadowView( const FShadowsDemoState& State );

	// 名札を作る（動くものの名札は、いまの位置）
	TArray<FStageLabel> MakeLabels( const FShadowsDemoState& State );

	// 舞台を、State の設定で、記録する（カメラは、呼ぶ側が、先に PushCamera しておく）。影の絵を描き、ライトを足し、影を受けるものを描く
	void RecordStage( FDrawContext& CommandList, const FShadowsDemoState& State );
}
