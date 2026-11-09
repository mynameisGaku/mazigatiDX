// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GeometryVertex.h"
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Render/TextureDrawView.h"

// APIに依存しない頂点色・画像の3D実行先。ゲームスレッド・フレーム内だけ呼ぶ。
class IRenderGeometry3D
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IRenderGeometry3D() = default;
	// 論理画面の幅。
	virtual int32 GetWidth() const = 0;
	// 論理画面の高さ。
	virtual int32 GetHeight() const = 0;
	// 頂点と状態を呼び出し中に複写し、前の命令より後に描く。
	// VerticesはNumVertices個の有限な同次座標。失敗時はfalseで描画を記録しない。
	virtual bool DrawGeometry( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State ) = 0;
	// 同じ頂点に借用画像を貼る。TextureはGPU完了まで所有側が生存させる。
	// 未対応の実行先はfalse。頂点のUVと画像のbindingは呼び出し中に複写する。
	virtual bool DrawTexturedGeometry( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State, const FTextureDrawView& Texture ) { return false; }
	// 既存の描画より後に深度を1へ消す。クリップに関係なく全体を対象にする。
	virtual void ClearDepth() = 0;
};
