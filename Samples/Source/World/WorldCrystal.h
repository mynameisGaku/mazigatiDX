// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"
#include "Source/World/WorldSampleActor.h"

//=============================================================================
// CWorldCrystal
//   タグ "Crystal" の置き物に結び付けるコード。見た目（メッシュ）と位置は、ワールドのファイルにある。
//   コードは、プロパティ "Speed"（度 / 秒）を読んで、置き物（親）を回すだけ。
//   「見た目はデータ、動きはコード」の見本。
//=============================================================================
class CWorldCrystal : public CWorldSampleActor
{
public:
	explicit CWorldCrystal( float32 InDegreesPerSecond );

	void Tick( float32 DeltaTime ) override;

private:
	float32 DegreesPerSecond;
	float32 Angle = 0.0f;

	// 置き物の、もとの向き（最初の Tick に読む）
	FQuaternion BaseRotation = FQuaternion::Identity;
	bool bHasBase = false;
};
