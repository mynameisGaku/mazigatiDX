// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Common/PlatformerStatus.h"

struct FPlatformer2DState;

//=============================================================================
// CPlatformer2DPowerUp
//   ブロックから出てくる、能力のアイテム。キノコ（右へ歩く）、花（動かない）、スター（跳ねながら進む）。
//   出てくる間（0.5 秒）は、ブロックの中から せり上がって、ふれても取れない。出きったら、タイルの上を動く。
//   主人公がふれたら、FPlatformerStatus::Collect で能力が得られる（得たあとの見た目の変化は、主人公が決める）。
//=============================================================================
class CPlatformer2DPowerUp : public CGameObject
{
public:
	// InBlockTop は、出てくるブロックの上の面の中心（面の上の位置）
	CPlatformer2DPowerUp( const TSharedPtr<FPlatformer2DState>& InState, EPlatformerItem InItem, const FVector2& InBlockTop );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

	FORCEINLINE EPlatformerItem GetItem() const { return Item; }
	FORCEINLINE bool IsEmerging() const { return EmergeTime < EmergeSeconds; }
	FORCEINLINE const FVector2& GetPosition() const { return Position; }

	static constexpr float32 HalfWidth = 14.0f;
	static constexpr float32 Height = 28.0f;

private:
	static constexpr float32 EmergeSeconds = 0.5f;

	TSharedPtr<FPlatformer2DState> State;
	EPlatformerItem Item;
	FVector2 Position;
	FVector2 Velocity;
	float32 BlockTopY;
	float32 EmergeTime = 0.0f;
};
