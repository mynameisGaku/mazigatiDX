#include "WorldRender.h"

#include "Library/Framework/World/Mesh/EditMeshBuild.h"

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

FMeshRenderer NWorld::MakeMeshRenderer( const FEditMesh& Mesh, const std::function<TSharedPtr<CTexture>( const FString& )>& LoadTexture )
{
	FMeshRenderer Renderer;
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
		if ( !Material.Texture.IsEmpty() && LoadTexture ) Draw.Texture = LoadTexture( Material.Texture );
		Renderer.Batches.Add( Draw );
	}
	return Renderer;
}
