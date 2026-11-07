// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/Common/PlatformerStatus.h"
#include "Source/Platformer3D/Platformer3DActor.h"

struct FPlatformer3DState;

//=============================================================================
// CPlatformer3DPowerUp
//   ブロックから出てくる、能力のアイテム。キノコ（前へ歩く）、花（動かない。回る）、スター（跳ねながら進む）。
//   出てくる間（0.5 秒）は、ブロックの中から せり上がって、ふれても取れない。出きったら、足場の上を動く。
//   主人公がふれたら、FPlatformerStatus::Collect で能力が得られる。2D 版（CPlatformer2DPowerUp）と同じ役割。
//=============================================================================
class CPlatformer3DPowerUp : public CPlatformer3DActor
{
public:
	// InBlockTop は、出てくるブロックの上の面の中心
	CPlatformer3DPowerUp( const TSharedPtr<FPlatformer3DState>& InState, EPlatformerItem InItem, const FVector3& InBlockTop );

	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

	FORCEINLINE EPlatformerItem GetItem() const { return Item; }
	FORCEINLINE bool IsEmerging() const { return EmergeTime < EmergeSeconds; }
	FORCEINLINE const FVector3& GetPosition() const { return Position; }

	static constexpr float32 HalfWidth = 0.4f;
	static constexpr float32 Height = 0.8f;

private:
	static constexpr float32 EmergeSeconds = 0.5f;

	TSharedPtr<FPlatformer3DState> State;
	EPlatformerItem Item;
	FVector3 Position;
	FVector3 Velocity;
	float32 BlockTopY;
	float32 EmergeTime = 0.0f;
};
