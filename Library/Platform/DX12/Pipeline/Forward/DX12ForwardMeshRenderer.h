// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Render/IGraphicsPipelineResources.h"
#include "Library/Core/Interfaces/Render/IRenderBufferResources.h"
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Render/ForwardMeshDraw.h"
#include "Library/Platform/Render/Texture/TextureAssetStore.h"

//=============================================================================
// CDX12ForwardMeshRenderer
//   Forward の、GPU が変換と光を計算する描画（R2）。共有のメッシュ（頂点と添字）を、GPU のバッファに 1 度だけ置いて（MeshKey）、
//   個体ごとの行列・光・不透明度（定数）だけを替えて描く。同じメッシュの複数の個体は、バッファを共有する。
//   ・頂点シェーダーが、クリップ座標への変換と、頂点ごとの光（画素ごとの光と同じ式）を計算する（法線は、ワールドの逆行列の転置で変換する）。
//   ・ピクセルシェーダーは、頂点の色（と、あれば画像）を出す。深度・表裏・合成・画像の外側は、描画の状態から、パイプラインを分けて作る。
//   ・メッシュは、使われなくなったら（一定の描画のあいだ呼ばれない）、GPU の完了後に手放す。置いてある量の上限を超えたら、古いものから手放す。
//   描画 API の型は、このクラスの中に閉じる（窓口は IShaderDrawSource::DrawForwardMesh）。ゲームスレッドから使う。
//   できないこと（false を返して、呼び出し側が CPU の経路で描く）: マスク・異方性・深度の偏り・縦横で違う画像の外側・反転 / 乗算アルファの合成・頂点が 65536 を超えるメッシュ。
//=============================================================================
class CDX12ForwardMeshRenderer
{
public:
	CDX12ForwardMeshRenderer() = default;
	CDX12ForwardMeshRenderer( const CDX12ForwardMeshRenderer& ) = delete;
	CDX12ForwardMeshRenderer& operator=( const CDX12ForwardMeshRenderer& ) = delete;

	// 窓口を借りる。借用先は Shutdown まで生き続けること。
	void Initialize( IGraphicsPipelineResources* InPipelines, IRenderBufferResources* InBuffers, CTextureAssetStore* InTextures );
	// 置いたメッシュとパイプラインを手放す（GPU の完了を待つのは、所有者）。
	void Shutdown();
	// 1 回描く。使えない条件・GPU が受け取らなかったときは false（何も描かない）。
	bool Draw( const FForwardMeshDraw& Draw, IRenderGeometry3D& Geometry );
	// GPU に置いてあるメッシュの数。
	int32 GetNumMeshes() const { return Meshes.Num(); }
	// GPU に置いてあるメッシュのバイト数。
	uint64 GetMeshBytes() const { return TotalBytes; }

private:
	struct FMesh
	{
		FBufferResourceHandle Vertices;
		FBufferResourceHandle Indices;
		uint64 Version = 0;
		int32 NumVertices = 0;
		int32 NumIndices = 0;
		uint64 Bytes = 0;
		uint64 LastUsed = 0;   // 最後に使った描画の通し番号。
	};

	bool BuildShaders();
	// 頂点・添字を GPU に置く（版が同じなら、置いてあるものを使う）。置けなければ nullptr。
	const FMesh* AcquireMesh( const FForwardMeshDraw& Draw );
	void ReleaseMesh( FMesh& Mesh );
	// 使われていないメッシュと、上限を超えたぶんを手放す。
	void Purge( uint64 Keep );
	FGraphicsPipelineHandle GetPipeline( const FForwardMeshDraw& Draw, bool bTextured, ERenderTargetColorFormat TargetFormat, EComputeTextureFormat TextureFormat );

	IGraphicsPipelineResources* Pipelines = nullptr;
	IRenderBufferResources* Buffers = nullptr;
	CTextureAssetStore* Textures = nullptr;
	TMap<uint64, FMesh> Meshes;
	TMap<FString, FGraphicsPipelineHandle> PipelineCache;   // 鍵は、状態と画像の有無
	TArray<uint8> VertexCode;
	TArray<uint8> PixelCode;
	TArray<uint8> TexturedPixelCode;
	bool bShadersTried = false;
	bool bShadersReady = false;
	uint64 Clock = 0;        // 描いた回数。
	uint64 TotalBytes = 0;   // 置いてあるメッシュのバイト数。
};
