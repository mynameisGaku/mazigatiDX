// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/StaticModelDrawView.h"

class IModelBackend;

// 描画はモデルの読み込み・複製・所有を知らず、準備済みの静的形状だけを借りる。
class IStaticModelSource
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IStaticModelSource() = default;
	// 無効なモデルはfalseで出力を保つ。借用は登録・更新・解放より前に消費する。
	virtual bool GetStaticModelDrawView( int32 Handle, FStaticModelDrawView& Out ) = 0;
	// 描く命令が、モデルの状態を設定する窓口（DxLib の MV1SetMatrix / MV1SetAttachAnimTime / MV1SetAttachAnimBlendRate / MV1SetAttachAnimBlendRateToFrame に当たる）。
	// 骨組みの無い実装・モデル・付けていない番号は false（描くのは、そのまま続ける）。時間は 30 = 1 秒の単位
	// 骨の一時的な上書き（ModelFramePose3D）に使う、同じモデルの登録を持つ窓口（Get / Set / ClearModelFrameLocalMatrixOverride）。無ければ nullptr
	virtual IModelBackend* GetStaticModelBackend() { return nullptr; }
	virtual bool SetStaticModelPlacement( int32 /*Handle*/, const float32 ( & /*Matrix*/ )[12] ) { return false; }
	virtual bool SetStaticModelAnimTime( int32 /*Handle*/, int32 /*AttachIndex*/, float32 /*Time*/ ) { return false; }
	virtual bool SetStaticModelAnimRate( int32 /*Handle*/, int32 /*AttachIndex*/, float32 /*Rate*/ ) { return false; }
	virtual bool SetStaticModelAnimFrameRate( int32 /*Handle*/, int32 /*AttachIndex*/, int32 /*Frame*/, float32 /*Rate*/, bool /*bIncludeChildren*/ ) { return false; }
};
