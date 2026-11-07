// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FPlatformer3DState;

//=============================================================================
// CPlatformer3DView
//   3D の面を描く。空 → カメラ → ライト（太陽 + やわらかい光）→ 箱（地面・足場・土管・ブロック）・コイン・ゴール →
//   すべてのアクター（CPlatformer3DActor::Record3D）の順に記録する。アクターの絵は、カメラとライトが決まったあとでないと
//   正しく記録されないので、この 1 つのオブジェクトが、順番を受け持つ。いちばん奥に描く。
//=============================================================================
class CPlatformer3DView : public CGameObject
{
public:
	explicit CPlatformer3DView( const TSharedPtr<FPlatformer3DState>& InState );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	void DrawSky( FDrawContext& CommandList ) const;
	void RecordBlocks( FDrawContext& CommandList ) const;
	void RecordCoins( FDrawContext& CommandList ) const;
	void RecordGoal( FDrawContext& CommandList ) const;

	TSharedPtr<FPlatformer3DState> State;
};
