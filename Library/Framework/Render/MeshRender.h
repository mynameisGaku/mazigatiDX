// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_RENDER_MESHRENDER_H
#define FRAMEWORK_RENDER_MESHRENDER_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/MeshBuffer.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Render/MeshMaterialEffectPlayback.h"
#include "Library/Math/Color.h"
#include "Library/Math/Transform.h"

class FTransformView;
struct FMeshMaterialEffectResources;

//=============================================================================
// FMeshRenderer
//   編集メッシュ（Mesh 部品）を描くための、ECS のデータ。オブジェクトに AddData で付ける。毎フレーム自動で描かれる（組み込みの描画システム。
//   CTransformComponent のワールドの位置・向き・拡大に従う）。ワールドの設置（WorldBuiltinSpawners.cpp）が付ける。
//
//   頂点バッファは、材質ごとに範囲（Batches）を持ち、1 つの範囲 = 1 回の描画。材質の色は、頂点の色としてバッファに入っている（描くときには変えられない。
//   色を変えたいときは、編集メッシュの材質を変えて、作り直す）。
//   自分で描きたいときは bAutoDraw を false にして、描画システムの中で Record / RecordAt を呼ぶ。
//=============================================================================
struct FMeshDrawBatch : FMeshMaterialEffectPlayback
{
	int32 StartIndex = 0;                 // インデックスバッファの何番目から
	int32 NumIndices = 0;                 // 何個（3 の倍数）
	TSharedPtr<CTexture> Texture;         // 画像。無ければ、頂点の色だけ
	bool bTwoSided = false;
	// 定義上effectが有効か。準備失敗時もopaque描画へ切り替えないため保持する。
	bool bEffectRequested = false;
	// Make時に完成した、複製間で共有するslot資源。
	TSharedPtr<const FMeshMaterialEffectResources> EffectResources;
};

struct FMeshRenderer
{
	TSharedPtr<CMeshBuffer> Buffer;
	TArray<FMeshDrawBatch> Batches;
	bool bVisible = true;
	bool bAutoDraw = true;

	bool IsValid() const { return Buffer.IsValid() && !Batches.IsEmpty(); }
	// 有効なslotの曲線時間だけを進める。負数・非有限deltaは無視する。
	void Update( float32 DeltaTime );
	// 停止中なら時刻0から始め、pause中なら保持した時刻から再開する。
	void PlayEffects();
	// 現在の画面表示を保って時計だけ止める。
	void PauseEffects();
	// 時刻を0へ戻し、従来Mesh3D描画へ切り替える。
	void StopEffects();
	// 時刻0へ戻してeffect描画を開始する。
	void RestartEffects();
	// いずれかのeffect slotが進行中ならtrue。
	bool IsEffectsPlaying() const;
	// いずれかのeffect slotが一時停止中ならtrue。
	bool IsEffectsPaused() const;
	// 有効effect slotが一つ以上あり、すべて終端表示中ならtrue。
	bool IsEffectsFinished() const;
	// 有効slotの曲線秒数を返す。無効slotまたは範囲外なら0。
	float64 GetEffectTime( int32 BatchIndex = 0 ) const;
	// 有効指定の材質画像・mesh chunkを生成できなかったslotがあるか。
	bool HasEffectPreparationFailures() const;

	// World（ワールドの位置・向き・拡大）で記録する。バッファが無い・見えない設定なら何も記録しない。画像が読み込み中（ハンドルが負）の材質は、色だけで描く
	void Record( FDrawContext& Draw, const FTransform& World ) const;
	void RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const;
};

#endif
