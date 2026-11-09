// SPDX-License-Identifier: MIT
#include "GeometryBufferStore.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

bool FGeometryBufferStore::IsValidVertex( const FDrawVertex3D& Vertex )
{
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		if ( !NMath::IsFinite( Vertex.Position[Index] ) || !NMath::IsFinite( Vertex.Normal[Index] ) )
		{
			return false;
		}
	}
	return NMath::IsFinite( Vertex.U ) && NMath::IsFinite( Vertex.V ) && NMath::IsFinite( Vertex.SubU ) && NMath::IsFinite( Vertex.SubV );
}

int32 FGeometryBufferStore::MakeVertexBuffer( int32 NumVertices )
{
	if ( NumVertices <= 0 || static_cast<uint64>( NumVertices ) * sizeof( FDrawVertex3D ) > MaxBytes - Bytes || NextHandle == TNumericLimits<int32>::Max() || Vertices.Num() + Indices.Num() >= 1024 )
	{
		return -1;
	}
	TArray<FDrawVertex3D> Values; // 作成時は契約どおり全バイトをゼロにする。
	Values.SetNum( NumVertices );
	FMemory::Memzero( Values.GetData(), static_cast<usize>( NumVertices ) * sizeof( FDrawVertex3D ) );
	const int32 Handle = NextHandle++; // この資源だけに使う番号。
	Bytes += static_cast<uint64>( NumVertices ) * sizeof( FDrawVertex3D );
	Vertices.Add( Handle, MoveTemp( Values ) );
	return Handle;
}

bool FGeometryBufferStore::WriteVertexBuffer( int32 Handle, int32 Start, const FDrawVertex3D* Source, int32 Count )
{
	auto* Values = Vertices.Find( Handle ); // 更新先の登録。
	if ( !Values || !Source || Start < 0 || Count <= 0 || static_cast<int64>( Start ) + Count > Values->Num() )
	{
		return false;
	}
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		if ( !IsValidVertex( Source[Index] ) )
		{
			return false;
		}
	}
	FMemory::Memmove( Values->GetData() + Start, Source, static_cast<usize>( Count ) * sizeof( FDrawVertex3D ) );
	return true;
}

void FGeometryBufferStore::DeleteVertexBuffer( int32 Handle )
{
	if ( const auto* Values = Vertices.Find( Handle ) )
	{
		Bytes -= static_cast<uint64>( Values->Num() ) * sizeof( FDrawVertex3D );
		Vertices.Remove( Handle );
	}
}

int32 FGeometryBufferStore::MakeIndexBuffer( int32 NumIndices, bool b32Bit )
{
	if ( NumIndices <= 0 || static_cast<uint64>( NumIndices ) * sizeof( uint32 ) > MaxBytes - Bytes || NextHandle == TNumericLimits<int32>::Max() || Vertices.Num() + Indices.Num() >= 1024 )
	{
		return -1;
	}
	FIndexRecord Record; // 書き込みの形式だけを保持する。
	Record.Values.SetNum( NumIndices );
	FMemory::Memzero( Record.Values.GetData(), static_cast<usize>( NumIndices ) * sizeof( uint32 ) );
	Record.b32Bit = b32Bit;
	const int32 Handle = NextHandle++; // この資源だけに使う番号。
	Bytes += static_cast<uint64>( NumIndices ) * sizeof( uint32 );
	Indices.Add( Handle, MoveTemp( Record ) );
	return Handle;
}

bool FGeometryBufferStore::WriteIndexBuffer( int32 Handle, int32 Start, const void* Source, int32 Count )
{
	auto* Record = Indices.Find( Handle ); // 更新先の登録。
	if ( !Record || !Source || Start < 0 || Count <= 0 || static_cast<int64>( Start ) + Count > Record->Values.Num() )
	{
		return false;
	}
	TArray<uint32> Values; // 元の列と重なる場合も、読む内容を先に固定する。
	Values.SetNum( Count );
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		if ( Record->b32Bit )
		{
			FMemory::Memcpy( Values.GetData() + Index, static_cast<const uint8*>( Source ) + static_cast<usize>( Index ) * sizeof( uint32 ), sizeof( uint32 ) );
		}
		else
		{
			uint16 Value = 0; // alignmentに頼らず16bitを読む。
			FMemory::Memcpy( &Value, static_cast<const uint8*>( Source ) + static_cast<usize>( Index ) * sizeof( uint16 ), sizeof( uint16 ) );
			Values[Index] = Value;
		}
	}
	FMemory::Memcpy( Record->Values.GetData() + Start, Values.GetData(), static_cast<usize>( Count ) * sizeof( uint32 ) );
	return true;
}

void FGeometryBufferStore::DeleteIndexBuffer( int32 Handle )
{
	if ( const auto* Record = Indices.Find( Handle ) )
	{
		Bytes -= static_cast<uint64>( Record->Values.Num() ) * sizeof( uint32 );
		Indices.Remove( Handle );
	}
}

bool FGeometryBufferStore::CreateMesh( const FDrawVertex3D* Source, int32 NumVertices, const uint32* SourceIndices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer )
{
	if ( !Source || !SourceIndices || NumVertices <= 0 || NumIndices <= 0 || NumIndices % 3 != 0 || static_cast<uint64>( NumVertices ) * sizeof( FDrawVertex3D ) + static_cast<uint64>( NumIndices ) * sizeof( uint32 ) > MaxBytes - Bytes
		|| Vertices.Num() + Indices.Num() > 1022 || NextHandle > TNumericLimits<int32>::Max() - 2 )
	{
		return false;
	}
	for ( int32 Index = 0; Index < NumVertices; ++Index )
	{
		if ( !IsValidVertex( Source[Index] ) )
		{
			return false;
		}
	}
	for ( int32 Index = 0; Index < NumIndices; ++Index )
	{
		if ( SourceIndices[Index] >= static_cast<uint32>( NumVertices ) )
		{
			return false;
		}
	}
	const int32 VertexHandle = MakeVertexBuffer( NumVertices ); // 全検査後に登録する。
	if ( VertexHandle < 0 )
	{
		return false;
	}
	const int32 IndexHandle = MakeIndexBuffer( NumIndices, true ); // indexを作れなければ頂点も戻す。
	if ( IndexHandle < 0 )
	{
		DeleteVertexBuffer( VertexHandle );
		return false;
	}
	if ( !WriteVertexBuffer( VertexHandle, 0, Source, NumVertices ) || !WriteIndexBuffer( IndexHandle, 0, SourceIndices, NumIndices ) )
	{
		DeleteVertexBuffer( VertexHandle );
		DeleteIndexBuffer( IndexHandle );
		return false;
	}
	OutVertexBuffer = VertexHandle;
	OutIndexBuffer = IndexHandle;
	return true;
}

bool FGeometryBufferStore::GetVertexBufferView( int32 Handle, FGeometryVertexBufferView& Out )
{
	const auto* Values = Vertices.Find( Handle ); // 登録済みの頂点。
	if ( !Values )
	{
		return false;
	}
	Out = { Values->GetData(), Values->Num() };
	return true;
}

bool FGeometryBufferStore::GetIndexBufferView( int32 Handle, FGeometryIndexBufferView& Out )
{
	const auto* Record = Indices.Find( Handle ); // 登録済みのindex。
	if ( !Record )
	{
		return false;
	}
	Out = { Record->Values.GetData(), Record->Values.Num() };
	return true;
}

void FGeometryBufferStore::Reset()
{
	Vertices.Reset();
	Indices.Reset();
	Bytes = 0;
}
