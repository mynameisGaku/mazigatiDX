// SPDX-License-Identifier: MIT
#include "IndexBuffer.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Framework/Asset/Asset.h"

TSharedPtr<CIndexBuffer> CIndexBuffer::Create( int32 NumIndices, EIndexType Type )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !NAssetService::IsMainThread() || NumIndices < 1 || NumIndices > MaxIndices )
	{
		return nullptr;
	}
	const int32 IndexBuffer = Backend->MakeIndexBuffer( NumIndices, Type == EIndexType::Index32 );
	if ( IndexBuffer < 0 )
	{
		return nullptr;
	}
	return TSharedPtr<CIndexBuffer>( new CIndexBuffer( IndexBuffer, NumIndices, Type ) );
}

TSharedPtr<CIndexBuffer> CIndexBuffer::Create( TArrayView<const uint16> Indices )
{
	TSharedPtr<CIndexBuffer> Buffer = Create( Indices.Num(), EIndexType::Index16 );
	if ( !Buffer.IsValid() || !Buffer->SetData( 0, Indices ) )
	{
		return nullptr;
	}
	return Buffer;
}

TSharedPtr<CIndexBuffer> CIndexBuffer::Create( TArrayView<const uint32> Indices )
{
	TSharedPtr<CIndexBuffer> Buffer = Create( Indices.Num(), EIndexType::Index32 );
	if ( !Buffer.IsValid() || !Buffer->SetData( 0, Indices ) )
	{
		return nullptr;
	}
	return Buffer;
}

CIndexBuffer::~CIndexBuffer()
{
	NAssetService::ReleaseIndexBuffer( IndexBuffer );
}

bool CIndexBuffer::SetData( int32 StartIndex, TArrayView<const uint16> Indices )
{
	return Write( StartIndex, Indices.GetData(), Indices.Num(), EIndexType::Index16 );
}

bool CIndexBuffer::SetData( int32 StartIndex, TArrayView<const uint32> Indices )
{
	return Write( StartIndex, Indices.GetData(), Indices.Num(), EIndexType::Index32 );
}

bool CIndexBuffer::Write( int32 StartIndex, const void* Indices, int32 Count, EIndexType ExpectedType )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	// 足し算は、溢れないよう 64 ビットで
	if ( Backend == nullptr || !NAssetService::IsMainThread() || ExpectedType != Type || StartIndex < 0 || Count < 1 || static_cast<int64>( StartIndex ) + Count > NumIndices )
	{
		return false;
	}
	return Backend->WriteIndexBuffer( IndexBuffer, StartIndex, Indices, Count );
}

FIndexBufferHandle CIndexBuffer::GetIndexBufferHandle() const
{
	return FIndexBufferHandle( IndexBuffer, NumIndices );
}
