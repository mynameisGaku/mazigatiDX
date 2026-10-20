#include "MeshBuffer.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Asset/Asset.h"

TSharedPtr<CMeshBuffer> CMeshBuffer::Create( TArrayView<const FMeshVertex> Vertices, TArrayView<const uint32> Indices )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || Vertices.Num() < 3 || Indices.Num() < 3 || Indices.Num() % 3 != 0 ) return nullptr;
	for ( const uint32 Index : Indices )
	{
		if ( Index >= static_cast<uint32>( Vertices.Num() ) ) return nullptr;
	}
	int32 VertexBuffer = NAsset::InvalidHandle;
	int32 IndexBuffer = NAsset::InvalidHandle;
	if ( !Backend->CreateMeshBuffers( Vertices.GetData(), Vertices.Num(), Indices.GetData(), Indices.Num(), VertexBuffer, IndexBuffer ) ) return nullptr;
	return TSharedPtr<CMeshBuffer>( new CMeshBuffer( VertexBuffer, IndexBuffer, Vertices.Num(), Indices.Num() ) );
}

CMeshBuffer::~CMeshBuffer()
{
	NAssetService::ReleaseVertexBuffer( VertexBuffer );
	NAssetService::ReleaseIndexBuffer( IndexBuffer );
}
