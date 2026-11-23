// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Render/StaticModelDrawView.h"

class IModelBackend;
struct FModelMaterialGeometry;

// 描画はモデルの読み込み・複製・所有を知らず、準備済みの静的形状だけを借りる。
class IStaticModelSource
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IStaticModelSource() = default;
	// 無効なモデルはfalseで出力を保つ。VertexOwner が空なら登録・更新・解放前に消費する。
	// 所有付きでも画像の解放先のバックエンドより先にビューを破棄する。
	virtual bool GetStaticModelDrawView( int32 Handle, FStaticModelDrawView& Out ) = 0;
	// GPU スキニングのビュー（FStaticModelDrawView の SkinVertices・BoneMatrices を持つ。Vertices は骨の変形の前）。骨の動きでは頂点の版が変わらないので、GPU が共有メッシュを置いたまま使い回せる。
	// GPU でスキニングできない個体・実装（骨組みの無い個体・影響が 4 つを超える頂点・骨の行列が多すぎる・材質の色を上書きした個体）は false で、出力を保つ（呼び出し側は、GetStaticModelDrawView で、変形済みの頂点を借りる）。
	virtual bool GetStaticModelSkinnedDrawView( int32 /*Handle*/, FStaticModelDrawView& /*Out*/ ) { return false; }
	// 描く命令が、モデルの状態を設定する窓口（DxLib の MV1SetMatrix / MV1SetAttachAnimTime / MV1SetAttachAnimBlendRate / MV1SetAttachAnimBlendRateToFrame に当たる）。
	// 骨組みの無い実装・モデル・付けていない番号は false（描くのは、そのまま続ける）。時間は 30 = 1 秒の単位
	// 骨の一時的な上書き（ModelFramePose3D）に使う、同じモデルの登録を持つ窓口（Get / Set / ClearModelFrameLocalMatrixOverride）。無ければ nullptr
	virtual IModelBackend* GetStaticModelBackend() { return nullptr; }
	// モデル材質効果（ModelMaterial3D）が選んだ材質の面（CreateModelMaterialGeometry で作った形状）。無い番号・未対応は nullptr。借用でなく共有（描いている間に削除されても、使える）
	virtual TSharedPtr<const FModelMaterialGeometry> FindStaticModelMaterialGeometry( int32 /*Geometry*/ ) { return TSharedPtr<const FModelMaterialGeometry>(); }
	// 元のモデルのメッシュ SourceMesh（フレーム SourceFrame の中）が、いま描かれるか（メッシュと、親までのフレームの表示を合わせた結果）。分からないときは false（bOut は変えない）
	virtual bool GetStaticModelMeshDrawn( int32 /*Handle*/, int32 /*SourceMesh*/, int32 /*SourceFrame*/, bool& /*bOut*/ ) { return false; }
	virtual bool SetStaticModelPlacement( int32 /*Handle*/, const float32 ( & /*Matrix*/ )[12] ) { return false; }
	virtual bool SetStaticModelAnimTime( int32 /*Handle*/, int32 /*AttachIndex*/, float32 /*Time*/ ) { return false; }
	virtual bool SetStaticModelAnimRate( int32 /*Handle*/, int32 /*AttachIndex*/, float32 /*Rate*/ ) { return false; }
	virtual bool SetStaticModelAnimFrameRate( int32 /*Handle*/, int32 /*AttachIndex*/, int32 /*Frame*/, float32 /*Rate*/, bool /*bIncludeChildren*/ ) { return false; }
};
