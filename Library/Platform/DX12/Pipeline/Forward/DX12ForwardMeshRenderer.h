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
//   GPU スキニング（FForwardMeshDraw の SkinVertices があるとき）: 頂点は骨の変形の前の姿で置き（版は、骨の動きでは変わらない）、頂点ごとの骨と重みを構造化バッファ（t2）、
//   骨の行列を定数（b1）で渡して、頂点シェーダーが変形する。パイプラインは、スキニングの有無で分ける。
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
	// 共有メッシュまたはフレームの転送領域から1回描く。使えない条件・GPU拒否時はfalse（何も描かない）。
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
		FBufferResourceHandle Skin;   // GPU スキニングのメッシュだけ: 頂点ごとの骨と重み（構造化バッファ。Vertices と同じ並び）。
		uint64 Version = 0;
		int32 NumVertices = 0;
		int32 NumIndices = 0;
		int32 NumBones = 0;           // GPU スキニングのメッシュだけ: 骨の行列の数（骨の番号は、これより小さいと確かめてある）。
		uint64 Bytes = 0;
		uint64 LastUsed = 0;   // 最後に使った描画の通し番号。
	};

	bool BuildShaders();
	// 頂点・添字を GPU に置く（版が同じなら、置いてあるものを使う）。置けなければ nullptr。
	const FMesh* AcquireMesh( const FForwardMeshDraw& Draw );
	void ReleaseMesh( FMesh& Mesh );
	// 使われていないメッシュと、上限を超えたぶんを手放す。
	void Purge( uint64 Keep );
	FGraphicsPipelineHandle GetPipeline( const FForwardMeshDraw& Draw, bool bTextured, bool bReceive, ERenderTargetColorFormat TargetFormat, EComputeTextureFormat TextureFormat );

	IGraphicsPipelineResources* Pipelines = nullptr;
	IRenderBufferResources* Buffers = nullptr;
	CTextureAssetStore* Textures = nullptr;
	TMap<uint64, FMesh> Meshes;
	TMap<FString, FGraphicsPipelineHandle> PipelineCache;   // 鍵は、状態と画像の有無
	// 直前の GetPipeline の入力（鍵の文字列に入れる値と同じ並び）と答え。同じ入力が続くとき、描画のたびに文字列の鍵を作らずに答える。
	static constexpr int32 NumPipelineInputs = 19;
	int32 LastPipelineInputs[NumPipelineInputs] = {};
	FGraphicsPipelineHandle LastPipelineHandle;
	bool bLastPipelineValid = false;
	TArray<uint8> VertexCode;
	TArray<uint8> PixelCode;
	TArray<uint8> TexturedPixelCode;
	TArray<uint8> ShadowPixelCode;           // 影を受ける描画（画像なし）。
	TArray<uint8> ShadowTexturedPixelCode;   // 影を受ける描画（画像あり）。
	TArray<uint8> DepthVertexCode;           // 影の絵へ深度だけを書く描画の頂点シェーダー。
	TArray<uint8> DepthPixelCode;            // 同じく、ピクセルシェーダー。
	TArray<uint8> SkinnedVertexCode;         // GPU スキニングの頂点シェーダー（骨の行列で、頂点を変形してから、ForwardVS と同じ計算）。
	TArray<uint8> SkinnedDepthVertexCode;    // GPU スキニングで、影の絵へ深度だけを書く頂点シェーダー。
	TArray<float32> PaletteScratch;          // 骨の行列を、シェーダーの定数の大きさ（MaxStaticModelSkinBones 個）に揃えて渡すための作業領域。
	bool bShadersTried = false;
	bool bShadersReady = false;
	uint64 Clock = 0;        // 描いた回数。
	uint64 TotalBytes = 0;   // 置いてあるメッシュのバイト数。
};
