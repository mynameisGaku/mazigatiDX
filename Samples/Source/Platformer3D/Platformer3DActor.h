// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FPlatformer3DLevel;

//=============================================================================
// CPlatformer3DActor
//   3D の面の中にいるもの（主人公・敵・アイテム・火の玉）の基底。
//   3D の絵は、カメラとライトを決めた「あと」で記録しなければならないので、アクターは Draw では描かず、
//   Record3D に描く。CPlatformer3DView が、カメラとライトを決めてから、すべてのアクターの Record3D を呼ぶ。
//=============================================================================
class CPlatformer3DActor : public CGameObject
{
public:
	// 3D の図形を記録する（カメラとライトは、呼ぶ側が決めてある）
	virtual void Record3D( FDrawContext& CommandList ) = 0;

protected:
	// 足元の影（地面に落ちる、平たい丸）。高さの見当が付くように、足元の真下の足場の上に描く
	static void RecordBlobShadow( FDrawContext& CommandList, const FPlatformer3DLevel& Level, const FVector3& Feet, float32 Radius );
};
