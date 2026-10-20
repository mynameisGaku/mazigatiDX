// SPDX-License-Identifier: NOASSERTION
#ifndef SOURCE_SCENE_WORLDEFFECTMESHREALSELFTEST_H
#define SOURCE_SCENE_WORLDEFFECTMESHREALSELFTEST_H

#include "WorldSelfTestSupport.h"

// 実際のDxLib材質メッシュ描画をpixel readbackで確認する。
namespace NWorldTest
{
	void RunEffectMaterialMeshRealTests( NAssetTest::FChecker& C, IDrawExecutor& Executor );
}

#endif
