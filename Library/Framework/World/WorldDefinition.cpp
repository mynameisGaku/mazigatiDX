#include "WorldDefinition.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Library/Json/JsonHelpers.h"
#include "Library/Math/MathUtils.h"

//=============================================================================
// ワールドの定義ファイル（JSON、拡張子は .world）の書式
//
//   {
//     "Version": 1,
//     "Name": "Stage1",
//     "NextId": 12,                                       // 次に割り当てる Id（オブジェクトを消しても戻らない）
//     "Settings": {
//       "CollisionRules": [                               // 当たり判定のタグ規則（上から見て、最初に一致したもの）
//         { "Tag": "Decoration", "Collision": "Off" },
//         { "Tag": "Wall", "Collision": "On" }
//       ]
//     },
//     "Objects": [
//       {
//         "Id": 3,                                        // 必須（無ければ、読み込み時に新しく振る）。1 以上で、ワールドの中で一意
//         "Name": "Torch",
//         "Parent": 1,                                    // 省略なら最上位
//         "Enabled": false,                               // 省略なら true。false なら実行時に置かない
//         "Tags": [ "Light", "Interactable" ],
//         "Props": { "Damage": 5, "Speed": 2.5, "Label": "x", "Offset": [0, 1, 0], "Next": { "Object": 4 }, "Sound": { "Asset": "Sounds/a.wav" } },
//         "Transform": { "Position": [0, 1, 0], "Rotation": [0, 90, 0], "Scale": [1, 1, 1] },   // 角度は度（Pitch, Yaw, Roll）。省略した項目は既定値
//         "Collision": "Auto",                            // "Auto"（既定）/ "On" / "Off"
//         "Components": {                                 // 種類ごとに 1 つまで。知らない種類は、そのまま保存される
//           "Model": { "Path": "Models/Torch.mv1", "Animations": "Models/Torch.modelanim.json", "Clip": "Burn", "Speed": 1 },
//           "Billboard": { "Texture": "Sprites/Glow.png", "Size": [1, 1], "Pivot": [0.5, 0.5], "Mode": "CameraY", "Tint": "#FFFFFF80",
//                          "Rect": [0, 0, 64, 64], "FlipX": false, "FlipY": false },
//           "Sound": { "Path": "Sounds/Fire.wav", "Bus": "Ambient", "Volume": 1, "Pitch": 1, "Loop": true, "AutoPlay": true, "MinDistance": 1, "MaxDistance": 25 },
//           "Effect": { "Path": "Effects/Fire.effect.json", "AutoPlay": true, "Scale": 1, "Speed": 1, "Seed": 0, "Loop": true },   // Loop を省略すると、エフェクトの指定のまま
//           "Collider": { "Shape": "Box", "Size": [1, 2, 1], "Radius": 0.5, "Height": 2, "Offset": [0, 1, 0], "Trigger": false,
//                         "Mesh": "Models/Floor.mv1" }     // Shape が "Mesh" のとき、三角形を取るモデル（省略なら、Model 部品のファイル）
//           "Mesh": { "Vertices": [[0, 0, 0], [1, 0, 0], ...], "Faces": [[0, 1, 2, 3], ...], "FaceMaterials": [0, 1, ...],   // 編集メッシュ（EditMesh.h に詳しい）
//                     "Materials": [{ "Name": "Wall", "Color": "#C8C8C8", "Texture": "Textures/Brick.png", "Tile": 2, "Offset": [0, 0], "TwoSided": false }], "Smooth": 30 },
//           "Prefab": { "Path": "Prefabs/Door.world" }   // 別の .world を、このオブジェクトの子として置く（入れ子。深さは 8 まで。循環は置かれない）
//         }
//       }
//     ]
//   }
//
//   既定値と同じ項目は書き出さない（ファイルを小さく、差分を読みやすく保つ）。
//=============================================================================

namespace
{
	constexpr const char* KnownWorldKeys[] = { "Version", "Name", "NextId", "Settings", "Objects" };
	constexpr const char* KnownObjectKeys[] = { "Id", "Name", "Parent", "Enabled", "Tags", "Props", "Transform", "Collision", "Components" };
	constexpr const char* KnownComponentTypes[] = { "Model", "Billboard", "Sound", "Effect", "Collider", "Mesh", "Prefab" };

	template<size_t N>
	bool IsKnownKey( const char* const ( &Keys )[N], const std::string& Key )
	{
		for ( const char* Known : Keys )
		{
			if ( Key == Known ) return true;
		}
		return false;
	}

	void Warn( TArray<FString>* Warnings, const FString& Message )
	{
		if ( Warnings != nullptr ) Warnings->Add( Message );
	}

	float64 Round6( float64 Value )
	{
		return std::round( Value * 1000000.0 ) / 1000000.0;
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
		if ( !Object.is_object() ) return Default;
		const auto It = Object.find( Key );
		if ( It == Object.end() || !It->is_array() || It->size() < 2 ) return Default;
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
		if ( !Json.is_array() || Json.size() != Count ) return false;
		for ( const NlohmannJson& Item : Json )
		{
			if ( !Item.is_number() ) return false;
		}
		return true;
	}

	//-------------------------------------------------------------------------
	// 部品
	//-------------------------------------------------------------------------
	NlohmannJson ModelToJson( const FWorldModel& Model )
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Path"] = Model.Path;
		if ( !Model.Animations.IsEmpty() ) Json["Animations"] = Model.Animations;
		if ( !Model.Clip.IsEmpty() ) Json["Clip"] = Model.Clip;
		if ( Model.Speed != 1.0f ) Json["Speed"] = NJson::Round4( Model.Speed );
		if ( Model.MaterialEffect.IsSet() ) Json["MaterialEffect"] = Model.MaterialEffect.GetValue().ToJson();
		return Json;
	}

	FWorldModel ModelFromJson( const NlohmannJson& Json )
	{
		FWorldModel Model;
		Model.Path = NJson::GetOr<FString>( Json, "Path", FString() );
		Model.Animations = NJson::GetOr<FString>( Json, "Animations", FString() );
		Model.Clip = NJson::GetOr<FString>( Json, "Clip", FString() );
		Model.Speed = NJson::GetOr<float32>( Json, "Speed", 1.0f );
		if ( Json.contains( "MaterialEffect" ) )
		{
			// nullや配列も設定済みbindingとして残し、Validateへ渡す。
			FWorldModelMaterialEffect MaterialEffect;
			FWorldModelMaterialEffect::FromJson( Json["MaterialEffect"], MaterialEffect );
			Model.MaterialEffect = std::move( MaterialEffect );
		}
		return Model;
	}

	NlohmannJson PrefabToJson( const FWorldPrefab& Prefab )
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Path"] = Prefab.Path;
		return Json;
	}

	FWorldPrefab PrefabFromJson( const NlohmannJson& Json )
	{
		FWorldPrefab Prefab;
		Prefab.Path = NJson::GetOr<FString>( Json, "Path", FString() );
		return Prefab;
	}

	NlohmannJson BillboardToJson( const FWorldBillboard& Board )
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Texture"] = Board.Texture;
		if ( Board.Size.x != 1.0f || Board.Size.y != 1.0f ) Json["Size"] = Vector2ToJson( Board.Size );
		if ( Board.Pivot.x != 0.5f || Board.Pivot.y != 0.5f ) Json["Pivot"] = Vector2ToJson( Board.Pivot );
		if ( Board.Mode != EWorldBillboardMode::Camera ) Json["Mode"] = ToString( Board.Mode );
		const FColor32 White( 255, 255, 255, 255 );
		if ( Board.Tint.r != White.r || Board.Tint.g != White.g || Board.Tint.b != White.b || Board.Tint.a != White.a ) Json["Tint"] = NWorld::FormatColor( Board.Tint );
		if ( Board.bUseRect ) Json["Rect"] = NlohmannJson::array( { Board.RectX, Board.RectY, Board.RectWidth, Board.RectHeight } );
		if ( Board.bFlipX ) Json["FlipX"] = true;
		if ( Board.bFlipY ) Json["FlipY"] = true;
		return Json;
	}

	FWorldBillboard BillboardFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, const FString& Where )
	{
		FWorldBillboard Board;
		Board.Texture = NJson::GetOr<FString>( Json, "Texture", FString() );
		Board.Size = ReadVector2( Json, "Size", Board.Size );
		Board.Pivot = ReadVector2( Json, "Pivot", Board.Pivot );
		if ( Json.contains( "Mode" ) )
		{
			const FString Text = NJson::GetOr<FString>( Json, "Mode", FString() );
			if ( !TryParse( Text, Board.Mode ) ) Warn( Warnings, Where + ": unknown billboard mode '" + Text + "' (using Camera)" );
		}
		if ( Json.contains( "Tint" ) )
		{
			const FString Text = NJson::GetOr<FString>( Json, "Tint", FString() );
			if ( !NWorld::TryParseColor( Text, Board.Tint ) ) Warn( Warnings, Where + ": bad tint color '" + Text + "' (using white)" );
		}
		if ( Json.contains( "Rect" ) && IsNumberArray( Json["Rect"], 4 ) )
		{
			Board.bUseRect = true;
			Board.RectX = Json["Rect"][0].get<int32>();
			Board.RectY = Json["Rect"][1].get<int32>();
			Board.RectWidth = Json["Rect"][2].get<int32>();
			Board.RectHeight = Json["Rect"][3].get<int32>();
		}
		Board.bFlipX = NJson::GetOr<bool>( Json, "FlipX", false );
		Board.bFlipY = NJson::GetOr<bool>( Json, "FlipY", false );
		return Board;
	}

	NlohmannJson SoundToJson( const FWorldSound& Sound )
	{
		const FWorldSound Default;
		NlohmannJson Json = NlohmannJson::object();
		Json["Path"] = Sound.Path;
		if ( Sound.Bus != Default.Bus ) Json["Bus"] = Sound.Bus;
		if ( Sound.Volume != Default.Volume ) Json["Volume"] = NJson::Round4( Sound.Volume );
		if ( Sound.Pitch != Default.Pitch ) Json["Pitch"] = NJson::Round4( Sound.Pitch );
		if ( Sound.bLoop != Default.bLoop ) Json["Loop"] = Sound.bLoop;
		if ( Sound.bAutoPlay != Default.bAutoPlay ) Json["AutoPlay"] = Sound.bAutoPlay;
		if ( Sound.MinDistance != Default.MinDistance ) Json["MinDistance"] = NJson::Round4( Sound.MinDistance );
		if ( Sound.MaxDistance != Default.MaxDistance ) Json["MaxDistance"] = NJson::Round4( Sound.MaxDistance );
		return Json;
	}

	NlohmannJson EffectToJson( const FWorldEffect& Effect )
	{
		const FWorldEffect Default;
		NlohmannJson Json = NlohmannJson::object();
		Json["Path"] = Effect.Path;
		if ( Effect.bAutoPlay != Default.bAutoPlay ) Json["AutoPlay"] = Effect.bAutoPlay;
		if ( Effect.Scale != Default.Scale ) Json["Scale"] = NJson::Round4( Effect.Scale );
		if ( Effect.Speed != Default.Speed ) Json["Speed"] = NJson::Round4( Effect.Speed );
		if ( Effect.Seed != Default.Seed ) Json["Seed"] = Effect.Seed;
		if ( Effect.Loop >= 0 ) Json["Loop"] = Effect.Loop != 0;
		return Json;
	}

	FWorldEffect EffectFromJson( const NlohmannJson& Json )
	{
		FWorldEffect Effect;
		Effect.Path = NJson::GetOr<FString>( Json, "Path", FString() );
		Effect.bAutoPlay = NJson::GetOr<bool>( Json, "AutoPlay", Effect.bAutoPlay );
		Effect.Scale = NJson::GetOr<float32>( Json, "Scale", Effect.Scale );
		Effect.Speed = NJson::GetOr<float32>( Json, "Speed", Effect.Speed );
		Effect.Seed = NJson::GetOr<int32>( Json, "Seed", Effect.Seed );
		if ( Json.contains( "Loop" ) && Json["Loop"].is_boolean() ) Effect.Loop = Json["Loop"].get<bool>() ? 1 : 0;
		return Effect;
	}

	FWorldSound SoundFromJson( const NlohmannJson& Json )
	{
		FWorldSound Sound;
		Sound.Path = NJson::GetOr<FString>( Json, "Path", FString() );
		Sound.Bus = NJson::GetOr<FString>( Json, "Bus", Sound.Bus );
		Sound.Volume = NJson::GetOr<float32>( Json, "Volume", Sound.Volume );
		Sound.Pitch = NJson::GetOr<float32>( Json, "Pitch", Sound.Pitch );
		Sound.bLoop = NJson::GetOr<bool>( Json, "Loop", Sound.bLoop );
		Sound.bAutoPlay = NJson::GetOr<bool>( Json, "AutoPlay", Sound.bAutoPlay );
		Sound.MinDistance = NJson::GetOr<float32>( Json, "MinDistance", Sound.MinDistance );
		Sound.MaxDistance = NJson::GetOr<float32>( Json, "MaxDistance", Sound.MaxDistance );
		return Sound;
	}

	NlohmannJson ColliderToJson( const FWorldCollider& Collider )
	{
		const FWorldCollider Default;
		NlohmannJson Json = NlohmannJson::object();
		Json["Shape"] = ToString( Collider.Shape );
		// 使う項目だけ書く（形に関係のない項目は、既定値のまま）
		if ( Collider.Shape == EWorldColliderShape::Box ) Json["Size"] = NJson::ToJson( Collider.Size );
		else if ( Collider.Shape != EWorldColliderShape::Mesh ) Json["Radius"] = NJson::Round4( Collider.Radius );
		if ( Collider.Shape == EWorldColliderShape::Capsule ) Json["Height"] = NJson::Round4( Collider.Height );
		if ( Collider.Shape == EWorldColliderShape::Mesh && !Collider.Mesh.IsEmpty() ) Json["Mesh"] = Collider.Mesh.ToUtf8().GetData();
		if ( Collider.Shape != EWorldColliderShape::Mesh && !IsNearlyEqual( Collider.Offset, Default.Offset ) ) Json["Offset"] = NJson::ToJson( Collider.Offset );
		if ( Collider.bTrigger ) Json["Trigger"] = true;
		return Json;
	}

	FWorldCollider ColliderFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, const FString& Where )
	{
		FWorldCollider Collider;
		if ( Json.contains( "Shape" ) )
		{
			const FString Text = NJson::GetOr<FString>( Json, "Shape", FString() );
			if ( !TryParse( Text, Collider.Shape ) ) Warn( Warnings, Where + ": unknown collider shape '" + Text + "' (using Box)" );
		}
		Collider.Size = NJson::ReadVector3( Json, "Size", Collider.Size );
		Collider.Radius = NJson::GetOr<float32>( Json, "Radius", Collider.Radius );
		Collider.Height = NJson::GetOr<float32>( Json, "Height", Collider.Height );
		Collider.Offset = NJson::ReadVector3( Json, "Offset", Collider.Offset );
		Collider.Mesh = NJson::GetOr<FString>( Json, "Mesh", FString() );
		Collider.bTrigger = NJson::GetOr<bool>( Json, "Trigger", false );
		return Collider;
	}

	//-------------------------------------------------------------------------
	// オブジェクト
	//-------------------------------------------------------------------------
	NlohmannJson TransformToJson( const FWorldTransform& Transform )
	{
		NlohmannJson Json = NlohmannJson::object();
		if ( !IsNearlyEqual( Transform.Position, FVector3::Zero ) ) Json["Position"] = NJson::ToJson( Transform.Position );
		if ( !IsNearlyEqual( Transform.RotationDegrees, FVector3::Zero ) ) Json["Rotation"] = NJson::ToJson( Transform.RotationDegrees );
		if ( !IsNearlyEqual( Transform.Scale, FVector3::One ) ) Json["Scale"] = NJson::ToJson( Transform.Scale );
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
			for ( auto It = Object.Extra.begin(); It != Object.Extra.end(); ++It ) Json[It.key()] = It.value();
		}
		Json["Id"] = Object.Id;
		if ( !Object.Name.IsEmpty() ) Json["Name"] = Object.Name;
		if ( Object.Parent != WorldNoId ) Json["Parent"] = Object.Parent;
		if ( !Object.bEnabled ) Json["Enabled"] = false;
		if ( !Object.Tags.IsEmpty() )
		{
			NlohmannJson Tags = NlohmannJson::array();
			for ( const FString& Tag : Object.Tags ) Tags.push_back( Tag );
			Json["Tags"] = Tags;
		}
		if ( !Object.Props.IsEmpty() )
		{
			NlohmannJson Props = NlohmannJson::object();
			for ( const FWorldProp& Prop : Object.Props ) Props[*Prop.Name.ToUtf8()] = Prop.ValueToJson();
			Json["Props"] = Props;
		}
		const NlohmannJson Transform = TransformToJson( Object.Transform );
		if ( !Transform.empty() ) Json["Transform"] = Transform;
		if ( Object.Collision != EWorldCollisionMode::Auto ) Json["Collision"] = ToString( Object.Collision );

		NlohmannJson Components = NlohmannJson::object();
		for ( const FWorldRawComponent& Raw : Object.RawComponents ) Components[*Raw.Type.ToUtf8()] = Raw.Data;
		if ( Object.Model.IsSet() ) Components["Model"] = ModelToJson( Object.Model.GetValue() );
		if ( Object.Billboard.IsSet() ) Components["Billboard"] = BillboardToJson( Object.Billboard.GetValue() );
		if ( Object.Sound.IsSet() ) Components["Sound"] = SoundToJson( Object.Sound.GetValue() );
		if ( Object.Effect.IsSet() ) Components["Effect"] = EffectToJson( Object.Effect.GetValue() );
		if ( Object.Collider.IsSet() ) Components["Collider"] = ColliderToJson( Object.Collider.GetValue() );
		if ( Object.Mesh.IsSet() ) Components["Mesh"] = Object.Mesh.GetValue().ToJson();
		if ( Object.Prefab.IsSet() ) Components["Prefab"] = PrefabToJson( Object.Prefab.GetValue() );
		if ( !Components.empty() ) Json["Components"] = Components;
		return Json;
	}

	FWorldObject ObjectFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, int32 Index )
	{
		FWorldObject Object;
		Object.Id = NJson::GetOr<int32>( Json, "Id", WorldNoId );
		Object.Name = NJson::GetOr<FString>( Json, "Name", FString() );
		Object.Parent = NJson::GetOr<int32>( Json, "Parent", WorldNoId );
		if ( Object.Parent < 0 ) Object.Parent = WorldNoId;
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
				const FString Tag = FString::FromUtf8( Item.get<std::string>().c_str() ).TrimStartAndEnd();
				if ( Tag.IsEmpty() || !Object.AddTag( Tag ) ) Warn( Warnings, Where + ": an empty or duplicate tag '" + Tag + "' was skipped" );
			}
		}

		if ( Json.contains( "Props" ) && Json["Props"].is_object() )
		{
			for ( auto It = Json["Props"].begin(); It != Json["Props"].end(); ++It )
			{
				FWorldProp Prop;
				const FString PropName = FString::FromUtf8( It.key().c_str() );
				if ( PropName.IsEmpty() || !FWorldProp::FromJsonValue( PropName, It.value(), Prop ) )
				{
					Warn( Warnings, Where + ": the property '" + PropName + "' has a value of an unsupported form and was skipped" );
					continue;
				}
				Object.SetProp( Prop );
			}
		}

		if ( Json.contains( "Transform" ) ) Object.Transform = TransformFromJson( Json["Transform"] );
		if ( Json.contains( "Collision" ) )
		{
			const FString Text = NJson::GetOr<FString>( Json, "Collision", FString() );
			if ( !TryParse( Text, Object.Collision ) ) Warn( Warnings, Where + ": unknown collision mode '" + Text + "' (using Auto)" );
		}

		if ( Json.contains( "Components" ) && Json["Components"].is_object() )
		{
			for ( auto It = Json["Components"].begin(); It != Json["Components"].end(); ++It )
			{
				const std::string Key = It.key();
				const NlohmannJson& Value = It.value();
				if ( Key == "Model" && Value.is_object() ) Object.Model = ModelFromJson( Value );
				else if ( Key == "Billboard" && Value.is_object() ) Object.Billboard = BillboardFromJson( Value, Warnings, Where );
				else if ( Key == "Sound" && Value.is_object() ) Object.Sound = SoundFromJson( Value );
				else if ( Key == "Effect" && Value.is_object() ) Object.Effect = EffectFromJson( Value );
				else if ( Key == "Collider" && Value.is_object() ) Object.Collider = ColliderFromJson( Value, Warnings, Where );
				else if ( Key == "Mesh" && Value.is_object() )
				{
					FEditMesh Mesh;
					TArray<FString> MeshWarnings;
					FEditMesh::FromJson( Value, Mesh, &MeshWarnings );
					for ( const FString& Message : MeshWarnings ) Warn( Warnings, Where + ": " + Message );
					Object.Mesh = Mesh;
				}
				else if ( Key == "Prefab" && Value.is_object() ) Object.Prefab = PrefabFromJson( Value );
				else
				{
					// 知らない種類（と、形が壊れた既知の種類）は、そのまま残す
					FWorldRawComponent Raw;
					Raw.Type = FString::FromUtf8( Key.c_str() );
					Raw.Data = Value;
					Object.RawComponents.Add( Raw );
					if ( IsKnownKey( KnownComponentTypes, Key ) ) Warn( Warnings, Where + ": the " + Raw.Type + " component is not an object and was kept as it is" );
				}
			}
		}

		if ( Json.is_object() )
		{
			for ( auto It = Json.begin(); It != Json.end(); ++It )
			{
				if ( !IsKnownKey( KnownObjectKeys, It.key() ) )
				{
					if ( !Object.Extra.is_object() ) Object.Extra = NlohmannJson::object();
					Object.Extra[It.key()] = It.value();
				}
			}
		}
		return Object;
	}
}

//=============================================================================
// 種類の名前
//=============================================================================
const char* ToString( EWorldBillboardMode Mode )
{
	switch ( Mode )
	{
	case EWorldBillboardMode::Camera: return "Camera";
	case EWorldBillboardMode::CameraY: return "CameraY";
	case EWorldBillboardMode::World: return "World";
	}
	return "Camera";
}

const char* ToString( EWorldColliderShape Shape )
{
	switch ( Shape )
	{
	case EWorldColliderShape::Box: return "Box";
	case EWorldColliderShape::Sphere: return "Sphere";
	case EWorldColliderShape::Capsule: return "Capsule";
	case EWorldColliderShape::Mesh: return "Mesh";
	}
	return "Box";
}

const char* ToString( EWorldCollisionMode Mode )
{
	switch ( Mode )
	{
	case EWorldCollisionMode::Auto: return "Auto";
	case EWorldCollisionMode::On: return "On";
	case EWorldCollisionMode::Off: return "Off";
	}
	return "Auto";
}

const char* ToString( EWorldPropType Type )
{
	switch ( Type )
	{
	case EWorldPropType::Bool: return "Bool";
	case EWorldPropType::Int: return "Int";
	case EWorldPropType::Float: return "Float";
	case EWorldPropType::String: return "String";
	case EWorldPropType::Vector3: return "Vector3";
	case EWorldPropType::Asset: return "Asset";
	case EWorldPropType::Object: return "Object";
	}
	return "String";
}

bool TryParse( const FString& Text, EWorldBillboardMode& Out )
{
	for ( const EWorldBillboardMode Mode : { EWorldBillboardMode::Camera, EWorldBillboardMode::CameraY, EWorldBillboardMode::World } )
	{
		if ( EqualsIgnoreCase( Text, ToString( Mode ) ) )
		{
			Out = Mode;
			return true;
		}
	}
	return false;
}

bool TryParse( const FString& Text, EWorldColliderShape& Out )
{
	for ( const EWorldColliderShape Shape : { EWorldColliderShape::Box, EWorldColliderShape::Sphere, EWorldColliderShape::Capsule, EWorldColliderShape::Mesh } )
	{
		if ( EqualsIgnoreCase( Text, ToString( Shape ) ) )
		{
			Out = Shape;
			return true;
		}
	}
	return false;
}

bool TryParse( const FString& Text, EWorldCollisionMode& Out )
{
	for ( const EWorldCollisionMode Mode : { EWorldCollisionMode::Auto, EWorldCollisionMode::On, EWorldCollisionMode::Off } )
	{
		if ( EqualsIgnoreCase( Text, ToString( Mode ) ) )
		{
			Out = Mode;
			return true;
		}
	}
	return false;
}

//=============================================================================
// 色
//=============================================================================
FString NWorld::NormalizeAssetPath( const FString& Path )
{
	return Path.TrimStartAndEnd().Replace( "\\", "/" ).ToLower();
}

bool NWorld::TryParseColor( const FString& Text, FColor32& OutColor )
{
	FString Digits = Text.TrimStartAndEnd();
	if ( Digits.StartsWith( "#" ) ) Digits = Digits.Mid( 1 );
	if ( Digits.Len() != 6 && Digits.Len() != 8 ) return false;
	uint8 Bytes[4] = { 0, 0, 0, 255 };
	for ( int32 i = 0; i < Digits.Len(); ++i )
	{
		const char Ch = Digits[i];
		int32 Value = 0;
		if ( Ch >= '0' && Ch <= '9' ) Value = Ch - '0';
		else if ( Ch >= 'a' && Ch <= 'f' ) Value = Ch - 'a' + 10;
		else if ( Ch >= 'A' && Ch <= 'F' ) Value = Ch - 'A' + 10;
		else return false;
		uint8& Byte = Bytes[i / 2];
		Byte = static_cast<uint8>( ( i % 2 == 0 ) ? Value * 16 : Byte + Value );
	}
	OutColor = FColor32( Bytes[0], Bytes[1], Bytes[2], Bytes[3] );
	return true;
}

FString NWorld::FormatColor( const FColor32& Color )
{
	if ( Color.a == 255 ) return FString::Printf( "#%02X%02X%02X", Color.r, Color.g, Color.b );
	return FString::Printf( "#%02X%02X%02X%02X", Color.r, Color.g, Color.b, Color.a );
}

//=============================================================================
// FWorldProp
//=============================================================================
FWorldProp FWorldProp::MakeBool( const FString& InName, bool bInValue )
{
	FWorldProp Prop;
	Prop.Name = InName;
	Prop.Type = EWorldPropType::Bool;
	Prop.bValue = bInValue;
	return Prop;
}

FWorldProp FWorldProp::MakeInt( const FString& InName, int64 InValue )
{
	FWorldProp Prop;
	Prop.Name = InName;
	Prop.Type = EWorldPropType::Int;
	Prop.IntValue = InValue;
	return Prop;
}

FWorldProp FWorldProp::MakeFloat( const FString& InName, float64 InValue )
{
	FWorldProp Prop;
	Prop.Name = InName;
	Prop.Type = EWorldPropType::Float;
	Prop.FloatValue = InValue;
	return Prop;
}

FWorldProp FWorldProp::MakeString( const FString& InName, const FString& InValue )
{
	FWorldProp Prop;
	Prop.Name = InName;
	Prop.Type = EWorldPropType::String;
	Prop.StringValue = InValue;
	return Prop;
}

FWorldProp FWorldProp::MakeVector3( const FString& InName, const FVector3& InValue )
{
	FWorldProp Prop;
	Prop.Name = InName;
	Prop.Type = EWorldPropType::Vector3;
	Prop.VectorValue = InValue;
	return Prop;
}

FWorldProp FWorldProp::MakeAsset( const FString& InName, const FString& InPath )
{
	FWorldProp Prop;
	Prop.Name = InName;
	Prop.Type = EWorldPropType::Asset;
	Prop.StringValue = InPath;
	return Prop;
}

FWorldProp FWorldProp::MakeObject( const FString& InName, int32 InObjectId )
{
	FWorldProp Prop;
	Prop.Name = InName;
	Prop.Type = EWorldPropType::Object;
	Prop.ObjectId = InObjectId;
	return Prop;
}

NlohmannJson FWorldProp::ValueToJson() const
{
	switch ( Type )
	{
	case EWorldPropType::Bool: return NlohmannJson( bValue );
	case EWorldPropType::Int: return NlohmannJson( static_cast<int64>( IntValue ) );
	case EWorldPropType::Float: return NlohmannJson( Round6( FloatValue ) );
	case EWorldPropType::String: return NlohmannJson( *StringValue.ToUtf8() );
	case EWorldPropType::Vector3: return NJson::ToJson( VectorValue );
	case EWorldPropType::Asset:
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Asset"] = *StringValue.ToUtf8();
		return Json;
	}
	case EWorldPropType::Object:
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Object"] = ObjectId;
		return Json;
	}
	}
	return NlohmannJson();
}

bool FWorldProp::FromJsonValue( const FString& InName, const NlohmannJson& Json, FWorldProp& OutProp )
{
	FWorldProp Prop;
	Prop.Name = InName;
	if ( Json.is_boolean() )
	{
		Prop.Type = EWorldPropType::Bool;
		Prop.bValue = Json.get<bool>();
	}
	else if ( Json.is_number_integer() )
	{
		Prop.Type = EWorldPropType::Int;
		Prop.IntValue = Json.get<int64>();
	}
	else if ( Json.is_number_float() )
	{
		Prop.Type = EWorldPropType::Float;
		Prop.FloatValue = Json.get<float64>();
	}
	else if ( Json.is_string() )
	{
		Prop.Type = EWorldPropType::String;
		Prop.StringValue = FString::FromUtf8( Json.get<std::string>().c_str() );
	}
	else if ( IsNumberArray( Json, 3 ) )
	{
		Prop.Type = EWorldPropType::Vector3;
		Prop.VectorValue = FVector3( Json[0].get<float32>(), Json[1].get<float32>(), Json[2].get<float32>() );
	}
	else if ( Json.is_object() && Json.contains( "Asset" ) && Json["Asset"].is_string() )
	{
		Prop.Type = EWorldPropType::Asset;
		Prop.StringValue = FString::FromUtf8( Json["Asset"].get<std::string>().c_str() );
	}
	else if ( Json.is_object() && Json.contains( "Object" ) && Json["Object"].is_number_integer() )
	{
		Prop.Type = EWorldPropType::Object;
		Prop.ObjectId = Json["Object"].get<int32>();
	}
	else
	{
		return false;
	}
	OutProp = Prop;
	return true;
}

FString FWorldProp::ToDisplayString() const
{
	switch ( Type )
	{
	case EWorldPropType::Bool: return bValue ? "true" : "false";
	case EWorldPropType::Int: return FString::Printf( "%lld", static_cast<long long>( IntValue ) );
	case EWorldPropType::Float: return FString::Printf( "%g", FloatValue );
	case EWorldPropType::String: return StringValue;
	case EWorldPropType::Vector3: return FString::Printf( "(%g, %g, %g)", VectorValue.x, VectorValue.y, VectorValue.z );
	case EWorldPropType::Asset: return StringValue;
	case EWorldPropType::Object: return FString::Printf( "#%d", ObjectId );
	}
	return FString();
}

//=============================================================================
// FWorldTransform
//=============================================================================
FTransform FWorldTransform::ToTransform() const
{
	const FVector3 Radians( NMath::ToRadians( RotationDegrees.x ), NMath::ToRadians( RotationDegrees.y ), NMath::ToRadians( RotationDegrees.z ) );
	return FTransform( Position, FQuaternion::EulerToQuat( Radians ), Scale );
}

FWorldTransform FWorldTransform::FromTransform( const FTransform& Transform )
{
	const FVector3 Radians = Transform.Rotation.ToEuler();
	FWorldTransform Result;
	Result.Position = Transform.Position;
	// 人が読む・書く値なので、計算の誤差（0.0000013 や -0.0）は整える（小数第 4 位まで。-0 は +0 にする）
	const auto Tidy = []( float32 Radians ) { return std::round( NMath::ToDegrees( Radians ) * 10000.0f ) / 10000.0f + 0.0f; };
	Result.RotationDegrees = FVector3( Tidy( Radians.x ), Tidy( Radians.y ), Tidy( Radians.z ) );
	Result.Scale = Transform.Scale;
	return Result;
}

bool FWorldTransform::IsIdentity() const
{
	return IsNearlyEqual( Position, FVector3::Zero ) && IsNearlyEqual( RotationDegrees, FVector3::Zero ) && IsNearlyEqual( Scale, FVector3::One );
}

//=============================================================================
// FWorldObject
//=============================================================================
bool FWorldObject::HasTag( const FString& Tag ) const
{
	return Tags.Contains( Tag );
}

bool FWorldObject::AddTag( const FString& Tag )
{
	const FString Trimmed = Tag.TrimStartAndEnd();
	if ( Trimmed.IsEmpty() || Tags.Contains( Trimmed ) ) return false;
	Tags.Add( Trimmed );
	return true;
}

bool FWorldObject::RemoveTag( const FString& Tag )
{
	return Tags.RemoveSingle( Tag );
}

const FWorldProp* FWorldObject::FindProp( const FString& PropName ) const
{
	for ( const FWorldProp& Prop : Props )
	{
		if ( Prop.Name == PropName ) return &Prop;
	}
	return nullptr;
}

void FWorldObject::SetProp( const FWorldProp& Prop )
{
	for ( FWorldProp& Existing : Props )
	{
		if ( Existing.Name == Prop.Name )
		{
			Existing = Prop;
			return;
		}
	}
	Props.Add( Prop );
	Props.Sort( []( const FWorldProp& A, const FWorldProp& B ) { return A.Name < B.Name; } );
}

bool FWorldObject::RemoveProp( const FString& PropName )
{
	for ( int32 i = 0; i < Props.Num(); ++i )
	{
		if ( Props[i].Name == PropName )
		{
			Props.RemoveAt( i );
			return true;
		}
	}
	return false;
}

const FWorldRawComponent* FWorldObject::FindRawComponent( const FString& Type ) const
{
	for ( const FWorldRawComponent& Raw : RawComponents )
	{
		if ( Raw.Type == Type ) return &Raw;
	}
	return nullptr;
}

TArray<FString> FWorldObject::GetComponentNames() const
{
	TArray<FString> Names;
	if ( Model.IsSet() ) Names.Add( "Model" );
	if ( Billboard.IsSet() ) Names.Add( "Billboard" );
	if ( Sound.IsSet() ) Names.Add( "Sound" );
	if ( Effect.IsSet() ) Names.Add( "Effect" );
	if ( Collider.IsSet() ) Names.Add( "Collider" );
	if ( Mesh.IsSet() ) Names.Add( "Mesh" );
	if ( Prefab.IsSet() ) Names.Add( "Prefab" );
	for ( const FWorldRawComponent& Raw : RawComponents ) Names.Add( Raw.Type );
	return Names;
}

//=============================================================================
// FWorldDefinition
//=============================================================================
int32 FWorldDefinition::AllocateId()
{
	return NextId++;
}

int32 FWorldDefinition::FindIndexById( int32 Id ) const
{
	if ( Id == WorldNoId ) return INDEX_NONE;
	for ( int32 i = 0; i < Objects.Num(); ++i )
	{
		if ( Objects[i].Id == Id ) return i;
	}
	return INDEX_NONE;
}

FWorldObject* FWorldDefinition::FindObject( int32 Id )
{
	const int32 Index = FindIndexById( Id );
	return Index != INDEX_NONE ? &Objects[Index] : nullptr;
}

const FWorldObject* FWorldDefinition::FindObject( int32 Id ) const
{
	const int32 Index = FindIndexById( Id );
	return Index != INDEX_NONE ? &Objects[Index] : nullptr;
}

const FWorldObject* FWorldDefinition::FindObjectByName( const FString& ObjectName ) const
{
	for ( const FWorldObject& Object : Objects )
	{
		if ( Object.Name == ObjectName ) return &Object;
	}
	return nullptr;
}

NlohmannJson FWorldDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	if ( Extra.is_object() )
	{
		for ( auto It = Extra.begin(); It != Extra.end(); ++It ) Json[It.key()] = It.value();
	}
	Json["Version"] = Version;
	if ( !Name.IsEmpty() ) Json["Name"] = Name;
	Json["NextId"] = NextId;

	NlohmannJson SettingsJson = NlohmannJson::object();
	if ( !Settings.CollisionRules.IsEmpty() )
	{
		NlohmannJson Rules = NlohmannJson::array();
		for ( const FWorldCollisionRule& Rule : Settings.CollisionRules )
		{
			NlohmannJson RuleJson = NlohmannJson::object();
			RuleJson["Tag"] = Rule.Tag;
			RuleJson["Collision"] = Rule.bCollide ? "On" : "Off";
			Rules.push_back( RuleJson );
		}
		SettingsJson["CollisionRules"] = Rules;
	}
	if ( !SettingsJson.empty() ) Json["Settings"] = SettingsJson;

	NlohmannJson ObjectsJson = NlohmannJson::array();
	for ( const FWorldObject& Object : Objects ) ObjectsJson.push_back( ObjectToJson( Object ) );
	Json["Objects"] = ObjectsJson;
	return Json;
}

bool FWorldDefinition::FromJson( const NlohmannJson& Json, FWorldDefinition& OutDefinition, TArray<FString>* OutWarnings )
{
	if ( !Json.is_object() ) return false;

	FWorldDefinition World;
	World.Version = NJson::GetOr<int32>( Json, "Version", WorldFormatVersion );
	if ( World.Version > WorldFormatVersion )
	{
		Warn( OutWarnings, FString::Printf( "the file is version %d, newer than this program's version %d; some data may be lost when it is saved", World.Version, WorldFormatVersion ) );
	}
	World.Name = NJson::GetOr<FString>( Json, "Name", FString() );

	if ( Json.contains( "Settings" ) && Json["Settings"].is_object() && Json["Settings"].contains( "CollisionRules" ) && Json["Settings"]["CollisionRules"].is_array() )
	{
		for ( const NlohmannJson& RuleJson : Json["Settings"]["CollisionRules"] )
		{
			FWorldCollisionRule Rule;
			Rule.Tag = NJson::GetOr<FString>( RuleJson, "Tag", FString() ).TrimStartAndEnd();
			const FString Text = NJson::GetOr<FString>( RuleJson, "Collision", FString( "Off" ) );
			EWorldCollisionMode Mode = EWorldCollisionMode::Off;
			if ( !TryParse( Text, Mode ) || Mode == EWorldCollisionMode::Auto )
			{
				Warn( OutWarnings, "collision rule for tag '" + Rule.Tag + "': '" + Text + "' is not On or Off (using Off)" );
				Mode = EWorldCollisionMode::Off;
			}
			Rule.bCollide = Mode == EWorldCollisionMode::On;
			if ( Rule.Tag.IsEmpty() )
			{
				Warn( OutWarnings, "a collision rule without a tag was skipped" );
				continue;
			}
			World.Settings.CollisionRules.Add( Rule );
		}
	}

	int32 MaxId = 0;
	TArray<int32> NeedsId;   // Id が無い・重複しているオブジェクトの番号
	if ( Json.contains( "Objects" ) && Json["Objects"].is_array() )
	{
		int32 Index = 0;
		for ( const NlohmannJson& ObjectJson : Json["Objects"] )
		{
			if ( !ObjectJson.is_object() )
			{
				Warn( OutWarnings, FString::Printf( "object #%d is not an object and was skipped", Index ) );
				++Index;
				continue;
			}
			FWorldObject Object = ObjectFromJson( ObjectJson, OutWarnings, Index );
			++Index;
			if ( Object.Id <= WorldNoId || World.FindIndexById( Object.Id ) != INDEX_NONE ) NeedsId.Add( World.Objects.Num() );
			else MaxId = std::max( MaxId, Object.Id );
			World.Objects.Add( Object );
		}
	}

	// NextId は、使われている Id より大きく（古い値が保存されていても、既存の Id と重ならないように）
	for ( const FWorldObject& Object : World.Objects ) MaxId = std::max( MaxId, Object.Id );
	World.NextId = std::max( NJson::GetOr<int32>( Json, "NextId", 1 ), MaxId + 1 );
	for ( const int32 ObjectIndex : NeedsId )
	{
		FWorldObject& Object = World.Objects[ObjectIndex];
		const int32 OldId = Object.Id;
		Object.Id = World.AllocateId();
		Warn( OutWarnings, FString::Printf( "object #%d ('%s') had %s; it was given the id %d", ObjectIndex, *Object.Name, OldId <= WorldNoId ? "no valid id" : *FString::Printf( "the duplicate id %d", OldId ), Object.Id ) );
	}

	if ( Json.is_object() )
	{
		for ( auto It = Json.begin(); It != Json.end(); ++It )
		{
			if ( IsKnownKey( KnownWorldKeys, It.key() ) ) continue;
			if ( !World.Extra.is_object() ) World.Extra = NlohmannJson::object();
			World.Extra[It.key()] = It.value();
		}
	}
	OutDefinition = World;
	return true;
}
