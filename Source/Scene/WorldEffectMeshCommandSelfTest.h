// SPDX-License-Identifier: NOASSERTION
#ifndef SOURCE_SCENE_WORLDEFFECTMESHCOMMANDSELFTEST_H
#define SOURCE_SCENE_WORLDEFFECTMESHCOMMANDSELFTEST_H

namespace NAssetTest { struct FChecker; }

namespace NWorldTest
{
	// 材質メッシュ命令の検証、所有コピー、上限と旧命令のbyte保持を確認する。
	void RunEffectMeshCommandTests( NAssetTest::FChecker& C );
}

#endif
