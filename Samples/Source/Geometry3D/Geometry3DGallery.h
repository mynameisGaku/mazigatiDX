// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Asset/Gpu/MeshBuffer.h"
#include "Library/Framework/Asset/Gpu/VertexBuffer.h"
#include "Library/Framework/Asset/Gpu/IndexBuffer.h"
#include "Library/Framework/Asset/Texture/DynamicTexture.h"

// APIに依存せず、回転する箱と重なる三角形を描く。
class CGeometry3DGallery : public CGameObject
{
public:
	// 画像と二種類の形状バッファを記録前に作る。
	void OnSpawn() override;
	// 時間を進める。描画の記録中には変えない。
	void Tick( float32 DeltaTime ) override;
	// 更新済みの時計から共通の3D命令だけを記録する。
	void Draw( FDrawContext& Draw ) override;
private:
	TSharedPtr<CDynamicTexture> Texture; // 生成する市松模様。
	TSharedPtr<CMeshBuffer> Mesh; // 編集メッシュと同じ作成窓口。
	TSharedPtr<CVertexBuffer> Vertices; // 汎用の頂点バッファ。
	TSharedPtr<CIndexBuffer> Indices; // 16bitのindexバッファ。
	float32 Time = 0; // 回転角を求める秒数。
};
