#include "EditMeshBuild.h"

#include <algorithm>
#include <cmath>

#include "Library/Math/MathConstants.h"

FVector2 NEditMesh::ComputeBoxUV( const FVector3& Position, const FVector3& Normal, const FEditMeshMaterial& Material )
{
	const float32 AX = std::fabs( Normal.x ), AY = std::fabs( Normal.y ), AZ = std::fabs( Normal.z );
	float32 U = 0.0f, V = 0.0f;
	// 表から見て、右が U、下が V（見ている人の上は、水平な面では +Z）
	if ( AY >= AX && AY >= AZ )
	{
		U = Normal.y >= 0.0f ? Position.x : -Position.x;
		V = -Position.z;
	}
	else if ( AX >= AZ )
	{
		U = Normal.x >= 0.0f ? Position.z : -Position.z;
		V = -Position.y;
	}
	else
	{
		U = Normal.z >= 0.0f ? -Position.x : Position.x;
		V = -Position.y;
	}
	const float32 Tile = Material.Tile > 1.0e-6f ? Material.Tile : 1.0f;
	return FVector2( U / Tile + Material.Offset.x, V / Tile + Material.Offset.y );
}

FEditMeshBuild NEditMesh::BuildRenderMesh( const FEditMesh& Mesh )
{
	FEditMeshBuild Result;

	// 面ごとの法線（長さ 1）と、面積で重みを付けた法線（長さ = 面積の 2 倍）。使える面だけ
	const int32 NumFaces = Mesh.Faces.Num();
	TArray<FVector3> Unit, Weighted;
	Unit.Init( FVector3::Zero, NumFaces );
	Weighted.Init( FVector3::Zero, NumFaces );
	TArray<bool> Drawn;
	Drawn.Init( false, NumFaces );
	TArray<TArray<int32>> VertexFaces;
	if ( Mesh.SmoothAngle > 0.0f ) VertexFaces.SetNum( Mesh.Vertices.Num() );
	for ( int32 Face = 0; Face < NumFaces; ++Face )
	{
		if ( !Mesh.IsFaceValid( Face ) ) continue;
		const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
		const FVector3& Origin = Mesh.Vertices[Indices[0]];
		FVector3 Sum = FVector3::Zero;
		for ( int32 i = 1; i + 1 < Indices.Num(); ++i ) Sum = Sum + FVector3::Cross( Mesh.Vertices[Indices[i]] - Origin, Mesh.Vertices[Indices[i + 1]] - Origin );
		const float32 Length = Sum.Length();
		if ( !( Length > 1.0e-12f ) || !std::isfinite( Length ) ) continue;
		Weighted[Face] = Sum;
		Unit[Face] = Sum / Length;
		Drawn[Face] = true;
		if ( Mesh.SmoothAngle > 0.0f )
		{
			for ( const int32 Index : Indices ) VertexFaces[Index].Add( Face );
		}
	}
	const float32 CosLimit = std::cos( std::clamp( Mesh.SmoothAngle, 0.0f, 180.0f ) * NMath::DEG2RAD ) - 1.0e-5f;   // 角度ちょうどの面も混ぜる

	// 材質ごとの三角形（頂点は共通の配列に）
	TArray<int32> Slots;   // 材質の番号の、出てきた順ではなく、小さい順に並べるための一覧
	TArray<TArray<uint32>> PerSlot;
	const auto SlotOf = [&]( int32 Material )
	{
		const int32 Slot = Mesh.Materials.IsValidIndex( Material ) ? Material : -1;
		int32 Position = Slots.Find( Slot );
		if ( Position == INDEX_NONE )
		{
			Position = Slots.Add( Slot );
			PerSlot.SetNum( Slots.Num() );
		}
		return Position;
	};

	for ( int32 Face = 0; Face < NumFaces; ++Face )
	{
		if ( !Drawn[Face] ) continue;
		const FEditMeshFace& Item = Mesh.Faces[Face];
		const FEditMeshMaterial& Material = Mesh.GetMaterial( Item.Material );
		const int32 First = Result.Vertices.Num();
		for ( const int32 Index : Item.Indices )
		{
			const FVector3& Position = Mesh.Vertices[Index];
			FVector3 Normal = Unit[Face];
			if ( Mesh.SmoothAngle > 0.0f )
			{
				FVector3 Mixed = FVector3::Zero;
				for ( const int32 Other : VertexFaces[Index] )
				{
					if ( FVector3::Dot( Unit[Face], Unit[Other] ) >= CosLimit ) Mixed = Mixed + Weighted[Other];
				}
				const float32 Length = Mixed.Length();
				if ( Length > 1.0e-12f ) Normal = Mixed / Length;
			}
			const FVector2 UV = NEditMesh::ComputeBoxUV( Position, Unit[Face], Material );
			FMeshVertex Vertex;
			Vertex.Position[0] = Position.x;
			Vertex.Position[1] = Position.y;
			Vertex.Position[2] = Position.z;
			Vertex.Normal[0] = Normal.x;
			Vertex.Normal[1] = Normal.y;
			Vertex.Normal[2] = Normal.z;
			Vertex.U = UV.x;
			Vertex.V = UV.y;
			Vertex.Color[0] = Material.Color.r;
			Vertex.Color[1] = Material.Color.g;
			Vertex.Color[2] = Material.Color.b;
			Result.Vertices.Add( Vertex );
		}
		TArray<uint32>& Target = PerSlot[SlotOf( Item.Material )];
		for ( int32 i = 1; i + 1 < Item.Indices.Num(); ++i )
		{
			Target.Add( static_cast<uint32>( First ) );
			Target.Add( static_cast<uint32>( First + i ) );
			Target.Add( static_cast<uint32>( First + i + 1 ) );
		}
	}

	// 材質の番号の小さい順にまとめる
	TArray<int32> Order;
	for ( int32 i = 0; i < Slots.Num(); ++i ) Order.Add( i );
	Order.Sort( [&]( int32 A, int32 B ) { return Slots[A] < Slots[B]; } );
	for ( const int32 Position : Order )
	{
		FEditMeshBatch Batch;
		Batch.Material = Slots[Position];
		Batch.StartIndex = Result.Indices.Num();
		Batch.NumIndices = PerSlot[Position].Num();
		Result.Indices.Append( PerSlot[Position].GetData(), PerSlot[Position].Num() );
		Result.Batches.Add( Batch );
	}
	return Result;
}
