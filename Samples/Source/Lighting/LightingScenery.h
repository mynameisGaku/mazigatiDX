// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Render/Lighting.h"
#include "Source/Lighting/LightingDemoState.h"

class CAssetManager;

//=============================================================================
// NLightingScenery
//   サンプル Lighting の舞台: 夜の中庭。床（市松模様）、5 つの球（材質の違い）、壁・柱・箱（影のできかた）を、
//   月明かり（方向ライト）、床の上を回る電球（点光源）、床を舐めるスポットライト、周りの街灯（点光源）で照らす。
//   画像を作る関数と、ライトを作る関数、舞台を描く（記録する）関数、名札の置き場所を作る関数。
//=============================================================================
namespace NLightingScenery
{
	// 名札 1 つ（世界の位置と、文字）
	struct FStageLabel
	{
		FString Text;
		FVector3 Position;
	};

	// 舞台で使う画像を作って、State に入れる。作れなければ false
	bool MakeTextures( CAssetManager& Assets, FLightingDemoState& State );

	// 月明かり（青白い方向ライト）
	FLight MakeMoon();

	// 床の上を回る電球（橙色の点光源）。時刻 Time での位置
	FLight MakeLamp( float32 Time );

	// 床を舐めるスポットライト（白い）。時刻 Time での向き
	FLight MakeSpotlight( float32 Time );

	// 周りの街灯 Index 番目（色違いの点光源）
	FLight MakeStreetLamp( int32 Index );

	// 足したいライトの数と、実際に効くライトの数（DxLib の仕様で、同時に 6 つまで）
	int32 CountRequestedLights( const FLightingDemoState& State );
	int32 CountActiveLights( const FLightingDemoState& State );

	// 名札を作る（動くライトの名札は、いまの位置）
	TArray<FStageLabel> MakeLabels( const FLightingDemoState& State );

	// 舞台を、State のライティングで、記録する（カメラは、呼ぶ側が、先に PushCamera しておく）
	void RecordStage( FDrawContext& CommandList, const FLightingDemoState& State );
}
