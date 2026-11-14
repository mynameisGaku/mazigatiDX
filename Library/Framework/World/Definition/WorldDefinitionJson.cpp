#include "WorldDefinitionJson.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Math/MathUtils.h"

namespace NWorldDefinitionJson
{
	void Warn( TArray<FString>* Warnings, const FString& Message )
	{
		if ( Warnings != nullptr )
		{
			Warnings->Add( Message );
		}
	}

	float64 Round6( float64 Value )
	{
		return NMath::Round( Value * 1000000.0 ) / 1000000.0;
	}

	bool IsNearlyEqual( const FVector3& A, const FVector3& B )
	{
		return A.x == B.x && A.y == B.y && A.z == B.z;
	}

	NlohmannJson Vector2ToJson( const FVector2& V )
	{
		return NlohmannJson::array( { NJson::Round4( V.x ), NJson::Round4( V.y ) } );
	}

	FVector2 ReadVector2( const NlohmannJson& Object, const char* Key, const FVector2& Default )
	{
		if ( !Object.is_object() )
		{
			return Default;
		}
		const auto It = Object.find( Key );
		if ( It == Object.end() || !It->is_array() || It->size() < 2 )
		{
			return Default;
		}
		try
		{
			return FVector2( ( *It )[0].get<float32>(), ( *It )[1].get<float32>() );
		}
		catch ( const nlohmann::json::exception& )
		{
			return Default;
		}
	}

	bool EqualsIgnoreCase( const FString& A, const char* B )
	{
		return A.Equals( B, ESearchCase::IgnoreCase );
	}

	// 非 null の数の配列か
	bool IsNumberArray( const NlohmannJson& Json, size_t Count )
	{
		if ( !Json.is_array() || Json.size() != Count )
		{
			return false;
		}
		for ( const NlohmannJson& Item : Json )
		{
			if ( !Item.is_number() )
			{
				return false;
			}
		}
		return true;
	}

	//-------------------------------------------------------------------------
	//-------------------------------------------------------------------------
	// オブジェクト
	//-------------------------------------------------------------------------
	NlohmannJson TransformToJson( const FWorldTransform& Transform )
	{
		NlohmannJson Json = NlohmannJson::object();
		if ( !IsNearlyEqual( Transform.Position, FVector3::Zero ) )
		{
			Json["Position"] = NJson::ToJson( Transform.Position );
		}
		if ( !IsNearlyEqual( Transform.RotationDegrees, FVector3::Zero ) )
		{
			Json["Rotation"] = NJson::ToJson( Transform.RotationDegrees );
		}
		if ( !IsNearlyEqual( Transform.Scale, FVector3::One ) )
		{
			Json["Scale"] = NJson::ToJson( Transform.Scale );
		}
		return Json;
	}

	FWorldTransform TransformFromJson( const NlohmannJson& Json )
	{
		FWorldTransform Transform;
		Transform.Position = NJson::ReadVector3( Json, "Position", Transform.Position );
		Transform.RotationDegrees = NJson::ReadVector3( Json, "Rotation", Transform.RotationDegrees );
		Transform.Scale = NJson::ReadVector3( Json, "Scale", Transform.Scale );
		return Transform;
	}

	NlohmannJson ObjectToJson( const FWorldObject& Object )
	{
		NlohmannJson Json = NlohmannJson::object();
		// 知らない項目を先に入れて、知っている項目で上書きする（知らない項目が、知っている項目を壊さない）
		if ( Object.Extra.is_object() )
		{
			for ( auto It = Object.Extra.begin(); It != Object.Extra.end(); ++It )
			{
				Json[It.key()] = It.value();
			}
		}
		Json["Id"] = Object.Id;
		if ( !Object.Name.IsEmpty() )
		{
			Json["Name"] = Object.Name;
		}
		if ( Object.Parent != WorldNoId )
		{
			Json["Parent"] = Object.Parent;
		}
		if ( !Object.bEnabled )
		{
			Json["Enabled"] = false;
		}
		if ( !Object.Tags.IsEmpty() )
		{
			NlohmannJson Tags = NlohmannJson::array();
			for ( const FString& Tag : Object.Tags )
			{
				Tags.push_back( Tag );
			}
			Json["Tags"] = Tags;
		}
		if ( !Object.Props.IsEmpty() )
		{
			NlohmannJson Props = NlohmannJson::object();
			for ( const FWorldProp& Prop : Object.Props )
			{
				Props[NJson::ToJsonString( Prop.Name )] = Prop.ValueToJson();
			}
			Json["Props"] = Props;
		}
		const NlohmannJson Transform = TransformToJson( Object.Transform );
		if ( !Transform.empty() )
		{
			Json["Transform"] = Transform;
		}
		if ( Object.Collision != EWorldCollisionMode::Auto )
		{
			Json["Collision"] = ToString( Object.Collision );
		}

		NlohmannJson Components = NlohmannJson::object();
		for ( const FWorldRawComponent& Raw : Object.RawComponents )
		{
			Components[NJson::ToJsonString( Raw.Type )] = Raw.Data;
		}
		if ( Object.Model.IsSet() )
		{
			Components["Model"] = ModelToJson( Object.Model.GetValue() );
		}
		if ( Object.Billboard.IsSet() )
		{
			Components["Billboard"] = BillboardToJson( Object.Billboard.GetValue() );
		}
		if ( Object.Sound.IsSet() )
		{
			Components["Sound"] = SoundToJson( Object.Sound.GetValue() );
		}
		if ( Object.Effect.IsSet() )
		{
			Components["Effect"] = EffectToJson( Object.Effect.GetValue() );
		}
		if ( Object.Collider.IsSet() )
		{
			Components["Collider"] = ColliderToJson( Object.Collider.GetValue() );
		}
		if ( Object.Mesh.IsSet() )
		{
			Components["Mesh"] = Object.Mesh.GetValue().ToJson();
		}
		if ( Object.Prefab.IsSet() )
		{
			Components["Prefab"] = PrefabToJson( Object.Prefab.GetValue() );
		}
		if ( !Components.empty() )
		{
			Json["Components"] = Components;
		}
		return Json;
	}

	FWorldObject ObjectFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, int32 Index )
	{
		FWorldObject Object;
		Object.Id = NJson::GetOr<int32>( Json, "Id", WorldNoId );
		Object.Name = NJson::GetOr<FString>( Json, "Name", FString() );
		Object.Parent = NJson::GetOr<int32>( Json, "Parent", WorldNoId );
		if ( Object.Parent < 0 )
		{
			Object.Parent = WorldNoId;
		}
		Object.bEnabled = NJson::GetOr<bool>( Json, "Enabled", true );
		const FString Where = FString::Printf( "object #%d ('%s')", Index, *Object.Name );

		if ( Json.contains( "Tags" ) && Json["Tags"].is_array() )
		{
			for ( const NlohmannJson& Item : Json["Tags"] )
			{
				if ( !Item.is_string() )
				{
					Warn( Warnings, Where + ": a tag that is not a string was skipped" );
					continue;
				}
				const FString Tag = NJson::FromJsonString( Item.get<NJson::FJsonString>() ).TrimStartAndEnd();
				if ( Tag.IsEmpty() || !Object.AddTag( Tag ) )
				{
					Warn( Warnings, Where + ": an empty or duplicate tag '" + Tag + "' was skipped" );
				}
			}
		}

		if ( Json.contains( "Props" ) && Json["Props"].is_object() )
		{
			for ( auto It = Json["Props"].begin(); It != Json["Props"].end(); ++It )
			{
				FWorldProp Prop;
				const FString PropName = NJson::FromJsonString( It.key() );
				if ( PropName.IsEmpty() || !FWorldProp::FromJsonValue( PropName, It.value(), Prop ) )
				{
					Warn( Warnings, Where + ": the property '" + PropName + "' has a value of an unsupported form and was skipped" );
					continue;
				}
				Object.SetProp( Prop );
			}
		}

		if ( Json.contains( "Transform" ) )
		{
			Object.Transform = TransformFromJson( Json["Transform"] );
		}
		if ( Json.contains( "Collision" ) )
		{
			const FString Text = NJson::GetOr<FString>( Json, "Collision", FString() );
			if ( !TryParse( Text, Object.Collision ) )
			{
				Warn( Warnings, Where + ": unknown collision mode '" + Text + "' (using Auto)" );
			}
		}

		if ( Json.contains( "Components" ) && Json["Components"].is_object() )
		{
			for ( auto It = Json["Components"].begin(); It != Json["Components"].end(); ++It )
			{
				const NJson::FJsonString Key = It.key();
				const NlohmannJson& Value = It.value();
				if ( Key == "Model" && Value.is_object() )
				{
					Object.Model = ModelFromJson( Value );
				}
				else if ( Key == "Billboard" && Value.is_object() )
				{
					Object.Billboard = BillboardFromJson( Value, Warnings, Where );
				}
				else if ( Key == "Sound" && Value.is_object() )
				{
					Object.Sound = SoundFromJson( Value );
				}
				else if ( Key == "Effect" && Value.is_object() )
				{
					Object.Effect = EffectFromJson( Value );
				}
				else if ( Key == "Collider" && Value.is_object() )
				{
					Object.Collider = ColliderFromJson( Value, Warnings, Where );
				}
				else if ( Key == "Mesh" && Value.is_object() )
				{
					FEditMesh Mesh;
					TArray<FString> MeshWarnings;
					FEditMesh::FromJson( Value, Mesh, &MeshWarnings );
					for ( const FString& Message : MeshWarnings )
					{
						Warn( Warnings, Where + ": " + Message );
					}
					Object.Mesh = Mesh;
				}
				else if ( Key == "Prefab" && Value.is_object() )
				{
					Object.Prefab = PrefabFromJson( Value );
				}
				else
				{
					// 知らない種類（と、形が壊れた既知の種類）は、そのまま残す
					FWorldRawComponent Raw;
					Raw.Type = NJson::FromJsonString( Key );
					Raw.Data = Value;
					Object.RawComponents.Add( Raw );
					if ( IsKnownKey( KnownComponentTypes, Key ) )
					{
						Warn( Warnings, Where + ": the " + Raw.Type + " component is not an object and was kept as it is" );
					}
				}
			}
		}

		if ( Json.is_object() )
		{
			for ( auto It = Json.begin(); It != Json.end(); ++It )
			{
				if ( !IsKnownKey( KnownObjectKeys, It.key() ) )
				{
					if ( !Object.Extra.is_object() )
					{
						Object.Extra = NlohmannJson::object();
					}
					Object.Extra[It.key()] = It.value();
				}
			}
		}
		return Object;
	}
}
