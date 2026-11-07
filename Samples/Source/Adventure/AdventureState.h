// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/World/Runtime/WorldSpawner.h"
#include "Library/Math/Math.h"

class CSound;

// 通れない四角形（地面の上から見た。壁・柱・門）。置き物が消えたら（門が開いたら）、通れるようになる
struct FAdventureSolid
{
	TWeakPtr<CGameObject> Object;
	float32 MinX = 0.0f;
	float32 MaxX = 0.0f;
	float32 MinZ = 0.0f;
	float32 MaxZ = 0.0f;
};

//=============================================================================
// FAdventureState
//   Adventure のオブジェクトが分け合う状態。置いたワールド、通れない四角形、カメラ、主人公の位置と進み具合（体力・回復薬・鍵・
//   開けた宝箱・倒した敵・開けた門 = セーブされるもの）、メッセージ、エフェクトと効果音。シーンが作って、各オブジェクトに渡す。
//=============================================================================
struct FAdventureState
{
	static constexpr int32 ScreenWidth = 1280;
	static constexpr int32 ScreenHeight = 720;
	static constexpr int32 MaxHp = 100;
	static constexpr int32 MaxPotions = 9;

	FWorldInstance Stage;
	TArray<FAdventureSolid> Solids;
	FCamera Camera;
	float32 CameraYaw = 0.0f;
	float32 Time = 0.0f;

	// 主人公
	TWeakPtr<CGameObject> Player;
	FVector3 PlayerPosition;
	bool bPlayerRolling = false;
	bool bPlayerDead = false;

	// 出口の位置（ワールドの "Exit" の置き物）
	FVector3 ExitPosition;

	// 進み具合（セーブされる）
	int32 Hp = MaxHp;
	int32 Potions = 1;
	int32 Keys = 0;
	int32 Gems = 0;
	bool bGateOpen = false;
	TArray<int32> OpenedChests;
	TArray<int32> DefeatedEnemies;
	TArray<int32> PickedUp;

	bool bWon = false;

	// 近くの、調べられるものの案内（主人公が、毎フレーム決める。空なら、無い）
	FString Prompt;

	// 焚き火で休んだ（ヒール + セーブをシーンに頼む）
	bool bSaveRequested = false;

	TArray<FString> Messages;
	TArray<TSharedPtr<IEffectPlayback>> Effects;

	// 効果音（計算で作った短い音。シーンが作る）
	TSharedPtr<CSound> HitSound;
	TSharedPtr<CSound> HurtSound;
	TSharedPtr<CSound> PickupSound;
	TSharedPtr<CSound> OpenSound;

	void Say( const FString& Text )
	{
		Messages.Add( Text );
		while ( Messages.Num() > 4 )
		{
			Messages.RemoveAt( 0 );
		}
	}

	static bool Contains( const TArray<int32>& Ids, int32 Id )
	{
		for ( const int32 Item : Ids )
		{
			if ( Item == Id )
			{
				return true;
			}
		}
		return false;
	}

	// その場に、エフェクト（Effects/*.gxeffect）を出す。使い切りの出し方（終わったら、自動で消える）
	void PlayEffect( const char* Path, const FVector3& Position, float32 Scale );

	// 効果音を鳴らす（音が作れていなければ、何もしない）
	void PlaySound( const TSharedPtr<CSound>& Sound, float32 Pitch, float32 Volume ) const;

	FCameraViewport GetViewport() const { return FCameraViewport { ScreenWidth, ScreenHeight }; }
};
