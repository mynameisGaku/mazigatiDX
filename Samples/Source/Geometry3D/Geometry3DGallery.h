// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Object/GameObject.h"

// APIに依存せず、回転する箱と重なる三角形を描く。
class CGeometry3DGallery : public CGameObject
{
public:
	// 時間を進める。描画の記録中には変えない。
	void Tick( float32 DeltaTime ) override;
	// 更新済みの時計から共通の3D命令だけを記録する。
	void Draw( FDrawContext& Draw ) override;
private:
	float32 Time = 0; // 回転角を求める秒数。
};
