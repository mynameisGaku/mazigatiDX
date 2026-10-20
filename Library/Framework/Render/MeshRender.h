#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/MeshBuffer.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Math/Color.h"
#include "Library/Math/Transform.h"

class FTransformView;

//=============================================================================
// FMeshRenderer
//   編集メッシュ（Mesh 部品）を描くための、ECS のデータ。オブジェクトに AddData で付ける。毎フレーム自動で描かれる（組み込みの描画システム。
//   CTransformComponent のワールドの位置・向き・拡大に従う）。ワールドの設置（WorldBuiltinSpawners.cpp）が付ける。
//
//   頂点バッファは、材質ごとに範囲（Batches）を持ち、1 つの範囲 = 1 回の描画。材質の色は、頂点の色としてバッファに入っている（描くときには変えられない。
//   色を変えたいときは、編集メッシュの材質を変えて、作り直す）。
//   自分で描きたいときは bAutoDraw を false にして、描画システムの中で Record / RecordAt を呼ぶ。
//=============================================================================
struct FMeshDrawBatch
{
	int32 StartIndex = 0;                 // インデックスバッファの何番目から
	int32 NumIndices = 0;                 // 何個（3 の倍数）
	TSharedPtr<CTexture> Texture;         // 画像。無ければ、頂点の色だけ
	bool bTwoSided = false;
};

struct FMeshRenderer
{
	TSharedPtr<CMeshBuffer> Buffer;
	TArray<FMeshDrawBatch> Batches;
	bool bVisible = true;
	bool bAutoDraw = true;

	bool IsValid() const { return Buffer.IsValid() && !Batches.IsEmpty(); }

	// World（ワールドの位置・向き・拡大）で記録する。バッファが無い・見えない設定なら何も記録しない。画像が読み込み中（ハンドルが負）の材質は、色だけで描く
	void Record( FDrawContext& Draw, const FTransform& World ) const;
	void RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const;
};
