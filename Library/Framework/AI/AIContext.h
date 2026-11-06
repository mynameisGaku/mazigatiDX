// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

class CBlackboard;
class CGameObject;
class CScene;
class IAIAnimationControl;

//=============================================================================
// FAIContext
//   AI の部品（State の処理・Condition・Task など）に渡す文脈。毎フレーム、コンポーネントが作って渡す。
//   部品は、ここから持ち主・シーン・ブラックボード・アニメーションの窓口を使う（ゲームの型には直接依存しない）。
//
//   - Owner / Scene は、AI のコンポーネントがシーンに入っていないとき nullptr になりうる
//   - Animation は、ゲームが SetAnimationControl で渡していなければ nullptr
//   - Time は、AI のコンポーネントが動き始めてからの秒数（ポーズ中は進まない）
//=============================================================================
struct FAIContext
{
	CGameObject* Owner = nullptr;
	CScene* Scene = nullptr;
	CBlackboard* Blackboard = nullptr;
	IAIAnimationControl* Animation = nullptr;
	float32 DeltaTime = 0.0f;
	float64 Time = 0.0;
};
