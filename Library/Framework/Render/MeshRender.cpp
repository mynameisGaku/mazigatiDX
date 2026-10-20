#include "MeshRender.h"

#include "Library/Framework/Ecs/SystemParams.h"

void FMeshRenderer::Record( FDrawContext& Draw, const FTransform& World ) const
{
	if ( !bVisible || !Buffer.IsValid() || Buffer->GetVertexBuffer() < 0 || Buffer->GetIndexBuffer() < 0 ) return;
	const FMatrix4x4 Matrix = World.ToMatrix();
	for ( const FMeshDrawBatch& Batch : Batches )
	{
		if ( Batch.NumIndices < 3 ) continue;
		const int32 TextureHandle = Batch.Texture.IsValid() ? Batch.Texture->GetHandle() : -1;
		Draw.Mesh3D( Buffer->GetVertexBuffer(), Buffer->GetIndexBuffer(), Buffer->GetNumVertices(), Batch.StartIndex, Batch.NumIndices, TextureHandle, Matrix, Batch.bTwoSided );
	}
}

void FMeshRenderer::RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const
{
	Record( Draw, Transform.GetWorld() );
}
