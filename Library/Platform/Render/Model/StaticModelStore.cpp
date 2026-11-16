// SPDX-License-Identifier: MIT
#include "StaticModelStore.h"
#include "Library/Framework/Asset/Model/Import/StaticModelBuilder.h"
#include "Library/Core/Collection/Set.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

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
	if ( Instances.Num() >= 128 || NextHandle == TNumericLimits<int32>::Max() )
	{
		Error = "Static model instance limit exceeded";
		return -1;
	}
	auto Resource = MakeShared<FResource>(); // 読み込み途中の所有も局所化する。
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
		// スキンつきの頂点は、メッシュの座標のまま。基準の姿（アニメーション無し）に変形して、描画・面データ・外接箱の基準にする。
		TArray<FMatrix4x4> Local = Resource->Rig->BaseLocal;
		TArray<FMatrix4x4> Global;
		NModelRig::ComputeGlobals( *Resource->Rig, Local, Global );
		NModelRig::Deform( *Resource->Rig, Global, Resource->VertexSources, Resource->Vertices.GetData(), Resource->Vertices.Num() );
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
	Rig.PoseVersion = Rig.Pose.Version;
}

void CStaticModelStore::BuildDeformed( FInstance& Instance )
{
	FInstanceRig& Rig = *Instance.Rig;
	const FResource& Resource = *Instance.Resource;
	EnsurePose( Instance );
	Rig.Vertices = Resource.Vertices;
	NModelRig::Deform( *Resource.Rig, Rig.Global, Resource.VertexSources, Rig.Vertices.GetData(), Rig.Vertices.Num() );
	Rig.Indices.Reset();
	Rig.Sections.Reset();
	const auto& SourceSections = Resource.Definition.Surface.Sections;
	for ( int32 Index = 0; Index < Resource.Sections.Num() && Index < SourceSections.Num(); ++Index )
	{
		if ( IsHidden( Instance, SourceSections[Index].SourceMeshIndex, SourceSections[Index].SourceFrameIndex ) )
		{
			continue;
		}
		FStaticModelDrawSection Section = Resource.Sections[Index];
		const int32 First = Section.FirstIndex;
		Section.FirstIndex = Rig.Indices.Num();
		for ( int32 Offset = 0; Offset < Section.NumIndices; ++Offset )
		{
			Rig.Indices.Add( Resource.Definition.Surface.Indices[First + Offset] );
		}
		Rig.Sections.Add( Section );
	}
	Rig.BuiltVersion = Rig.Pose.Version;
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
			if ( Rig.BuiltVersion != Rig.Pose.Version || Rig.Vertices.IsEmpty() )
			{
				BuildDeformed( *Instance );
			}
			Out = { Rig.Vertices.GetData(), Rig.Vertices.Num(), Rig.Indices.GetData(), Rig.Indices.Num(), Rig.Sections.GetData(), Rig.Sections.Num(), Instance->Opacity };
			return true;
		}
	}
	Out = { Data.Vertices.GetData(), Data.Vertices.Num(), Data.Definition.Surface.Indices.GetData(), Data.Definition.Surface.Indices.Num(), Data.Sections.GetData(), Data.Sections.Num(), Instance->Opacity };
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
	return true;
}
