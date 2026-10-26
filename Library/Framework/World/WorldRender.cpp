#include "WorldRender.h"

#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/World/Mesh/EditMeshBuild.h"

#include <utility>

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Core/Interfaces/Render/MeshGeometryChunk.h"
#include "Library/Framework/Render/MeshMaterialEffectResources.h"

namespace
{
	// 画像をMake時に一度だけ読み、再生中に使える状態なら保持する。
	TSharedPtr<CTexture> LoadReadyMeshEffectTexture( const TCallable<TSharedPtr<CTexture>( const FString& )>& LoadTexture, const FString& Path )
	{
		if ( !LoadTexture || Path.IsEmpty() ) return nullptr;
		TSharedPtr<CTexture> Texture = LoadTexture( Path );
		return Texture.IsValid() && Texture->IsReady() && Texture->GetHandle() >= 0 ? Texture : TSharedPtr<CTexture>();
	}

	// 一つの元slotを、三角形境界を保つ最大1024頂点・3072 indexの列へ分ける。
	bool BuildMeshEffectChunks( const FEditMeshBuild& Build, const FEditMeshBatch& Batch, FMeshMaterialEffectResources& Resources )
	{
		if ( Batch.StartIndex < 0 || Batch.NumIndices < 3 || Batch.NumIndices % 3 != 0 || Batch.StartIndex > Build.Indices.Num() - Batch.NumIndices ) return false;
		// 現在の材質batchだけを共通上限制約で分割した作業結果。
		TArray<FMeshGeometryChunk> Chunks;
		if ( !NMeshGeometry::BuildChunks( TArrayView<const FMeshVertex>( Build.Vertices ), TArrayView<const uint32>( Build.Indices.GetData() + Batch.StartIndex, Batch.NumIndices ), Chunks ) ) return false;
		Resources.Chunks = MoveTemp( Chunks );
		return !Resources.Chunks.IsEmpty();
	}

	// enabled distortionで正の曲線値がある場合だけ専用画像を要求する。
	bool MeshEffectNeedsDistortionTexture( const FEffectUVDistortionDefinition& Distortion )
	{
		if ( !Distortion.bEnabled ) return false;
		for ( const FEffectScalarCurve::FKey& Key : Distortion.Strength.Keys ) if ( Key.Value > 0.0f ) return true;
		return false;
	}

	// 一つのeffect slotを資源・設定・全chunkまで完成させてから返す。
	TSharedPtr<const FMeshMaterialEffectResources> BuildMeshEffectResources( const FEditMeshBuild& Build, const FEditMeshBatch& Batch, const FEditMeshMaterial& Material, const TCallable<TSharedPtr<CTexture>( const FString& )>& LoadTexture )
	{
		const FMeshMaterialEffectDefinition& Definition = Material.Effect;
		if ( !Definition.Material.bEnabled || !Definition.IsValidSettings() ) return nullptr;
		const FString SourcePath = Material.Texture.IsEmpty() ? FString( "Effects/material_white.tga" ) : Material.Texture;
		TSharedPtr<CTexture> SourceTexture = LoadReadyMeshEffectTexture( LoadTexture, SourcePath );
		if ( !SourceTexture.IsValid() ) return nullptr;
		TSharedPtr<CTexture> MaskTexture;
		if ( Definition.Material.bDissolve && !Definition.Material.MaskTexture.IsEmpty() )
		{
			MaskTexture = LoadReadyMeshEffectTexture( LoadTexture, Definition.Material.MaskTexture );
			if ( !MaskTexture.IsValid() ) return nullptr;
		}
		TSharedPtr<CTexture> DistortionTexture;
		if ( MeshEffectNeedsDistortionTexture( Definition.Material.Distortion ) )
		{
			DistortionTexture = LoadReadyMeshEffectTexture( LoadTexture, Definition.Material.Distortion.Texture );
			if ( !DistortionTexture.IsValid() ) return nullptr;
		}
		TSharedPtr<FMeshMaterialEffectResources> Resources = MakeShared<FMeshMaterialEffectResources>();
		Resources->Definition = Definition;
		Resources->Texture = MoveTemp( SourceTexture );
		Resources->MaskTexture = MoveTemp( MaskTexture );
		Resources->DistortionTexture = MoveTemp( DistortionTexture );
		Resources->TextureHandle = Resources->Texture->GetHandle();
		Resources->MaskTextureHandle = Resources->MaskTexture.IsValid() ? Resources->MaskTexture->GetHandle() : -1;
		Resources->DistortionTextureHandle = Resources->DistortionTexture.IsValid() ? Resources->DistortionTexture->GetHandle() : -1;
		Resources->bTwoSided = Material.bTwoSided;
		if ( Resources->TextureHandle < 0 || ( Resources->MaskTexture.IsValid() && Resources->MaskTextureHandle < 0 ) || ( Resources->DistortionTexture.IsValid() && Resources->DistortionTextureHandle < 0 ) || !BuildMeshEffectChunks( Build, Batch, *Resources ) ) return nullptr;
		return Resources;
	}
}

EDrawQuadOrient NWorld::ToQuadOrient( EWorldBillboardMode Mode )
{
	switch ( Mode )
	{
	case EWorldBillboardMode::Camera: return EDrawQuadOrient::Camera;
	case EWorldBillboardMode::CameraY: return EDrawQuadOrient::CameraY;
	case EWorldBillboardMode::World: return EDrawQuadOrient::Fixed;
	}
	return EDrawQuadOrient::Camera;
}

FBillboard NWorld::MakeBillboard( const FWorldBillboard& Spec, const TSharedPtr<CTexture>& Texture, bool* OutRectApplied )
{
	FBillboard Board;
	Board.Texture = Texture;
	Board.Size = Spec.Size;
	Board.Pivot = Spec.Pivot;
	Board.Orient = ToQuadOrient( Spec.Mode );
	Board.Tint = Spec.Tint;
	Board.bFlipX = Spec.bFlipX;
	Board.bFlipY = Spec.bFlipY;
	if ( OutRectApplied != nullptr ) *OutRectApplied = true;
	if ( Spec.bUseRect && !Board.SetRect( Spec.RectX, Spec.RectY, Spec.RectWidth, Spec.RectHeight ) && OutRectApplied != nullptr ) *OutRectApplied = false;
	return Board;
}

FMeshRenderer NWorld::MakeMeshRenderer( const FEditMesh& Mesh, const TCallable<TSharedPtr<CTexture>( const FString& )>& LoadTexture )
{
	FMeshRenderer Renderer;
	// 同一mesh内でsource・mask・noiseが同じ画像を指す場合に一度だけ読むcache。
	TMap<FString, TSharedPtr<CTexture>> LoadedTextures;
	// 既存画像描画とeffect資源準備が同じTexture objectを共有する窓口。
	const auto LoadOnce = [&LoadTexture, &LoadedTextures]( const FString& Path )
	{
		// 同一asset pathで前に得た共有texture。
		if ( const TSharedPtr<CTexture>* Existing = LoadedTextures.Find( Path ) ) return *Existing;
		// loaderが返したtextureを通常描画とeffect準備で共有する値。
		TSharedPtr<CTexture> Loaded = LoadTexture ? LoadTexture( Path ) : TSharedPtr<CTexture>();
		LoadedTextures.Add( Path, Loaded );
		return Loaded;
	};
	const FEditMeshBuild Build = NEditMesh::BuildRenderMesh( Mesh );
	if ( Build.Indices.IsEmpty() ) return Renderer;
	Renderer.Buffer = CMeshBuffer::Create( TArrayView<const FMeshVertex>( Build.Vertices ), TArrayView<const uint32>( Build.Indices ) );
	if ( !Renderer.Buffer.IsValid() ) return Renderer;
	for ( const FEditMeshBatch& Batch : Build.Batches )
	{
		const FEditMeshMaterial& Material = Mesh.GetMaterial( Batch.Material );
		FMeshDrawBatch Draw;
		Draw.StartIndex = Batch.StartIndex;
		Draw.NumIndices = Batch.NumIndices;
		Draw.bTwoSided = Material.bTwoSided;
		if ( !Material.Texture.IsEmpty() && LoadTexture ) Draw.Texture = LoadOnce( Material.Texture );
		Draw.bEffectRequested = Material.Effect.Material.bEnabled;
		if ( Draw.bEffectRequested )
		{
			Draw.EffectState = FMeshDrawBatch::EEffectPlaybackState::Playing;
			Draw.EffectResources = BuildMeshEffectResources( Build, Batch, Material, LoadOnce );
		}
		Renderer.Batches.Add( Draw );
	}
	return Renderer;
}
