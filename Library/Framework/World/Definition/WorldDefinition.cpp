#include "WorldDefinition.h"
#include "WorldDefinitionJson.h"


#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Json/JsonHelpers.h"
#include "Library/Math/MathUtils.h"

//=============================================================================
// ワールドの定義ファイル（JSON、拡張子は .gxworld）の書式
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
//           "Model": { "Path": "Models/Torch.mv1", "Animations": "Models/Torch.gxmodelanim", "Clip": "Burn", "Speed": 1 },
//           "Billboard": { "Texture": "Sprites/Glow.png", "Size": [1, 1], "Pivot": [0.5, 0.5], "Mode": "CameraY", "Tint": "#FFFFFF80",
//                          "Rect": [0, 0, 64, 64], "FlipX": false, "FlipY": false },
//           "Sound": { "Path": "Sounds/Fire.wav", "Bus": "Ambient", "Volume": 1, "Pitch": 1, "Loop": true, "AutoPlay": true, "MinDistance": 1, "MaxDistance": 25 },
//           "Effect": { "Path": "Effects/Fire.gxeffect", "AutoPlay": true, "Scale": 1, "Speed": 1, "Seed": 0, "Loop": true },   // Loop を省略すると、エフェクトの指定のまま
//           "Collider": { "Shape": "Box", "Size": [1, 2, 1], "Radius": 0.5, "Height": 2, "Offset": [0, 1, 0], "Trigger": false,
//                         "Mesh": "Models/Floor.mv1" }     // Shape が "Mesh" のとき、三角形を取るモデル（省略なら、Model 部品のファイル）
//           "Mesh": { "Vertices": [[0, 0, 0], [1, 0, 0], ...], "Faces": [[0, 1, 2, 3], ...], "FaceMaterials": [0, 1, ...],   // 編集メッシュ（EditMesh.h に詳しい）
//                     "Materials": [{ "Name": "Wall", "Color": "#C8C8C8", "Texture": "Textures/Brick.png", "Tile": 2, "Offset": [0, 0], "TwoSided": false }], "Smooth": 30 },
//           "Prefab": { "Path": "Prefabs/Door.gxworld" }   // 別の .gxworld を、このオブジェクトの子として置く（入れ子。深さは 8 まで。循環は置かれない）
//         }
//       }
//     ]
//   }
//
//   既定値と同じ項目は書き出さない（ファイルを小さく、差分を読みやすく保つ）。
//=============================================================================

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
		if ( NWorldDefinitionJson::EqualsIgnoreCase( Text, ToString( Mode ) ) )
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
		if ( NWorldDefinitionJson::EqualsIgnoreCase( Text, ToString( Shape ) ) )
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
		if ( NWorldDefinitionJson::EqualsIgnoreCase( Text, ToString( Mode ) ) )
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
	if ( Digits.StartsWith( "#" ) )
	{
		Digits = Digits.Mid( 1 );
	}
	if ( Digits.Len() != 6 && Digits.Len() != 8 )
	{
		return false;
	}
	uint8 Bytes[4] = { 0, 0, 0, 255 };
	for ( int32 i = 0; i < Digits.Len(); ++i )
	{
		const char Ch = Digits[i];
		int32 Value = 0;
		if ( Ch >= '0' && Ch <= '9' )
		{
			Value = Ch - '0';
		}
		else if ( Ch >= 'a' && Ch <= 'f' )
		{
			Value = Ch - 'a' + 10;
		}
		else if ( Ch >= 'A' && Ch <= 'F' )
		{
			Value = Ch - 'A' + 10;
		}
		else
		{
			return false;
		}
		uint8& Byte = Bytes[i / 2];
		Byte = static_cast<uint8>( ( i % 2 == 0 ) ? Value * 16 : Byte + Value );
	}
	OutColor = FColor32( Bytes[0], Bytes[1], Bytes[2], Bytes[3] );
	return true;
}

FString NWorld::FormatColor( const FColor32& Color )
{
	if ( Color.a == 255 )
	{
		return FString::Printf( "#%02X%02X%02X", Color.r, Color.g, Color.b );
	}
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
	case EWorldPropType::Float: return NlohmannJson( NWorldDefinitionJson::Round6( FloatValue ) );
	case EWorldPropType::String: return NlohmannJson( NJson::ToJsonString( StringValue ) );
	case EWorldPropType::Vector3: return NJson::ToJson( VectorValue );
	case EWorldPropType::Asset:
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Asset"] = NJson::ToJsonString( StringValue );
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
		Prop.StringValue = NJson::FromJsonString( Json.get<NJson::FJsonString>() );
	}
	else if ( NWorldDefinitionJson::IsNumberArray( Json, 3 ) )
	{
		Prop.Type = EWorldPropType::Vector3;
		Prop.VectorValue = FVector3( Json[0].get<float32>(), Json[1].get<float32>(), Json[2].get<float32>() );
	}
	else if ( Json.is_object() && Json.contains( "Asset" ) && Json["Asset"].is_string() )
	{
		Prop.Type = EWorldPropType::Asset;
		Prop.StringValue = NJson::FromJsonString( Json["Asset"].get<NJson::FJsonString>() );
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
	const auto Tidy = []( float32 Radians ) { return NMath::Round( NMath::ToDegrees( Radians ) * 10000.0f ) / 10000.0f + 0.0f; };
	Result.RotationDegrees = FVector3( Tidy( Radians.x ), Tidy( Radians.y ), Tidy( Radians.z ) );
	Result.Scale = Transform.Scale;
	return Result;
}

bool FWorldTransform::IsIdentity() const
{
	return NWorldDefinitionJson::IsNearlyEqual( Position, FVector3::Zero ) && NWorldDefinitionJson::IsNearlyEqual( RotationDegrees, FVector3::Zero ) && NWorldDefinitionJson::IsNearlyEqual( Scale, FVector3::One );
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
	if ( Trimmed.IsEmpty() || Tags.Contains( Trimmed ) )
	{
		return false;
	}
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
		if ( Prop.Name == PropName )
		{
			return &Prop;
		}
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
		if ( Raw.Type == Type )
		{
			return &Raw;
		}
	}
	return nullptr;
}

TArray<FString> FWorldObject::GetComponentNames() const
{
	TArray<FString> Names;
	if ( Model.IsSet() )
	{
		Names.Add( "Model" );
	}
	if ( Billboard.IsSet() )
	{
		Names.Add( "Billboard" );
	}
	if ( Sound.IsSet() )
	{
		Names.Add( "Sound" );
	}
	if ( Effect.IsSet() )
	{
		Names.Add( "Effect" );
	}
	if ( Collider.IsSet() )
	{
		Names.Add( "Collider" );
	}
	if ( Mesh.IsSet() )
	{
		Names.Add( "Mesh" );
	}
	if ( Prefab.IsSet() )
	{
		Names.Add( "Prefab" );
	}
	for ( const FWorldRawComponent& Raw : RawComponents )
	{
		Names.Add( Raw.Type );
	}
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
	if ( Id == WorldNoId )
	{
		return INDEX_NONE;
	}
	for ( int32 i = 0; i < Objects.Num(); ++i )
	{
		if ( Objects[i].Id == Id )
		{
			return i;
		}
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
		if ( Object.Name == ObjectName )
		{
			return &Object;
		}
	}
	return nullptr;
}

NlohmannJson FWorldDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	if ( Extra.is_object() )
	{
		for ( auto It = Extra.begin(); It != Extra.end(); ++It )
		{
			Json[It.key()] = It.value();
		}
	}
	Json["Version"] = Version;
	if ( !Name.IsEmpty() )
	{
		Json["Name"] = Name;
	}
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
	if ( !SettingsJson.empty() )
	{
		Json["Settings"] = SettingsJson;
	}

	NlohmannJson ObjectsJson = NlohmannJson::array();
	for ( const FWorldObject& Object : Objects )
	{
		ObjectsJson.push_back( NWorldDefinitionJson::ObjectToJson( Object ) );
	}
	Json["Objects"] = ObjectsJson;
	return Json;
}

bool FWorldDefinition::FromJson( const NlohmannJson& Json, FWorldDefinition& OutDefinition, TArray<FString>* OutWarnings )
{
	if ( !Json.is_object() )
	{
		return false;
	}

	FWorldDefinition World;
	World.Version = NJson::GetOr<int32>( Json, "Version", WorldFormatVersion );
	if ( World.Version > WorldFormatVersion )
	{
		NWorldDefinitionJson::Warn( OutWarnings, FString::Printf( "the file is version %d, newer than this program's version %d; some data may be lost when it is saved", World.Version, WorldFormatVersion ) );
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
				NWorldDefinitionJson::Warn( OutWarnings, "collision rule for tag '" + Rule.Tag + "': '" + Text + "' is not On or Off (using Off)" );
				Mode = EWorldCollisionMode::Off;
			}
			Rule.bCollide = Mode == EWorldCollisionMode::On;
			if ( Rule.Tag.IsEmpty() )
			{
				NWorldDefinitionJson::Warn( OutWarnings, "a collision rule without a tag was skipped" );
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
				NWorldDefinitionJson::Warn( OutWarnings, FString::Printf( "object #%d is not an object and was skipped", Index ) );
				++Index;
				continue;
			}
			FWorldObject Object = NWorldDefinitionJson::ObjectFromJson( ObjectJson, OutWarnings, Index );
			++Index;
			if ( Object.Id <= WorldNoId || World.FindIndexById( Object.Id ) != INDEX_NONE )
			{
				NeedsId.Add( World.Objects.Num() );
			}
			else
			{
				MaxId = NMath::Max( MaxId, Object.Id );
			}
			World.Objects.Add( Object );
		}
	}

	// NextId は、使われている Id より大きく（古い値が保存されていても、既存の Id と重ならないように）
	for ( const FWorldObject& Object : World.Objects )
	{
		MaxId = NMath::Max( MaxId, Object.Id );
	}
	World.NextId = NMath::Max( NJson::GetOr<int32>( Json, "NextId", 1 ), MaxId + 1 );
	for ( const int32 ObjectIndex : NeedsId )
	{
		FWorldObject& Object = World.Objects[ObjectIndex];
		const int32 OldId = Object.Id;
		Object.Id = World.AllocateId();
		NWorldDefinitionJson::Warn( OutWarnings, FString::Printf( "object #%d ('%s') had %s; it was given the id %d", ObjectIndex, *Object.Name, OldId <= WorldNoId ? "no valid id" : *FString::Printf( "the duplicate id %d", OldId ), Object.Id ) );
	}

	if ( Json.is_object() )
	{
		for ( auto It = Json.begin(); It != Json.end(); ++It )
		{
			if ( NWorldDefinitionJson::IsKnownKey( NWorldDefinitionJson::KnownWorldKeys, It.key() ) )
			{
				continue;
			}
			if ( !World.Extra.is_object() )
			{
				World.Extra = NlohmannJson::object();
			}
			World.Extra[It.key()] = It.value();
		}
	}
	OutDefinition = World;
	return true;
}
