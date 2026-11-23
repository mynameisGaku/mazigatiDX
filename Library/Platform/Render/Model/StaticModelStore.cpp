// SPDX-License-Identifier: MIT
#include "StaticModelStore.h"
#include "Library/Framework/Asset/Model/Import/StaticModelBuilder.h"
#include "Library/Core/Collection/Set.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	uint8 ColorByte( float32 Value )
	{
		return static_cast<uint8>( NMath::Clamp( Value, 0.0f, 1.0f ) * 255 + 0.5f );
	}

	// 三角形 ABC の上の、点 P にいちばん近い点（Ericson, Real-Time Collision Detection）
	FVector3 ClosestPointOnTriangle( const FVector3& P, const FVector3& A, const FVector3& B, const FVector3& C )
	{
		const FVector3 AB = B - A;
		const FVector3 AC = C - A;
		const FVector3 AP = P - A;
		const float32 D1 = FVector3::Dot( AB, AP );
		const float32 D2 = FVector3::Dot( AC, AP );
		if ( D1 <= 0.0f && D2 <= 0.0f )
		{
			return A;
		}
		const FVector3 BP = P - B;
		const float32 D3 = FVector3::Dot( AB, BP );
		const float32 D4 = FVector3::Dot( AC, BP );
		if ( D3 >= 0.0f && D4 <= D3 )
		{
			return B;
		}
		const float32 VC = D1 * D4 - D3 * D2;
		if ( VC <= 0.0f && D1 >= 0.0f && D3 <= 0.0f )
		{
			return A + AB * ( D1 / ( D1 - D3 ) );
		}
		const FVector3 CP = P - C;
		const float32 D5 = FVector3::Dot( AB, CP );
		const float32 D6 = FVector3::Dot( AC, CP );
		if ( D6 >= 0.0f && D5 <= D6 )
		{
			return C;
		}
		const float32 VB = D5 * D2 - D1 * D6;
		if ( VB <= 0.0f && D2 >= 0.0f && D6 <= 0.0f )
		{
			return A + AC * ( D2 / ( D2 - D6 ) );
		}
		const float32 VA = D3 * D6 - D5 * D4;
		if ( VA <= 0.0f && ( D4 - D3 ) >= 0.0f && ( D5 - D6 ) >= 0.0f )
		{
			return B + ( C - B ) * ( ( D4 - D3 ) / ( ( D4 - D3 ) + ( D5 - D6 ) ) );
		}
		const float32 Denominator = 1.0f / ( VA + VB + VC );
		return A + AB * ( VB * Denominator ) + AC * ( VC * Denominator );
	}
}

CStaticModelStore::FResource::~FResource()
{
	if ( TextureBackend )
	{
		for ( int32 Texture : OwnedTextures )
		{
			TextureBackend->DeleteTexture( Texture );
		}
	}
}

int32 CStaticModelStore::Create( const FImportedScene& Scene, const FString& SourcePath, ITextureBackend& Textures, FString& Error, bool bAllowRig )
{
	FScopedLock Guard( Lock );
	if ( Instances.Num() >= 128 || NextHandle == TNumericLimits<int32>::Max() || NextResourceId >= ( 1ull << 62 ) )
	{
		Error = "Static model instance limit exceeded";
		return -1;
	}
	auto Resource = MakeShared<FResource>(); // 読み込み途中の所有も局所化する。
	Resource->Id = NextResourceId++; // 解放後も再利用しない識別（GPU の共有メッシュの鍵）。
	Resource->TextureBackend = &Textures;
	if ( !FStaticModelBuilder::Build( Scene, Resource->Definition, Error, bAllowRig, bAllowRig ? &Resource->VertexSources : nullptr ) )
	{
		return -1;
	}
	if ( bAllowRig )
	{
		auto Rig = MakeShared<FModelRig>();
		if ( !NModelRig::Build( Scene, *Rig, Error ) )
		{
			return -1;
		}
		for ( const FRigMesh& Mesh : Rig->Meshes )
		{
			Resource->bHasSkin = Resource->bHasSkin || Mesh.bSkinned;
		}
		Resource->Rig = Rig;
	}
	auto& Surface = Resource->Definition.Surface; // 成功前は公開しない面データ。
	Resource->Bytes = static_cast<int64>( Surface.Vertices.Num() ) * ( sizeof( FMeshVertex ) + sizeof( FDrawVertex3D ) )
		+ static_cast<int64>( Surface.Indices.Num() ) * sizeof( uint32 ) + static_cast<int64>( Surface.Sections.Num() ) * ( sizeof( FStaticModelSurfaceSection ) + sizeof( FStaticModelDrawSection ) );
	int64 TotalBytes = Resource->Bytes; // 同じ共有形状は一度だけ数える。
	TSet<const FResource*> Seen; // 複製分を重複加算しない。
	for ( const auto& Pair : Instances )
	{
		const FResource* Existing = Pair.Value.Resource.Get(); // 登録済みの共有形状。
		if ( !Seen.Contains( Existing ) )
		{
			Seen.Add( Existing );
			TotalBytes += Existing->Bytes;
		}
	}
	if ( TotalBytes > 64 * 1024 * 1024 )
	{
		Error = "Static model geometry memory limit exceeded";
		return -1;
	}
	TMap<int32, int32> LoadedTextures; // 同じ元画像を一度だけ読み込む。
	for ( const auto& Section : Surface.Sections )
	{
		if ( Section.MaterialIndex >= Scene.Materials.Num() )
		{
			continue; // 材質未指定用の白。
		}
		const int32 TextureIndex = Scene.Materials[Section.MaterialIndex].Texture; // 元の拡散画像番号。
		if ( TextureIndex < 0 )
		{
			continue;
		}
		if ( TextureIndex >= Scene.Textures.Num() )
		{
			Error = "Invalid diffuse texture index";
			return -1;
		}
		int32 Texture = -1; // モデルが所有する画像の登録番号。
		if ( const int32* Existing = LoadedTextures.Find( TextureIndex ) )
		{
			Texture = *Existing;
		}
		else
		{
			const auto& Input = Scene.Textures[TextureIndex]; // 埋め込み画像か外部参照。
			if ( !Input.EmbeddedData.IsEmpty() )
			{
				Texture = Textures.CreateTextureFromMemory( Input.EmbeddedData.GetData(), Input.EmbeddedData.Num() );
			}
			else
			{
				FString Path = Input.File.Replace( "\\", "/" ); // 相対パスは元モデルのフォルダーを基準にする。
				if ( !Path.StartsWith( "/" ) && !( Path.Len() >= 2 && Path[1] == ':' ) )
				{
					const int32 Separator = NMath::Max( SourcePath.FindLast( "/" ), SourcePath.FindLast( "\\" ) ); // 元ファイルのフォルダー。
					Path = ( Separator >= 0 ? SourcePath.Left( Separator + 1 ) : FString() ) + Path;
				}
				Texture = Textures.LoadTexture( Path, false );
			}
			if ( Texture < 0 )
			{
				Error = FString( "Cannot load static model texture: " ) + Input.File;
				return -1;
			}
			Resource->OwnedTextures.Add( Texture );
			LoadedTextures.Add( TextureIndex, Texture );
		}
		Surface.Materials[Section.MaterialIndex].BorrowedDiffuseTexture = Texture;
		int32 Width = 0, Height = 0; // 画像の実寸をメタデータへ反映する。
		if ( Textures.GetTextureSize( Texture, Width, Height ) )
		{
			Resource->Definition.Structure.Textures[TextureIndex].Width = Width;
			Resource->Definition.Structure.Textures[TextureIndex].Height = Height;
		}
	}
	Resource->Vertices.SetNum( Surface.Vertices.Num() );
	for ( int32 Index = 0; Index < Surface.Vertices.Num(); ++Index )
	{
		const auto& Input = Surface.Vertices[Index]; // 材質色を含むreference頂点。
		auto& Output = Resource->Vertices[Index]; // API共通のローカル頂点。
		FMemory::Memcpy( Output.Position, Input.Position, sizeof( Input.Position ) );
		FMemory::Memcpy( Output.Normal, Input.Normal, sizeof( Input.Normal ) );
		FMemory::Memcpy( Output.Diffuse, Input.Color, sizeof( Input.Color ) );
		Output.U = Input.U;
		Output.V = Input.V;
	}
	for ( const auto& Section : Surface.Sections )
	{
		Resource->Sections.Add( { Section.FirstIndex, Section.NumIndices, Surface.Materials[Section.MaterialIndex].BorrowedDiffuseTexture } );
	}
	if ( Resource->bHasSkin )
	{
		// 骨の変形の前の頂点と、頂点ごとの骨と重みを、GPU スキニングの基準として取っておく。
		Resource->RestVertices = Resource->Vertices;
		Resource->bGpuSkin = Resource->VertexSources.Num() == Resource->RestVertices.Num()
			&& NModelRig::BuildSkinVertices( *Resource->Rig, Resource->VertexSources, Resource->SkinVertices, Resource->PaletteBase, Resource->NumPaletteBones );
		if ( Resource->bGpuSkin )
		{
			// 変形（Deform）は、骨組みの元の位置と法線から計算する。ビルダーが作った頂点は、骨の影響が無いメッシュに、節（ノード）の行列がすでにかかっていることがあるので、そのまま使うと、GPU で二重にかかる。
			for ( int32 Index = 0; Index < Resource->RestVertices.Num(); ++Index )
			{
				const FModelVertexSource& Source = Resource->VertexSources[Index]; // 範囲は、BuildSkinVertices が確かめた。
				const FRigMesh& RigMesh = Resource->Rig->Meshes[Source.Mesh];
				FDrawVertex3D& Rest = Resource->RestVertices[Index];
				const FVector3& Position = RigMesh.Positions[Source.Vertex];
				const FVector3& Normal = RigMesh.Normals[Source.Vertex];
				Rest.Position[0] = Position.x;
				Rest.Position[1] = Position.y;
				Rest.Position[2] = Position.z;
				Rest.Normal[0] = Normal.x;
				Rest.Normal[1] = Normal.y;
				Rest.Normal[2] = Normal.z;
			}
		}
		else
		{
			Resource->RestVertices.Empty();
			Resource->SkinVertices.Empty();
		}
		// スキンつきの頂点は、メッシュの座標のまま。基準の姿（アニメーション無し）に変形して、描画・面データ・外接箱の基準にする。
		TArray<FMatrix4x4> Local = Resource->Rig->BaseLocal;
		TArray<FMatrix4x4> Global;
		NModelRig::ComputeGlobals( *Resource->Rig, Local, Global );
		NModelRig::Deform( *Resource->Rig, Global, Resource->VertexSources, Resource->Vertices.GetData(), Resource->Vertices.Num(), Jobs );
		bool bFirst = true;
		for ( int32 Index = 0; Index < Resource->Vertices.Num(); ++Index )
		{
			const FDrawVertex3D& Vertex = Resource->Vertices[Index];
			FMemory::Memcpy( Surface.Vertices[Index].Position, Vertex.Position, sizeof( Vertex.Position ) );
			FMemory::Memcpy( Surface.Vertices[Index].Normal, Vertex.Normal, sizeof( Vertex.Normal ) );
			const FVector3 Position( Vertex.Position[0], Vertex.Position[1], Vertex.Position[2] );
			Resource->Definition.BoundsMin = bFirst ? Position : FVector3( NMath::Min( Resource->Definition.BoundsMin.x, Position.x ), NMath::Min( Resource->Definition.BoundsMin.y, Position.y ), NMath::Min( Resource->Definition.BoundsMin.z, Position.z ) );
			Resource->Definition.BoundsMax = bFirst ? Position : FVector3( NMath::Max( Resource->Definition.BoundsMax.x, Position.x ), NMath::Max( Resource->Definition.BoundsMax.y, Position.y ), NMath::Max( Resource->Definition.BoundsMax.z, Position.z ) );
			bFirst = false;
		}
	}
	const int32 Handle = NextHandle++; // 完成してから登録を公開する。
	FInstance Instance;
	Instance.Resource = Resource;
	if ( Resource->Rig )
	{
		Instance.Rig = MakeShared<FInstanceRig>();
		NModelRig::InitPose( *Resource->Rig, Instance.Rig->Pose );
	}
	Instances.Add( Handle, Instance );
	Error = FString();
	return Handle;
}

CStaticModelStore::FInstance* CStaticModelStore::FindInstance( int32 Handle )
{
	return Instances.Find( Handle );
}

const CStaticModelStore::FInstance* CStaticModelStore::FindInstance( int32 Handle ) const
{
	return Instances.Find( Handle );
}

int32 CStaticModelStore::Duplicate( int32 Source )
{
	FScopedLock Guard( Lock );
	const FInstance* Instance = Instances.Find( Source ); // 複写前の個体。
	if ( !Instance || Instances.Num() >= 128 || NextHandle == TNumericLimits<int32>::Max() )
	{
		return -1;
	}
	FInstance Copy = *Instance; // 表の再確保前に共有と状態を固定する。
	if ( Instance->Rig )
	{
		// ポーズは共有せず、骨の上書きと表示だけ引き継ぐ（付けたアニメーションは引き継がない）。
		Copy.Rig = MakeShared<FInstanceRig>();
		NModelRig::InitPose( *Instance->Resource->Rig, Copy.Rig->Pose );
		Copy.Rig->Pose.bOverride = Instance->Rig->Pose.bOverride;
		Copy.Rig->Pose.Override = Instance->Rig->Pose.Override;
		Copy.Rig->Pose.FrameHidden = Instance->Rig->Pose.FrameHidden;
		Copy.Rig->Pose.MeshHidden = Instance->Rig->Pose.MeshHidden;
		Copy.Rig->Pose.ShapeRate = Instance->Rig->Pose.ShapeRate;
		Copy.Rig->Pose.World = Instance->Rig->Pose.World;
	}
	const int32 Handle = NextHandle++; // 別個体の番号。
	Instances.Add( Handle, Copy );
	return Handle;
}

void CStaticModelStore::Delete( int32 Handle )
{
	FScopedLock Guard( Lock );
	Instances.Remove( Handle );
}

void CStaticModelStore::Reset()
{
	FScopedLock Guard( Lock );
	Instances.Reset();
}

int32 CStaticModelStore::Num() const
{
	FScopedLock Guard( Lock );
	return Instances.Num();
}

bool CStaticModelStore::NeedsDeform( const FInstance& Instance ) const
{
	if ( !Instance.Rig )
	{
		return false;
	}
	const FModelPose& Pose = Instance.Rig->Pose;
	for ( uint8 Flag : Pose.bOverride )
	{
		if ( Flag )
		{
			return true;
		}
	}
	for ( float32 Rate : Pose.ShapeRate )
	{
		if ( Rate != 0.0f )
		{
			return true;
		}
	}
	for ( uint8 Flag : Instance.Rig->bMaterialSet )
	{
		if ( Flag )
		{
			return true;
		}
	}
	for ( const FModelAttach& Attach : Pose.Attaches )
	{
		if ( !Attach.bUsed )
		{
			continue;
		}
		if ( Attach.Rate > 0.0f )
		{
			return true;
		}
		for ( float32 Rate : Attach.FrameRate )
		{
			if ( Rate > 0.0f )
			{
				return true;
			}
		}
	}
	return false;
}

bool CStaticModelStore::IsHidden( const FInstance& Instance, int32 SourceMesh, int32 SourceFrame ) const
{
	if ( !Instance.Rig )
	{
		return false;
	}
	const FModelPose& Pose = Instance.Rig->Pose;
	if ( SourceMesh >= 0 && SourceMesh < Pose.MeshHidden.Num() && Pose.MeshHidden[SourceMesh] )
	{
		return true;
	}
	const TArray<int32>& Parent = Instance.Resource->Rig->Parent;
	for ( int32 Steps = 0, Frame = SourceFrame; Frame >= 0 && Frame < Pose.FrameHidden.Num() && Steps < Parent.Num(); ++Steps, Frame = Parent[Frame] )
	{
		if ( Pose.FrameHidden[Frame] )
		{
			return true;
		}
	}
	return false;
}

void CStaticModelStore::EnsurePose( FInstance& Instance )
{
	FInstanceRig& Rig = *Instance.Rig;
	if ( Rig.PoseVersion == Rig.Pose.Version && !Rig.Global.IsEmpty() )
	{
		return;
	}
	NModelRig::EvaluateLocals( *Instance.Resource->Rig, Rig.Pose, Rig.Local );
	NModelRig::ComputeGlobals( *Instance.Resource->Rig, Rig.Local, Rig.Global );
	if ( Rig.MorphedVersion != Rig.Pose.ShapeVersion ) // 形状を足した位置は、割合を変えたときだけ作り直す（骨の動きでは、作り直さない）
	{
		NModelRig::EvaluateShapes( *Instance.Resource->Rig, Rig.Pose, Rig.Morphed );
		Rig.MorphedVersion = Rig.Pose.ShapeVersion;
	}
	Rig.PoseVersion = Rig.Pose.Version;
}

bool CStaticModelStore::IsDefaultGeometry( const FInstance& Instance ) const
{
	const FModelPose& Pose = Instance.Rig->Pose;
	for ( const float32 Rate : Pose.ShapeRate )
	{
		if ( Rate != 0.0f )
		{
			return false;
		}
	}
	for ( const uint8 Flag : Pose.MeshHidden )
	{
		if ( Flag )
		{
			return false;
		}
	}
	for ( const uint8 Flag : Pose.FrameHidden )
	{
		if ( Flag )
		{
			return false;
		}
	}
	return true;
}

void CStaticModelStore::CollectVisibleSections( const FInstance& Instance, TArray<uint32>& OutIndices, TArray<FStaticModelDrawSection>& OutSections ) const
{
	const FResource& Resource = *Instance.Resource;
	const auto& SourceSections = Resource.Definition.Surface.Sections;
	for ( int32 Index = 0; Index < Resource.Sections.Num() && Index < SourceSections.Num(); ++Index )
	{
		if ( IsHidden( Instance, SourceSections[Index].SourceMeshIndex, SourceSections[Index].SourceFrameIndex ) )
		{
			continue;
		}
		FStaticModelDrawSection Section = Resource.Sections[Index];
		const int32 First = Section.FirstIndex;
		Section.FirstIndex = OutIndices.Num();
		if ( Section.NumIndices > 0 )
		{
			OutIndices.Append( Resource.Definition.Surface.Indices.GetData() + First, Section.NumIndices ); // 1 つずつ足さずに、まとめて複写する。
		}
		OutSections.Add( Section );
	}
}

TSharedPtr<const CStaticModelStore::FSkinGeometry> CStaticModelStore::MakeSkinGeometry( const FInstance& Instance ) const
{
	const FResource& Resource = *Instance.Resource;
	auto Geometry = MakeShared<FSkinGeometry>(); // 完成してから公開する。
	Geometry->Resource = Instance.Resource;
	Geometry->Vertices = Resource.RestVertices;
	const TArray<TArray<FVector3>>& Morphed = Instance.Rig->Morphed;
	bool bAnyShape = false;
	for ( const TArray<FVector3>& Positions : Morphed )
	{
		bAnyShape = bAnyShape || !Positions.IsEmpty();
	}
	if ( bAnyShape ) // 形状を足した位置（骨の変形の前）を、頂点へ写す
	{
		for ( int32 Index = 0; Index < Geometry->Vertices.Num() && Index < Resource.VertexSources.Num(); ++Index )
		{
			const FModelVertexSource& Source = Resource.VertexSources[Index];
			if ( Source.Mesh >= 0 && Source.Mesh < Morphed.Num() && !Morphed[Source.Mesh].IsEmpty() )
			{
				const FVector3& Position = Morphed[Source.Mesh][Source.Vertex];
				Geometry->Vertices[Index].Position[0] = Position.x;
				Geometry->Vertices[Index].Position[1] = Position.y;
				Geometry->Vertices[Index].Position[2] = Position.z;
			}
		}
	}
	CollectVisibleSections( Instance, Geometry->Indices, Geometry->Sections );
	return Geometry;
}

bool CStaticModelStore::GetStaticModelSkinnedDrawView( int32 Handle, FStaticModelDrawView& Out )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || !Instance->Resource->bGpuSkin )
	{
		return false;
	}
	FInstanceRig& Rig = *Instance->Rig;
	FResource& Resource = *Instance->Resource;
	for ( const uint8 Flag : Rig.bMaterialSet )
	{
		if ( Flag )
		{
			return false; // 材質の色を上書きした個体は、頂点の色を作り直すので、変形済みの頂点で描く。
		}
	}
	EnsurePose( *Instance );
	TSharedPtr<const FSkinGeometry> Geometry;
	uint64 Key = 0;
	uint64 Version = 1;
	if ( IsDefaultGeometry( *Instance ) )
	{
		if ( !Resource.DefaultSkinGeometry )
		{
			Resource.DefaultSkinGeometry = MakeSkinGeometry( *Instance ); // 非表示も形状も無いので、同じモデルの個体は、同じ形状を共有する。
		}
		Geometry = Resource.DefaultSkinGeometry;
		Key = ( 3ull << 62 ) | ( Resource.Id << 1 );
	}
	else
	{
		if ( !Rig.SkinGeometry || Rig.SkinGeometryVersion != Rig.Pose.GeometryVersion )
		{
			Rig.SkinGeometry = MakeSkinGeometry( *Instance );
			Rig.SkinGeometryVersion = Rig.Pose.GeometryVersion;
		}
		Geometry = Rig.SkinGeometry;
		Key = ( 3ull << 62 ) | ( static_cast<uint64>( Handle ) << 1 ) | 1ull;
		Version = Rig.SkinGeometryVersion + 1;
	}
	if ( !Rig.SkinPalette || Rig.SkinPaletteVersion != Rig.Pose.Version )
	{
		auto Palette = MakeShared<TArray<float32>>();
		NModelRig::EvaluatePalette( *Resource.Rig, Rig.Global, Resource.PaletteBase, Resource.NumPaletteBones, *Palette );
		Rig.SkinPalette = Palette;
		Rig.SkinPaletteVersion = Rig.Pose.Version;
	}
	Out = { Geometry->Vertices.GetData(), Geometry->Vertices.Num(), Geometry->Indices.GetData(), Geometry->Indices.Num(), Geometry->Sections.GetData(), Geometry->Sections.Num(), Instance->Opacity, Key, Version };
	Out.VertexOwner = TSharedPtr<const FDrawVertex3D>( Geometry, Out.Vertices );
	Out.SkinVertices = Resource.SkinVertices.GetData();
	Out.BoneMatrices = Rig.SkinPalette->GetData();
	Out.NumBones = Resource.NumPaletteBones;
	Out.SkinOwner = TSharedPtr<const float32>( Rig.SkinPalette, Out.BoneMatrices );
	return true;
}

void CStaticModelStore::BuildDeformed( FInstance& Instance )
{
	FInstanceRig& Rig = *Instance.Rig;
	const FResource& Resource = *Instance.Resource;
	EnsurePose( Instance );
	auto Snapshot = MakeShared<FDrawSnapshot>(); // ロック内で構築し、完成した形状だけを公開する。
	Snapshot->Resource = Instance.Resource;
	Snapshot->Vertices = Resource.Vertices;
	NModelRig::Deform( *Resource.Rig, Rig.Global, Resource.VertexSources, Snapshot->Vertices.GetData(), Snapshot->Vertices.Num(), Jobs, &Rig.Morphed );
	const auto& SourceSections = Resource.Definition.Surface.Sections;
	CollectVisibleSections( Instance, Snapshot->Indices, Snapshot->Sections );
	// 材質の色の上書きを、頂点の拡散色へ（頂点は、範囲ごとに専用なので、範囲の材質で決められる）
	bool bAnyColor = false;
	for ( uint8 Flag : Rig.bMaterialSet )
	{
		bAnyColor = bAnyColor || Flag != 0;
	}
	if ( bAnyColor )
	{
		int32 Cursor = 0;
		for ( int32 Index = 0; Index < Resource.Sections.Num() && Index < SourceSections.Num(); ++Index )
		{
			if ( IsHidden( Instance, SourceSections[Index].SourceMeshIndex, SourceSections[Index].SourceFrameIndex ) )
			{
				continue;
			}
			const int32 Material = SourceSections[Index].MaterialIndex;
			if ( Material >= 0 && Material < Rig.bMaterialSet.Num() && Rig.bMaterialSet[Material] )
			{
				const FColor& Diffuse = Rig.MaterialColors[Material].Diffuse;
				for ( int32 Offset = 0; Offset < Snapshot->Sections[Cursor].NumIndices; ++Offset )
				{
					FDrawVertex3D& Vertex = Snapshot->Vertices[Snapshot->Indices[Snapshot->Sections[Cursor].FirstIndex + Offset]];
					Vertex.Diffuse[0] = ColorByte( Diffuse.r );
					Vertex.Diffuse[1] = ColorByte( Diffuse.g );
					Vertex.Diffuse[2] = ColorByte( Diffuse.b );
					Vertex.Diffuse[3] = ColorByte( Diffuse.a );
				}
			}
			++Cursor;
		}
	}
	Rig.BuiltVersion = Rig.Pose.Version;
	Rig.DrawSnapshot = Snapshot;
}

bool CStaticModelStore::GetStaticModelDrawView( int32 Handle, FStaticModelDrawView& Out )
{
	FScopedLock Guard( Lock );
	auto* Instance = FindInstance( Handle ); // 描画まで生存する個体。
	if ( !Instance )
	{
		return false;
	}
	const auto& Data = *Instance->Resource; // 書き換えない共有形状。
	if ( Instance->Rig )
	{
		bool bAnyHidden = false;
		for ( uint8 Flag : Instance->Rig->Pose.MeshHidden )
		{
			bAnyHidden = bAnyHidden || Flag != 0;
		}
		for ( uint8 Flag : Instance->Rig->Pose.FrameHidden )
		{
			bAnyHidden = bAnyHidden || Flag != 0;
		}
		if ( Data.bHasSkin || bAnyHidden || NeedsDeform( *Instance ) )
		{
			FInstanceRig& Rig = *Instance->Rig;
			if ( Rig.BuiltVersion != Rig.Pose.Version || !Rig.DrawSnapshot )
			{
				BuildDeformed( *Instance );
			}
			const auto& Snapshot = *Rig.DrawSnapshot; // 保持中のビューは次のポーズの構築でも変わらない。
			Out = { Snapshot.Vertices.GetData(), Snapshot.Vertices.Num(), Snapshot.Indices.GetData(), Snapshot.Indices.Num(), Snapshot.Sections.GetData(), Snapshot.Sections.Num(), Instance->Opacity, ( 1ull << 63 ) | static_cast<uint64>( Handle ), Rig.BuiltVersion + 1 };
			Out.VertexOwner = TSharedPtr<const FDrawVertex3D>( Rig.DrawSnapshot, Out.Vertices );
			return true;
		}
	}
	Out = { Data.Vertices.GetData(), Data.Vertices.Num(), Data.Definition.Surface.Indices.GetData(), Data.Definition.Surface.Indices.Num(), Data.Sections.GetData(), Data.Sections.Num(), Instance->Opacity, Data.Id, 1 }; // 共有の形状は、変わらないので、GPU に置いて使い回せる。
	Out.VertexOwner = TSharedPtr<const FDrawVertex3D>( Instance->Resource, Out.Vertices );
	return true;
}

bool CStaticModelStore::SetStaticModelPlacement( int32 Handle, const float32 ( &Matrix )[12] )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig )
	{
		return false;
	}
	FMatrix4x4 World;
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		for ( int32 Column = 0; Column < 3; ++Column )
		{
			World.m[Row][Column] = Matrix[Row * 3 + Column];
		}
	}
	Instance->Rig->Pose.World = World;
	return true;
}

bool CStaticModelStore::SetStaticModelAnimTime( int32 Handle, int32 AttachIndex, float32 Time )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || !NMath::IsFinite( Time ) )
	{
		return false;
	}
	FModelPose& Pose = Instance->Rig->Pose;
	if ( AttachIndex < 0 || AttachIndex >= Pose.Attaches.Num() || !Pose.Attaches[AttachIndex].bUsed )
	{
		return false;
	}
	if ( Pose.Attaches[AttachIndex].Time != Time )
	{
		Pose.Attaches[AttachIndex].Time = Time;
		++Pose.Version;
	}
	return true;
}

bool CStaticModelStore::SetStaticModelAnimRate( int32 Handle, int32 AttachIndex, float32 Rate )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || !NMath::IsFinite( Rate ) )
	{
		return false;
	}
	FModelPose& Pose = Instance->Rig->Pose;
	if ( AttachIndex < 0 || AttachIndex >= Pose.Attaches.Num() || !Pose.Attaches[AttachIndex].bUsed )
	{
		return false;
	}
	const float32 Clamped = NMath::Clamp( Rate, 0.0f, 1.0f );
	if ( Pose.Attaches[AttachIndex].Rate != Clamped )
	{
		Pose.Attaches[AttachIndex].Rate = Clamped;
		++Pose.Version;
	}
	return true;
}

bool CStaticModelStore::SetStaticModelAnimFrameRate( int32 Handle, int32 AttachIndex, int32 Frame, float32 Rate, bool bIncludeChildren )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || !NMath::IsFinite( Rate ) )
	{
		return false;
	}
	FModelPose& Pose = Instance->Rig->Pose;
	const TArray<int32>& Parent = Instance->Resource->Rig->Parent;
	if ( AttachIndex < 0 || AttachIndex >= Pose.Attaches.Num() || !Pose.Attaches[AttachIndex].bUsed || Frame < 0 || Frame >= Parent.Num() )
	{
		return false;
	}
	FModelAttach& Attach = Pose.Attaches[AttachIndex];
	const float32 Clamped = NMath::Clamp( Rate, 0.0f, 1.0f );
	for ( int32 Node = 0; Node < Parent.Num(); ++Node )
	{
		bool bTarget = Node == Frame;
		if ( bIncludeChildren && !bTarget )
		{
			for ( int32 Steps = 0, Walk = Parent[Node]; Walk >= 0 && Steps < Parent.Num(); ++Steps, Walk = Parent[Walk] )
			{
				if ( Walk == Frame )
				{
					bTarget = true;
					break;
				}
			}
		}
		if ( bTarget )
		{
			Attach.FrameRate[Node] = Clamped;
		}
	}
	++Pose.Version;
	return true;
}

const FStaticModelDefinition* CStaticModelStore::GetDefinition( int32 Handle ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle ); // 定義の借用元。
	return Instance ? &Instance->Resource->Definition : nullptr;
}

bool CStaticModelStore::GetSurface( int32 Handle, FStaticModelSurfaceData& Out ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle ); // 有効な登録だけ返す。
	if ( !Instance )
	{
		return false;
	}
	Out = Instance->Resource->Definition.Surface;
	return true;
}

bool CStaticModelStore::GetOpacity( int32 Handle, float32& Out ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle ); // 個体別の状態。
	if ( !Instance )
	{
		return false;
	}
	Out = Instance->Opacity;
	return true;
}

bool CStaticModelStore::SetOpacity( int32 Handle, float32 Opacity )
{
	FScopedLock Guard( Lock );
	auto* Instance = FindInstance( Handle ); // 共有形状は変更しない。
	if ( !Instance || !NMath::IsFinite( Opacity ) || Opacity < 0 || Opacity > 1 )
	{
		return false;
	}
	Instance->Opacity = Opacity;
	return true;
}

int64 CStaticModelStore::EstimateBytes( int32 Handle ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle ); // この個体が共有する形状。
	return Instance ? Instance->Resource->Bytes : 0;
}

bool CStaticModelStore::HasRig( int32 Handle ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle );
	return Instance && Instance->Rig;
}

int32 CStaticModelStore::GetAnimCount( int32 Handle ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle );
	return Instance && Instance->Resource->Rig ? Instance->Resource->Rig->Clips.Num() : 0;
}

FString CStaticModelStore::GetAnimName( int32 Handle, int32 AnimIndex ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle );
	const TSharedPtr<const FModelRig>* Rig = Instance ? &Instance->Resource->Rig : nullptr;
	return Rig && Rig->IsValid() && AnimIndex >= 0 && AnimIndex < ( *Rig )->Clips.Num() ? ( *Rig )->Clips[AnimIndex].Name : FString();
}

float32 CStaticModelStore::GetAnimLength( int32 Handle, int32 AnimIndex ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle );
	return Instance && Instance->Resource->Rig ? NModelRig::GetClipLength( *Instance->Resource->Rig, AnimIndex ) : 0.0f;
}

int32 CStaticModelStore::AttachAnim( int32 Handle, int32 AnimIndex, int32 Source )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	const FInstance* SourceInstance = FindInstance( Source );
	if ( !Instance || !Instance->Rig || !SourceInstance || !SourceInstance->Resource->Rig )
	{
		return -1;
	}
	return NModelRig::Attach( *Instance->Resource->Rig, Instance->Rig->Pose, SourceInstance->Resource->Rig, AnimIndex );
}

void CStaticModelStore::DetachAnim( int32 Handle, int32 AttachIndex )
{
	FScopedLock Guard( Lock );
	if ( FInstance* Instance = FindInstance( Handle ) )
	{
		if ( Instance->Rig )
		{
			NModelRig::Detach( Instance->Rig->Pose, AttachIndex );
		}
	}
}

bool CStaticModelStore::SampleAnimFrameLocal( int32 Handle, int32 AttachIndex, int32 Frame, float32 Time, FMatrix4x4& Out ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || !NMath::IsFinite( Time ) )
	{
		return false;
	}
	const FModelPose& Pose = Instance->Rig->Pose;
	const FModelRig& Rig = *Instance->Resource->Rig;
	if ( AttachIndex < 0 || AttachIndex >= Pose.Attaches.Num() || !Pose.Attaches[AttachIndex].bUsed || Frame < 0 || Frame >= Rig.GetNumNodes() )
	{
		return false;
	}
	const FModelAttach& Attach = Pose.Attaches[AttachIndex];
	const float32 Length = NModelRig::GetClipLength( *Attach.Source, Attach.Clip );
	if ( Time < 0.0f || Time > Length )
	{
		return false;
	}
	const int32 Track = Attach.NodeTrack[Frame];
	Out = Track < 0 ? Rig.BaseLocal[Frame] : NModelRig::SampleTrack( Attach.Source->Clips[Attach.Clip], Track, Time );
	return true;
}

bool CStaticModelStore::GetFrameMatrix( int32 Handle, int32 Frame, bool bWorld, FMatrix4x4& Out )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Frame < 0 || Frame >= Instance->Resource->Rig->GetNumNodes() )
	{
		return false;
	}
	EnsurePose( *Instance );
	Out = bWorld ? Instance->Rig->Global[Frame] * Instance->Rig->Pose.World : Instance->Rig->Local[Frame];
	return true;
}

bool CStaticModelStore::GetFramePosition( int32 Handle, int32 Frame, float32 ( &Out )[3] )
{
	FMatrix4x4 World;
	if ( !GetFrameMatrix( Handle, Frame, true, World ) )
	{
		return false;
	}
	Out[0] = World.m[3][0];
	Out[1] = World.m[3][1];
	Out[2] = World.m[3][2];
	return true;
}

bool CStaticModelStore::GetFrameOverride( int32 Handle, int32 Frame, bool& bOutOverridden, FMatrix4x4& Out ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Frame < 0 || Frame >= Instance->Rig->Pose.bOverride.Num() )
	{
		return false;
	}
	bOutOverridden = Instance->Rig->Pose.bOverride[Frame] != 0;
	if ( bOutOverridden )
	{
		Out = Instance->Rig->Pose.Override[Frame];
	}
	return true;
}

bool CStaticModelStore::SetFrameOverride( int32 Handle, int32 Frame, const FMatrix4x4& Local )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Frame < 0 || Frame >= Instance->Rig->Pose.bOverride.Num() )
	{
		return false;
	}
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		for ( int32 Column = 0; Column < 4; ++Column )
		{
			if ( !NMath::IsFinite( Local.m[Row][Column] ) )
			{
				return false;
			}
		}
	}
	if ( Local.m[0][3] != 0.0f || Local.m[1][3] != 0.0f || Local.m[2][3] != 0.0f || Local.m[3][3] != 1.0f )
	{
		return false;
	}
	FModelPose& Pose = Instance->Rig->Pose;
	Pose.bOverride[Frame] = 1;
	Pose.Override[Frame] = Local;
	++Pose.Version;
	return true;
}

bool CStaticModelStore::ClearFrameOverride( int32 Handle, int32 Frame )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Frame < 0 || Frame >= Instance->Rig->Pose.bOverride.Num() )
	{
		return false;
	}
	FModelPose& Pose = Instance->Rig->Pose;
	if ( Pose.bOverride[Frame] )
	{
		Pose.bOverride[Frame] = 0;
		++Pose.Version;
	}
	return true;
}

bool CStaticModelStore::GetFrameVisible( int32 Handle, int32 Frame, bool& bOut ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Frame < 0 || Frame >= Instance->Rig->Pose.FrameHidden.Num() )
	{
		return false;
	}
	bOut = Instance->Rig->Pose.FrameHidden[Frame] == 0;
	return true;
}

bool CStaticModelStore::SetFrameVisible( int32 Handle, int32 Frame, bool bVisible )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Frame < 0 || Frame >= Instance->Rig->Pose.FrameHidden.Num() )
	{
		return false;
	}
	Instance->Rig->Pose.FrameHidden[Frame] = bVisible ? 0 : 1;
	++Instance->Rig->Pose.Version;
	++Instance->Rig->Pose.GeometryVersion;
	return true;
}

bool CStaticModelStore::GetMeshVisible( int32 Handle, int32 Mesh, bool& bOut ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Mesh < 0 || Mesh >= Instance->Rig->Pose.MeshHidden.Num() )
	{
		return false;
	}
	bOut = Instance->Rig->Pose.MeshHidden[Mesh] == 0;
	return true;
}

bool CStaticModelStore::GetMeshDrawn( int32 Handle, int32 Mesh, int32 Frame, bool& bOut ) const
{
	FScopedLock Guard( Lock );
	const auto* Instance = FindInstance( Handle );
	if ( !Instance )
	{
		return false;
	}
	bOut = !IsHidden( *Instance, Mesh, Frame ); // 骨組みの無いモデルは、隠せないので、いつも描かれる。
	return true;
}

bool CStaticModelStore::GetMeshData( int32 Handle, int32 Mesh, bool bCurrentPose, FModelMeshData& Out )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance )
	{
		return false;
	}
	const FResource& Resource = *Instance->Resource;
	const FStaticModelSurfaceData& Surface = Resource.Definition.Surface;
	if ( Mesh < -1 || Mesh >= Resource.Definition.NumMeshes )
	{
		return false;
	}
	TArray<FDrawVertex3D> Pose; // いまの姿勢のときだけ、変形した頂点（非表示のメッシュも含む）。
	if ( bCurrentPose )
	{
		GetFullVertices( *Instance, Pose );
	}
	const TArray<FDrawVertex3D>& Source = bCurrentPose ? Pose : Resource.Vertices;
	const int32 DefaultMaterial = Resource.Definition.Structure.Materials.Num() - 1; // 最後は、材質未指定用
	FModelMeshData Result;
	TMap<int32, int32> NewIndex; // 元の頂点 -> 出力の頂点
	TMap<uint64, TArray<int32>> Buckets; // 同じ中身の頂点を探す（三角形ごとに展開された頂点を、共有に戻す）。元の頂点の番号を、中身の hash でまとめる。
	const auto Same = [&]( int32 First, int32 Second ) -> bool
	{
		if ( FMemory::Memcmp( &Source[First], &Source[Second], sizeof( FDrawVertex3D ) ) != 0 )
		{
			return false;
		}
		if ( First < Resource.VertexSources.Num() && Second < Resource.VertexSources.Num() ) // 変形元が違う頂点は、別の頂点（同じ位置でも、別の骨に付くことがある）。
		{
			return Resource.VertexSources[First].Mesh == Resource.VertexSources[Second].Mesh && Resource.VertexSources[First].Vertex == Resource.VertexSources[Second].Vertex;
		}
		return true;
	};
	const auto AddVertex = [&]( int32 Old ) -> int32
	{
		if ( const int32* Found = NewIndex.Find( Old ) )
		{
			return *Found;
		}
		uint64 Hash = 1469598103934665603ull; // 中身の hash（FNV-1a）。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Source[Old] );
		for ( usize Byte = 0; Byte < sizeof( FDrawVertex3D ); ++Byte )
		{
			Hash = ( Hash ^ Bytes[Byte] ) * 1099511628211ull;
		}
		TArray<int32>& Bucket = Buckets.FindOrAdd( Hash );
		for ( const int32 Candidate : Bucket )
		{
			if ( Same( Old, Candidate ) )
			{
				const int32 Shared = NewIndex[Candidate];
				NewIndex.Add( Old, Shared );
				return Shared;
			}
		}
		Bucket.Add( Old );
		const FDrawVertex3D& Vertex = Source[Old];
		FModelMeshVertex Output;
		Output.Position = FVector3( Vertex.Position[0], Vertex.Position[1], Vertex.Position[2] );
		Output.Normal = FVector3( Vertex.Normal[0], Vertex.Normal[1], Vertex.Normal[2] );
		Output.UV = FVector2( Vertex.U, Vertex.V );
		Output.Color = FColor( Vertex.Diffuse[0] / 255.0f, Vertex.Diffuse[1] / 255.0f, Vertex.Diffuse[2] / 255.0f, Vertex.Diffuse[3] / 255.0f );
		if ( Resource.Rig && Old < Resource.VertexSources.Num() ) // いちばん強く影響する骨（同じ重みなら、先の影響）。
		{
			const FModelVertexSource& From = Resource.VertexSources[Old];
			if ( From.Mesh >= 0 && From.Mesh < Resource.Rig->Meshes.Num() )
			{
				const FRigMesh& RigMesh = Resource.Rig->Meshes[From.Mesh];
				if ( RigMesh.bSkinned && From.Vertex >= 0 && From.Vertex + 1 < RigMesh.Influences.Start.Num() )
				{
					float32 BestWeight = 0.0f;
					for ( int32 Influence = RigMesh.Influences.Start[From.Vertex]; Influence < RigMesh.Influences.Start[From.Vertex + 1]; ++Influence )
					{
						const int32 Cluster = RigMesh.Influences.Cluster[Influence];
						if ( RigMesh.Influences.Weight[Influence] > BestWeight && Cluster >= 0 && Cluster < RigMesh.ClusterBone.Num() )
						{
							BestWeight = RigMesh.Influences.Weight[Influence];
							Output.DominantFrame = RigMesh.ClusterBone[Cluster];
						}
					}
				}
			}
		}
		const int32 Added = Result.Vertices.Add( Output );
		NewIndex.Add( Old, Added );
		return Added;
	};
	for ( const FStaticModelSurfaceSection& Section : Surface.Sections )
	{
		if ( Mesh >= 0 && Section.SourceMeshIndex != Mesh )
		{
			continue;
		}
		for ( int32 Offset = 0; Offset + 2 < Section.NumIndices; Offset += 3 )
		{
			FModelMeshTriangle Triangle;
			for ( int32 Corner = 0; Corner < 3; ++Corner )
			{
				const int32 Old = static_cast<int32>( Surface.Indices[Section.FirstIndex + Offset + Corner] );
				if ( Old < 0 || Old >= Source.Num() )
				{
					return false;
				}
				Triangle.Vertices[Corner] = AddVertex( Old );
			}
			Triangle.Mesh = Section.SourceMeshIndex;
			Triangle.Material = Section.MaterialIndex == DefaultMaterial ? -1 : Section.MaterialIndex;
			Triangle.Frame = Section.SourceFrameIndex;
			Result.Triangles.Add( Triangle );
		}
	}
	if ( Result.Triangles.IsEmpty() )
	{
		return false; // メッシュが無い（番号が合わない）。
	}
	Result.BoundsMin = Result.Vertices[0].Position;
	Result.BoundsMax = Result.Vertices[0].Position;
	for ( const FModelMeshVertex& Vertex : Result.Vertices )
	{
		Result.BoundsMin = FVector3( NMath::Min( Result.BoundsMin.x, Vertex.Position.x ), NMath::Min( Result.BoundsMin.y, Vertex.Position.y ), NMath::Min( Result.BoundsMin.z, Vertex.Position.z ) );
		Result.BoundsMax = FVector3( NMath::Max( Result.BoundsMax.x, Vertex.Position.x ), NMath::Max( Result.BoundsMax.y, Vertex.Position.y ), NMath::Max( Result.BoundsMax.z, Vertex.Position.z ) );
	}
	Out = MoveTemp( Result );
	return true;
}

bool CStaticModelStore::GetShapeRate( int32 Handle, int32 Shape, float32& Out ) const
{
	FScopedLock Guard( Lock );
	const FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Shape < 0 || Shape >= Instance->Rig->Pose.ShapeRate.Num() )
	{
		return false;
	}
	Out = Instance->Rig->Pose.ShapeRate[Shape];
	return true;
}

bool CStaticModelStore::SetShapeRate( int32 Handle, int32 Shape, float32 Rate )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Shape < 0 || Shape >= Instance->Rig->Pose.ShapeRate.Num() || !( Rate == Rate ) )
	{
		return false;
	}
	const float32 Clamped = NMath::Clamp( Rate, 0.0f, MaxShapeRate );
	float32& Current = Instance->Rig->Pose.ShapeRate[Shape];
	if ( Current != Clamped ) // 同じ値なら、ポーズを変えない（頂点を作り直さない）
	{
		Current = Clamped;
		++Instance->Rig->Pose.Version;
		++Instance->Rig->Pose.ShapeVersion;
		++Instance->Rig->Pose.GeometryVersion;
	}
	return true;
}

bool CStaticModelStore::SetMeshVisible( int32 Handle, int32 Mesh, bool bVisible )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig || Mesh < 0 || Mesh >= Instance->Rig->Pose.MeshHidden.Num() )
	{
		return false;
	}
	Instance->Rig->Pose.MeshHidden[Mesh] = bVisible ? 0 : 1;
	++Instance->Rig->Pose.Version;
	++Instance->Rig->Pose.GeometryVersion;
	return true;
}

void CStaticModelStore::GetFullVertices( FInstance& Instance, TArray<FDrawVertex3D>& Out )
{
	const FResource& Resource = *Instance.Resource;
	Out = Resource.Vertices;
	if ( Instance.Rig && Resource.Rig && ( Resource.bHasSkin || NeedsDeform( Instance ) ) )
	{
		EnsurePose( Instance );
		NModelRig::Deform( *Resource.Rig, Instance.Rig->Global, Resource.VertexSources, Out.GetData(), Out.Num(), nullptr, &Instance.Rig->Morphed );
	}
}

bool CStaticModelStore::EnsureCollision( FInstance& Instance )
{
	if ( Instance.Collision.IsValid() )
	{
		return true;
	}
	const FResource& Resource = *Instance.Resource;
	TArray<FDrawVertex3D> Vertices;
	GetFullVertices( Instance, Vertices );
	auto Collision = MakeShared<FCollision>();
	const FStaticModelSurfaceData& Surface = Resource.Definition.Surface;
	const int32 DefaultMaterial = Resource.Definition.Structure.Materials.Num() - 1; // 最後は、材質未指定用
	TMap<int32, int32> FrameTriangles; // フレームごとの、これまでの三角形の数。
	for ( const FStaticModelSurfaceSection& Section : Surface.Sections )
	{
		int32& Count = FrameTriangles.FindOrAdd( Section.SourceFrameIndex );
		for ( int32 Offset = 0; Offset + 2 < Section.NumIndices; Offset += 3 )
		{
			FCollisionTriangle Triangle;
			FVector3 VertexNormal( 0.0f, 0.0f, 0.0f );
			for ( int32 Corner = 0; Corner < 3; ++Corner )
			{
				const FDrawVertex3D& Vertex = Vertices[Surface.Indices[Section.FirstIndex + Offset + Corner]];
				Triangle.P[Corner] = FVector3( Vertex.Position[0], Vertex.Position[1], Vertex.Position[2] );
				VertexNormal = VertexNormal + FVector3( Vertex.Normal[0], Vertex.Normal[1], Vertex.Normal[2] );
			}
			FVector3 Normal = FVector3::Cross( Triangle.P[1] - Triangle.P[0], Triangle.P[2] - Triangle.P[0] );
			if ( Normal.LengthSquared() < 1.0e-20f )
			{
				++Count;
				continue; // 面積の無い三角形は、当たらない
			}
			Normal = Normal.Normalized();
			Triangle.Normal = FVector3::Dot( Normal, VertexNormal ) < 0.0f ? Normal * -1.0f : Normal; // 頂点の法線の側を表にする
			Triangle.Min = FVector3( NMath::Min( Triangle.P[0].x, NMath::Min( Triangle.P[1].x, Triangle.P[2].x ) ), NMath::Min( Triangle.P[0].y, NMath::Min( Triangle.P[1].y, Triangle.P[2].y ) ), NMath::Min( Triangle.P[0].z, NMath::Min( Triangle.P[1].z, Triangle.P[2].z ) ) );
			Triangle.Max = FVector3( NMath::Max( Triangle.P[0].x, NMath::Max( Triangle.P[1].x, Triangle.P[2].x ) ), NMath::Max( Triangle.P[0].y, NMath::Max( Triangle.P[1].y, Triangle.P[2].y ) ), NMath::Max( Triangle.P[0].z, NMath::Max( Triangle.P[1].z, Triangle.P[2].z ) ) );
			Triangle.Frame = Section.SourceFrameIndex;
			Triangle.Material = Section.MaterialIndex == DefaultMaterial ? -1 : Section.MaterialIndex;
			Triangle.Triangle = Count++;
			Collision->Triangles.Add( Triangle );
		}
	}
	Instance.Collision = Collision;
	return true;
}

bool CStaticModelStore::PrepareCollision( int32 Handle )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance )
	{
		return false;
	}
	Instance->Collision.Reset(); // 写し直す。
	return EnsureCollision( *Instance );
}

bool CStaticModelStore::Raycast( int32 Handle, const FVector3& From, const FVector3& To, FModelHit& Out )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !EnsureCollision( *Instance ) )
	{
		return false;
	}
	const FVector3 Direction = To - From;
	const float32 Length = Direction.Length();
	if ( !( Length > 0.0f ) || !NMath::IsFinite( Length ) )
	{
		return false;
	}
	float32 BestT = 2.0f;
	const FCollisionTriangle* Best = nullptr;
	for ( const FCollisionTriangle& Triangle : Instance->Collision->Triangles )
	{
		// 両面に当たる、線分と三角形の交差（Moller-Trumbore）
		const FVector3 Edge1 = Triangle.P[1] - Triangle.P[0];
		const FVector3 Edge2 = Triangle.P[2] - Triangle.P[0];
		const FVector3 P = FVector3::Cross( Direction, Edge2 );
		const float32 Determinant = FVector3::Dot( Edge1, P );
		if ( NMath::Abs( Determinant ) < 1.0e-12f )
		{
			continue;
		}
		const float32 InverseDeterminant = 1.0f / Determinant;
		const FVector3 T = From - Triangle.P[0];
		const float32 U = FVector3::Dot( T, P ) * InverseDeterminant;
		if ( U < 0.0f || U > 1.0f )
		{
			continue;
		}
		const FVector3 Q = FVector3::Cross( T, Edge1 );
		const float32 V = FVector3::Dot( Direction, Q ) * InverseDeterminant;
		if ( V < 0.0f || U + V > 1.0f )
		{
			continue;
		}
		const float32 Distance = FVector3::Dot( Edge2, Q ) * InverseDeterminant; // 線分の割合
		if ( Distance >= 0.0f && Distance <= 1.0f && Distance < BestT )
		{
			BestT = Distance;
			Best = &Triangle;
		}
	}
	if ( !Best )
	{
		return false;
	}
	Out.Position = From + Direction * BestT;
	Out.Normal = Best->Normal;
	Out.Distance = BestT * Length;
	Out.Frame = Best->Frame;
	Out.Material = Best->Material;
	Out.Triangle = Best->Triangle;
	return true;
}

bool CStaticModelStore::OverlapSphere( int32 Handle, const FVector3& Center, float32 Radius, TArray<FModelHit>& Out )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !( Radius > 0.0f ) || !NMath::IsFinite( Radius ) || !EnsureCollision( *Instance ) )
	{
		return false;
	}
	Out.Reset();
	for ( const FCollisionTriangle& Triangle : Instance->Collision->Triangles )
	{
		if ( Center.x + Radius < Triangle.Min.x || Center.x - Radius > Triangle.Max.x || Center.y + Radius < Triangle.Min.y || Center.y - Radius > Triangle.Max.y || Center.z + Radius < Triangle.Min.z || Center.z - Radius > Triangle.Max.z )
		{
			continue;
		}
		const FVector3 Closest = ClosestPointOnTriangle( Center, Triangle.P[0], Triangle.P[1], Triangle.P[2] );
		const float32 Distance = ( Closest - Center ).Length();
		if ( Distance <= Radius )
		{
			FModelHit Hit;
			Hit.Position = Closest;
			Hit.Normal = Triangle.Normal;
			Hit.Distance = Distance;
			Hit.Frame = Triangle.Frame;
			Hit.Material = Triangle.Material;
			Hit.Triangle = Triangle.Triangle;
			Out.Add( Hit );
		}
	}
	return true;
}

bool CStaticModelStore::GetMaterialColors( int32 Handle, int32 Material, FModelMaterialColors& Out ) const
{
	FScopedLock Guard( Lock );
	const FInstance* Instance = FindInstance( Handle );
	if ( !Instance )
	{
		return false;
	}
	const auto& Materials = Instance->Resource->Definition.Structure.Materials;
	if ( Material < 0 || Material >= Materials.Num() )
	{
		return false;
	}
	if ( Instance->Rig && Material < Instance->Rig->bMaterialSet.Num() && Instance->Rig->bMaterialSet[Material] )
	{
		Out = Instance->Rig->MaterialColors[Material];
	}
	else
	{
		Out = Materials[Material].Colors;
	}
	return true;
}

bool CStaticModelStore::SetMaterialColors( int32 Handle, int32 Material, const FModelMaterialColors& Colors )
{
	FScopedLock Guard( Lock );
	FInstance* Instance = FindInstance( Handle );
	if ( !Instance || !Instance->Rig )
	{
		return false; // 骨組みの無い（従来の）モデルは、共有した形状の色を変えられない
	}
	const auto& Materials = Instance->Resource->Definition.Structure.Materials;
	if ( Material < 0 || Material >= Materials.Num() )
	{
		return false;
	}
	const FColor Parts[4] = { Colors.Diffuse, Colors.Ambient, Colors.Specular, Colors.Emissive };
	for ( const FColor& Part : Parts )
	{
		if ( !NMath::IsFinite( Part.r ) || !NMath::IsFinite( Part.g ) || !NMath::IsFinite( Part.b ) || !NMath::IsFinite( Part.a ) )
		{
			return false;
		}
	}
	FInstanceRig& Rig = *Instance->Rig;
	if ( Rig.bMaterialSet.IsEmpty() )
	{
		Rig.bMaterialSet.SetNum( Materials.Num(), 0 );
		Rig.MaterialColors.SetNum( Materials.Num() );
	}
	Rig.bMaterialSet[Material] = 1;
	Rig.MaterialColors[Material] = Colors;
	++Rig.Pose.Version;
	return true;
}
