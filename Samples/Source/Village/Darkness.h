// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/RenderTarget.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Village/VillageState.h"

//=============================================================================
// CDarkness
//   夜の暗さ。画面全体に、暗い青を重ねる。ただし、主人公のランタンと街灯のまわりだけは、明るく残す。
//   やり方（描画先とカスタムブレンド）:
//   1. 「夜の層」（アルファを持つ描画先）を、暗い青で塗る（アルファが夜の濃さ）。その上に、光の画像を、「アルファを減らす」ブレンドで置く
//      （光の強さの分だけ、アルファが減る。光が重なると、さらに減る。光の中心は、ほぼ透明になる）。
//   2. 夜の層を、ふつうの画像として、画面へ重ねる（アルファブレンド）。
//   3. 光の画像を、暖かい色で、加算ブレンドで重ねる。
//   N キーで、昼と夜を切り替える（暗さは、なめらかに変わる）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//   （マスク（FMaskScope）で、画面へ直接描くのは、うまくいかない。DxLib は、アルファを持つ描画先へ、マスクつきで描くと、色を置き換えてしまうため。
//   マスクで描く先は、アルファのない描画先にする。サンプル Filters を参照）
//=============================================================================
class CDarkness : public CGameObject
{
public:
	// InLayer は、画面と同じ大きさ（1280 x 720）の、アルファを持つ描画先（夜の層）。InGlow は、光の画像（NVillageArt::MakeGlow）
	CDarkness( const TSharedPtr<FVillageState>& InState, const TArray<FVector2>& InLampPositions, const TSharedPtr<CRenderTarget>& InLayer, const TSharedPtr<CTexture>& InGlow );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 光 1 つ（画面の座標と、光の半径）
	struct FLight
	{
		FVector2 Center;
		float32 Radius = 0.0f;
	};

	// 画面に見えている光を集める
	void CollectLights( TArray<FLight>& OutLights ) const;

	TSharedPtr<FVillageState> State;
	TArray<FVector2> LampPositions;
	TSharedPtr<CRenderTarget> Layer;
	TSharedPtr<CTexture> Glow;
};
