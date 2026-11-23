// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GeometryBufferView.h"

// 登録済みの形状を命令実行側へ貸す。GPUや登録番号の所有方法を公開しない。
class IGeometryBufferSource
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IGeometryBufferSource() = default;
	// 無効な番号ならfalseで出力を保つ。借用中に登録・更新・解放を呼ばない。
	virtual bool GetVertexBufferView( int32 Handle, FGeometryVertexBufferView& Out ) = 0;
	// 無効な番号ならfalseで出力を保つ。借用中に登録・更新・解放を呼ばない。
	virtual bool GetIndexBufferView( int32 Handle, FGeometryIndexBufferView& Out ) = 0;
	// 頂点と添字の組の不変な鍵と更新版を返す。未対応はfalse。借用中の変更は禁止。
	virtual bool GetMeshCacheIdentity( int32 /*Vertices*/, int32 /*Indices*/, uint64& /*Key*/, uint64& /*Version*/ ) { return false; }
	// GetMeshCacheIdentity と同じ。ただし、同じ版を何度も描いたあとだけ true を返す（描くたびに中身を書き換えるデータは、いつまでも false）。
	// GPU へ共有のメッシュを置く費用は、同じ中身を何度も描くときだけ、見合う。描くたびに呼ぶ。未対応はfalse。
	virtual bool GetStableMeshCacheIdentity( int32 /*Vertices*/, int32 /*Indices*/, uint64& /*Key*/, uint64& /*Version*/ ) { return false; }
};
