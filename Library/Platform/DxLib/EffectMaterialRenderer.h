// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_PLATFORM_DXLIB_EFFECTMATERIALRENDERER_H
#define LIBRARY_PLATFORM_DXLIB_EFFECTMATERIALRENDERER_H

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Interfaces/Render/DrawDistortedMaterialQuad.h"
#include "Library/Core/Interfaces/Render/DrawDistortedMaterialRibbon.h"
#include "Library/Core/Interfaces/Render/DrawMaterialQuad.h"
#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Core/Interfaces/Render/DrawMaterialRibbon.h"

// DxLib executor が所有し、材質付き四角形・帯・meshで共有する Direct3D 11 資源を管理する。
class FEffectMaterialRenderer
{
public:
	// 実装領域を確保する。GPU資源はInitialize後に作る。
	FEffectMaterialRenderer();
	// DxLib終了前の明示解放も含め、残るGPU資源を閉じる。
	~FEffectMaterialRenderer();
	// GPU資源の所有権を複製するcopy構築を許可しない。
	FEffectMaterialRenderer( const FEffectMaterialRenderer& ) = delete;
	// GPU資源の所有権を複製するcopy代入を許可しない。
	FEffectMaterialRenderer& operator=( const FEffectMaterialRenderer& ) = delete;

	// GPU 資源を作る。Direct3D 11 が無効または作成に失敗した場合は false。
	bool Initialize();
	// 現在の DxLib 描画状態を尊重して一枚描く。未対応のブレンドや不正資源では false。
	bool Draw( const FDrawMaterialQuadData& Data );
	// ゆがみ画像を加えて材質四角形を一枚描く。入力値や画像を使えない場合は false。
	bool DrawDistorted( const FDrawDistortedMaterialQuadData& Data );
	// 通常帯と同じmiter meshへ材質を補間して描く。未対応状態や不正資源では false。
	bool DrawRibbon( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points );
	// 固定設定と点列を受け取り、点ごとのUVゆがみを補間して帯を描く。
	// Dataには描画設定とRibbon.NumPointsを指定する。
	// PointsにはNumPoints個以上の有効な型付き配列を渡す。
	// 無効値、画像資源不足、未対応状態ではfalseを返す。
	bool DrawDistortedRibbon( const FDrawDistortedMaterialRibbonData& Data, const FDrawDistortedMaterialRibbonData::FPoint* Points );
	// 固定設定と頂点・index配列を受け取り、world座標の材質付き三角形を描く。
	// Data: 頂点数、index数、材質と画像handleを含む固定設定。
	// Vertices: Data.NumVertices個以上のworld頂点を持つ型付き配列。
	// Indices: Data.NumIndices個以上の範囲内indexを持つ型付き配列。
	// 不正値、画像不足、未対応blendや描画状態ではfalseを返す。
	// bUseRegularCulling: trueなら論理設定を使い、falseなら呼出元のnative rasterを保つ。
	bool DrawMesh( const FDrawMaterialMeshData& Data, const FMeshVertex* Vertices, const uint32* Indices, bool bUseRegularCulling );
	// DxLib 終了前に所有している GPU 資源を解放する。
	void Deinitialize();
	// Direct3D 11 の GPU 資源を使用できるか返す。
	bool IsAvailable() const;
	// 材質付き帯のGPU描画資源を使用できるか返す。
	bool IsRibbonAvailable() const;
	// Direct3D 11でゆがみ画像を使う描画資源が使用できるか返す。
	bool IsUVDistortionAvailable() const;
	// Direct3D 11で点ごとのUVゆがみを使う帯描画資源が使用できるか返す。
	bool IsRibbonUVDistortionAvailable() const;
	// 材質付き頂点メッシュの描画資源が使用可能かを返す。
	bool IsMeshAvailable() const;

private:
	// Direct3D 11 の型を公開headerへ漏らさない実装。
	struct FImpl;
	// DxLib終了前に解放するGPU資源の所有者。
	FImpl* Impl = nullptr;
};

#endif
