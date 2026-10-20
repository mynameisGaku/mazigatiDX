#include "EditorPlacement.h"

#include <algorithm>
#include <cmath>

#include "Library/Framework/Asset/Import/ModelFormats.h"
#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/World/Mesh/EditMeshOps.h"
#include "Library/Framework/World/WorldHierarchy.h"

namespace
{
	FString GetExtension( const FString& Path )
	{
		const int32 Dot = Path.FindLast( "." );
		const int32 Slash = Path.FindLast( "/" );
		const int32 BackSlash = Path.FindLast( "\\" );
		if ( Dot == INDEX_NONE || Dot < Slash || Dot < BackSlash ) return FString();
		return Path.Mid( Dot );
	}

	bool IsOneOf( const FString& Extension, std::initializer_list<const char*> List )
	{
		for ( const char* Item : List )
		{
			if ( Extension.Equals( Item, ESearchCase::IgnoreCase ) ) return true;
		}
		return false;
	}

	// 地面や表面の点を、親の中のローカルの位置にして、足す
	FWorldTransform MakeLocalPlacement( const FWorldDefinition& World, int32 ParentId, const FVector3& WorldPosition )
	{
		FWorldTransform Transform;
		if ( ParentId == WorldNoId || World.FindObject( ParentId ) == nullptr )
		{
			Transform.Position = WorldPosition;
			return Transform;
		}
		Transform.Position = NWorld::WorldToLocal( NWorld::ComputeWorldTransform( World, ParentId ), FTransform( WorldPosition ) ).Position;
		return Transform;
	}

	float32 SnapTo( float32 Value, float32 Step )
	{
		return Step > 0.0f ? std::round( Value / Step ) * Step : Value;
	}
}

EEditorAssetKind NWorldEditor::ClassifyAsset( const FString& Path )
{
	if ( NModelFormat::Classify( Path ) != EModelFileKind::Unknown ) return EEditorAssetKind::Model;
	// エフェクト: 内蔵のパーティクルと、登録された外部のバックエンドが扱うファイル
	if ( FEffectBackendRegistry::Global().Find( Path ) != nullptr ) return EEditorAssetKind::Effect;
	const FString Extension = GetExtension( Path );
	if ( IsOneOf( Extension, { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds" } ) ) return EEditorAssetKind::Texture;
	if ( IsOneOf( Extension, { ".wav", ".ogg", ".mp3" } ) ) return EEditorAssetKind::Sound;
	if ( IsOneOf( Extension, { ".world" } ) ) return EEditorAssetKind::World;
	return EEditorAssetKind::Unknown;
}

FString NWorldEditor::GetAssetDisplayName( const FString& Path )
{
	const int32 Slash = std::max( Path.FindLast( "/" ), Path.FindLast( "\\" ) );
	FString Name = Slash == INDEX_NONE ? Path : Path.Mid( Slash + 1 );
	// 二重の拡張子（.effect.json）は、まとめて外す
	if ( Name.EndsWith( ".effect.json", ESearchCase::IgnoreCase ) ) return Name.Left( Name.Len() - 12 );
	const int32 Dot = Name.FindLast( "." );
	if ( Dot > 0 ) Name = Name.Left( Dot );
	return Name;
}

FEditorPlacement NWorldEditor::ComputePlacement( const FEditorRay& Ray, const FWorldDefinition* World, const FEditorShapeContext& Context, const FEditorPlacementOptions& Options )
{
	FEditorPlacement Result;
	float32 BestT = NMath::MaxFloat;

	// 地面
	float32 GroundT = 0.0f;
	if ( RayVsPlane( Ray, FVector3( 0.0f, Options.GroundY, 0.0f ), FVector3::Up, GroundT ) )
	{
		BestT = GroundT;
		Result.bHitGround = true;
	}

	// すでにあるものの表面（モデルの箱・当たり判定・板）
	if ( Options.bOnSurfaces && World != nullptr )
	{
		const TArray<FEditorHit> Hits = PickObjects( *World, Ray, Context );
		if ( !Hits.IsEmpty() && Hits[0].Distance < BestT )
		{
			BestT = Hits[0].Distance;
			Result.bHitGround = false;
			Result.bHitObject = true;
			Result.HitObjectId = Hits[0].ObjectId;
		}
	}

	if ( !Result.bHitGround && !Result.bHitObject ) BestT = Options.FallbackDistance;
	Result.Position = Ray.PointAt( BestT );
	Result.Position.x = SnapTo( Result.Position.x, Options.SnapStep );
	Result.Position.z = SnapTo( Result.Position.z, Options.SnapStep );
	if ( Result.bHitGround ) Result.Position.y = Options.GroundY;   // 地面は、誤差なく
	return Result;
}

int32 NWorldEditor::AddAssetObject( TWorldDocument& Document, const FString& AssetPath, int32 ParentId, const FVector3& WorldPosition, const IEditorAssetInfo* Info )
{
	const EEditorAssetKind Kind = ClassifyAsset( AssetPath );
	if ( AssetPath.IsEmpty() || Kind == EEditorAssetKind::Unknown ) return WorldNoId;
	if ( ParentId != WorldNoId && Document.Get().FindObject( ParentId ) == nullptr ) return WorldNoId;

	const FString Path = AssetPath.Replace( "\\", "/" );
	TWorldDocument::FBatch Batch( Document );
	const int32 Id = NWorldEdit::AddObject( Document, GetAssetDisplayName( Path ), ParentId, MakeLocalPlacement( Document.Get(), ParentId, WorldPosition ) );
	if ( Id == WorldNoId ) return WorldNoId;

	FWorldObject* Object = NWorldEdit::EditObject( Document, Id );
	switch ( Kind )
	{
	case EEditorAssetKind::Model:
	{
		FWorldModel Model;
		Model.Path = Path;
		Object->Model = Model;
		break;
	}
	case EEditorAssetKind::Texture:
	{
		FWorldBillboard Billboard;
		Billboard.Texture = Path;
		int32 Width = 0, Height = 0;
		if ( Info != nullptr && Info->GetTextureSize( Path, Width, Height ) && Width > 0 && Height > 0 )
		{
			Billboard.Size = FVector2( static_cast<float32>( Width ) / static_cast<float32>( Height ), 1.0f );
		}
		Billboard.Pivot = FVector2( 0.5f, 1.0f );   // 足元（画像の下の中央）が置いた位置。地面に立てやすい
		Object->Billboard = Billboard;
		break;
	}
	case EEditorAssetKind::Sound:
	{
		FWorldSound Sound;
		Sound.Path = Path;
		Object->Sound = Sound;
		break;
	}
	case EEditorAssetKind::World:
	{
		FWorldPrefab Prefab;
		Prefab.Path = Path;
		Object->Prefab = Prefab;
		break;
	}
	case EEditorAssetKind::Effect:
	{
		FWorldEffect Effect;
		Effect.Path = Path;
		Object->Effect = Effect;
		break;
	}
	case EEditorAssetKind::Unknown: break;
	}
	return Id;
}

int32 NWorldEditor::AddEmptyObject( TWorldDocument& Document, const FString& Name, int32 ParentId, const FVector3& WorldPosition )
{
	if ( ParentId != WorldNoId && Document.Get().FindObject( ParentId ) == nullptr ) return WorldNoId;
	return NWorldEdit::AddObject( Document, Name, ParentId, MakeLocalPlacement( Document.Get(), ParentId, WorldPosition ) );
}

int32 NWorldEditor::AddMeshObject( TWorldDocument& Document, EEditMeshPrimitive Kind, int32 ParentId, const FVector3& WorldPosition, bool bCollider )
{
	if ( ParentId != WorldNoId && Document.Get().FindObject( ParentId ) == nullptr ) return WorldNoId;
	TWorldDocument::FBatch Batch( Document );
	const int32 Id = NWorldEdit::AddObject( Document, NEditMesh::ToString( Kind ), ParentId, MakeLocalPlacement( Document.Get(), ParentId, WorldPosition ) );
	if ( Id == WorldNoId ) return WorldNoId;
	FWorldObject* Object = NWorldEdit::EditObject( Document, Id );
	Object->Mesh = NEditMesh::MakePrimitive( Kind );
	if ( bCollider )
	{
		FWorldCollider Collider;
		Collider.Shape = EWorldColliderShape::Mesh;
		Object->Collider = Collider;
	}
	return Id;
}

bool NWorldEditor::ConvertModelToMesh( TWorldDocument& Document, int32 ObjectId, const IEditorAssetInfo& Assets )
{
	const FWorldObject* Object = Document.Get().FindObject( ObjectId );
	if ( Object == nullptr || !Object->Model.IsSet() || Object->Mesh.IsSet() || Object->Model.GetValue().Path.IsEmpty() ) return false;
	const FEditorCollisionMesh* Source = Assets.GetCollisionMesh( Object->Model.GetValue().Path );
	if ( Source == nullptr || Source->Vertices.IsEmpty() || Source->Indices.IsEmpty() ) return false;
	FEditMesh Mesh = NEditMesh::MakeFromTriangles( TArrayView<const FVector3>( Source->Vertices ), TArrayView<const uint32>( Source->Indices ), 1.0e-4f );
	if ( Mesh.GetNumFaces() == 0 ) return false;
	FWorldObject* Edited = NWorldEdit::EditObject( Document, ObjectId );
	Edited->Mesh = Mesh;
	Edited->Model.Reset();
	return true;
}

int32 NWorldEditor::AddColliderObject( TWorldDocument& Document, EWorldColliderShape Shape, bool bTrigger, int32 ParentId, const FVector3& WorldPosition )
{
	if ( ParentId != WorldNoId && Document.Get().FindObject( ParentId ) == nullptr ) return WorldNoId;
	TWorldDocument::FBatch Batch( Document );
	const char* BaseName = bTrigger ? "Trigger" : ( Shape == EWorldColliderShape::Box ? "Box" : Shape == EWorldColliderShape::Sphere ? "Sphere" : "Capsule" );
	const int32 Id = NWorldEdit::AddObject( Document, BaseName, ParentId, MakeLocalPlacement( Document.Get(), ParentId, WorldPosition ) );
	if ( Id == WorldNoId ) return WorldNoId;
	FWorldCollider Collider;
	Collider.Shape = Shape;
	Collider.bTrigger = bTrigger;
	NWorldEdit::EditObject( Document, Id )->Collider = Collider;
	return Id;
}
