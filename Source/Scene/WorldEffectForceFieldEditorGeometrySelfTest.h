// SPDX-License-Identifier: NOASSERTION
#pragma once

namespace NAssetTest
{
	struct FChecker;
}

class CAssetManager;
class IDrawExecutor;

namespace NWorldTest
{
	// 力場focusとguideを独立した既知値・投影計算で確かめる。
	void RunEffectForceFieldEditorGeometryTests( NAssetTest::FChecker& C );
	// 実プリセットのcopy・編集・SaveAs・読込・配置・通常再生を確かめる。
	void RunEffectForceFieldEditorPresetRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor );
}
