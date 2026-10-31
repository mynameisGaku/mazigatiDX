// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Source/Shapes3D/Shapes3DDemoState.h"

class CAssetManager;

//=============================================================================
// NShapes3DScenery
//   サンプル Shapes3D の舞台: 夜の広場。床（市松模様の四角形）の上に、3D 図形の追加で足したものを並べる。
//     円錐（Cone3D）           松の木
//     点（Pixel3D）             星空
//     頂点の並び（Primitive3D）  蛍（点）・軸（線）・らせん（折れ線）・波（帯）・傘（扇）・宝石（添字つきの三角形）・床（帯）
//     カメラへ向ける画像        草（BillboardPivot3D。基準点が足元）・旗（BillboardQuad3D）・回る星（Sprite3D）・脈打つ心臓（SpriteStretch3D）・揺れるランタン（SpritePivot3D）
//     頂点バッファ              森（木 1 本の形を、一度だけ作って、位置と向きと大きさを変えて、何度も描く）・池の水面（頂点を毎フレーム書き換える格子。インデックスバッファは変わらない）
//   画像を作る関数と、舞台を描く（記録する）関数、名札の置き場所を作る関数。
//=============================================================================
namespace NShapes3DScenery
{
	// 名札 1 つ（世界の位置と、文字）
	struct FStageLabel
	{
		FString Text;
		FVector3 Position;
	};

	// 舞台で使う画像を作って、State に入れる。作れなければ false
	bool MakeTextures( CAssetManager& Assets, FShapes3DDemoState& State );

	// 頂点バッファ・インデックスバッファ（森の木の形と、池の水面）を作って、State に入れる。作れなければ false。メインスレッドで呼ぶ
	bool MakeBuffers( FShapes3DDemoState& State );

	// 池の水面の頂点バッファを、時刻 Time の波で書き換える。メインスレッドで呼ぶ（毎フレーム）
	void UpdateWater( FShapes3DDemoState& State );

	// 名札を作る（動くものの名札は、いまの位置）
	TArray<FStageLabel> MakeLabels( const FShapes3DDemoState& State );

	// 舞台を、State の設定で、記録する（カメラは、呼ぶ側が、先に PushCamera しておく）
	void RecordStage( FDrawContext& CommandList, const FShapes3DDemoState& State );
}
