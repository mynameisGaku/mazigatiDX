#include "WorldValidate.h"

#include "Library/Framework/World/WorldCollision.h"

#include <cmath>

#include "Library/Framework/Audio/AudioTypes.h"
#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/World/WorldHierarchy.h"

const char* ToString( EWorldIssueSeverity Severity )
{
	return Severity == EWorldIssueSeverity::Error ? "Error" : "Warning";
}

const char* ToString( EWorldIssueCode Code )
{
	switch ( Code )
	{
	case EWorldIssueCode::NewerVersion: return "NewerVersion";
	case EWorldIssueCode::InvalidId: return "InvalidId";
	case EWorldIssueCode::DuplicateId: return "DuplicateId";
	case EWorldIssueCode::NextIdTooSmall: return "NextIdTooSmall";
	case EWorldIssueCode::EmptyRuleTag: return "EmptyRuleTag";
	case EWorldIssueCode::ShadowedRule: return "ShadowedRule";
	case EWorldIssueCode::SelfParent: return "SelfParent";
	case EWorldIssueCode::MissingParent: return "MissingParent";
	case EWorldIssueCode::ParentCycle: return "ParentCycle";
	case EWorldIssueCode::EmptyTag: return "EmptyTag";
	case EWorldIssueCode::DuplicateTag: return "DuplicateTag";
	case EWorldIssueCode::EmptyPropName: return "EmptyPropName";
	case EWorldIssueCode::DuplicatePropName: return "DuplicatePropName";
	case EWorldIssueCode::BadPropValue: return "BadPropValue";
	case EWorldIssueCode::MissingObjectReference: return "MissingObjectReference";
	case EWorldIssueCode::NonFiniteValue: return "NonFiniteValue";
	case EWorldIssueCode::ZeroScale: return "ZeroScale";
	case EWorldIssueCode::EmptyAssetPath: return "EmptyAssetPath";
	case EWorldIssueCode::MissingAsset: return "MissingAsset";
	case EWorldIssueCode::BadBillboard: return "BadBillboard";
	case EWorldIssueCode::BadSound: return "BadSound";
	case EWorldIssueCode::BadEffect: return "BadEffect";
	case EWorldIssueCode::BadCollider: return "BadCollider";
	case EWorldIssueCode::BadModel: return "BadModel";
	case EWorldIssueCode::BadMesh: return "BadMesh";
	case EWorldIssueCode::BadPrefab: return "BadPrefab";
	case EWorldIssueCode::CollisionWithoutCollider: return "CollisionWithoutCollider";
	case EWorldIssueCode::UnsupportedComponent: return "UnsupportedComponent";
	}
	return "?";
}

FString FWorldIssue::ToDisplayString() const
{
	FString Where;
	if ( ObjectId != WorldNoId ) Where = FString::Printf( " #%d", ObjectId );
	return FString::Printf( "[%s]%s: %s", ToString( Severity ), *Where, *Message );
}

namespace
{
	bool IsFinite( float32 Value ) { return std::isfinite( Value ); }
	bool IsFinite( const FVector3& V ) { return IsFinite( V.x ) && IsFinite( V.y ) && IsFinite( V.z ); }

	struct FReporter
	{
		TArray<FWorldIssue> Issues;

		void Add( EWorldIssueSeverity Severity, EWorldIssueCode Code, int32 ObjectId, const FString& Message )
		{
			FWorldIssue Issue;
			Issue.Severity = Severity;
			Issue.Code = Code;
			Issue.ObjectId = ObjectId;
			Issue.Message = Message;
			Issues.Add( Issue );
		}
		void Error( EWorldIssueCode Code, int32 ObjectId, const FString& Message ) { Add( EWorldIssueSeverity::Error, Code, ObjectId, Message ); }
		void Warning( EWorldIssueCode Code, int32 ObjectId, const FString& Message ) { Add( EWorldIssueSeverity::Warning, Code, ObjectId, Message ); }
	};

	// 必須のアセットのパスを確かめる（空・無い）
	void CheckAsset( FReporter& Report, const IWorldAssetQuery* Assets, int32 ObjectId, const char* What, const FString& Path )
	{
		if ( Path.IsEmpty() )
		{
			Report.Error( EWorldIssueCode::EmptyAssetPath, ObjectId, FString( What ) + " has no file" );
			return;
		}
		if ( Assets != nullptr && !Assets->Exists( Path ) ) Report.Error( EWorldIssueCode::MissingAsset, ObjectId, FString( What ) + " file '" + Path + "' was not found" );
	}

	// 省略できるアセットのパス（空ならよい。あれば、あることを確かめる）
	void CheckOptionalAsset( FReporter& Report, const IWorldAssetQuery* Assets, int32 ObjectId, const char* What, const FString& Path )
	{
		if ( Path.IsEmpty() || Assets == nullptr ) return;
		if ( !Assets->Exists( Path ) ) Report.Error( EWorldIssueCode::MissingAsset, ObjectId, FString( What ) + " file '" + Path + "' was not found" );
	}

	// 入れ子（Prefab）をたどって、循環・深すぎる入れ子を探す。見つかったら、道筋（"A -> B -> A"）を返す。Stack は、いま見ているワールドまでの道筋
	enum class EPrefabProblem : uint8 { None, Loop, TooDeep };

	// SelfKey は、検証しているワールド自身のファイル（正規化したもの。分からなければ空）。Stack は、そこから入れ子をたどってきた、入れ子のファイル（SelfKey を含まない）
	EPrefabProblem FindPathProblem( const FString& Path, const IWorldAssetQuery& Assets, const FString& SelfKey, TArray<FString>& Stack, FString& OutChain )
	{
		const FString Key = NWorld::NormalizeAssetPath( Path );
		bool bLoop = !SelfKey.IsEmpty() && Key == SelfKey;
		for ( const FString& Visited : Stack ) bLoop = bLoop || Visited == Key;
		if ( bLoop )
		{
			OutChain = SelfKey;
			for ( const FString& Visited : Stack ) OutChain += ( OutChain.IsEmpty() ? "" : " -> " ) + Visited;
			OutChain += ( OutChain.IsEmpty() ? "" : " -> " ) + Key;
			return EPrefabProblem::Loop;
		}
		if ( Stack.Num() >= WorldMaxPrefabDepth )
		{
			OutChain = Key;
			return EPrefabProblem::TooDeep;
		}
		const FWorldDefinition* Nested = Assets.GetWorld( Path );
		if ( Nested == nullptr ) return EPrefabProblem::None;
		Stack.Add( Key );
		EPrefabProblem Problem = EPrefabProblem::None;
		for ( const FWorldObject& Object : Nested->Objects )
		{
			if ( !Object.Prefab.IsSet() || Object.Prefab.GetValue().Path.IsEmpty() ) continue;
			Problem = FindPathProblem( Object.Prefab.GetValue().Path, Assets, SelfKey, Stack, OutChain );
			if ( Problem != EPrefabProblem::None ) break;
		}
		Stack.RemoveAt( Stack.Num() - 1 );
		return Problem;
	}

	void CheckObject( FReporter& Report, const FWorldDefinition& World, const FWorldObject& Object, const IWorldAssetQuery* Assets, const FString& WorldPath )
	{
		const int32 Id = Object.Id;

		// 値
		if ( !IsFinite( Object.Transform.Position ) || !IsFinite( Object.Transform.RotationDegrees ) || !IsFinite( Object.Transform.Scale ) )
		{
			Report.Error( EWorldIssueCode::NonFiniteValue, Id, "the transform has a value that is not a finite number" );
		}
		else if ( Object.Transform.Scale.x == 0.0f || Object.Transform.Scale.y == 0.0f || Object.Transform.Scale.z == 0.0f )
		{
			Report.Warning( EWorldIssueCode::ZeroScale, Id, "the scale has a 0 component (the object collapses to a plane or a point)" );
		}

		// タグ
		for ( int32 i = 0; i < Object.Tags.Num(); ++i )
		{
			const FString& Tag = Object.Tags[i];
			if ( Tag.TrimStartAndEnd().IsEmpty() || Tag != Tag.TrimStartAndEnd() ) Report.Warning( EWorldIssueCode::EmptyTag, Id, "a tag is empty or has spaces around it ('" + Tag + "')" );
			for ( int32 j = 0; j < i; ++j )
			{
				if ( Object.Tags[j] == Tag )
				{
					Report.Warning( EWorldIssueCode::DuplicateTag, Id, "the tag '" + Tag + "' is listed more than once" );
					break;
				}
			}
		}

		// プロパティ
		for ( int32 i = 0; i < Object.Props.Num(); ++i )
		{
			const FWorldProp& Prop = Object.Props[i];
			if ( Prop.Name.IsEmpty() )
			{
				Report.Error( EWorldIssueCode::EmptyPropName, Id, "a property has no name" );
				continue;
			}
			for ( int32 j = 0; j < i; ++j )
			{
				if ( Object.Props[j].Name == Prop.Name )
				{
					Report.Error( EWorldIssueCode::DuplicatePropName, Id, "the property '" + Prop.Name + "' is defined more than once" );
					break;
				}
			}
			switch ( Prop.Type )
			{
			case EWorldPropType::Float:
				if ( !std::isfinite( Prop.FloatValue ) ) Report.Error( EWorldIssueCode::BadPropValue, Id, "the property '" + Prop.Name + "' is not a finite number" );
				break;
			case EWorldPropType::Vector3:
				if ( !IsFinite( Prop.VectorValue ) ) Report.Error( EWorldIssueCode::BadPropValue, Id, "the property '" + Prop.Name + "' has a value that is not a finite number" );
				break;
			case EWorldPropType::Asset:
				if ( Prop.StringValue.IsEmpty() ) Report.Warning( EWorldIssueCode::EmptyAssetPath, Id, "the asset property '" + Prop.Name + "' is empty" );
				else if ( Assets != nullptr && !Assets->Exists( Prop.StringValue ) ) Report.Error( EWorldIssueCode::MissingAsset, Id, "the asset property '" + Prop.Name + "' points to '" + Prop.StringValue + "', which was not found" );
				break;
			case EWorldPropType::Object:
				if ( Prop.ObjectId == Id ) Report.Warning( EWorldIssueCode::MissingObjectReference, Id, "the property '" + Prop.Name + "' refers to the object itself" );
				else if ( World.FindObject( Prop.ObjectId ) == nullptr ) Report.Error( EWorldIssueCode::MissingObjectReference, Id, FString::Printf( "the property '%s' refers to object #%d, which does not exist", *Prop.Name, Prop.ObjectId ) );
				break;
			default: break;
			}
		}

		// モデル
		if ( Object.Model.IsSet() )
		{
			const FWorldModel& Model = Object.Model.GetValue();
			CheckAsset( Report, Assets, Id, "the model", Model.Path );
			CheckOptionalAsset( Report, Assets, Id, "the animation set", Model.Animations );
			if ( !IsFinite( Model.Speed ) ) Report.Error( EWorldIssueCode::NonFiniteValue, Id, "the model's animation speed is not a finite number" );
			else if ( !Model.Clip.IsEmpty() && Model.Speed == 0.0f ) Report.Warning( EWorldIssueCode::BadModel, Id, "the model plays a clip at speed 0 (it will not move)" );
		}

		// ビルボード
		if ( Object.Billboard.IsSet() )
		{
			const FWorldBillboard& Board = Object.Billboard.GetValue();
			CheckAsset( Report, Assets, Id, "the billboard's image", Board.Texture );
			if ( !IsFinite( Board.Size.x ) || !IsFinite( Board.Size.y ) || !IsFinite( Board.Pivot.x ) || !IsFinite( Board.Pivot.y ) )
			{
				Report.Error( EWorldIssueCode::NonFiniteValue, Id, "the billboard has a size or pivot that is not a finite number" );
			}
			else
			{
				if ( Board.Size.x <= 0.0f || Board.Size.y <= 0.0f ) Report.Error( EWorldIssueCode::BadBillboard, Id, "the billboard's size must be greater than 0" );
				if ( Board.Pivot.x < 0.0f || Board.Pivot.x > 1.0f || Board.Pivot.y < 0.0f || Board.Pivot.y > 1.0f ) Report.Warning( EWorldIssueCode::BadBillboard, Id, "the billboard's pivot is outside 0..1" );
			}
			if ( Board.bUseRect && ( Board.RectWidth <= 0 || Board.RectHeight <= 0 || Board.RectX < 0 || Board.RectY < 0 ) )
			{
				Report.Error( EWorldIssueCode::BadBillboard, Id, "the billboard's rectangle must have a positive size and start inside the image" );
			}
			if ( Board.Tint.a == 0 ) Report.Warning( EWorldIssueCode::BadBillboard, Id, "the billboard is fully transparent (alpha 0)" );
		}

		// 環境音
		if ( Object.Sound.IsSet() )
		{
			const FWorldSound& Sound = Object.Sound.GetValue();
			CheckAsset( Report, Assets, Id, "the sound", Sound.Path );
			EAudioBus Bus = EAudioBus::Ambient;
			if ( !TryParse( Sound.Bus, Bus ) ) Report.Error( EWorldIssueCode::BadSound, Id, "the sound's bus '" + Sound.Bus + "' is not one of Master, Music, Sfx, Ambient, Voice, Ui" );
			if ( !IsFinite( Sound.Volume ) || !IsFinite( Sound.Pitch ) || !IsFinite( Sound.MinDistance ) || !IsFinite( Sound.MaxDistance ) )
			{
				Report.Error( EWorldIssueCode::NonFiniteValue, Id, "the sound has a value that is not a finite number" );
			}
			else
			{
				if ( Sound.Volume < 0.0f ) Report.Error( EWorldIssueCode::BadSound, Id, "the sound's volume must not be negative" );
				if ( Sound.Pitch <= 0.0f ) Report.Error( EWorldIssueCode::BadSound, Id, "the sound's pitch must be greater than 0" );
				if ( Sound.MinDistance < 0.0f || Sound.MaxDistance <= Sound.MinDistance ) Report.Error( EWorldIssueCode::BadSound, Id, "the sound's MaxDistance must be greater than MinDistance (which must not be negative)" );
			}
		}

		// エフェクト
		if ( Object.Effect.IsSet() )
		{
			const FWorldEffect& Effect = Object.Effect.GetValue();
			CheckAsset( Report, Assets, Id, "the effect", Effect.Path );
			if ( !IsFinite( Effect.Scale ) || !IsFinite( Effect.Speed ) )
			{
				Report.Error( EWorldIssueCode::NonFiniteValue, Id, "the effect has a value that is not a finite number" );
			}
			else
			{
				if ( Effect.Scale <= 0.0f ) Report.Error( EWorldIssueCode::BadEffect, Id, "the effect's scale must be greater than 0" );
				if ( Effect.Speed < 0.0f ) Report.Error( EWorldIssueCode::BadEffect, Id, "the effect's speed must not be negative" );
			}
			if ( Effect.Seed < 0 ) Report.Error( EWorldIssueCode::BadEffect, Id, "the effect's seed must not be negative" );
			// 扱うバックエンドが無い種類のファイルは、置かれない（外部のライブラリを登録していなければ、ここで分かる）
			if ( !Effect.Path.IsEmpty() && FEffectBackendRegistry::Global().Find( Effect.Path ) == nullptr )
			{
				Report.Warning( EWorldIssueCode::BadEffect, Id, "no effect backend handles '" + Effect.Path + "' (the effect is not placed unless a backend for it is registered)" );
			}
		}

		// 入れ子のワールド
		if ( Object.Prefab.IsSet() )
		{
			const FString& Path = Object.Prefab.GetValue().Path;
			CheckAsset( Report, Assets, Id, "the prefab", Path );
			if ( !Path.IsEmpty() && !Path.EndsWith( ".world", ESearchCase::IgnoreCase ) ) Report.Warning( EWorldIssueCode::BadPrefab, Id, "the prefab '" + Path + "' is not a .world file" );
			if ( !Path.IsEmpty() && Assets != nullptr )
			{
				// このオブジェクトの Prefab から先をたどる（ほかのオブジェクトの問題は、そのオブジェクトの検証で出る）
				TArray<FString> Stack;
				FString Chain;
				const EPrefabProblem Problem = FindPathProblem( Path, *Assets, NWorld::NormalizeAssetPath( WorldPath ), Stack, Chain );
				if ( Problem == EPrefabProblem::Loop ) Report.Error( EWorldIssueCode::BadPrefab, Id, "the prefab leads back to itself (" + Chain + "): it is not placed" );
				else if ( Problem == EPrefabProblem::TooDeep ) Report.Error( EWorldIssueCode::BadPrefab, Id, FString::Printf( "the prefab is nested deeper than %d levels (at '%s'): it is not placed", WorldMaxPrefabDepth, *Chain ) );
			}
		}

		// 編集メッシュ
		if ( Object.Mesh.IsSet() )
		{
			const FEditMesh& Mesh = Object.Mesh.GetValue();
			for ( const FEditMeshIssue& Issue : Mesh.Validate() )
			{
				if ( Issue.bError ) Report.Error( EWorldIssueCode::BadMesh, Id, "the mesh: " + Issue.Message );
				else Report.Warning( EWorldIssueCode::BadMesh, Id, "the mesh: " + Issue.Message );
			}
			for ( const FEditMeshMaterial& Material : Mesh.Materials ) CheckOptionalAsset( Report, Assets, Id, "the mesh material's image", Material.Texture );
		}

		// 当たり判定の形
		if ( Object.Collider.IsSet() )
		{
			const FWorldCollider& Collider = Object.Collider.GetValue();
			if ( !IsFinite( Collider.Size ) || !IsFinite( Collider.Radius ) || !IsFinite( Collider.Height ) || !IsFinite( Collider.Offset ) )
			{
				Report.Error( EWorldIssueCode::NonFiniteValue, Id, "the collider has a value that is not a finite number" );
			}
			else
			{
				switch ( Collider.Shape )
				{
				case EWorldColliderShape::Box:
					if ( Collider.Size.x <= 0.0f || Collider.Size.y <= 0.0f || Collider.Size.z <= 0.0f ) Report.Error( EWorldIssueCode::BadCollider, Id, "the box collider's size must be greater than 0 on every axis" );
					break;
				case EWorldColliderShape::Sphere:
					if ( Collider.Radius <= 0.0f ) Report.Error( EWorldIssueCode::BadCollider, Id, "the sphere collider's radius must be greater than 0" );
					break;
				case EWorldColliderShape::Capsule:
					if ( Collider.Radius <= 0.0f ) Report.Error( EWorldIssueCode::BadCollider, Id, "the capsule collider's radius must be greater than 0" );
					else if ( Collider.Height < Collider.Radius * 2.0f ) Report.Error( EWorldIssueCode::BadCollider, Id, "the capsule collider's height must be at least twice its radius" );
					break;
				case EWorldColliderShape::Mesh:
				{
					const FString MeshPath = NWorld::GetColliderMeshPath( Object );
					if ( NWorld::UsesEditMeshCollider( Object ) )
					{
						if ( Object.Mesh.GetValue().GetNumTriangles() == 0 ) Report.Warning( EWorldIssueCode::BadCollider, Id, "the mesh collider uses the object's Mesh component, which has no usable faces (nothing to collide with)" );
					}
					else if ( MeshPath.IsEmpty() ) Report.Error( EWorldIssueCode::BadCollider, Id, "the mesh collider has no triangles to use: add a Mesh or Model component, or set the collider's Mesh file" );
					else CheckOptionalAsset( Report, Assets, Id, "the mesh collider's model", MeshPath );
					if ( Collider.bTrigger ) Report.Warning( EWorldIssueCode::BadCollider, Id, "a trigger with a mesh shape only detects bodies that are moving (a body that comes to rest inside is reported as leaving), and not one that is completely inside: a box, sphere or capsule is safer" );
					break;
				}
				}
			}
		}
		else if ( Object.Collision == EWorldCollisionMode::On )
		{
			Report.Warning( EWorldIssueCode::CollisionWithoutCollider, Id, "Collision is set to On, but the object has no collider shape" );
		}

		// 知らない部品
		for ( const FWorldRawComponent& Raw : Object.RawComponents )
		{
			Report.Warning( EWorldIssueCode::UnsupportedComponent, Id, "the component '" + Raw.Type + "' is not supported by this version (it is kept as it is, and not placed)" );
		}
	}
}

TArray<FWorldIssue> NWorld::ValidateWorld( const FWorldDefinition& World, const IWorldAssetQuery* Assets, const FString& WorldPath )
{
	FReporter Report;

	if ( World.Version > WorldFormatVersion )
	{
		Report.Warning( EWorldIssueCode::NewerVersion, WorldNoId, FString::Printf( "the world is version %d, newer than this program's version %d", World.Version, WorldFormatVersion ) );
	}

	// Id
	int32 MaxId = 0;
	for ( int32 i = 0; i < World.Objects.Num(); ++i )
	{
		const FWorldObject& Object = World.Objects[i];
		if ( Object.Id <= WorldNoId )
		{
			Report.Error( EWorldIssueCode::InvalidId, WorldNoId, FString::Printf( "object #%d ('%s') has an invalid id (%d)", i, *Object.Name, Object.Id ) );
			continue;
		}
		MaxId = Object.Id > MaxId ? Object.Id : MaxId;
		for ( int32 j = 0; j < i; ++j )
		{
			if ( World.Objects[j].Id == Object.Id )
			{
				Report.Error( EWorldIssueCode::DuplicateId, Object.Id, FString::Printf( "the id %d is used by more than one object ('%s' and '%s')", Object.Id, *World.Objects[j].Name, *Object.Name ) );
				break;
			}
		}
	}
	if ( World.NextId <= MaxId ) Report.Warning( EWorldIssueCode::NextIdTooSmall, WorldNoId, FString::Printf( "NextId (%d) is not greater than the largest id in use (%d)", World.NextId, MaxId ) );

	// タグ規則
	for ( int32 i = 0; i < World.Settings.CollisionRules.Num(); ++i )
	{
		const FWorldCollisionRule& Rule = World.Settings.CollisionRules[i];
		if ( Rule.Tag.TrimStartAndEnd().IsEmpty() )
		{
			Report.Error( EWorldIssueCode::EmptyRuleTag, WorldNoId, FString::Printf( "collision rule #%d has no tag", i ) );
			continue;
		}
		for ( int32 j = 0; j < i; ++j )
		{
			if ( World.Settings.CollisionRules[j].Tag == Rule.Tag )
			{
				Report.Warning( EWorldIssueCode::ShadowedRule, WorldNoId, FString::Printf( "collision rule #%d for the tag '%s' never applies (rule #%d comes first)", i, *Rule.Tag, j ) );
				break;
			}
		}
	}

	// 階層
	for ( const FWorldObject& Object : World.Objects )
	{
		if ( Object.Id <= WorldNoId || Object.Parent == WorldNoId ) continue;
		if ( Object.Parent == Object.Id ) Report.Error( EWorldIssueCode::SelfParent, Object.Id, "the object is its own parent" );
		else if ( World.FindObject( Object.Parent ) == nullptr ) Report.Error( EWorldIssueCode::MissingParent, Object.Id, FString::Printf( "the parent #%d does not exist", Object.Parent ) );
	}
	const NWorld::FWorldSpawnOrder Order = NWorld::ComputeSpawnOrder( World, false );
	for ( const int32 Index : Order.Cyclic )
	{
		const FWorldObject& Object = World.Objects[Index];
		if ( Object.Parent == Object.Id ) continue;   // 自分が親のものは、上で報告した
		Report.Error( EWorldIssueCode::ParentCycle, Object.Id, "this object is part of a loop of parents, or hangs below one" );
	}

	for ( const FWorldObject& Object : World.Objects )
	{
		if ( Object.Id <= WorldNoId ) continue;
		CheckObject( Report, World, Object, Assets, WorldPath );
	}
	return Report.Issues;
}

bool NWorld::HasErrors( const TArray<FWorldIssue>& Issues )
{
	for ( const FWorldIssue& Issue : Issues )
	{
		if ( Issue.Severity == EWorldIssueSeverity::Error ) return true;
	}
	return false;
}
