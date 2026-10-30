// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/RenderTarget.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Camera/CameraDemoState.h"

//=============================================================================
// CTopView
//   右下の、上から見る図。もう 1 つのカメラ（真上から見下ろす正射影）で、同じ景色を、別の描画先（深度つき）に描いて、ふつうの画像として貼る。
//   メインのカメラが映している地面の範囲（画面の四隅から出した光線 FCamera::ScreenToRay が、地面にあたる点を結んだ線）と、
//   物が視錐台に入っているか（緑）出ているか（赤）を重ねる。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CTopView : public CGameObject
{
public:
	// InTarget は、TopViewSize 四方の、深度（Z バッファ）つきの描画先
	CTopView( const TSharedPtr<FCameraDemoState>& InState, const TSharedPtr<CRenderTarget>& InTarget );

	void Draw( FDrawContext& CommandList ) override;

private:
	// メインのカメラが映している地面の範囲を、線で記録する（上から見る図のカメラで）
	void RecordFootprint( FDrawContext& CommandList ) const;

	TSharedPtr<FCameraDemoState> State;
	TSharedPtr<CRenderTarget> Target;
};
