// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Village/TileMap.h"
#include "Source/Village/VillageState.h"

//=============================================================================
// CPlayer
//   主人公。WASD か矢印キーで歩き、Shift で走る（体力を使う）。地図の通れないタイルには入れない。
//   キーを押すまでは、道すじ（地図の見回りの道）を、ひとりで歩く（見本を、すぐに動かして見せるため）。
//   カメラは、主人公を追う（FVillageState::CameraPosition を決める）。
//   Draw( FDrawContext& CommandList ) は、足元の影と、向きと足踏みのコマを記録するだけ。
//=============================================================================
class CPlayer : public CGameObject
{
public:
	// InSheet は、キャラクターの画像（NVillageArt::MakeCharacter）
	CPlayer( const TSharedPtr<FVillageState>& InState, const TSharedPtr<CTileMap>& InMap, const TSharedPtr<CTexture>& InSheet );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 向き
	enum class EFacing : uint8
	{
		Down,
		Up,
		Right,
		Left,
	};

	// 道すじに沿って歩くときの、進みたい向き（軸に沿った単位ベクトル）。次の点に着いたら、その次の点へ向かう
	FVector2 GetRouteDirection();

	// 足元の矩形が、通れないタイルに触れるか
	bool IsBlockedAt( const FVector2& FootPosition ) const;

	// カメラを、主人公に向けて動かす（Snap が true なら、すぐに合わせる）
	void UpdateCamera( float32 DeltaTime, bool bSnap );

	TSharedPtr<FVillageState> State;
	TSharedPtr<CTileMap> Map;
	TSharedPtr<CTexture> Sheet;

	// 足元の世界の座標
	FVector2 Position;
	// なめらかに動くカメラの位置（State には、整数にそろえた値を渡す）
	FVector2 SmoothCamera;

	EFacing Facing = EFacing::Down;
	float32 AnimationTime = 0.0f;
	bool bMoving = false;
	bool bRunning = false;

	// キーを押すまで、ひとりで歩く
	bool bAutoWalk = true;
	int32 NextRouteIndex = 1;
};
