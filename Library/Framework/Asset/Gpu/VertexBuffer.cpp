// SPDX-License-Identifier: MIT
#include "VertexBuffer.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Asset/Asset.h"

TSharedPtr<CVertexBuffer> CVertexBuffer::Create( int32 NumVertices )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !NAssetService::IsMainThread() || NumVertices < 1 || NumVertices > MaxVertices )
	{
		return nullptr;
	}
	const int32 VertexBuffer = Backend->MakeVertexBuffer( NumVertices );
	if ( VertexBuffer < 0 )
	{
		return nullptr;
	}
	return TSharedPtr<CVertexBuffer>( new CVertexBuffer( VertexBuffer, NumVertices ) );
}

TSharedPtr<CVertexBuffer> CVertexBuffer::Create( TArrayView<const FVertex3D> Vertices )
{
	TSharedPtr<CVertexBuffer> Buffer = Create( Vertices.Num() );
	if ( !Buffer.IsValid() || !Buffer->SetData( 0, Vertices ) )
	{
		return nullptr;
	}
	return Buffer;
}

CVertexBuffer::~CVertexBuffer()
{
	NAssetService::ReleaseVertexBuffer( VertexBuffer );
}

bool CVertexBuffer::SetData( int32 StartIndex, TArrayView<const FVertex3D> Vertices )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	// 足し算は、溢れないよう 64 ビットで
	if ( Backend == nullptr || !NAssetService::IsMainThread() || StartIndex < 0 || Vertices.Num() < 1 || static_cast<int64>( StartIndex ) + Vertices.Num() > NumVertices )
	{
		return false;
	}
	// 一部の頂点だけ書くと、形が変わってしまうので、全部を先に確かめる
	TArray<FDrawVertex3D> Converted;
	Converted.Reserve( Vertices.Num() );
	for ( const FVertex3D& Vertex : Vertices )
	{
		if ( !Vertex.IsValid() )
		{
			return false;
		}
		Converted.Add( Vertex.ToDrawData() );
	}
	return Backend->WriteVertexBuffer( VertexBuffer, StartIndex, Converted.GetData(), Converted.Num() );
}

FVertexBufferHandle CVertexBuffer::GetVertexBufferHandle() const
{
	return FVertexBufferHandle( VertexBuffer, NumVertices );
}
