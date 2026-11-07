// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"

//=============================================================================
// CWorldSampleActor
//   World サンプルの、コードで作った 3D のオブジェクト（コイン・結晶・主人公）の基底。
//   3D の絵は、カメラとライトを決めたあとでしか記録できないので、Draw ではなく Record3D に描く。
//   CWorldSampleView が、カメラとライトを決めてから、すべてのアクターの Record3D を呼ぶ。
//=============================================================================
class CWorldSampleActor : public CGameObject
{
public:
	virtual void Record3D( FDrawContext& CommandList ) {}
};
