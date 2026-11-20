// SPDX-License-Identifier: MIT
#include "ShaderMeshBufferCache.h"
#include "Library/Core/Collection/Hash.h"
#include "Library/Core/Memory/Memory.h"

namespace
{
	// 大きな配列も一定の費用で候補を絞る。同一性はAcquireで全バイトを確認する。
	uint32 SampleHash( TArrayView<const uint8> Bytes )
	{
		constexpr int32 SampleSize = 32; // 各位置から読む最大バイト数。
		if ( Bytes.Num() <= 3 * SampleSize ) { return HashBytes( Bytes.GetData(), Bytes.Num() ); }
		const int32 Last = Bytes.Num() - SampleSize; // 末尾の見本の開始位置。
		const uint32 Head = HashBytes( Bytes.GetData(), SampleSize ); // 先頭の見本。
		const uint32 Middle = HashBytes( Bytes.GetData() + Last / 2, SampleSize ); // 中央の見本。
		const uint32 Tail = HashBytes( Bytes.GetData() + Last, SampleSize ); // 末尾の見本。
		return HashCombine( HashCombine( Head, Middle ), Tail );
	}
}

CShaderMeshBufferCache::~CShaderMeshBufferCache() { Shutdown(); }

void CShaderMeshBufferCache::Initialize( IRenderBufferResources* InBuffers, uint64 InMaxBytes, int32 InMaxEntries )
{
	if ( Buffers || !InBuffers ) { return; }
	Buffers = InBuffers;
	MaxBytes = InMaxBytes;
	MaxEntries = InMaxEntries > 0 ? InMaxEntries : 0;
}

void CShaderMeshBufferCache::Remove( int32 Index )
{
	const FEntry& Entry = Entries[Index]; // 資源を返してから、所有する比較用コピーを解放する。
	if ( Entry.IndexBuffer.IsValid() ) { Buffers->ReleaseBuffer( Entry.IndexBuffer ); }
	if ( Entry.VertexBuffer.IsValid() ) { Buffers->ReleaseBuffer( Entry.VertexBuffer ); }
	NumBytes -= static_cast<uint64>( Entry.Vertices.Num() ) + Entry.Indices.Num();
	Entries.RemoveAt( Index );
}

void CShaderMeshBufferCache::Shutdown()
{
	while ( !Entries.IsEmpty() ) { Remove( Entries.Num() - 1 ); }
	Buffers = nullptr;
	MaxBytes = 0;
	MaxEntries = 0;
}

bool CShaderMeshBufferCache::Acquire( TArrayView<const uint8> Vertices, TArrayView<const uint8> Indices, FBufferResourceHandle& OutVertices, FBufferResourceHandle& OutIndices )
{
	if ( !Buffers || MaxEntries == 0 || !Vertices.GetData() || Vertices.Num() <= 0 || Vertices.Num() % 4 != 0 || Indices.Num() < 0 || Indices.Num() % 4 != 0 || ( Indices.Num() > 0 && !Indices.GetData() ) ) { return false; }
	const uint64 Size = static_cast<uint64>( Vertices.Num() ) + Indices.Num(); // 足し算前に64bitへ広げる。
	if ( Size > MaxBytes ) { return false; }
	const uint32 Hash = HashCombine( SampleHash( Vertices ), SampleHash( Indices ) ); // 長さは配列ごとに別途照合する。
	for ( int32 Index = 0; Index < Entries.Num(); ++Index )
	{
		FEntry& Entry = Entries[Index]; // ハッシュだけでは同一と判定しない。
		if ( Entry.Hash != Hash || Entry.Vertices.Num() != Vertices.Num() || Entry.Indices.Num() != Indices.Num() || FMemory::Memcmp( Entry.Vertices.GetData(), Vertices.GetData(), Vertices.Num() ) != 0 || FMemory::Memcmp( Entry.Indices.GetData(), Indices.GetData(), Indices.Num() ) != 0 ) { continue; }
		if ( !Entry.VertexBuffer.IsValid() )
		{
			const FBufferResourceHandle Vertex = Buffers->CreateBuffer( Vertices ); // 部分成功を項目へ残さない。
			if ( !Vertex.IsValid() ) { return false; }
			const FBufferResourceHandle IndexBuffer = Indices.IsEmpty() ? FBufferResourceHandle() : Buffers->CreateBuffer( Indices ); // 添字なしでは作成しない。
			if ( !Indices.IsEmpty() && !IndexBuffer.IsValid() ) { Buffers->ReleaseBuffer( Vertex ); return false; }
			Entry.VertexBuffer = Vertex;
			Entry.IndexBuffer = IndexBuffer;
		}
		OutVertices = Entry.VertexBuffer;
		OutIndices = Entry.IndexBuffer;
		FEntry Recent = MoveTemp( Entry ); // コピーせず最も最近使った位置へ移す。
		Entries.RemoveAt( Index );
		Entries.Add( MoveTemp( Recent ) );
		return true;
	}
	while ( Entries.Num() >= MaxEntries || Size > MaxBytes - NumBytes ) { Remove( 0 ); }
	FEntry Candidate; // 初回は同期GPU転送せず、再登場した形だけを登録する。
	Candidate.Hash = Hash;
	Candidate.Vertices.Append( Vertices.GetData(), Vertices.Num() );
	Candidate.Indices.Append( Indices.GetData(), Indices.Num() );
	Entries.Add( MoveTemp( Candidate ) );
	NumBytes += Size;
	return false;
}
