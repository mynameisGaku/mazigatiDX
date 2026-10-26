#include "EditMesh.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Json/JsonHelpers.h"

namespace
{
	const FEditMeshMaterial DefaultMaterial;

	bool IsFinite( const FVector3& V )
	{
		return NMath::IsFinite( V.x ) && NMath::IsFinite( V.y ) && NMath::IsFinite( V.z );
	}

	void Warn( TArray<FString>* Warnings, const FString& Message )
	{
		if ( Warnings != nullptr ) Warnings->Add( Message );
	}

	// FNV-1a（64 ビット）に、値のバイトを混ぜる
	struct FHasher
	{
		uint64 Hash = 1469598103934665603ull;

		void Bytes( const void* Data, usize Size )
		{
			const uint8* Ptr = static_cast<const uint8*>( Data );
			for ( usize i = 0; i < Size; ++i )
			{
				Hash ^= Ptr[i];
				Hash *= 1099511628211ull;
			}
		}
		template<typename T>
		void Value( const T& Item ) { Bytes( &Item, sizeof( T ) ); }
		void String( const FString& Text )
		{
			Bytes( *Text, static_cast<usize>( Text.Len() ) );
			Value( 0xFFu );   // 区切り（"ab" + "c" と "a" + "bc" を区別する）
		}
	};

	NlohmannJson MaterialToJson( const FEditMeshMaterial& Material )
	{
		const FEditMeshMaterial Default;
		NlohmannJson Json = NlohmannJson::object();
		if ( !Material.Name.IsEmpty() ) Json["Name"] = Material.Name.ToUtf8().GetData();
		if ( Material.Color.r != Default.Color.r || Material.Color.g != Default.Color.g || Material.Color.b != Default.Color.b ) Json["Color"] = NWorld::FormatColor( FColor32( Material.Color.r, Material.Color.g, Material.Color.b ) ).ToUtf8().GetData();
		if ( !Material.Texture.IsEmpty() ) Json["Texture"] = Material.Texture.ToUtf8().GetData();
		if ( Material.Tile != Default.Tile ) Json["Tile"] = NJson::Round4( Material.Tile );
		if ( Material.Offset.x != 0.0f || Material.Offset.y != 0.0f ) Json["Offset"] = NlohmannJson::array( { NJson::Round4( Material.Offset.x ), NJson::Round4( Material.Offset.y ) } );
		if ( Material.bTwoSided ) Json["TwoSided"] = true;
		if ( !Material.Effect.IsDefault() ) Json["Effect"] = Material.Effect.ToJson();
		return Json;
	}

	FEditMeshMaterial MaterialFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, int32 Index )
	{
		FEditMeshMaterial Material;
		if ( !Json.is_object() )
		{
			Warn( Warnings, FString::Printf( "Mesh: material %d is not an object (using the default)", Index ) );
			return Material;
		}
		Material.Name = NJson::GetOr<FString>( Json, "Name", FString() );
		if ( Json.contains( "Color" ) )
		{
			const FString Text = NJson::GetOr<FString>( Json, "Color", FString() );
			FColor32 Color;
			if ( NWorld::TryParseColor( Text, Color ) ) Material.Color = FColor32( Color.r, Color.g, Color.b, 255 );
			else Warn( Warnings, FString::Printf( "Mesh: material %d has a bad color '%s' (using the default)", Index, *Text ) );
		}
		Material.Texture = NJson::GetOr<FString>( Json, "Texture", FString() );
		Material.Tile = NJson::GetOr<float32>( Json, "Tile", Material.Tile );
		if ( Json.contains( "Offset" ) && Json["Offset"].is_array() && Json["Offset"].size() >= 2 )
		{
			try
			{
				Material.Offset = FVector2( Json["Offset"][0].get<float32>(), Json["Offset"][1].get<float32>() );
			}
			catch ( const nlohmann::json::exception& )
			{
				Material.Offset = FVector2( 0.0f, 0.0f );
				Warn( Warnings, FString::Printf( "Mesh: material %d has a bad offset (using 0, 0)", Index ) );
			}
		}
		Material.bTwoSided = NJson::GetOr<bool>( Json, "TwoSided", false );
		if ( Json.contains( "Effect" ) ) FMeshMaterialEffectDefinition::FromJson( Json["Effect"], Material.Effect, Warnings );
		return Material;
	}
}

const FEditMeshMaterial& FEditMesh::GetMaterial( int32 Index ) const
{
	return Materials.IsValidIndex( Index ) ? Materials[Index] : DefaultMaterial;
}

bool FEditMesh::IsFaceValid( int32 Face ) const
{
	if ( !Faces.IsValidIndex( Face ) ) return false;
	const TArray<int32>& Indices = Faces[Face].Indices;
	if ( Indices.Num() < 3 ) return false;
	for ( int32 i = 0; i < Indices.Num(); ++i )
	{
		if ( !Vertices.IsValidIndex( Indices[i] ) ) return false;
		for ( int32 j = 0; j < i; ++j )
		{
			if ( Indices[j] == Indices[i] ) return false;
		}
	}
	return true;
}

FVector3 FEditMesh::ComputeFaceNormal( int32 Face ) const
{
	if ( !IsFaceValid( Face ) ) return FVector3::Zero;
	// 多角形の面積ベクトル（先頭の頂点から見た、扇形の三角形の外積の和）。向きは表、長さは面積の 2 倍
	const TArray<int32>& Indices = Faces[Face].Indices;
	const FVector3& Origin = Vertices[Indices[0]];
	FVector3 Sum = FVector3::Zero;
	for ( int32 i = 1; i + 1 < Indices.Num(); ++i ) Sum = Sum + FVector3::Cross( Vertices[Indices[i]] - Origin, Vertices[Indices[i + 1]] - Origin );
	const float32 Length = Sum.Length();
	if ( !( Length > 1.0e-12f ) ) return FVector3::Zero;
	return Sum / Length;
}

float32 FEditMesh::ComputeFaceArea( int32 Face ) const
{
	if ( !IsFaceValid( Face ) ) return 0.0f;
	const TArray<int32>& Indices = Faces[Face].Indices;
	const FVector3& Origin = Vertices[Indices[0]];
	FVector3 Sum = FVector3::Zero;
	for ( int32 i = 1; i + 1 < Indices.Num(); ++i ) Sum = Sum + FVector3::Cross( Vertices[Indices[i]] - Origin, Vertices[Indices[i + 1]] - Origin );
	return Sum.Length() * 0.5f;
}

FVector3 FEditMesh::ComputeFaceCenter( int32 Face ) const
{
	if ( !IsFaceValid( Face ) ) return FVector3::Zero;
	FVector3 Sum = FVector3::Zero;
	for ( const int32 Index : Faces[Face].Indices ) Sum = Sum + Vertices[Index];
	return Sum / static_cast<float32>( Faces[Face].Indices.Num() );
}

TArray<uint32> FEditMesh::BuildTriangles( TArray<int32>* OutFaceOfTriangle ) const
{
	TArray<uint32> Triangles;
	if ( OutFaceOfTriangle != nullptr ) OutFaceOfTriangle->Reset();
	for ( int32 Face = 0; Face < Faces.Num(); ++Face )
	{
		if ( !IsFaceValid( Face ) ) continue;
		const TArray<int32>& Indices = Faces[Face].Indices;
		for ( int32 i = 1; i + 1 < Indices.Num(); ++i )
		{
			Triangles.Add( static_cast<uint32>( Indices[0] ) );
			Triangles.Add( static_cast<uint32>( Indices[i] ) );
			Triangles.Add( static_cast<uint32>( Indices[i + 1] ) );
			if ( OutFaceOfTriangle != nullptr ) OutFaceOfTriangle->Add( Face );
		}
	}
	return Triangles;
}

int32 FEditMesh::GetNumTriangles() const
{
	int32 Count = 0;
	for ( int32 Face = 0; Face < Faces.Num(); ++Face )
	{
		if ( IsFaceValid( Face ) ) Count += Faces[Face].Indices.Num() - 2;
	}
	return Count;
}

bool FEditMesh::GetBounds( FVector3& OutMin, FVector3& OutMax ) const
{
	bool bAny = false;
	for ( int32 Face = 0; Face < Faces.Num(); ++Face )
	{
		if ( !IsFaceValid( Face ) ) continue;
		for ( const int32 Index : Faces[Face].Indices )
		{
			const FVector3& V = Vertices[Index];
			OutMin = bAny ? FVector3::Min( OutMin, V ) : V;
			OutMax = bAny ? FVector3::Max( OutMax, V ) : V;
			bAny = true;
		}
	}
	return bAny;
}

float32 FEditMesh::ComputeVolume() const
{
	double Sum = 0.0;
	const TArray<uint32> Triangles = BuildTriangles();
	for ( int32 i = 0; i + 2 < Triangles.Num(); i += 3 )
	{
		const FVector3& A = Vertices[static_cast<int32>( Triangles[i] )];
		const FVector3& B = Vertices[static_cast<int32>( Triangles[i + 1] )];
		const FVector3& C = Vertices[static_cast<int32>( Triangles[i + 2] )];
		Sum += static_cast<double>( FVector3::Dot( A, FVector3::Cross( B, C ) ) );
	}
	return static_cast<float32>( Sum / 6.0 );
}

uint64 FEditMesh::ComputeHash() const
{
	FHasher Hasher;
	Hasher.Value( Vertices.Num() );
	for ( const FVector3& V : Vertices )
	{
		Hasher.Value( V.x );
		Hasher.Value( V.y );
		Hasher.Value( V.z );
	}
	Hasher.Value( Faces.Num() );
	for ( const FEditMeshFace& Face : Faces )
	{
		Hasher.Value( Face.Material );
		Hasher.Value( Face.Indices.Num() );
		for ( const int32 Index : Face.Indices ) Hasher.Value( Index );
	}
	Hasher.Value( Materials.Num() );
	for ( const FEditMeshMaterial& Material : Materials )
	{
		Hasher.String( Material.Name );
		Hasher.Value( Material.Color.r );
		Hasher.Value( Material.Color.g );
		Hasher.Value( Material.Color.b );
		Hasher.String( Material.Texture );
		Hasher.Value( Material.Tile );
		Hasher.Value( Material.Offset.x );
		Hasher.Value( Material.Offset.y );
		Hasher.Value( Material.bTwoSided );
		if ( !Material.Effect.IsDefault() )
		{
			// 旧材質hashのbyte列を保ち、拡張時だけ固有tagと効果hashを加える。
			Hasher.Value( uint32( 0x4D534546u ) );
			Hasher.Value( Material.Effect.ComputeHash() );
		}
	}
	Hasher.Value( SmoothAngle );
	return Hasher.Hash;
}

TArray<FEditMeshIssue> FEditMesh::Validate() const
{
	TArray<FEditMeshIssue> Issues;
	const auto Add = [&]( bool bError, int32 Face, const FString& Message )
	{
		FEditMeshIssue Issue;
		Issue.bError = bError;
		Issue.Face = Face;
		Issue.Message = Message;
		Issues.Add( Issue );
	};

	if ( Vertices.Num() > MaxVertices ) Add( true, -1, FString::Printf( "the mesh has %d vertices (the limit is %d)", Vertices.Num(), MaxVertices ) );
	if ( Faces.Num() > MaxFaces ) Add( true, -1, FString::Printf( "the mesh has %d faces (the limit is %d)", Faces.Num(), MaxFaces ) );
	if ( Faces.IsEmpty() ) Add( false, -1, "the mesh has no faces (nothing is drawn, and a mesh collider has nothing to collide with)" );
	if ( !NMath::IsFinite( SmoothAngle ) || SmoothAngle < 0.0f || SmoothAngle > 180.0f ) Add( true, -1, "the smoothing angle must be between 0 and 180 degrees" );

	int32 NumNonFinite = 0;
	for ( const FVector3& V : Vertices ) NumNonFinite += IsFinite( V ) ? 0 : 1;
	if ( NumNonFinite > 0 ) Add( true, -1, FString::Printf( "%d vertex position(s) are not finite numbers", NumNonFinite ) );

	for ( int32 i = 0; i < Materials.Num(); ++i )
	{
		const FEditMeshMaterial& Material = Materials[i];
		if ( !NMath::IsFinite( Material.Tile ) || Material.Tile <= 0.0f ) Add( true, -1, FString::Printf( "material %d: the tile length must be greater than 0", i ) );
		if ( !NMath::IsFinite( Material.Offset.x ) || !NMath::IsFinite( Material.Offset.y ) ) Add( true, -1, FString::Printf( "material %d: the offset is not a finite number", i ) );
		// 追加材質効果の検証内容をmesh issueへ渡す。
		for ( const FString& Issue : Material.Effect.Validate() ) Add( true, -1, FString::Printf( "material %d: %s", i, *Issue ) );
	}

	TArray<bool> Used;
	Used.Init( false, Vertices.Num() );
	int32 NumBrokenFaces = 0;
	for ( int32 Face = 0; Face < Faces.Num(); ++Face )
	{
		const FEditMeshFace& Item = Faces[Face];
		if ( Item.Indices.Num() < 3 )
		{
			Add( true, Face, FString::Printf( "face %d has %d vertices (it needs at least 3)", Face, Item.Indices.Num() ) );
			++NumBrokenFaces;
			continue;
		}
		bool bInRange = true;
		for ( const int32 Index : Item.Indices )
		{
			if ( Vertices.IsValidIndex( Index ) ) Used[Index] = true;
			else bInRange = false;
		}
		if ( !bInRange )
		{
			Add( true, Face, FString::Printf( "face %d uses a vertex that does not exist", Face ) );
			++NumBrokenFaces;
			continue;
		}
		if ( !IsFaceValid( Face ) )
		{
			Add( false, Face, FString::Printf( "face %d uses the same vertex twice (it is not drawn)", Face ) );
			continue;
		}
		if ( Item.Material < 0 ) Add( true, Face, FString::Printf( "face %d has a negative material number", Face ) );
		else if ( Item.Material >= NMath::Max( Materials.Num(), 1 ) ) Add( false, Face, FString::Printf( "face %d uses material %d, which does not exist (the default material is used)", Face, Item.Material ) );

		const FVector3 Normal = ComputeFaceNormal( Face );
		if ( Normal.IsNearlyZero() )
		{
			Add( false, Face, FString::Printf( "face %d has no area (it is not drawn properly)", Face ) );
			continue;
		}
		// 凸でない・ねじれた面は、扇形の三角形の向きが、面の向きと食い違う
		if ( Item.Indices.Num() > 3 )
		{
			const FVector3& Origin = Vertices[Item.Indices[0]];
			bool bTwisted = false;
			for ( int32 i = 1; i + 1 < Item.Indices.Num() && !bTwisted; ++i )
			{
				const FVector3 Part = FVector3::Cross( Vertices[Item.Indices[i]] - Origin, Vertices[Item.Indices[i + 1]] - Origin );
				if ( FVector3::Dot( Part, Normal ) < -1.0e-6f ) bTwisted = true;
			}
			if ( bTwisted ) Add( false, Face, FString::Printf( "face %d is not convex, or is twisted (split it into triangles)", Face ) );
		}
	}
	int32 NumUnused = 0;
	for ( const bool bUsed : Used ) NumUnused += bUsed ? 0 : 1;
	if ( NumUnused > 0 && NumBrokenFaces == 0 ) Add( false, -1, FString::Printf( "%d vertex(es) are not used by any face", NumUnused ) );
	return Issues;
}

NlohmannJson FEditMesh::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	NlohmannJson Positions = NlohmannJson::array();
	for ( const FVector3& V : Vertices ) Positions.push_back( NJson::ToJson( V ) );
	Json["Vertices"] = Positions;

	NlohmannJson FaceArray = NlohmannJson::array();
	bool bAnyMaterial = false;
	for ( const FEditMeshFace& Face : Faces )
	{
		NlohmannJson Loop = NlohmannJson::array();
		for ( const int32 Index : Face.Indices ) Loop.push_back( Index );
		FaceArray.push_back( Loop );
		bAnyMaterial = bAnyMaterial || Face.Material != 0;
	}
	Json["Faces"] = FaceArray;
	if ( bAnyMaterial )
	{
		NlohmannJson FaceMaterials = NlohmannJson::array();
		for ( const FEditMeshFace& Face : Faces ) FaceMaterials.push_back( Face.Material );
		Json["FaceMaterials"] = FaceMaterials;
	}
	if ( !Materials.IsEmpty() )
	{
		NlohmannJson MaterialArray = NlohmannJson::array();
		for ( const FEditMeshMaterial& Material : Materials ) MaterialArray.push_back( MaterialToJson( Material ) );
		Json["Materials"] = MaterialArray;
	}
	if ( SmoothAngle > 0.0f ) Json["Smooth"] = NJson::Round4( SmoothAngle );
	return Json;
}

bool FEditMesh::FromJson( const NlohmannJson& Json, FEditMesh& Out, TArray<FString>* Warnings )
{
	if ( !Json.is_object() ) return false;
	FEditMesh Mesh;

	if ( Json.contains( "Vertices" ) && Json["Vertices"].is_array() )
	{
		int32 NumBad = 0;
		for ( const NlohmannJson& Item : Json["Vertices"] )
		{
			FVector3 V = FVector3::Zero;
			bool bOk = Item.is_array() && Item.size() >= 3;
			if ( bOk )
			{
				try
				{
					V = FVector3( Item[0].get<float32>(), Item[1].get<float32>(), Item[2].get<float32>() );
				}
				catch ( const nlohmann::json::exception& )
				{
					bOk = false;
				}
			}
			NumBad += bOk ? 0 : 1;
			Mesh.Vertices.Add( V );   // 番号がずれないよう、壊れた頂点は原点として残す（検証が、面の問題として見つけられるように）
		}
		if ( NumBad > 0 ) Warn( Warnings, FString::Printf( "Mesh: %d vertex(es) are not [x, y, z] (placed at the origin)", NumBad ) );
	}
	else if ( Json.contains( "Vertices" ) ) Warn( Warnings, "Mesh: 'Vertices' is not an array" );

	if ( Json.contains( "Faces" ) && Json["Faces"].is_array() )
	{
		int32 NumBad = 0;
		for ( const NlohmannJson& Item : Json["Faces"] )
		{
			FEditMeshFace Face;
			bool bOk = Item.is_array();
			if ( bOk )
			{
				for ( const NlohmannJson& Index : Item )
				{
					if ( Index.is_number_integer() ) Face.Indices.Add( Index.get<int32>() );
					else bOk = false;
				}
			}
			NumBad += bOk ? 0 : 1;
			Mesh.Faces.Add( Face );   // 番号がずれないよう、壊れた面も残す（検証が見つける）
		}
		if ( NumBad > 0 ) Warn( Warnings, FString::Printf( "Mesh: %d face(s) are not arrays of integers", NumBad ) );
	}
	else if ( Json.contains( "Faces" ) ) Warn( Warnings, "Mesh: 'Faces' is not an array" );

	if ( Json.contains( "FaceMaterials" ) && Json["FaceMaterials"].is_array() )
	{
		const NlohmannJson& Numbers = Json["FaceMaterials"];
		if ( Numbers.size() != static_cast<size_t>( Mesh.Faces.Num() ) ) Warn( Warnings, FString::Printf( "Mesh: 'FaceMaterials' has %d entries for %d faces (the missing ones use material 0)", static_cast<int32>( Numbers.size() ), Mesh.Faces.Num() ) );
		for ( int32 i = 0; i < Mesh.Faces.Num() && i < static_cast<int32>( Numbers.size() ); ++i )
		{
			if ( Numbers[static_cast<size_t>( i )].is_number_integer() ) Mesh.Faces[i].Material = Numbers[static_cast<size_t>( i )].get<int32>();
		}
	}

	if ( Json.contains( "Materials" ) && Json["Materials"].is_array() )
	{
		int32 Index = 0;
		for ( const NlohmannJson& Item : Json["Materials"] ) Mesh.Materials.Add( MaterialFromJson( Item, Warnings, Index++ ) );
	}
	Mesh.SmoothAngle = NJson::GetOr<float32>( Json, "Smooth", 0.0f );

	Out = MoveTemp( Mesh );
	return true;
}
