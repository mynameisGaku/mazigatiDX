// SPDX-License-Identifier: MIT
#pragma once

#include "Source/AI/AIActor.h"

class CBehaviorTreeComponent;

//=============================================================================
// CAIGuard
//   巡回して、主人公を見つけたら追いかけ、近づいたら殴る見張り。判断は、ビヘイビアツリー（Samples/Assets/AI/Guard.gxbt）が決める。
//   このクラスがすることは、目標（主人公）をブラックボードへ書くことと、やられたら bHurt を立てることだけ。
//   見つける・追う・殴る・怯む、の優先順位はデータ（木）にあり、コードを書き換えずに調整できる。
//=============================================================================
class CAIGuard : public CAIActor
{
public:
	// TreePath: 使うビヘイビアツリー。同じクラスのまま、ツリー（JSON）を変えるだけで、見張り・偵察兵・村人のような別の敵・NPC になる（コードは書かない）
	CAIGuard( const FVector2& InPosition, const FString& InTreePath = "AI/Guard.gxbt", const FString& InName = "Guard", const FColor32& InColor = FColor32( 235, 120, 90 ) );

	void OnSpawn() override;

	// 追いかける相手（主人公）を、ブラックボードの Target に書く
	void SetTarget( uint64 TargetObjectId );

	CBehaviorTreeComponent* GetBrain() const { return Brain; }

protected:
	void OnDamaged() override;

private:
	FString TreePath;
	CBehaviorTreeComponent* Brain = nullptr;
};
