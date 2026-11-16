// SPDX-License-Identifier: MIT
#include "PmxImporter.h"

#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// 読み込みの上限（壊れたファイルに、大きな確保をさせない）。
	constexpr int32 MaxVertices = 4 * 1024 * 1024;
	constexpr int32 MaxIndices = 12 * 1024 * 1024;
	constexpr int32 MaxCount = 1024 * 1024;

	// 範囲を確かめながら、ファイルの中身を前から読む。読めなかったら、以降の読み出しはすべて失敗になる。
	class CReader
	{
	public:
		CReader( const uint8* InData, usize InSize ) : Data( InData ), Size( InSize ) {}

		bool IsFailed() const { return bFailed; }
		usize GetPosition() const { return Position; }

		bool ReadBytes( void* Out, usize Count )
		{
			if ( bFailed || Count > Size - Position )
			{
				bFailed = true;
				return false;
			}
			FMemory::Memcpy( Out, Data + Position, Count );
			Position += Count;
			return true;
		}
		bool Skip( usize Count )
		{
			if ( bFailed || Count > Size - Position )
			{
				bFailed = true;
				return false;
			}
			Position += Count;
			return true;
		}
		uint8 ReadU8()
		{
			uint8 Value = 0;
			ReadBytes( &Value, 1 );
			return Value;
		}
		uint16 ReadU16()
		{
			uint16 Value = 0;
			ReadBytes( &Value, 2 );
			return Value;
		}
		int32 ReadI32()
		{
			int32 Value = 0;
			ReadBytes( &Value, 4 );
			return Value;
		}
		float32 ReadFloat()
		{
			float32 Value = 0.0f;
			ReadBytes( &Value, 4 );
			return Value;
		}
		FVector3 ReadVector3()
		{
			const float32 X = ReadFloat();
			const float32 Y = ReadFloat();
			const float32 Z = ReadFloat();
			return FVector3( X, Y, Z );
		}
		// 符号つきの番号（骨・画像・材質。-1 は「無い」）。大きさは、1・2・4 バイト。
		int32 ReadSignedIndex( int32 SizeBytes )
		{
			if ( SizeBytes == 1 )
			{
				return static_cast<int8>( ReadU8() );
			}
			if ( SizeBytes == 2 )
			{
				return static_cast<int16>( ReadU16() );
			}
			return ReadI32();
		}
		// 符号なしの番号（頂点。1・2 バイトは、符号なしとして読む）。
		int32 ReadVertexIndex( int32 SizeBytes )
		{
			if ( SizeBytes == 1 )
			{
				return ReadU8();
			}
			if ( SizeBytes == 2 )
			{
				return ReadU16();
			}
			return ReadI32();
		}
		// 文字列（長さ 4 バイト + 中身）。bUtf16 が true なら UTF-16LE、false なら UTF-8。結果は UTF-8。
		FString ReadText( bool bUtf16 )
		{
			const int32 Length = ReadI32();
			if ( bFailed || Length < 0 || static_cast<usize>( Length ) > Size - Position )
			{
				bFailed = true;
				return FString();
			}
			const uint8* Bytes = Data + Position;
			Position += static_cast<usize>( Length );
			TArray<char> Out;
			if ( !bUtf16 )
			{
				for ( int32 Index = 0; Index < Length; ++Index )
				{
					Out.Add( static_cast<char>( Bytes[Index] ) );
				}
			}
			else
			{
				for ( int32 Index = 0; Index + 1 < Length; Index += 2 )
				{
					uint32 Unit = static_cast<uint32>( Bytes[Index] ) | ( static_cast<uint32>( Bytes[Index + 1] ) << 8 );
					if ( Unit >= 0xD800 && Unit < 0xDC00 && Index + 3 < Length ) // 上位サロゲート。下位が続けば、1 つの文字にする。
					{
						const uint32 Low = static_cast<uint32>( Bytes[Index + 2] ) | ( static_cast<uint32>( Bytes[Index + 3] ) << 8 );
						if ( Low >= 0xDC00 && Low < 0xE000 )
						{
							Unit = 0x10000 + ( ( Unit - 0xD800 ) << 10 ) + ( Low - 0xDC00 );
							Index += 2;
						}
					}
					if ( Unit < 0x80 )
					{
						Out.Add( static_cast<char>( Unit ) );
					}
					else if ( Unit < 0x800 )
					{
						Out.Add( static_cast<char>( 0xC0 | ( Unit >> 6 ) ) );
						Out.Add( static_cast<char>( 0x80 | ( Unit & 0x3F ) ) );
					}
					else if ( Unit < 0x10000 )
					{
						Out.Add( static_cast<char>( 0xE0 | ( Unit >> 12 ) ) );
						Out.Add( static_cast<char>( 0x80 | ( ( Unit >> 6 ) & 0x3F ) ) );
						Out.Add( static_cast<char>( 0x80 | ( Unit & 0x3F ) ) );
					}
					else
					{
						Out.Add( static_cast<char>( 0xF0 | ( Unit >> 18 ) ) );
						Out.Add( static_cast<char>( 0x80 | ( ( Unit >> 12 ) & 0x3F ) ) );
						Out.Add( static_cast<char>( 0x80 | ( ( Unit >> 6 ) & 0x3F ) ) );
						Out.Add( static_cast<char>( 0x80 | ( Unit & 0x3F ) ) );
					}
				}
			}
			Out.Add( '\0' );
			return FString( Out.GetData() );
		}

	private:
		const uint8* Data = nullptr;
		usize Size = 0;
		usize Position = 0;
		bool bFailed = false;
	};

	bool IsFiniteVector( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}

	// 移動だけの行列（行ベクトル規約。移動は 4 行目）。
	FImportMatrix MakeTranslation( const FVector3& Offset )
	{
		FImportMatrix Result;
		Result.M[12] = Offset.x;
		Result.M[13] = Offset.y;
		Result.M[14] = Offset.z;
		return Result;
	}

	// PMX の画像のパスを、/ 区切りにする。
	FString NormalizePath( const FString& Path )
	{
		FString Result = Path;
		Result.ReplaceInline( "\\", "/" );
		return Result;
	}

	// 頂点 1 つの、骨 4 本までの影響。
	struct FPmxWeights
	{
		int32 Bones[4] = { -1, -1, -1, -1 };
		float32 Weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		int32 Count = 0;
	};
}

bool NModelImport::ImportPmx( const void* Data, usize Size, const FImportOptions& Options, FImportedScene& OutScene, FString& OutError )
{
	OutScene = FImportedScene();
	OutError.Empty();
	if ( Data == nullptr || Size < 9 )
	{
		OutError = "the file is too short for a PMX file";
		return false;
	}
	CReader Reader( static_cast<const uint8*>( Data ), Size );
	char Signature[4] = {};
	Reader.ReadBytes( Signature, 4 );
	if ( Signature[0] != 'P' || Signature[1] != 'M' || Signature[2] != 'X' )
	{
		OutError = "the file does not start with PMX";
		return false;
	}
	const float32 Version = Reader.ReadFloat();
	if ( !( Version >= 2.0f && Version < 3.0f ) )
	{
		OutError = FString::Printf( "unsupported PMX version %.2f (2.0 and 2.1 are supported)", Version );
		return false;
	}
	const int32 NumGlobals = Reader.ReadU8();
	if ( NumGlobals < 8 )
	{
		OutError = "the PMX header has too few global settings";
		return false;
	}
	uint8 Globals[8] = {};
	for ( int32 Index = 0; Index < NumGlobals; ++Index )
	{
		const uint8 Value = Reader.ReadU8();
		if ( Index < 8 )
		{
			Globals[Index] = Value;
		}
	}
	const bool bUtf16 = Globals[0] == 0;
	const int32 NumAdditionalUV = Globals[1];
	const int32 VertexIndexSize = Globals[2];
	const int32 TextureIndexSize = Globals[3];
	const int32 BoneIndexSize = Globals[5];
	for ( const int32 IndexSize : { VertexIndexSize, TextureIndexSize, static_cast<int32>( Globals[4] ), BoneIndexSize } )
	{
		if ( IndexSize != 1 && IndexSize != 2 && IndexSize != 4 )
		{
			OutError = "the PMX header has an invalid index size";
			return false;
		}
	}
	if ( NumAdditionalUV > 4 )
	{
		OutError = "the PMX header has too many additional UV sets";
		return false;
	}
	for ( int32 Index = 0; Index < 4; ++Index ) // モデル名・コメント（日本語・英語）。
	{
		Reader.ReadText( bUtf16 );
	}

	// 頂点
	const int32 NumVertices = Reader.ReadI32();
	if ( Reader.IsFailed() || NumVertices <= 0 || NumVertices > MaxVertices )
	{
		OutError = "the PMX file has an invalid number of vertices";
		return false;
	}
	FImportedMesh Mesh;
	Mesh.Name = FString( "Model" );
	Mesh.Positions.Reserve( NumVertices );
	Mesh.Normals.Reserve( NumVertices );
	Mesh.UVs.Reserve( NumVertices );
	TArray<FPmxWeights> Weights;
	Weights.Reserve( NumVertices );
	for ( int32 Index = 0; Index < NumVertices; ++Index )
	{
		const FVector3 Position = Reader.ReadVector3();
		const FVector3 Normal = Reader.ReadVector3();
		const float32 U = Reader.ReadFloat();
		const float32 V = Reader.ReadFloat();
		Reader.Skip( static_cast<usize>( NumAdditionalUV ) * 16 );
		FPmxWeights Influence;
		const uint8 Type = Reader.ReadU8();
		if ( Type == 0 ) // BDEF1
		{
			Influence.Bones[0] = Reader.ReadSignedIndex( BoneIndexSize );
			Influence.Weights[0] = 1.0f;
			Influence.Count = 1;
		}
		else if ( Type == 1 || Type == 3 ) // BDEF2 / SDEF（SDEF は、BDEF2 として扱う）
		{
			Influence.Bones[0] = Reader.ReadSignedIndex( BoneIndexSize );
			Influence.Bones[1] = Reader.ReadSignedIndex( BoneIndexSize );
			Influence.Weights[0] = Reader.ReadFloat();
			Influence.Weights[1] = 1.0f - Influence.Weights[0];
			Influence.Count = 2;
			if ( Type == 3 )
			{
				Reader.Skip( 36 ); // C / R0 / R1
			}
		}
		else if ( Type == 2 || Type == 4 ) // BDEF4 / QDEF
		{
			for ( int32 Slot = 0; Slot < 4; ++Slot )
			{
				Influence.Bones[Slot] = Reader.ReadSignedIndex( BoneIndexSize );
			}
			for ( int32 Slot = 0; Slot < 4; ++Slot )
			{
				Influence.Weights[Slot] = Reader.ReadFloat();
			}
			Influence.Count = 4;
		}
		else
		{
			OutError = FString::Printf( "the PMX vertex %d has an unknown weight type %d", Index, static_cast<int32>( Type ) );
			return false;
		}
		Reader.ReadFloat(); // 輪郭の太さ
		if ( Reader.IsFailed() )
		{
			OutError = "the PMX file ends inside the vertices";
			return false;
		}
		if ( !IsFiniteVector( Position ) || !IsFiniteVector( Normal ) || !NMath::IsFinite( U ) || !NMath::IsFinite( V ) )
		{
			OutError = FString::Printf( "the PMX vertex %d has a non-finite value", Index );
			return false;
		}
		Mesh.Positions.Add( Position );
		Mesh.Normals.Add( Normal );
		Mesh.UVs.Add( FVector2( U, V ) );
		Weights.Add( Influence );
	}

	// 面（三角形の頂点の番号）
	const int32 NumIndices = Reader.ReadI32();
	if ( Reader.IsFailed() || NumIndices <= 0 || NumIndices > MaxIndices || NumIndices % 3 != 0 )
	{
		OutError = "the PMX file has an invalid number of face indices";
		return false;
	}
	Mesh.Indices.Reserve( NumIndices );
	for ( int32 Index = 0; Index < NumIndices; ++Index )
	{
		const int32 Vertex = Reader.ReadVertexIndex( VertexIndexSize );
		if ( Reader.IsFailed() || Vertex < 0 || Vertex >= NumVertices )
		{
			OutError = "the PMX file has a face index out of range";
			return false;
		}
		Mesh.Indices.Add( Vertex );
	}

	// 画像
	const int32 NumTextures = Reader.ReadI32();
	if ( Reader.IsFailed() || NumTextures < 0 || NumTextures > MaxCount )
	{
		OutError = "the PMX file has an invalid number of textures";
		return false;
	}
	for ( int32 Index = 0; Index < NumTextures; ++Index )
	{
		FImportedTexture Texture;
		Texture.File = NormalizePath( Reader.ReadText( bUtf16 ) );
		OutScene.Textures.Add( Texture );
	}

	// 材質。面は、材質の順に並んでいる（材質ごとの index の数）。
	const int32 NumMaterials = Reader.ReadI32();
	if ( Reader.IsFailed() || NumMaterials < 0 || NumMaterials > MaxCount )
	{
		OutError = "the PMX file has an invalid number of materials";
		return false;
	}
	int64 MaterialIndexTotal = 0;
	for ( int32 Index = 0; Index < NumMaterials; ++Index )
	{
		FImportedMaterial Material;
		Material.Name = Reader.ReadText( bUtf16 );
		Reader.ReadText( bUtf16 );
		for ( int32 Slot = 0; Slot < 4; ++Slot )
		{
			Material.Diffuse[Slot] = Reader.ReadFloat();
		}
		for ( int32 Slot = 0; Slot < 3; ++Slot )
		{
			Material.Specular[Slot] = Reader.ReadFloat();
		}
		Material.Power = Reader.ReadFloat();
		Reader.Skip( 12 ); // 環境色
		Reader.ReadU8();   // 描画のフラグ（両面・影・輪郭）
		Reader.Skip( 20 ); // 輪郭の色と太さ
		const int32 Texture = Reader.ReadSignedIndex( TextureIndexSize );
		Reader.ReadSignedIndex( TextureIndexSize ); // スフィア
		Reader.ReadU8();                            // スフィアの方式
		const uint8 SharedToon = Reader.ReadU8();
		if ( SharedToon == 0 )
		{
			Reader.ReadSignedIndex( TextureIndexSize );
		}
		else
		{
			Reader.ReadU8();
		}
		Reader.ReadText( bUtf16 ); // メモ
		const int32 Count = Reader.ReadI32();
		if ( Reader.IsFailed() || Count < 0 || Count % 3 != 0 )
		{
			OutError = FString::Printf( "the PMX material %d has an invalid face count", Index );
			return false;
		}
		Material.Texture = Texture >= 0 && Texture < NumTextures ? Texture : -1;
		OutScene.Materials.Add( Material );
		for ( int32 Triangle = 0; Triangle < Count / 3; ++Triangle )
		{
			Mesh.TriangleMaterials.Add( Index );
		}
		MaterialIndexTotal += Count;
	}
	if ( MaterialIndexTotal != NumIndices )
	{
		OutError = "the face counts of the PMX materials do not add up to the number of face indices";
		return false;
	}

	// 骨（位置と親だけ。PMX の骨には向きが無い）
	const int32 NumBones = Reader.ReadI32();
	if ( Reader.IsFailed() || NumBones < 0 || NumBones > MaxCount )
	{
		OutError = "the PMX file has an invalid number of bones";
		return false;
	}
	TArray<FVector3> BonePositions;
	for ( int32 Index = 0; Index < NumBones; ++Index )
	{
		FImportedNode Node;
		Node.Name = Reader.ReadText( bUtf16 );
		Reader.ReadText( bUtf16 );
		const FVector3 Position = Reader.ReadVector3();
		const int32 Parent = Reader.ReadSignedIndex( BoneIndexSize );
		Reader.ReadI32(); // 変形の階層
		const uint16 Flags = Reader.ReadU16();
		if ( ( Flags & 0x0001 ) != 0 ) // 接続先: 骨の番号
		{
			Reader.ReadSignedIndex( BoneIndexSize );
		}
		else // 接続先: 位置のずれ
		{
			Reader.Skip( 12 );
		}
		if ( ( Flags & 0x0300 ) != 0 ) // 回転・移動の付与
		{
			Reader.ReadSignedIndex( BoneIndexSize );
			Reader.ReadFloat();
		}
		if ( ( Flags & 0x0400 ) != 0 ) // 軸の固定
		{
			Reader.Skip( 12 );
		}
		if ( ( Flags & 0x0800 ) != 0 ) // ローカルの軸
		{
			Reader.Skip( 24 );
		}
		if ( ( Flags & 0x2000 ) != 0 ) // 外部の親
		{
			Reader.ReadI32();
		}
		if ( ( Flags & 0x0020 ) != 0 ) // IK
		{
			Reader.ReadSignedIndex( BoneIndexSize );
			Reader.ReadI32();
			Reader.ReadFloat();
			const int32 NumLinks = Reader.ReadI32();
			if ( Reader.IsFailed() || NumLinks < 0 || NumLinks > MaxCount )
			{
				OutError = FString::Printf( "the PMX bone %d has an invalid IK link count", Index );
				return false;
			}
			for ( int32 Link = 0; Link < NumLinks; ++Link )
			{
				Reader.ReadSignedIndex( BoneIndexSize );
				if ( Reader.ReadU8() != 0 )
				{
					Reader.Skip( 24 );
				}
			}
		}
		if ( Reader.IsFailed() )
		{
			OutError = "the PMX file ends inside the bones";
			return false;
		}
		if ( !IsFiniteVector( Position ) || Parent < -1 || Parent >= NumBones || Parent == Index )
		{
			OutError = FString::Printf( "the PMX bone %d has an invalid position or parent", Index );
			return false;
		}
		Node.Parent = Parent;
		BonePositions.Add( Position );
		OutScene.Nodes.Add( Node );
	}
	for ( int32 Index = 0; Index < NumBones; ++Index ) // 親から見た移動（親は、後ろにあってもよい）。
	{
		const int32 Parent = OutScene.Nodes[Index].Parent;
		OutScene.Nodes[Index].Local = MakeTranslation( Parent < 0 ? BonePositions[Index] : BonePositions[Index] - BonePositions[Parent] );
	}
	if ( NumBones == 0 ) // 骨の無いモデルは、メッシュの付け先に、原点の骨を 1 つ置く。
	{
		FImportedNode Root;
		Root.Name = FString( "Root" );
		OutScene.Nodes.Add( Root );
	}
	Mesh.Node = 0;

	// スキン: 骨ごとのクラスター（メッシュの座標から、バインドの骨の座標へ = 骨の位置の逆の移動）。
	if ( NumBones > 0 )
	{
		TArray<int32> BoneToCluster;
		BoneToCluster.SetNum( NumBones, -1 );
		const int32 MaxInfluences = Options.MaxSkinInfluences > 0 ? Options.MaxSkinInfluences : 4;
		for ( int32 Vertex = 0; Vertex < NumVertices; ++Vertex )
		{
			const FPmxWeights& Source = Weights[Vertex];
			TArray<FImportInfluence> Influences;
			for ( int32 Slot = 0; Slot < Source.Count; ++Slot )
			{
				if ( Source.Bones[Slot] < 0 || Source.Bones[Slot] >= NumBones || !NMath::IsFinite( Source.Weights[Slot] ) )
				{
					continue; // 骨の無い影響・壊れた重みは、落とす。
				}
				FImportInfluence Influence;
				Influence.Cluster = Source.Bones[Slot]; // ここでは、骨の番号。あとでクラスターの番号にする。
				Influence.Weight = Source.Weights[Slot];
				Influences.Add( Influence );
			}
			NImportedScene::LimitInfluences( Influences, MaxInfluences );
			if ( Influences.IsEmpty() ) // 影響する骨が無い頂点は、先頭の骨に付ける（動かない）。
			{
				FImportInfluence Fallback;
				Fallback.Cluster = 0;
				Fallback.Weight = 1.0f;
				Influences.Add( Fallback );
			}
			for ( const FImportInfluence& Influence : Influences )
			{
				int32& ClusterIndex = BoneToCluster[Influence.Cluster];
				if ( ClusterIndex < 0 )
				{
					FImportedCluster Cluster;
					Cluster.BoneNode = Influence.Cluster;
					Cluster.Offset = MakeTranslation( BonePositions[Influence.Cluster] * -1.0f );
					ClusterIndex = Mesh.Clusters.Add( Cluster );
				}
				Mesh.Clusters[ClusterIndex].Vertices.Add( Vertex );
				Mesh.Clusters[ClusterIndex].Weights.Add( Influence.Weight );
			}
		}
	}
	OutScene.Meshes.Add( MoveTemp( Mesh ) );
	OutScene.bLeftHanded = true; // PMX は、DxLib と同じ左手系。座標・向きは、そのまま使う。
	const TArray<FString> Problems = NImportedScene::Validate( OutScene );
	if ( !Problems.IsEmpty() )
	{
		OutError = FString::Printf( "the PMX file gives an invalid scene: %s", *Problems[0] );
		return false;
	}
	return true;
}
