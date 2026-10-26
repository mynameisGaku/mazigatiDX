#include "FbxImporter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "VendorModules/ufbx/ufbx.h"

#include "Library/Core/Types/Numeric.h"

namespace
{
	FImportMatrix ToImportMatrix( const ufbx_matrix& Source )
	{
		// ufbx は列ベクトル（列が基底ベクトル・4 列目が移動）。DirectX は行ベクトル（行が基底ベクトル・4 行目が移動）なので、そのまま転置になる
		FImportMatrix Result;
		for ( int32 Column = 0; Column < 4; ++Column )
		{
			Result.M[Column * 4 + 0] = static_cast<float32>( Source.cols[Column].x );
			Result.M[Column * 4 + 1] = static_cast<float32>( Source.cols[Column].y );
			Result.M[Column * 4 + 2] = static_cast<float32>( Source.cols[Column].z );
			Result.M[Column * 4 + 3] = Column == 3 ? 1.0f : 0.0f;
		}
		return Result;
	}

	FString ToFString( const ufbx_string& Text )
	{
		return Text.data != nullptr && Text.length > 0 ? FString::FromUtf8( Text.data ) : FString();
	}

	// ディレクトリ部分を除いたファイル名
	FString GetFileNameOnly( const FString& Path )
	{
		const int32 Separator = NMath::Max( Path.FindLast( "/" ), Path.FindLast( "\\" ) );
		return Separator == INDEX_NONE ? Path : Path.Mid( Separator + 1 );
	}

	struct FCornerKey
	{
		uint32_t Position;
		uint32_t Normal;
		uint32_t UV;
		bool operator==( const FCornerKey& Other ) const { return Position == Other.Position && Normal == Other.Normal && UV == Other.UV; }
	};

	struct FCornerKeyHash
	{
		size_t operator()( const FCornerKey& Key ) const
		{
			size_t Hash = static_cast<size_t>( Key.Position ) * 73856093u;
			Hash ^= static_cast<size_t>( Key.Normal ) * 19349663u;
			Hash ^= static_cast<size_t>( Key.UV ) * 83492791u;
			return Hash;
		}
	};

	class FSceneBuilder
	{
	public:
		FSceneBuilder( const ufbx_scene& InSource, const FImportOptions& InOptions, FImportedScene& InOut )
			: Source( InSource )
			, Options( InOptions )
			, Out( InOut )
		{
		}

		void Build()
		{
			BuildNodes();
			BuildMaterials();
			BuildMeshes();
			if ( Options.bImportAnimations ) BuildAnimations();
		}

	private:
		int32 GetNodeIndex( const ufbx_node* Node ) const { return Node != nullptr && Node->typed_id < NodeMap.size() ? NodeMap[Node->typed_id] : -1; }

		void BuildNodes()
		{
			NodeMap.assign( Source.nodes.count, -1 );
			for ( size_t i = 0; i < Source.nodes.count; ++i )
			{
				const ufbx_node* Node = Source.nodes.data[i];
				if ( Node->is_root ) continue;   // 根は変換を持たない（空の入れ物）
				NodeMap[i] = Out.Nodes.Num();
				FImportedNode Imported;
				Imported.Name = ToFString( Node->name );
				Imported.Local = ToImportMatrix( Node->node_to_parent );
				Out.Nodes.Add( Imported );
			}
			for ( size_t i = 0; i < Source.nodes.count; ++i )
			{
				const ufbx_node* Node = Source.nodes.data[i];
				if ( NodeMap[i] < 0 ) continue;
				Out.Nodes[NodeMap[i]].Parent = Node->parent != nullptr && !Node->parent->is_root ? GetNodeIndex( Node->parent ) : -1;
			}
		}

		int32 AddTexture( const ufbx_texture* Texture )
		{
			if ( Texture == nullptr ) return -1;
			// 重ね合わせなどの合成テクスチャは、中の最初のファイルを使う
			if ( Texture->type != UFBX_TEXTURE_FILE && Texture->file_textures.count > 0 ) Texture = Texture->file_textures.data[0];
			if ( Texture->type != UFBX_TEXTURE_FILE ) return -1;

			if ( const auto Found = TextureMap.find( Texture->typed_id ); Found != TextureMap.end() ) return Found->second;

			FImportedTexture Imported;
			FString File = ToFString( Texture->relative_filename );
			if ( File.IsEmpty() ) File = ToFString( Texture->filename );
			if ( File.IsEmpty() ) File = ToFString( Texture->absolute_filename );
			if ( Texture->content.size > 0 )
			{
				const uint8* Bytes = static_cast<const uint8*>( Texture->content.data );
				Imported.EmbeddedData = TArray<uint8>( Bytes, static_cast<int32>( Texture->content.size ) );
				File = GetFileNameOnly( File );   // 埋め込みは、フォルダを付けずファイル名にする
				if ( File.IsEmpty() ) File = FString::Printf( "texture%d.png", Out.Textures.Num() );
			}
			if ( File.IsEmpty() ) return -1;
			Imported.File = File;
			const int32 Index = Out.Textures.Add( Imported );
			TextureMap[Texture->typed_id] = Index;
			return Index;
		}

		void BuildMaterials()
		{
			for ( size_t i = 0; i < Source.materials.count; ++i )
			{
				const ufbx_material* Material = Source.materials.data[i];
				FImportedMaterial Imported;
				Imported.Name = ToFString( Material->name );

				const ufbx_material_map& Base = Material->pbr.base_color.has_value || Material->pbr.base_color.texture != nullptr ? Material->pbr.base_color : Material->fbx.diffuse_color;
				if ( Base.has_value )
				{
					Imported.Diffuse[0] = static_cast<float32>( Base.value_vec4.x );
					Imported.Diffuse[1] = static_cast<float32>( Base.value_vec4.y );
					Imported.Diffuse[2] = static_cast<float32>( Base.value_vec4.z );
					Imported.Diffuse[3] = 1.0f;
				}
				Imported.Texture = AddTexture( Base.texture );

				if ( Material->fbx.specular_color.has_value )
				{
					const float32 Factor = Material->fbx.specular_factor.has_value ? static_cast<float32>( Material->fbx.specular_factor.value_real ) : 1.0f;
					Imported.Specular[0] = static_cast<float32>( Material->fbx.specular_color.value_vec3.x ) * Factor;
					Imported.Specular[1] = static_cast<float32>( Material->fbx.specular_color.value_vec3.y ) * Factor;
					Imported.Specular[2] = static_cast<float32>( Material->fbx.specular_color.value_vec3.z ) * Factor;
				}
				if ( Material->fbx.specular_exponent.has_value ) Imported.Power = static_cast<float32>( Material->fbx.specular_exponent.value_real );
				if ( Material->fbx.emission_color.has_value )
				{
					const float32 Factor = Material->fbx.emission_factor.has_value ? static_cast<float32>( Material->fbx.emission_factor.value_real ) : 1.0f;
					Imported.Emissive[0] = static_cast<float32>( Material->fbx.emission_color.value_vec3.x ) * Factor;
					Imported.Emissive[1] = static_cast<float32>( Material->fbx.emission_color.value_vec3.y ) * Factor;
					Imported.Emissive[2] = static_cast<float32>( Material->fbx.emission_color.value_vec3.z ) * Factor;
				}
				Out.Materials.Add( Imported );
			}
		}

		void BuildMeshes()
		{
			for ( size_t NodeIndex = 0; NodeIndex < Source.nodes.count; ++NodeIndex )
			{
				const ufbx_node* Node = Source.nodes.data[NodeIndex];
				if ( Node->mesh == nullptr || NodeMap[NodeIndex] < 0 ) continue;
				BuildMesh( *Node, *Node->mesh, NodeMap[NodeIndex] );
			}
		}

		void BuildMesh( const ufbx_node& Node, const ufbx_mesh& Mesh, int32 ImportedNode )
		{
			if ( Mesh.num_triangles == 0 || !Mesh.vertex_position.exists ) return;

			FImportedMesh Imported;
			Imported.Name = ToFString( Node.name );
			Imported.Node = ImportedNode;

			const ufbx_skin_deformer* Skin = Mesh.skin_deformers.count > 0 ? Mesh.skin_deformers.data[0] : nullptr;
			const bool bHasUV = Mesh.vertex_uv.exists;
			const bool bHasNormal = Mesh.vertex_normal.exists;

			// 頂点ごとの骨の影響（元の頂点の番号ごと。多すぎる分は切って、合計を 1 にしてある）
			std::vector<TArray<FImportInfluence>> Influences;
			if ( Skin != nullptr )
			{
				Influences.resize( Mesh.num_vertices );
				for ( size_t Vertex = 0; Vertex < Mesh.num_vertices && Vertex < Skin->vertices.count; ++Vertex )
				{
					const ufbx_skin_vertex& SkinVertex = Skin->vertices.data[Vertex];
					for ( uint32_t k = 0; k < SkinVertex.num_weights; ++k )
					{
						const ufbx_skin_weight& Weight = Skin->weights.data[SkinVertex.weight_begin + k];
						Influences[Vertex].Add( FImportInfluence { static_cast<int32>( Weight.cluster_index ), static_cast<float32>( Weight.weight ) } );
					}
					NImportedScene::LimitInfluences( Influences[Vertex] );
				}
			}

			std::unordered_map<FCornerKey, int32, FCornerKeyHash> VertexMap;
			std::vector<uint32_t> ControlPoints;   // 出力の頂点ごとの、元の頂点の番号
			std::vector<uint32_t> Triangles( NMath::Max<size_t>( Mesh.max_face_triangles * 3, 3 ) );

			for ( size_t FaceIndex = 0; FaceIndex < Mesh.faces.count; ++FaceIndex )
			{
				const ufbx_face Face = Mesh.faces.data[FaceIndex];
				if ( Face.num_indices < 3 ) continue;
				const uint32_t NumTriangles = ufbx_triangulate_face( Triangles.data(), Triangles.size(), &Mesh, Face );

				int32 Material = -1;
				if ( Mesh.face_material.count > FaceIndex )
				{
					const uint32_t Slot = Mesh.face_material.data[FaceIndex];
					const ufbx_material* Used = nullptr;
					if ( Node.materials.count > Slot ) Used = Node.materials.data[Slot];
					else if ( Mesh.materials.count > Slot ) Used = Mesh.materials.data[Slot];
					if ( Used != nullptr ) Material = static_cast<int32>( Used->typed_id );
				}

				for ( uint32_t Triangle = 0; Triangle < NumTriangles; ++Triangle )
				{
					for ( int32 Corner = 0; Corner < 3; ++Corner )
					{
						const uint32_t Index = Triangles[Triangle * 3 + Corner];
						const FCornerKey Key { Mesh.vertex_position.indices.data[Index], bHasNormal ? Mesh.vertex_normal.indices.data[Index] : 0u, bHasUV ? Mesh.vertex_uv.indices.data[Index] : 0u };
						auto Found = VertexMap.find( Key );
						if ( Found == VertexMap.end() )
						{
							const ufbx_vec3 Position = Mesh.vertex_position.values.data[Key.Position];
							Imported.Positions.Add( FVector3( static_cast<float32>( Position.x ), static_cast<float32>( Position.y ), static_cast<float32>( Position.z ) ) );
							if ( bHasNormal )
							{
								const ufbx_vec3 Normal = Mesh.vertex_normal.values.data[Key.Normal];
								Imported.Normals.Add( FVector3( static_cast<float32>( Normal.x ), static_cast<float32>( Normal.y ), static_cast<float32>( Normal.z ) ) );
							}
							else
							{
								Imported.Normals.Add( FVector3( 0.0f, 1.0f, 0.0f ) );
							}
							if ( bHasUV )
							{
								const ufbx_vec2 UV = Mesh.vertex_uv.values.data[Key.UV];
								Imported.UVs.Add( FVector2( static_cast<float32>( UV.x ), 1.0f - static_cast<float32>( UV.y ) ) );   // FBX は下が 0、DirectX は上が 0
							}
							ControlPoints.push_back( Mesh.vertex_indices.data[Index] );
							Found = VertexMap.emplace( Key, Imported.Positions.Num() - 1 ).first;
						}
						Imported.Indices.Add( Found->second );
					}
					Imported.TriangleMaterials.Add( Material );
				}
			}
			if ( Imported.Indices.IsEmpty() ) return;

			if ( Skin != nullptr )
			{
				for ( size_t ClusterIndex = 0; ClusterIndex < Skin->clusters.count; ++ClusterIndex )
				{
					const ufbx_skin_cluster* Cluster = Skin->clusters.data[ClusterIndex];
					FImportedCluster Imported_Cluster;
					Imported_Cluster.BoneNode = GetNodeIndex( Cluster->bone_node );
					Imported_Cluster.Offset = ToImportMatrix( Cluster->geometry_to_bone );
					Imported.Clusters.Add( Imported_Cluster );
				}
				for ( int32 Vertex = 0; Vertex < Imported.Positions.Num(); ++Vertex )
				{
					for ( const FImportInfluence& Influence : Influences[ControlPoints[Vertex]] )
					{
						if ( !Imported.Clusters.IsValidIndex( Influence.Cluster ) ) continue;
						Imported.Clusters[Influence.Cluster].Vertices.Add( Vertex );
						Imported.Clusters[Influence.Cluster].Weights.Add( Influence.Weight );
					}
				}
				// 骨が見つからなかった・頂点が 1 つもないクラスターは捨てる
				Imported.Clusters.RemoveAll( []( const FImportedCluster& Cluster ) { return Cluster.BoneNode < 0 || Cluster.Vertices.IsEmpty(); } );
			}
			Out.Meshes.Add( Imported );
		}

		void BuildAnimations()
		{
			const double Fps = static_cast<double>( NMath::Max( Options.AnimFps, 1.0f ) );
			for ( size_t StackIndex = 0; StackIndex < Source.anim_stacks.count; ++StackIndex )
			{
				const ufbx_anim_stack* Stack = Source.anim_stacks.data[StackIndex];
				const double Begin = Stack->time_begin, End = Stack->time_end;
				if ( !( End > Begin ) || Stack->anim == nullptr ) continue;

				FImportedAnimation Animation;
				Animation.Name = ToFString( Stack->name );
				Animation.Fps = static_cast<float32>( Fps );
				Animation.NumKeys = NMath::Max( static_cast<int32>( std::floor( ( End - Begin ) * Fps + 0.5 ) ) + 1, 2 );

				for ( size_t NodeIndex = 0; NodeIndex < Source.nodes.count; ++NodeIndex )
				{
					const ufbx_node* Node = Source.nodes.data[NodeIndex];
					const int32 Imported = NodeMap[NodeIndex];
					if ( Imported < 0 ) continue;

					FImportedTrack Track;
					Track.Node = Imported;
					bool bAnimated = false;
					for ( int32 Key = 0; Key < Animation.NumKeys; ++Key )
					{
						const double Time = NMath::Min( Begin + static_cast<double>( Key ) / Fps, End );
						const ufbx_transform Transform = ufbx_evaluate_transform( Stack->anim, Node, Time );
						const ufbx_matrix Matrix = ufbx_transform_to_matrix( &Transform );
						Track.Keys.Add( ToImportMatrix( Matrix ) );
						if ( !Track.Keys.Last().NearlyEquals( Out.Nodes[Imported].Local, 1.0e-5f ) ) bAnimated = true;
					}
					if ( bAnimated ) Animation.Tracks.Add( Track );
				}
				Out.Animations.Add( Animation );
			}
		}

		const ufbx_scene& Source;
		const FImportOptions& Options;
		FImportedScene& Out;
		std::vector<int32> NodeMap;                 // ufbx のノードの番号 -> FImportedScene::Nodes の番号（根は -1）
		std::unordered_map<uint32_t, int32> TextureMap;
	};
}

bool NModelImport::ImportFromMemory( const void* Data, usize Size, const FString& FileName, const FImportOptions& Options, FImportedScene& OutScene, FString& OutError )
{
	OutScene = FImportedScene();
	OutError.Empty();
	if ( Data == nullptr || Size == 0 )
	{
		OutError = "the file is empty";
		return false;
	}

	ufbx_load_opts LoadOptions = {};
	// Y が上の右手系に回す（FBX の上の軸が Z の場合など）。単位は変えない
	LoadOptions.target_axes = ufbx_axes_right_handed_y_up;
	LoadOptions.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
	// 単純な「移動・回転・拡大だけ」の階層にする
	LoadOptions.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
	LoadOptions.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
	LoadOptions.pivot_handling = UFBX_PIVOT_HANDLING_ADJUST_TO_ROTATION_PIVOT;
	LoadOptions.generate_missing_normals = true;
	// OBJ が参照する .mtl を読む（無くても失敗にしない）
	LoadOptions.load_external_files = true;
	LoadOptions.ignore_missing_external_files = true;
	const FString Utf8Name = FileName.ToUtf8();
	LoadOptions.filename.data = *Utf8Name;
	LoadOptions.filename.length = SIZE_MAX;

	ufbx_error Error;
	ufbx_scene* Scene = ufbx_load_memory( Data, Size, &LoadOptions, &Error );
	if ( Scene == nullptr )
	{
		char Buffer[512];
		ufbx_format_error( Buffer, sizeof( Buffer ), &Error );
		OutError = FString::FromUtf8( Buffer );
		return false;
	}

	FSceneBuilder Builder( *Scene, Options, OutScene );
	Builder.Build();
	ufbx_free_scene( Scene );
	return true;
}
