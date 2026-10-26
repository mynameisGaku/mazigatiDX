#include "WorldSelfTestSupport.h"

#include <cmath>
#include <limits>
#include <set>
#include <string>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/World/WorldCollision.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Framework/World/WorldHierarchy.h"
#include "Library/Framework/World/WorldValidate.h"

using namespace NAssetTest;

//=============================================================================
// ワールド（.world）の純粋な部分の確認
//   定義と JSON・階層・当たり判定の決定・検証・編集。DxLib・シーン・物理を使わない（偽のアセットの窓口だけ使う）。
//=============================================================================
namespace
{
	// 読み取り専用の JSON の窓口。無いキー・範囲外の番号・型の違いでも落ちず、null として扱う
	//   （const の nlohmann::json の operator[] は、無いキーで assert（Debug では、気づきにくい待ちになる）や未定義動作になる。
	//    壊したときに「止まる」のでなく「確認が失敗する」ように、テストは、これを通して読む）
	class FJ
	{
	public:
		FJ( const NlohmannJson& InJson ) : Json( &InJson ) {}

		FJ operator[]( const char* Key ) const
		{
			if ( !Json->is_object() ) return FJ( Null() );
			const auto It = Json->find( Key );
			return It != Json->end() ? FJ( *It ) : FJ( Null() );
		}
		FJ operator[]( int Index ) const { return Json->is_array() && Index >= 0 && static_cast<size_t>( Index ) < Json->size() ? FJ( ( *Json )[static_cast<size_t>( Index )] ) : FJ( Null() ); }

		template<typename T>
		bool operator==( const T& Value ) const { return *Json == NlohmannJson( Value ); }

		size_t size() const { return Json->size(); }
		bool contains( const char* Key ) const { return Json->is_object() && Json->contains( Key ); }
		bool is_number_integer() const { return Json->is_number_integer(); }
		bool is_number_float() const { return Json->is_number_float(); }

	private:
		static const NlohmannJson& Null()
		{
			static const NlohmannJson Value;
			return Value;
		}
		const NlohmannJson* Json;
	};

	NlohmannJson ParseJson( const char* Text )
	{
		return NlohmannJson::parse( Text, nullptr, false );
	}

	bool Near3( const FVector3& A, const FVector3& B, float32 Tolerance = 1.0e-3f )
	{
		return NMath::Abs( A.x - B.x ) <= Tolerance && NMath::Abs( A.y - B.y ) <= Tolerance && NMath::Abs( A.z - B.z ) <= Tolerance;
	}

	FWorldObject MakeObject( int32 Id, const char* Name, int32 Parent = WorldNoId )
	{
		FWorldObject Object;
		Object.Id = Id;
		Object.Name = Name;
		Object.Parent = Parent;
		return Object;
	}

	FWorldCollisionRule Rule( const char* Tag, bool bCollide )
	{
		FWorldCollisionRule Result;
		Result.Tag = Tag;
		Result.bCollide = bCollide;
		return Result;
	}

	// すべての種類の部品・プロパティ・知らない項目を持つ、ワールド
	FWorldDefinition MakeRichWorld()
	{
		FWorldDefinition World;
		World.Name = "Stage1";
		World.NextId = 20;
		World.Settings.CollisionRules.Add( Rule( "Decoration", false ) );
		World.Settings.CollisionRules.Add( Rule( "Wall", true ) );
		World.Extra = ParseJson( "{\"Author\":\"me\",\"Editor\":{\"Zoom\":2}}" );

		FWorldObject A = MakeObject( 1, "Torch" );
		A.Tags.Add( "Light" );
		A.Tags.Add( "Interactable" );
		A.SetProp( FWorldProp::MakeInt( "Damage", 5 ) );
		A.SetProp( FWorldProp::MakeFloat( "Speed", 2.5 ) );
		A.SetProp( FWorldProp::MakeBool( "Lit", true ) );
		A.SetProp( FWorldProp::MakeString( "Label", "Hello" ) );
		A.SetProp( FWorldProp::MakeVector3( "Offset", FVector3( 0.0f, 1.0f, -2.0f ) ) );
		A.SetProp( FWorldProp::MakeObject( "Next", 2 ) );
		A.SetProp( FWorldProp::MakeAsset( "Sound", "Sounds/a.wav" ) );
		A.Transform.Position = FVector3( 1.0f, 2.0f, 3.0f );
		A.Transform.RotationDegrees = FVector3( 0.0f, 90.0f, 0.0f );
		A.Transform.Scale = FVector3( 2.0f, 2.0f, 2.0f );
		A.Collision = EWorldCollisionMode::Off;
		FWorldModel Model;
		Model.Path = "Models/Torch.mv1";
		Model.Animations = "Models/Torch.modelanim.json";
		Model.Clip = "Burn";
		Model.Speed = 2.0f;
		A.Model = Model;
		FWorldBillboard Board;
		Board.Texture = "Sprites/Glow.png";
		Board.Size = FVector2( 2.0f, 1.5f );
		Board.Pivot = FVector2( 0.5f, 1.0f );
		Board.Mode = EWorldBillboardMode::CameraY;
		Board.Tint = FColor32( 255, 128, 0, 200 );
		Board.bUseRect = true;
		Board.RectX = 8;
		Board.RectY = 16;
		Board.RectWidth = 32;
		Board.RectHeight = 24;
		Board.bFlipX = true;
		A.Billboard = Board;
		FWorldSound Sound;
		Sound.Path = "Sounds/Fire.wav";
		Sound.Bus = "Sfx";
		Sound.Volume = 0.5f;
		Sound.Pitch = 1.25f;
		Sound.bLoop = false;
		Sound.bAutoPlay = false;
		Sound.MinDistance = 2.0f;
		Sound.MaxDistance = 40.0f;
		A.Sound = Sound;
		FWorldCollider Collider;
		Collider.Shape = EWorldColliderShape::Capsule;
		Collider.Radius = 0.4f;
		Collider.Height = 1.8f;
		Collider.Offset = FVector3( 0.0f, 0.9f, 0.0f );
		Collider.bTrigger = true;
		A.Collider = Collider;
		FWorldRawComponent Raw;
		Raw.Type = "Hologram";
		Raw.Data = ParseJson( "{\"File\":\"Fx/Fire.efk\",\"Loop\":true,\"Scale\":[1,2,3]}" );
		A.RawComponents.Add( Raw );
		A.Extra = ParseJson( "{\"EditorColor\":[0.1,0.2,0.3],\"Locked\":true}" );
		World.Objects.Add( A );

		FWorldObject B = MakeObject( 2, "Child", 1 );
		B.bEnabled = false;
		World.Objects.Add( B );

		FWorldObject C = MakeObject( 7, "" );
		World.Objects.Add( C );
		return World;
	}

	//=========================================================================
	// 色・名前
	//=========================================================================
	void TestColorAndNames( NAssetTest::FChecker& C )
	{
		C.Group = "world-color";
		FColor32 Color;
		C.Check( NWorld::TryParseColor( "#FF8000", Color ) && Color.r == 255 && Color.g == 128 && Color.b == 0 && Color.a == 255, "#RRGGBB" );
		C.Check( NWorld::TryParseColor( "11223344", Color ) && Color.r == 0x11 && Color.g == 0x22 && Color.b == 0x33 && Color.a == 0x44, "RRGGBBAA without the #" );
		C.Check( NWorld::TryParseColor( "#abcdef", Color ) && Color.r == 0xAB && Color.g == 0xCD && Color.b == 0xEF, "lower case digits" );
		C.Check( NWorld::TryParseColor( "  #010203  ", Color ) && Color.b == 3, "spaces around it are ignored" );
		bool bRejected = true;
		for ( const char* Text : { "", "#", "#12345", "#1234567", "#GG0000", "red", "#12 456", "#FF00FF0" } ) bRejected = bRejected && !NWorld::TryParseColor( Text, Color );
		C.Check( bRejected, "bad colours are refused" );
		C.Check( NWorld::FormatColor( FColor32( 255, 128, 0, 255 ) ) == "#FF8000" && NWorld::FormatColor( FColor32( 1, 2, 3, 4 ) ) == "#01020304", "FormatColor drops an opaque alpha" );
		bool bRoundTrip = true;
		for ( const FColor32& Source : { FColor32( 0, 0, 0, 0 ), FColor32( 255, 255, 255, 255 ), FColor32( 17, 34, 51, 68 ), FColor32( 250, 5, 100, 254 ) } )
		{
			FColor32 Back;
			bRoundTrip = bRoundTrip && NWorld::TryParseColor( NWorld::FormatColor( Source ), Back ) && Back.r == Source.r && Back.g == Source.g && Back.b == Source.b && Back.a == Source.a;
		}
		C.Check( bRoundTrip, "colours survive FormatColor -> TryParseColor" );

		C.Group = "world-enums";
		EWorldBillboardMode Mode = EWorldBillboardMode::Camera;
		C.Check( TryParse( "cameray", Mode ) && Mode == EWorldBillboardMode::CameraY && TryParse( "WORLD", Mode ) && Mode == EWorldBillboardMode::World, "enum names are not case sensitive" );
		C.Check( !TryParse( "Spin", Mode ) && Mode == EWorldBillboardMode::World, "an unknown name is refused and leaves the value alone" );
		EWorldColliderShape Shape = EWorldColliderShape::Box;
		C.Check( TryParse( "capsule", Shape ) && Shape == EWorldColliderShape::Capsule && TryParse( "MESH", Shape ) && Shape == EWorldColliderShape::Mesh && !TryParse( "Cone", Shape ) && Shape == EWorldColliderShape::Mesh, "collider shapes" );
		EWorldCollisionMode Collision = EWorldCollisionMode::Auto;
		C.Check( TryParse( "off", Collision ) && Collision == EWorldCollisionMode::Off && TryParse( "Auto", Collision ) && Collision == EWorldCollisionMode::Auto, "collision modes" );
		C.Check( FString( ToString( EWorldBillboardMode::CameraY ) ) == "CameraY" && FString( ToString( EWorldColliderShape::Sphere ) ) == "Sphere" && FString( ToString( EWorldCollisionMode::On ) ) == "On"
					 && FString( ToString( EWorldPropType::Vector3 ) ) == "Vector3",
				 "ToString" );
	}

	//=========================================================================
	// JSON
	//=========================================================================
	void TestJson( NAssetTest::FChecker& C )
	{
		C.Group = "world-json";
		const FWorldDefinition Rich = MakeRichWorld();
		const NlohmannJson Json = Rich.ToJson();
		const FJ J( Json );

		// 書き出しの形
		C.Check( J["Version"] == WorldFormatVersion && J["Name"] == "Stage1" && J["NextId"] == 20, "the world's header" );
		C.Check( J["Settings"]["CollisionRules"].size() == 2 && J["Settings"]["CollisionRules"][0]["Tag"] == "Decoration" && J["Settings"]["CollisionRules"][0]["Collision"] == "Off"
					 && J["Settings"]["CollisionRules"][1]["Collision"] == "On",
				 "collision rules keep their order" );
		C.Check( J["Author"] == "me" && J["Editor"]["Zoom"] == 2, "unknown top-level items are written back" );
		const FJ A = J["Objects"][0];
		C.Check( A["Id"] == 1 && A["Name"] == "Torch" && !A.contains( "Parent" ) && !A.contains( "Enabled" ), "an object: Id and Name; no Parent / Enabled when default" );
		C.Check( A["Transform"]["Position"] == ParseJson( "[1.0,2.0,3.0]" ) && A["Transform"]["Rotation"] == ParseJson( "[0.0,90.0,0.0]" ) && A["Transform"]["Scale"] == ParseJson( "[2.0,2.0,2.0]" ), "the transform (degrees)" );
		C.Check( A["Collision"] == "Off" && A["Tags"] == ParseJson( "[\"Light\",\"Interactable\"]" ), "collision mode and tags" );
		C.Check( A["Props"]["Damage"] == 5 && A["Props"]["Damage"].is_number_integer() && A["Props"]["Speed"] == 2.5 && A["Props"]["Speed"].is_number_float() && A["Props"]["Lit"] == true && A["Props"]["Label"] == "Hello"
					 && A["Props"]["Offset"] == ParseJson( "[0.0,1.0,-2.0]" ) && A["Props"]["Next"] == ParseJson( "{\"Object\":2}" ) && A["Props"]["Sound"] == ParseJson( "{\"Asset\":\"Sounds/a.wav\"}" ),
				 "every property type has its own JSON form" );
		const FJ Components = A["Components"];
		C.Check( Components["Model"]["Path"] == "Models/Torch.mv1" && Components["Model"]["Clip"] == "Burn" && Components["Model"]["Speed"] == 2.0 && Components["Model"]["Animations"] == "Models/Torch.modelanim.json",
				 "the model component" );
		C.Check( Components["Billboard"]["Mode"] == "CameraY" && Components["Billboard"]["Tint"] == "#FF8000C8" && Components["Billboard"]["Rect"] == ParseJson( "[8,16,32,24]" ) && Components["Billboard"]["FlipX"] == true
					 && !Components["Billboard"].contains( "FlipY" ) && Components["Billboard"]["Size"] == ParseJson( "[2.0,1.5]" ) && Components["Billboard"]["Pivot"] == ParseJson( "[0.5,1.0]" ),
				 "the billboard component" );
		C.Check( Components["Sound"]["Bus"] == "Sfx" && Components["Sound"]["Loop"] == false && Components["Sound"]["AutoPlay"] == false && Components["Sound"]["Volume"] == 0.5 && Components["Sound"]["MaxDistance"] == 40.0,
				 "the sound component" );
		C.Check( Components["Collider"]["Shape"] == "Capsule" && Components["Collider"]["Radius"] == 0.4 && Components["Collider"]["Height"] == 1.8 && Components["Collider"]["Trigger"] == true && !Components["Collider"].contains( "Size" ),
				 "the collider component (only the fields its shape uses)" );
		C.Check( Components["Hologram"]["File"] == "Fx/Fire.efk" && Components["Hologram"]["Scale"] == ParseJson( "[1,2,3]" ), "an unknown component is written back untouched" );
		C.Check( A["EditorColor"] == ParseJson( "[0.1,0.2,0.3]" ) && A["Locked"] == true, "unknown object items are written back" );
		const FJ B = J["Objects"][1];
		C.Check( B["Parent"] == 1 && B["Enabled"] == false && !B.contains( "Transform" ) && !B.contains( "Tags" ) && !B.contains( "Props" ) && !B.contains( "Components" ) && !B.contains( "Collision" ), "defaults are not written" );
		C.Check( J["Objects"][2].size() == 1 && J["Objects"][2]["Id"] == 7, "an empty object is just its Id" );

		// 書いて、読み戻すと、同じ
		TArray<FString> Warnings;
		FWorldDefinition Back;
		C.Check( FWorldDefinition::FromJson( ParseJson( Json.dump().c_str() ), Back, &Warnings ) && Warnings.IsEmpty(), "the written JSON reads back without warnings (%d)", Warnings.Num() );
		C.Check( Back.ToJson() == Json, "write -> read -> write is identical" );
		C.Check( Back.Name == "Stage1" && Back.NextId == 20 && Back.Objects.Num() == 3 && Back.Settings.CollisionRules.Num() == 2 && Back.Settings.CollisionRules[1].Tag == "Wall" && Back.Settings.CollisionRules[1].bCollide, "world fields" );
		const FWorldObject& BA = Back.Objects[0];
		C.Check( BA.Id == 1 && BA.Name == "Torch" && BA.Parent == WorldNoId && BA.bEnabled && BA.Collision == EWorldCollisionMode::Off && BA.Tags.Num() == 2 && BA.Tags[1] == "Interactable", "object fields" );
		C.Check( Near3( BA.Transform.Position, FVector3( 1.0f, 2.0f, 3.0f ) ) && Near3( BA.Transform.RotationDegrees, FVector3( 0.0f, 90.0f, 0.0f ) ) && Near3( BA.Transform.Scale, FVector3( 2.0f, 2.0f, 2.0f ) ), "transform" );
		C.Check( BA.Model.IsSet() && BA.Model->Path == "Models/Torch.mv1" && BA.Model->Speed == 2.0f && BA.Model->Clip == "Burn", "model" );
		C.Check( BA.Billboard.IsSet() && BA.Billboard->Mode == EWorldBillboardMode::CameraY && BA.Billboard->Tint.a == 200 && BA.Billboard->Tint.g == 128 && BA.Billboard->bUseRect && BA.Billboard->RectWidth == 32 && BA.Billboard->bFlipX && !BA.Billboard->bFlipY
					 && BA.Billboard->Size.y == 1.5f && BA.Billboard->Pivot.y == 1.0f,
				 "billboard" );
		C.Check( BA.Sound.IsSet() && BA.Sound->Bus == "Sfx" && !BA.Sound->bLoop && !BA.Sound->bAutoPlay && BA.Sound->Pitch == 1.25f && BA.Sound->MaxDistance == 40.0f, "sound" );
		C.Check( BA.Collider.IsSet() && BA.Collider->Shape == EWorldColliderShape::Capsule && BA.Collider->bTrigger && BA.Collider->Height == 1.8f && Near3( BA.Collider->Offset, FVector3( 0.0f, 0.9f, 0.0f ) ), "collider" );
		C.Check( BA.RawComponents.Num() == 1 && BA.RawComponents[0].Type == "Hologram" && FJ( BA.RawComponents[0].Data )["Loop"] == true && FJ( BA.Extra )["Locked"] == true && FJ( Back.Extra )["Author"] == "me", "unknown parts are kept in the data" );
		C.Check( BA.FindRawComponent( "Hologram" ) != nullptr && FJ( BA.FindRawComponent( "Hologram" )->Data )["Loop"] == true && BA.FindRawComponent( "hologram" ) == nullptr && BA.FindRawComponent( "Nope" ) == nullptr, "FindRawComponent finds an unknown component by its exact name" );
		const TArray<FString> ComponentNames = BA.GetComponentNames();
		C.Check( ComponentNames.Num() == 5 && ComponentNames[0] == "Model" && ComponentNames[1] == "Billboard" && ComponentNames[2] == "Sound" && ComponentNames[3] == "Collider" && ComponentNames[4] == "Hologram" && Back.Objects[1].GetComponentNames().IsEmpty(),
				 "GetComponentNames lists the known components, then the unknown ones" );
		C.Check( BA.FindProp( "Damage" ) != nullptr && BA.FindProp( "Damage" )->Type == EWorldPropType::Int && BA.FindProp( "Damage" )->IntValue == 5 && BA.FindProp( "Speed" )->Type == EWorldPropType::Float && BA.FindProp( "Speed" )->FloatValue == 2.5
					 && BA.FindProp( "Lit" )->Type == EWorldPropType::Bool && BA.FindProp( "Lit" )->bValue && BA.FindProp( "Label" )->StringValue == "Hello" && BA.FindProp( "Offset" )->Type == EWorldPropType::Vector3
					 && BA.FindProp( "Next" )->Type == EWorldPropType::Object && BA.FindProp( "Next" )->ObjectId == 2 && BA.FindProp( "Sound" )->Type == EWorldPropType::Asset && BA.FindProp( "Sound" )->StringValue == "Sounds/a.wav",
				 "property types survive" );
		C.Check( Back.Objects[1].Parent == 1 && !Back.Objects[1].bEnabled, "parent and enabled" );

		// 空・壊れた入力
		FWorldDefinition Empty;
		C.Check( FWorldDefinition::FromJson( ParseJson( "{}" ), Empty ) && Empty.Version == WorldFormatVersion && Empty.NextId == 1 && Empty.Objects.IsEmpty() && Empty.Name.IsEmpty(), "{} is an empty world" );
		C.Check( !FWorldDefinition::FromJson( ParseJson( "[1,2]" ), Empty ) && !FWorldDefinition::FromJson( ParseJson( "5" ), Empty ) && !FWorldDefinition::FromJson( NlohmannJson(), Empty ), "a JSON that is not an object is refused" );
		FWorldDefinition Untouched = Rich;
		FWorldDefinition::FromJson( ParseJson( "[1]" ), Untouched );
		C.Check( Untouched.Name == "Stage1", "and the output is left alone" );

		// 新しい版
		Warnings.Empty();
		FWorldDefinition Newer;
		C.Check( FWorldDefinition::FromJson( ParseJson( "{\"Version\":99,\"Objects\":[{\"Id\":1}]}" ), Newer, &Warnings ) && Newer.Version == 99 && Warnings.Num() == 1 && Warnings[0].Contains( "newer" ), "a newer file version is read, with a warning" );
		const NlohmannJson Resaved = Newer.ToJson();
		C.Check( Resaved["Version"] == 99, "and its version is kept when it is written back (not silently downgraded)" );
	}

	//=========================================================================
	// 読み込みでの修復・警告
	//=========================================================================
	void TestJsonRepair( NAssetTest::FChecker& C )
	{
		C.Group = "world-json-repair";
		TArray<FString> Warnings;
		FWorldDefinition World;
		const char* BadIds = "{\"NextId\":2,\"Objects\":[{\"Id\":3,\"Name\":\"a\"},{\"Id\":3,\"Name\":\"dup\"},{\"Name\":\"none\"},{\"Id\":-5,\"Name\":\"neg\"},{\"Id\":0,\"Name\":\"zero\"},5,{\"Id\":9,\"Name\":\"z\"}]}";
		C.Check( FWorldDefinition::FromJson( ParseJson( BadIds ), World, &Warnings ), "a world with bad ids loads" );
		std::set<int32> Ids;
		bool bAllPositive = true;
		for ( const FWorldObject& Object : World.Objects )
		{
			Ids.insert( Object.Id );
			bAllPositive = bAllPositive && Object.Id > 0;
		}
		C.Check( World.Objects.Num() == 6 && Ids.size() == 6 && bAllPositive, "every kept object has its own positive id (%d objects, %d ids)", World.Objects.Num(), static_cast<int32>( Ids.size() ) );
		C.Check( World.Objects[0].Id == 3 && World.Objects[5].Id == 9, "valid ids are never changed" );
		C.Check( World.NextId > 9, "NextId is above every id (%d)", World.NextId );
		C.Check( Warnings.Num() == 5, "one warning for each of the 4 repaired objects and the object that was skipped (%d)", Warnings.Num() );
		C.Check( World.Objects[1].Name == "dup" && World.Objects[1].Id != 3 && World.Objects[2].Name == "none", "the later duplicate is the one that gets a new id" );

		// 古い NextId でも、既存の Id と重ならない
		Warnings.Empty();
		FWorldDefinition Stale;
		FWorldDefinition::FromJson( ParseJson( "{\"NextId\":1,\"Objects\":[{\"Id\":10},{\"Id\":4}]}" ), Stale, &Warnings );
		C.Check( Stale.NextId == 11 && Warnings.IsEmpty() && Stale.AllocateId() == 11 && Stale.AllocateId() == 12, "a stale NextId is raised above the largest id" );

		// タグ
		Warnings.Empty();
		FWorldDefinition Tags;
		FWorldDefinition::FromJson( ParseJson( "{\"Objects\":[{\"Id\":1,\"Tags\":[\"A\",\"A\",\"  \",\"B \",5,\"a\"]}]}" ), Tags, &Warnings );
		C.Check( Tags.Objects[0].Tags.Num() == 3 && Tags.Objects[0].Tags[0] == "A" && Tags.Objects[0].Tags[1] == "B" && Tags.Objects[0].Tags[2] == "a", "tags are trimmed, de-duplicated (case sensitive), and non-strings are skipped" );
		C.Check( Warnings.Num() == 3, "and each skipped tag is reported (%d)", Warnings.Num() );

		// プロパティ
		Warnings.Empty();
		FWorldDefinition Props;
		FWorldDefinition::FromJson( ParseJson( "{\"Objects\":[{\"Id\":1,\"Props\":{\"A\":null,\"B\":[1,2],\"C\":{\"x\":1},\"D\":1,\"E\":[1,2,\"x\"],\"F\":{\"Asset\":5}}}]}" ), Props, &Warnings );
		C.Check( Props.Objects[0].Props.Num() == 1 && Props.Objects[0].Props[0].Name == "D" && Warnings.Num() == 5, "properties of an unsupported form are skipped, each with a warning (%d kept, %d warnings)", Props.Objects[0].Props.Num(),
				 Warnings.Num() );
		// 名前の順に並ぶ
		FWorldObject Sorted;
		Sorted.SetProp( FWorldProp::MakeInt( "b", 1 ) );
		Sorted.SetProp( FWorldProp::MakeInt( "a", 2 ) );
		Sorted.SetProp( FWorldProp::MakeInt( "c", 3 ) );
		Sorted.SetProp( FWorldProp::MakeInt( "a", 9 ) );
		C.Check( Sorted.Props.Num() == 3 && Sorted.Props[0].Name == "a" && Sorted.Props[0].IntValue == 9 && Sorted.Props[1].Name == "b" && Sorted.Props[2].Name == "c", "SetProp keeps names sorted and replaces a same-named property" );
		C.Check( Sorted.RemoveProp( "b" ) && !Sorted.RemoveProp( "b" ) && Sorted.Props.Num() == 2 && Sorted.FindProp( "b" ) == nullptr, "RemoveProp" );

		// 知らない名前
		Warnings.Empty();
		FWorldDefinition Names;
		FWorldDefinition::FromJson( ParseJson( "{\"Settings\":{\"CollisionRules\":[{\"Tag\":\"X\",\"Collision\":\"On\"},{\"Tag\":\"\",\"Collision\":\"On\"},{\"Tag\":\"Y\"},{\"Tag\":\"Z\",\"Collision\":\"Auto\"},{\"Tag\":\"W\",\"Collision\":\"Maybe\"}]},"
											   "\"Objects\":[{\"Id\":1,\"Collision\":\"Maybe\",\"Components\":{\"Billboard\":{\"Texture\":\"a.png\",\"Mode\":\"Spin\",\"Tint\":\"zz\"},\"Collider\":{\"Shape\":\"Cone\"}}}]}" ),
								Names, &Warnings );
		C.Check( Names.Settings.CollisionRules.Num() == 4 && Names.Settings.CollisionRules[0].bCollide && !Names.Settings.CollisionRules[1].bCollide && !Names.Settings.CollisionRules[2].bCollide && !Names.Settings.CollisionRules[3].bCollide,
				 "rules: an empty tag is skipped; a missing / Auto / unknown action becomes Off (%d rules)", Names.Settings.CollisionRules.Num() );
		const FWorldObject& N = Names.Objects[0];
		C.Check( N.Collision == EWorldCollisionMode::Auto && N.Billboard->Mode == EWorldBillboardMode::Camera && N.Billboard->Tint.r == 255 && N.Billboard->Tint.a == 255 && N.Collider->Shape == EWorldColliderShape::Box,
				 "unknown mode / colour / shape names fall back to the defaults" );
		C.Check( Warnings.Num() == 7, "and each is reported, with the rules' problems (%d)", Warnings.Num() );

		// 形の壊れた既知の部品は、そのまま残す
		Warnings.Empty();
		FWorldDefinition Broken;
		FWorldDefinition::FromJson( ParseJson( "{\"Objects\":[{\"Id\":1,\"Components\":{\"Model\":5,\"Sound\":[1]}}]}" ), Broken, &Warnings );
		C.Check( !Broken.Objects[0].Model.IsSet() && !Broken.Objects[0].Sound.IsSet() && Broken.Objects[0].RawComponents.Num() == 2 && Warnings.Num() == 2, "a known component that is not an object is kept as raw data, with a warning" );
		C.Check( FJ( Broken.ToJson() )["Objects"][0]["Components"]["Model"] == 5, "and written back as it was" );

		// 型が違う値は、既定値
		FWorldDefinition Types;
		FWorldDefinition::FromJson( ParseJson( "{\"Name\":5,\"Objects\":[{\"Id\":1,\"Name\":7,\"Parent\":\"x\",\"Enabled\":\"no\",\"Transform\":{\"Position\":[1,2],\"Scale\":\"big\"},\"Components\":{\"Model\":{\"Path\":3,\"Speed\":\"fast\"}}}]}" ), Types );
		C.Check( Types.Name.IsEmpty() && Types.Objects[0].Name.IsEmpty() && Types.Objects[0].Parent == WorldNoId && Types.Objects[0].bEnabled && Near3( Types.Objects[0].Transform.Position, FVector3::Zero )
					 && Near3( Types.Objects[0].Transform.Scale, FVector3::One ) && Types.Objects[0].Model->Path.IsEmpty() && Types.Objects[0].Model->Speed == 1.0f,
				 "values of the wrong type become the defaults" );

		// 日本語（Shift-JIS の FString <-> UTF-8 の JSON）
		FWorldDefinition Japanese;
		Japanese.Name = "ステージ1";
		FWorldObject Torch = MakeObject( 1, "松明" );
		Torch.AddTag( "照明" );
		Torch.SetProp( FWorldProp::MakeString( "説明", "表\\燃える松明\\です" ) );
		Japanese.Objects.Add( Torch );
		FWorldDefinition JapaneseBack;
		FWorldDefinition::FromJson( ParseJson( Japanese.ToJson().dump().c_str() ), JapaneseBack );
		C.Check( JapaneseBack.Name == "ステージ1" && JapaneseBack.Objects[0].Name == "松明" && JapaneseBack.Objects[0].HasTag( "照明" ) && JapaneseBack.Objects[0].FindProp( "説明" ) != nullptr
					 && JapaneseBack.Objects[0].FindProp( "説明" )->StringValue == "表\\燃える松明\\です",
				 "Japanese names, tags and properties survive (including a character whose second byte is a backslash)" );
	}

	//=========================================================================
	// 位置・回転
	//=========================================================================
	void TestTransform( NAssetTest::FChecker& C )
	{
		C.Group = "world-transform";
		FWorldTransform Spec;
		Spec.Position = FVector3( 1.0f, 2.0f, 3.0f );
		Spec.RotationDegrees = FVector3( 0.0f, 90.0f, 0.0f );
		Spec.Scale = FVector3( 2.0f, 1.0f, 1.0f );
		const FTransform Transform = Spec.ToTransform();
		C.Check( Near3( Transform.Position, FVector3( 1.0f, 2.0f, 3.0f ) ) && Near3( Transform.Scale, FVector3( 2.0f, 1.0f, 1.0f ) ), "position and scale are copied" );
		// Y 軸まわり 90 度（Yaw）で、前（+Z）が右（+X）を向く（左手系・Y 上）
		C.Check( Near3( Transform.TransformDirection( FVector3::Forward ), FVector3( 1.0f, 0.0f, 0.0f ) ), "yaw 90 degrees turns forward (+Z) to +X" );
		C.Check( Near3( Transform.TransformPoint( FVector3( 1.0f, 0.0f, 0.0f ) ), FVector3( 1.0f, 2.0f, 1.0f ) ), "scale, rotation, then position: (1,0,0) -> scaled (2,0,0) -> rotated (0,0,-2) -> (1,2,1)" );
		FWorldTransform Pitch;
		Pitch.RotationDegrees = FVector3( 90.0f, 0.0f, 0.0f );
		C.Check( Near3( Pitch.ToTransform().TransformDirection( FVector3::Forward ), FVector3( 0.0f, -1.0f, 0.0f ), 1.0e-3f ) || Near3( Pitch.ToTransform().TransformDirection( FVector3::Forward ), FVector3( 0.0f, 1.0f, 0.0f ), 1.0e-3f ),
				 "pitch 90 degrees points forward straight up or down" );

		bool bRoundTrip = true;
		for ( const FVector3& Degrees : { FVector3( 10.0f, 20.0f, 30.0f ), FVector3( -45.0f, 100.0f, 5.0f ), FVector3( 0.0f, 0.0f, 0.0f ), FVector3( 0.0f, -170.0f, 0.0f ) } )
		{
			FWorldTransform Source;
			Source.RotationDegrees = Degrees;
			const FTransform Converted = Source.ToTransform();
			const FWorldTransform Back = FWorldTransform::FromTransform( Converted );
			// 同じ向きになるか（角度の表し方が違っても、同じ回転なら、向きは同じ）
			const FTransform Again = Back.ToTransform();
			bRoundTrip = bRoundTrip && Near3( Again.TransformDirection( FVector3::Forward ), Converted.TransformDirection( FVector3::Forward ), 1.0e-3f ) && Near3( Again.TransformDirection( FVector3::Up ), Converted.TransformDirection( FVector3::Up ), 1.0e-3f );
		}
		C.Check( bRoundTrip, "FromTransform(ToTransform(x)) is the same orientation" );
		C.Check( FWorldTransform().IsIdentity() && !Spec.IsIdentity(), "IsIdentity" );
	}

	//=========================================================================
	// 階層
	//=========================================================================
	FWorldDefinition MakeHierarchyWorld()
	{
		FWorldDefinition World;
		FWorldObject Root = MakeObject( 1, "Root" );
		Root.Transform.Position = FVector3( 10.0f, 0.0f, 0.0f );
		Root.Transform.RotationDegrees = FVector3( 0.0f, 90.0f, 0.0f );
		Root.Transform.Scale = FVector3( 2.0f, 2.0f, 2.0f );
		World.Objects.Add( Root );
		FWorldObject ChildA = MakeObject( 2, "ChildA", 1 );
		ChildA.Transform.Position = FVector3( 0.0f, 0.0f, 1.0f );
		World.Objects.Add( ChildA );
		FWorldObject Grand = MakeObject( 3, "Grand", 2 );
		Grand.Transform.Position = FVector3( 0.0f, 1.0f, 0.0f );
		World.Objects.Add( Grand );
		World.Objects.Add( MakeObject( 4, "ChildB", 1 ) );
		World.Objects.Add( MakeObject( 5, "Other" ) );
		World.Objects.Add( MakeObject( 6, "Orphan", 99 ) );
		World.Objects.Add( MakeObject( 7, "CycA", 8 ) );
		World.Objects.Add( MakeObject( 8, "CycB", 7 ) );
		FWorldObject Disabled = MakeObject( 9, "Disabled" );
		Disabled.bEnabled = false;
		World.Objects.Add( Disabled );
		World.Objects.Add( MakeObject( 10, "UnderDisabled", 9 ) );
		World.Objects.Add( MakeObject( 11, "Self", 11 ) );
		World.NextId = 12;
		return World;
	}

	TArray<int32> IdsOf( const FWorldDefinition& World, const TArray<int32>& Indices )
	{
		TArray<int32> Ids;
		for ( const int32 Index : Indices ) Ids.Add( World.Objects[Index].Id );
		return Ids;
	}

	bool IdsAre( const FWorldDefinition& World, const TArray<int32>& Indices, std::initializer_list<int32> Expected )
	{
		const TArray<int32> Ids = IdsOf( World, Indices );
		if ( Ids.Num() != static_cast<int32>( Expected.size() ) ) return false;
		int32 i = 0;
		for ( const int32 Id : Expected )
		{
			if ( Ids[i++] != Id ) return false;
		}
		return true;
	}

	void TestHierarchy( NAssetTest::FChecker& C )
	{
		C.Group = "world-hierarchy";
		const FWorldDefinition World = MakeHierarchyWorld();

		C.Check( World.FindIndexById( 3 ) == 2 && World.FindIndexById( 99 ) == INDEX_NONE && World.FindIndexById( 0 ) == INDEX_NONE && World.FindObject( 5 ) != nullptr && World.FindObject( 5 )->Name == "Other" && World.FindObject( 99 ) == nullptr, "FindObject by id" );
		C.Check( World.FindObjectByName( "Grand" ) != nullptr && World.FindObjectByName( "Grand" )->Id == 3 && World.FindObjectByName( "grand" ) == nullptr, "FindObjectByName is case sensitive" );

		C.Check( IdsAre( World, NWorld::GetChildIndices( World, 1 ), { 2, 4 } ), "the children of an object are in array order" );
		C.Check( IdsAre( World, NWorld::GetChildIndices( World, WorldNoId ), { 1, 5, 9 } ), "the top-level objects have no parent (orphans and loops are not top-level)" );
		C.Check( NWorld::GetChildIndices( World, 3 ).IsEmpty() && NWorld::GetChildIndices( World, 11 ).IsEmpty(), "a leaf has no children (an object is not its own child)" );
		C.Check( IdsAre( World, NWorld::GetSubtreeIndices( World, 1 ), { 1, 2, 3, 4 } ), "a subtree is the object then its descendants, parents first" );
		C.Check( NWorld::GetSubtreeIndices( World, 99 ).IsEmpty() && IdsAre( World, NWorld::GetSubtreeIndices( World, 7 ), { 7, 8 } ), "no subtree for a missing id; a loop gives each member once" );

		C.Check( NWorld::IsDescendantOf( World, 1, 3 ) && NWorld::IsDescendantOf( World, 2, 3 ) && !NWorld::IsDescendantOf( World, 3, 1 ) && !NWorld::IsDescendantOf( World, 1, 1 ) && !NWorld::IsDescendantOf( World, 4, 3 ) && !NWorld::IsDescendantOf( World, WorldNoId, 3 ),
				 "IsDescendantOf" );
		C.Check( NWorld::IsDescendantOf( World, 7, 8 ) && NWorld::IsDescendantOf( World, 8, 7 ) && !NWorld::IsDescendantOf( World, 7, 7 ) && !NWorld::IsDescendantOf( World, 11, 11 ) && !NWorld::IsDescendantOf( World, 1, 7 ), "it stops on loops" );
		C.Check( NWorld::GetDepth( World, 1 ) == 0 && NWorld::GetDepth( World, 2 ) == 1 && NWorld::GetDepth( World, 3 ) == 2 && NWorld::GetDepth( World, 6 ) == 0 && NWorld::GetDepth( World, 99 ) == 0, "depth (an orphan counts as top-level)" );
		C.Check( NWorld::GetDepth( World, 7 ) >= 1 && NWorld::GetDepth( World, 7 ) < 5000, "depth of a loop is finite" );

		// ワールド変換: 親（位置 (10,0,0)、Yaw 90、拡大 2）の子の位置 (0,0,1) は、前に 2 進んで (12,0,0)
		const FTransform ChildWorld = NWorld::ComputeWorldTransform( World, 2 );
		C.Check( Near3( ChildWorld.Position, FVector3( 12.0f, 0.0f, 0.0f ) ) && Near3( ChildWorld.Scale, FVector3( 2.0f, 2.0f, 2.0f ) ), "a child's world position follows the parent's position, rotation and scale (%s)", *ChildWorld.Position.ToString() );
		const FTransform GrandWorld = NWorld::ComputeWorldTransform( World, 3 );
		C.Check( Near3( GrandWorld.Position, FVector3( 12.0f, 2.0f, 0.0f ) ) && Near3( GrandWorld.Scale, FVector3( 2.0f, 2.0f, 2.0f ) ) && Near3( GrandWorld.TransformDirection( FVector3::Forward ), FVector3( 1.0f, 0.0f, 0.0f ) ),
				 "and so on down the chain (%s)", *GrandWorld.Position.ToString() );
		C.Check( Near3( NWorld::ComputeWorldTransform( World, 5 ).Position, FVector3::Zero ) && Near3( NWorld::ComputeWorldTransform( World, 1 ).Position, FVector3( 10.0f, 0.0f, 0.0f ) ), "a top-level object's world transform is its own" );
		C.Check( Near3( NWorld::ComputeWorldTransform( World, 99 ).Position, FVector3::Zero ) && NWorld::ComputeWorldTransform( World, 7 ).Scale.x > 0.0f, "a missing id gives identity; a loop terminates" );

		// 設置の順
		const NWorld::FWorldSpawnOrder Order = NWorld::ComputeSpawnOrder( World, true );
		C.Check( IdsAre( World, Order.Order, { 1, 2, 3, 4, 5, 6 } ), "spawn order: parents first, siblings in array order, orphans placed as top-level" );
		C.Check( IdsAre( World, Order.Orphans, { 6 } ) && IdsAre( World, Order.Cyclic, { 7, 8, 11 } ), "orphans and loops (and self-parents) are reported separately (a loop is never placed)" );
		const NWorld::FWorldSpawnOrder All = NWorld::ComputeSpawnOrder( World, false );
		C.Check( IdsAre( World, All.Order, { 1, 2, 3, 4, 5, 6, 9, 10 } ) && IdsAre( World, All.Cyclic, { 7, 8, 11 } ), "without skipping the disabled, they and their children are included" );

		// 並び順が親の前でも大丈夫（子が配列の前にある）
		FWorldDefinition Reversed;
		Reversed.Objects.Add( MakeObject( 3, "Grand", 2 ) );
		Reversed.Objects.Add( MakeObject( 2, "Child", 1 ) );
		Reversed.Objects.Add( MakeObject( 1, "Root" ) );
		C.Check( IdsAre( Reversed, NWorld::ComputeSpawnOrder( Reversed, true ).Order, { 1, 2, 3 } ), "a child listed before its parent is still placed after it" );
	}

	//=========================================================================
	// 当たり判定の決定
	//=========================================================================
	void TestCollision( NAssetTest::FChecker& C )
	{
		C.Group = "world-collision";
		FWorldSettings Settings;
		Settings.CollisionRules.Add( Rule( "Decoration", false ) );
		Settings.CollisionRules.Add( Rule( "Wall", true ) );
		Settings.CollisionRules.Add( Rule( "Wall", false ) );   // 上の Wall の規則が先なので、使われない

		const auto Make = [&]( std::initializer_list<const char*> Tags, EWorldCollisionMode Mode, bool bCollider )
		{
			FWorldObject Object = MakeObject( 1, "o" );
			for ( const char* Tag : Tags ) Object.AddTag( Tag );
			Object.Collision = Mode;
			if ( bCollider ) Object.Collider = FWorldCollider();
			return Object;
		};

		FWorldCollisionDecision D = NWorld::ResolveCollision( Settings, Make( { "Wall" }, EWorldCollisionMode::On, false ) );
		C.Check( !D.bEnabled && D.Reason == EWorldCollisionReason::NoCollider && D.ToDisplayString().Contains( "no collider" ), "without a collider shape nothing can be generated, whatever is set" );
		D = NWorld::ResolveCollision( Settings, Make( {}, EWorldCollisionMode::Auto, true ) );
		C.Check( D.bEnabled && D.Reason == EWorldCollisionReason::Default && D.ToDisplayString().Contains( "default" ), "a collider with nothing else set is on by default" );
		D = NWorld::ResolveCollision( Settings, Make( { "Decoration" }, EWorldCollisionMode::Auto, true ) );
		C.Check( !D.bEnabled && D.Reason == EWorldCollisionReason::TagRule && D.Detail == "Decoration" && D.ToDisplayString().Contains( "'Decoration'" ), "a tag rule can turn collision off" );
		D = NWorld::ResolveCollision( Settings, Make( { "Wall" }, EWorldCollisionMode::Auto, true ) );
		C.Check( D.bEnabled && D.Reason == EWorldCollisionReason::TagRule && D.Detail == "Wall", "or on; the first matching rule wins (a later rule for the same tag is never used)" );
		D = NWorld::ResolveCollision( Settings, Make( { "Wall", "Decoration" }, EWorldCollisionMode::Auto, true ) );
		C.Check( !D.bEnabled && D.Detail == "Decoration", "with several tags, the rule order decides, not the order of the object's tags" );
		D = NWorld::ResolveCollision( Settings, Make( { "Decoration" }, EWorldCollisionMode::On, true ) );
		C.Check( D.bEnabled && D.Reason == EWorldCollisionReason::ObjectOverride && D.Detail == "On" && D.ToDisplayString().Contains( "set on the object" ), "the object's own On beats a tag rule that says off" );
		D = NWorld::ResolveCollision( Settings, Make( { "Wall" }, EWorldCollisionMode::Off, true ) );
		C.Check( !D.bEnabled && D.Reason == EWorldCollisionReason::ObjectOverride && D.Detail == "Off", "and Off beats a rule that says on" );
		D = NWorld::ResolveCollision( Settings, Make( { "decoration", "Unrelated" }, EWorldCollisionMode::Auto, true ) );
		C.Check( D.bEnabled && D.Reason == EWorldCollisionReason::Default, "tags are case sensitive (no rule matches 'decoration')" );
		D = NWorld::ResolveCollision( FWorldSettings(), Make( { "Decoration" }, EWorldCollisionMode::Auto, true ) );
		C.Check( D.bEnabled && D.Reason == EWorldCollisionReason::Default, "no rules, no effect" );
		FWorldObject Trigger = Make( { "Decoration" }, EWorldCollisionMode::Auto, true );
		Trigger.Collider->bTrigger = true;
		C.Check( !NWorld::ResolveCollision( Settings, Trigger ).bEnabled, "rules apply to triggers as well" );
	}

	//=========================================================================
	// 検証
	//=========================================================================
	class FFakeQuery final : public IWorldAssetQuery
	{
	public:
		std::set<std::string> Existing;
		bool Exists( const FString& Path ) const override { return Existing.count( std::string( *Path ) ) > 0; }
	};

	int32 CountIssues( const TArray<FWorldIssue>& Issues, EWorldIssueCode Code, int32 ObjectId = -1 )
	{
		int32 Count = 0;
		for ( const FWorldIssue& Issue : Issues )
		{
			if ( Issue.Code == Code && ( ObjectId < 0 || Issue.ObjectId == ObjectId ) ) ++Count;
		}
		return Count;
	}

	const FWorldIssue* FindIssue( const TArray<FWorldIssue>& Issues, EWorldIssueCode Code )
	{
		for ( const FWorldIssue& Issue : Issues )
		{
			if ( Issue.Code == Code ) return &Issue;
		}
		return nullptr;
	}

	void TestValidate( NAssetTest::FChecker& C )
	{
		C.Group = "world-validate";
		FFakeQuery Assets;
		for ( const char* Path : { "Models/Torch.mv1", "Models/Torch.modelanim.json", "Sprites/Glow.png", "Sounds/Fire.wav", "Sounds/a.wav" } ) Assets.Existing.insert( Path );

		// 問題のないワールド
		FWorldDefinition Clean = MakeRichWorld();
		Clean.Objects[0].RawComponents.Empty();   // 知らない部品は、警告になる
		Clean.Objects[0].Collision = EWorldCollisionMode::Auto;
		TArray<FWorldIssue> Issues = NWorld::ValidateWorld( Clean, &Assets );
		C.Check( Issues.IsEmpty(), "a sound world has no issues (%d: %s)", Issues.Num(), Issues.IsEmpty() ? "" : *Issues[0].ToDisplayString() );
		C.Check( NWorld::ValidateWorld( FWorldDefinition(), &Assets ).IsEmpty(), "an empty world has no issues" );

		// NextId は、使われている最大の Id より大きくなければならない（等しいと、次のオブジェクトが同じ Id になる）
		FWorldDefinition Boundary;
		Boundary.Objects.Add( MakeObject( 5, "a" ) );
		Boundary.NextId = 5;
		C.Check( CountIssues( NWorld::ValidateWorld( Boundary, nullptr ), EWorldIssueCode::NextIdTooSmall ) == 1, "a NextId equal to the largest id is too small" );
		Boundary.NextId = 6;
		C.Check( NWorld::ValidateWorld( Boundary, nullptr ).IsEmpty(), "and one above it is fine" );

		// ワールド全体
		FWorldDefinition W;
		W.Objects.Add( MakeObject( 0, "zero" ) );
		W.Objects.Add( MakeObject( 5, "a" ) );
		W.Objects.Add( MakeObject( 5, "b" ) );
		W.NextId = 3;
		W.Version = WorldFormatVersion + 1;
		W.Settings.CollisionRules.Add( Rule( "", true ) );
		W.Settings.CollisionRules.Add( Rule( "X", true ) );
		W.Settings.CollisionRules.Add( Rule( "X", false ) );
		Issues = NWorld::ValidateWorld( W, nullptr );
		C.Check( CountIssues( Issues, EWorldIssueCode::InvalidId ) == 1 && CountIssues( Issues, EWorldIssueCode::DuplicateId, 5 ) == 1 && CountIssues( Issues, EWorldIssueCode::NextIdTooSmall ) == 1 && CountIssues( Issues, EWorldIssueCode::NewerVersion ) == 1,
				 "ids, NextId and the file version" );
		C.Check( CountIssues( Issues, EWorldIssueCode::EmptyRuleTag ) == 1 && CountIssues( Issues, EWorldIssueCode::ShadowedRule ) == 1, "collision rules: an empty tag, and a rule that never applies" );
		const FWorldIssue* Version = FindIssue( Issues, EWorldIssueCode::NewerVersion );
		const FWorldIssue* Duplicate = FindIssue( Issues, EWorldIssueCode::DuplicateId );
		C.Check( Version != nullptr && Version->Severity == EWorldIssueSeverity::Warning && Duplicate != nullptr && Duplicate->Severity == EWorldIssueSeverity::Error && NWorld::HasErrors( Issues ), "severities: a newer version is a warning, a duplicate id an error" );
		C.Check( Duplicate != nullptr && Duplicate->ToDisplayString().StartsWith( "[Error] #5:" ) && Version->ToDisplayString().StartsWith( "[Warning]: " ), "ToDisplayString: severity, object id, message" );

		// 階層
		FWorldDefinition H;
		H.Objects.Add( MakeObject( 1, "self", 1 ) );
		H.Objects.Add( MakeObject( 2, "missing", 77 ) );
		H.Objects.Add( MakeObject( 3, "a", 4 ) );
		H.Objects.Add( MakeObject( 4, "b", 3 ) );
		H.Objects.Add( MakeObject( 5, "below", 3 ) );
		H.Objects.Add( MakeObject( 6, "fine" ) );
		H.NextId = 7;
		Issues = NWorld::ValidateWorld( H, nullptr );
		C.Check( CountIssues( Issues, EWorldIssueCode::SelfParent, 1 ) == 1 && CountIssues( Issues, EWorldIssueCode::ParentCycle, 1 ) == 0, "an object that is its own parent (reported once)" );
		C.Check( CountIssues( Issues, EWorldIssueCode::MissingParent, 2 ) == 1 && CountIssues( Issues, EWorldIssueCode::ParentCycle, 2 ) == 0, "a parent that does not exist (the object is still placed, so it is not a loop)" );
		C.Check( CountIssues( Issues, EWorldIssueCode::ParentCycle, 3 ) == 1 && CountIssues( Issues, EWorldIssueCode::ParentCycle, 4 ) == 1 && CountIssues( Issues, EWorldIssueCode::ParentCycle, 5 ) == 1 && CountIssues( Issues, EWorldIssueCode::ParentCycle, 6 ) == 0,
				 "a loop of parents, and what hangs below it" );

		// タグ・プロパティ・値
		FWorldDefinition P;
		FWorldObject O = MakeObject( 1, "o" );
		O.Tags.Add( "ok" );
		O.Tags.Add( "" );
		O.Tags.Add( " pad " );
		O.Tags.Add( "ok" );
		O.Props.Add( FWorldProp::MakeInt( "", 1 ) );
		O.Props.Add( FWorldProp::MakeInt( "x", 1 ) );
		O.Props.Add( FWorldProp::MakeInt( "x", 2 ) );
		O.Props.Add( FWorldProp::MakeFloat( "nan", TNumericLimits<float64>::QuietNaN() ) );
		O.Props.Add( FWorldProp::MakeVector3( "v", FVector3( 1.0f, TNumericLimits<float32>::Infinity(), 0.0f ) ) );
		O.Props.Add( FWorldProp::MakeObject( "gone", 42 ) );
		O.Props.Add( FWorldProp::MakeObject( "self", 1 ) );
		O.Props.Add( FWorldProp::MakeAsset( "missingFile", "nope.wav" ) );
		O.Props.Add( FWorldProp::MakeAsset( "emptyFile", "" ) );
		O.Props.Add( FWorldProp::MakeAsset( "goodFile", "Sounds/a.wav" ) );
		O.Transform.Scale = FVector3( 1.0f, 0.0f, 1.0f );
		P.Objects.Add( O );
		FWorldObject Nan = MakeObject( 2, "nan" );
		Nan.Transform.Position = FVector3( TNumericLimits<float32>::QuietNaN(), 0.0f, 0.0f );
		P.Objects.Add( Nan );
		P.NextId = 3;
		Issues = NWorld::ValidateWorld( P, &Assets );
		C.Check( CountIssues( Issues, EWorldIssueCode::EmptyTag, 1 ) == 2 && CountIssues( Issues, EWorldIssueCode::DuplicateTag, 1 ) == 1, "tags: empty or padded, and duplicated" );
		C.Check( CountIssues( Issues, EWorldIssueCode::EmptyPropName, 1 ) == 1 && CountIssues( Issues, EWorldIssueCode::DuplicatePropName, 1 ) == 1 && CountIssues( Issues, EWorldIssueCode::BadPropValue, 1 ) == 2, "properties: no name, same name twice, non-finite values" );
		C.Check( CountIssues( Issues, EWorldIssueCode::MissingObjectReference, 1 ) == 2 && CountIssues( Issues, EWorldIssueCode::MissingAsset, 1 ) == 1 && CountIssues( Issues, EWorldIssueCode::EmptyAssetPath, 1 ) == 1, "references: a missing object, itself (warning), a missing file, an empty path; an existing file is fine" );
		C.Check( CountIssues( Issues, EWorldIssueCode::ZeroScale, 1 ) == 1 && CountIssues( Issues, EWorldIssueCode::NonFiniteValue, 2 ) == 1, "a 0 scale (warning) and a NaN position (error)" );
		Issues = NWorld::ValidateWorld( P, nullptr );
		C.Check( CountIssues( Issues, EWorldIssueCode::MissingAsset ) == 0 && CountIssues( Issues, EWorldIssueCode::EmptyAssetPath ) == 1, "without an asset query, missing files are not checked (empty paths still are)" );

		// 部品
		FWorldDefinition K;
		FWorldObject M = MakeObject( 1, "model" );
		M.Model = FWorldModel();
		K.Objects.Add( M );
		FWorldObject M2 = MakeObject( 2, "model2" );
		FWorldModel Missing;
		Missing.Path = "nope.mv1";
		Missing.Animations = "nope.modelanim.json";
		Missing.Clip = "Burn";
		Missing.Speed = 0.0f;
		M2.Model = Missing;
		K.Objects.Add( M2 );
		FWorldObject B = MakeObject( 3, "board" );
		FWorldBillboard Bad;
		Bad.Texture = "nope.png";
		Bad.Size = FVector2( 0.0f, 1.0f );
		Bad.Pivot = FVector2( 1.5f, 0.5f );
		Bad.bUseRect = true;
		Bad.RectWidth = 0;
		Bad.RectHeight = 4;
		Bad.Tint.a = 0;
		B.Billboard = Bad;
		K.Objects.Add( B );
		FWorldObject S = MakeObject( 4, "sound" );
		FWorldSound BadSound;
		BadSound.Path = "nope.wav";
		BadSound.Bus = "Boom";
		BadSound.Volume = -1.0f;
		BadSound.Pitch = 0.0f;
		BadSound.MinDistance = 5.0f;
		BadSound.MaxDistance = 5.0f;
		S.Sound = BadSound;
		K.Objects.Add( S );
		const auto AddCollider = [&]( int32 Id, EWorldColliderShape Shape, const FVector3& Size, float32 Radius, float32 Height )
		{
			FWorldObject Object = MakeObject( Id, "collider" );
			FWorldCollider Collider;
			Collider.Shape = Shape;
			Collider.Size = Size;
			Collider.Radius = Radius;
			Collider.Height = Height;
			Object.Collider = Collider;
			K.Objects.Add( Object );
		};
		AddCollider( 5, EWorldColliderShape::Box, FVector3( 1.0f, 0.0f, 1.0f ), 1.0f, 2.0f );
		AddCollider( 6, EWorldColliderShape::Sphere, FVector3::One, 0.0f, 2.0f );
		AddCollider( 7, EWorldColliderShape::Capsule, FVector3::One, 0.5f, 0.9f );
		AddCollider( 8, EWorldColliderShape::Capsule, FVector3::One, 0.5f, 1.0f );   // ちょうど半径の 2 倍: よい
		AddCollider( 9, EWorldColliderShape::Sphere, FVector3::One, TNumericLimits<float32>::Infinity(), 2.0f );
		FWorldObject Forced = MakeObject( 10, "forced" );
		Forced.Collision = EWorldCollisionMode::On;
		K.Objects.Add( Forced );
		FWorldObject Off = MakeObject( 11, "off" );
		Off.Collision = EWorldCollisionMode::Off;
		K.Objects.Add( Off );
		FWorldObject Raw = MakeObject( 12, "raw" );
		Raw.RawComponents.Add( FWorldRawComponent { "Hologram", ParseJson( "{}" ) } );
		K.Objects.Add( Raw );
		K.NextId = 13;
		Issues = NWorld::ValidateWorld( K, &Assets );
		C.Check( CountIssues( Issues, EWorldIssueCode::EmptyAssetPath, 1 ) == 1, "a model with no file" );
		C.Check( CountIssues( Issues, EWorldIssueCode::MissingAsset, 2 ) == 2 && CountIssues( Issues, EWorldIssueCode::BadModel, 2 ) == 1, "a model whose file and animation set are missing; speed 0 with a clip" );
		C.Check( CountIssues( Issues, EWorldIssueCode::MissingAsset, 3 ) == 1 && CountIssues( Issues, EWorldIssueCode::BadBillboard, 3 ) == 4, "a billboard: missing image; size, pivot, rectangle and alpha problems (%d)", CountIssues( Issues, EWorldIssueCode::BadBillboard, 3 ) );
		C.Check( CountIssues( Issues, EWorldIssueCode::MissingAsset, 4 ) == 1 && CountIssues( Issues, EWorldIssueCode::BadSound, 4 ) == 4, "a sound: missing file, unknown bus, volume, pitch, distances (%d)", CountIssues( Issues, EWorldIssueCode::BadSound, 4 ) );
		C.Check( CountIssues( Issues, EWorldIssueCode::BadCollider, 5 ) == 1 && CountIssues( Issues, EWorldIssueCode::BadCollider, 6 ) == 1 && CountIssues( Issues, EWorldIssueCode::BadCollider, 7 ) == 1 && CountIssues( Issues, EWorldIssueCode::BadCollider, 8 ) == 0
					 && CountIssues( Issues, EWorldIssueCode::NonFiniteValue, 9 ) == 1,
				 "colliders: flat box, zero radius, a capsule shorter than 2 radii (exactly 2 radii is fine), infinity" );
		C.Check( CountIssues( Issues, EWorldIssueCode::CollisionWithoutCollider, 10 ) == 1 && CountIssues( Issues, EWorldIssueCode::CollisionWithoutCollider, 11 ) == 0, "Collision On with no collider shape is a warning; Off is fine" );
		C.Check( CountIssues( Issues, EWorldIssueCode::UnsupportedComponent, 12 ) == 1 && FindIssue( Issues, EWorldIssueCode::UnsupportedComponent )->Severity == EWorldIssueSeverity::Warning, "an unknown component is a warning" );
		FWorldDefinition OnlyWarnings;
		OnlyWarnings.Objects.Add( Raw );
		OnlyWarnings.NextId = 13;
		C.Check( !NWorld::ValidateWorld( OnlyWarnings, &Assets ).IsEmpty() && !NWorld::HasErrors( NWorld::ValidateWorld( OnlyWarnings, &Assets ) ), "a world with only warnings has no errors" );
		C.Check( ToString( EWorldIssueCode::MissingAsset ) != nullptr && FString( ToString( EWorldIssueCode::MissingAsset ) ) == "MissingAsset" && FString( ToString( EWorldIssueSeverity::Error ) ) == "Error", "issue names" );
	}

	//=========================================================================
	// メッシュの当たり判定・当たり判定の集計
	//=========================================================================
	void TestMeshCollider( NAssetTest::FChecker& C )
	{
		C.Group = "world-mesh-collider";

		// 書き出す: 使う項目だけ。Mesh の形は、Size / Radius / Height / Offset を書かない（使わないので、混乱のもと）
		FWorldDefinition World;
		FWorldObject Floor = MakeObject( 1, "Floor" );
		FWorldCollider Mesh;
		Mesh.Shape = EWorldColliderShape::Mesh;
		Mesh.Mesh = "Models/FloorCollision.mv1";
		Mesh.Offset = FVector3( 0.0f, 5.0f, 0.0f );   // 使われないので、書かれない
		Floor.Collider = Mesh;
		World.Objects.Add( Floor );
		FWorldObject Plain = MakeObject( 2, "Plain" );
		FWorldCollider PlainMesh;
		PlainMesh.Shape = EWorldColliderShape::Mesh;   // Mesh のファイルなし: Model のファイルを使う
		Plain.Collider = PlainMesh;
		World.Objects.Add( Plain );
		FWorldObject Boxed = MakeObject( 3, "Boxed" );
		FWorldCollider Box;
		Box.Mesh = "Models/Leftover.mv1";   // Box では使われない
		Boxed.Collider = Box;
		World.Objects.Add( Boxed );
		World.NextId = 4;
		const NlohmannJson Json = World.ToJson();
		const FJ J( Json );
		const FJ First = J["Objects"][0]["Components"]["Collider"];
		C.Check( First["Shape"] == "Mesh" && First["Mesh"] == "Models/FloorCollision.mv1" && !First.contains( "Size" ) && !First.contains( "Radius" ) && !First.contains( "Height" ) && !First.contains( "Offset" ), "a mesh collider writes its shape and file only" );
		const FJ Second = J["Objects"][1]["Components"]["Collider"];
		C.Check( Second["Shape"] == "Mesh" && !Second.contains( "Mesh" ), "and no file when it uses the model's" );
		C.Check( !J["Objects"][2]["Components"]["Collider"].contains( "Mesh" ), "other shapes do not write the mesh file" );

		// 読み戻す
		TArray<FString> Warnings;
		FWorldDefinition Back;
		C.Check( FWorldDefinition::FromJson( ParseJson( Json.dump().c_str() ), Back, &Warnings ) && Warnings.IsEmpty(), "it reads back without warnings (%d)", Warnings.Num() );
		const FWorldCollider ReadFloor = Back.Objects.Num() == 3 && Back.Objects[0].Collider.IsSet() ? Back.Objects[0].Collider.GetValue() : FWorldCollider();
		C.Check( ReadFloor.Shape == EWorldColliderShape::Mesh && ReadFloor.Mesh == "Models/FloorCollision.mv1" && !ReadFloor.bTrigger, "the shape and file survive" );
		C.Check( Back.ToJson() == Json, "write -> read -> write is identical" );
		FWorldDefinition Old;
		C.Check( FWorldDefinition::FromJson( ParseJson( "{\"Objects\":[{\"Id\":1,\"Components\":{\"Collider\":{\"Shape\":\"Sphere\",\"Radius\":2}}}]}" ), Old, &Warnings ) && Old.Objects.Num() == 1 && Old.Objects[0].Collider.IsSet()
					 && Old.Objects[0].Collider.GetValue().Shape == EWorldColliderShape::Sphere && Old.Objects[0].Collider.GetValue().Mesh.IsEmpty(),
				 "a file written before mesh colliders existed reads the same" );

		// 三角形を取るモデルのファイル
		C.Check( NWorld::GetColliderMeshPath( Back.Objects[0] ) == "Models/FloorCollision.mv1", "the collider's own file is used first" );
		FWorldObject WithModel = Back.Objects[1];
		FWorldModel Model;
		Model.Path = "Models/Floor.mv1";
		WithModel.Model = Model;
		C.Check( NWorld::GetColliderMeshPath( WithModel ) == "Models/Floor.mv1" && NWorld::GetColliderMeshPath( Back.Objects[0] ) == "Models/FloorCollision.mv1", "or the model's, when the collider names none" );
		FWorldObject WithBoth = Back.Objects[0];
		WithBoth.Model = Model;
		C.Check( NWorld::GetColliderMeshPath( WithBoth ) == "Models/FloorCollision.mv1", "the collider's file wins over the model's" );
		C.Check( NWorld::GetColliderMeshPath( Back.Objects[1] ).IsEmpty() && NWorld::GetColliderMeshPath( MakeObject( 9, "none" ) ).IsEmpty(), "none at all gives an empty path" );

		// 検証
		FFakeQuery Assets;
		for ( const char* Path : { "Models/FloorCollision.mv1", "Models/Floor.mv1" } ) Assets.Existing.insert( Path );
		const auto Validate = [&]( const FWorldObject& Object )
		{
			FWorldDefinition Single;
			Single.Objects.Add( Object );
			Single.NextId = Object.Id + 1;
			return NWorld::ValidateWorld( Single, &Assets );
		};
		C.Check( Validate( Back.Objects[0] ).IsEmpty(), "a mesh collider with an existing file is fine (and its Size is not checked)" );
		C.Check( Validate( WithModel ).IsEmpty(), "so is one that uses the model's file" );
		TArray<FWorldIssue> Issues = Validate( Back.Objects[1] );
		C.Check( CountIssues( Issues, EWorldIssueCode::BadCollider ) == 1 && NWorld::HasErrors( Issues ), "a mesh collider with no model and no file is an error" );
		FWorldObject Missing = Back.Objects[0];
		Missing.Collider->Mesh = "Models/Nope.mv1";
		Issues = Validate( Missing );
		C.Check( CountIssues( Issues, EWorldIssueCode::MissingAsset ) == 1 && CountIssues( Issues, EWorldIssueCode::BadCollider ) == 0, "a file that is not there is a missing asset" );
		FWorldObject MissingModel = Back.Objects[1];
		Model.Path = "Models/Nope.mv1";
		MissingModel.Model = Model;
		C.Check( CountIssues( Validate( MissingModel ), EWorldIssueCode::MissingAsset ) == 2, "and so is the model's file (once for the model, once for the collider that uses it)" );
		FWorldObject TriggerMesh = Back.Objects[0];
		TriggerMesh.Collider->bTrigger = true;
		Issues = Validate( TriggerMesh );
		C.Check( CountIssues( Issues, EWorldIssueCode::BadCollider ) == 1 && !NWorld::HasErrors( Issues ), "a trigger with a mesh shape is only a warning" );
		C.Check( NWorld::ValidateWorld( World, nullptr ).Num() == 1, "without an asset query the files are not looked for (only the mesh collider with no model at all is reported: %d)", NWorld::ValidateWorld( World, nullptr ).Num() );
	}

	void TestCollisionSummary( NAssetTest::FChecker& C )
	{
		C.Group = "world-collision-summary";
		FWorldDefinition World;
		World.Settings.CollisionRules.Add( Rule( "Decoration", false ) );
		World.Settings.CollisionRules.Add( Rule( "Wall", true ) );
		World.Settings.CollisionRules.Add( Rule( "Never", true ) );
		World.Settings.CollisionRules.Add( Rule( "Wall", false ) );   // 同じタグの 2 つめの規則は、使われない（数えない）

		const auto Add = [&]( int32 Id, std::initializer_list<const char*> Tags, EWorldCollisionMode Mode, bool bCollider, EWorldColliderShape Shape = EWorldColliderShape::Box, bool bTrigger = false, int32 Parent = WorldNoId, bool bEnabled = true )
		{
			FWorldObject Object = MakeObject( Id, "o", Parent );
			for ( const char* Tag : Tags ) Object.AddTag( Tag );
			Object.Collision = Mode;
			Object.bEnabled = bEnabled;
			if ( bCollider )
			{
				FWorldCollider Collider;
				Collider.Shape = Shape;
				Collider.bTrigger = bTrigger;
				Object.Collider = Collider;
			}
			World.Objects.Add( Object );
		};
		FWorldCollisionSummary Empty = NWorld::SummarizeCollision( World );
		C.Check( Empty.NumPlaced == 0 && Empty.NumWithCollider == 0 && Empty.NumEnabled == 0 && Empty.NumDecidedByRule.Num() == 4, "an empty world: nothing counted, one count per rule" );

		Add( 1, {}, EWorldCollisionMode::Auto, true );                                               // 既定で有効
		Add( 2, { "Decoration" }, EWorldCollisionMode::Auto, true );                                  // 規則で無効
		Add( 3, { "Wall" }, EWorldCollisionMode::Auto, true, EWorldColliderShape::Mesh );             // 規則で有効（メッシュ）
		Add( 4, { "Wall", "Decoration" }, EWorldCollisionMode::Auto, true, EWorldColliderShape::Box, true );   // 規則（Decoration が先）で無効（トリガー）
		Add( 5, { "Decoration" }, EWorldCollisionMode::On, true, EWorldColliderShape::Mesh, true );   // 指定で有効（メッシュのトリガー）
		Add( 6, { "Wall" }, EWorldCollisionMode::Off, true );                                         // 指定で無効
		Add( 7, {}, EWorldCollisionMode::Auto, false );                                               // 当たり判定なし
		Add( 8, {}, EWorldCollisionMode::Auto, true, EWorldColliderShape::Box, false, WorldNoId, false );   // 無効なオブジェクト（数えない）
		Add( 9, {}, EWorldCollisionMode::Auto, true, EWorldColliderShape::Box, false, 8 );            // 無効なものの子（数えない）
		Add( 10, {}, EWorldCollisionMode::Auto, true, EWorldColliderShape::Sphere, true, 7 );         // 有効な親の子（トリガー）
		const FWorldCollisionSummary S = NWorld::SummarizeCollision( World );
		C.Check( S.NumPlaced == 8, "disabled objects and their children are not counted (%d placed)", S.NumPlaced );
		C.Check( S.NumWithCollider == 7 && S.NumEnabled == 4 && S.NumDisabled == 3 && S.NumEnabled + S.NumDisabled == S.NumWithCollider, "with a collider shape: %d; on %d, off %d", S.NumWithCollider, S.NumEnabled, S.NumDisabled );
		C.Check( S.NumTriggers == 2 && S.NumMeshes == 2, "triggers and meshes are counted among the ones that are on (the rule-disabled trigger is not): %d, %d", S.NumTriggers, S.NumMeshes );
		C.Check( S.NumByOverride == 2 && S.NumByRule == 3 && S.NumByDefault == 2 && S.NumByOverride + S.NumByRule + S.NumByDefault == S.NumWithCollider, "why: override %d, rule %d, default %d", S.NumByOverride, S.NumByRule, S.NumByDefault );
		C.Check( S.NumDecidedByRule.Num() == 4 && S.NumDecidedByRule[0] == 2 && S.NumDecidedByRule[1] == 1 && S.NumDecidedByRule[2] == 0 && S.NumDecidedByRule[3] == 0, "per rule: Decoration 2, Wall 1, an unused rule 0, and a second rule for Wall 0 (%d, %d, %d, %d)", S.NumDecidedByRule[0], S.NumDecidedByRule[1],
				 S.NumDecidedByRule[2], S.NumDecidedByRule[3] );
	}

	//=========================================================================
	// 編集
	//=========================================================================
	void TestEdit( NAssetTest::FChecker& C )
	{
		C.Group = "world-edit";
		TWorldDocument Document;
		Document.Reset( FWorldDefinition() );

		C.Check( !Document.IsDirty() && !Document.CanUndo(), "a fresh document is clean" );
		const int32 Root = NWorldEdit::AddObject( Document, "Root" );
		const int32 Child = NWorldEdit::AddObject( Document, "Child", Root );
		const int32 Grand = NWorldEdit::AddObject( Document, "Grand", Child );
		const int32 Other = NWorldEdit::AddObject( Document, "Other" );
		C.Check( Root == 1 && Child == 2 && Grand == 3 && Other == 4 && Document.Get().NextId == 5 && Document.Get().Objects.Num() == 4 && Document.IsDirty(), "AddObject gives ids 1, 2, 3, 4 and advances NextId" );
		const int32 UndoDepth = Document.GetNumUndo();
		C.Check( NWorldEdit::AddObject( Document, "x", 99 ) == WorldNoId && Document.Get().Objects.Num() == 4 && Document.GetNumUndo() == UndoDepth && Document.Get().NextId == 5, "a missing parent adds nothing (no history, no id used)" );
		FWorldTransform Spec;
		Spec.Position = FVector3( 5.0f, 0.0f, 0.0f );
		const int32 Placed = NWorldEdit::AddObject( Document, "Placed", Other, Spec );
		C.Check( Placed == 5 && Near3( Document.Get().FindObject( Placed )->Transform.Position, FVector3( 5.0f, 0.0f, 0.0f ) ) && Document.Get().FindObject( Placed )->Parent == Other, "AddObject takes a transform and a parent" );

		// 取り消し・やり直し
		C.Check( Document.Undo() && Document.Get().Objects.Num() == 4 && Document.Get().FindObject( Placed ) == nullptr && Document.Get().NextId == 5, "Undo removes the last object (and gives the id back)" );
		C.Check( Document.Redo() && Document.Get().FindObject( Placed ) != nullptr && Document.Get().NextId == 6, "Redo puts it back" );

		// EditObject
		C.Check( NWorldEdit::EditObject( Document, 99 ) == nullptr, "EditObject of a missing id is null" );
		const int32 Before = Document.GetNumUndo();
		C.Check( NWorldEdit::EditObject( Document, 99 ) == nullptr && Document.GetNumUndo() == Before, "and does not touch the history" );
		FWorldObject* Edited = NWorldEdit::EditObject( Document, Other );
		C.Check( Edited != nullptr && Edited->Name == "Other" && Document.GetNumUndo() == Before + 1, "EditObject pushes one history entry" );
		Edited->Name = "Renamed";
		Edited->Extra = ParseJson( "{\"Keep\":[1,2,3]}" );
		FWorldRawComponent Raw;
		Raw.Type = "Hologram";
		Raw.Data = ParseJson( "{\"File\":\"a\"}" );
		Edited->RawComponents.Add( Raw );
		NWorldEdit::EditObject( Document, Other, "drag" )->Transform.Position.x = 1.0f;
		NWorldEdit::EditObject( Document, Other, "drag" )->Transform.Position.x = 2.0f;
		NWorldEdit::EditObject( Document, Other, "drag" )->Transform.Position.x = 3.0f;
		Document.EndEditSession();
		C.Check( Document.GetNumUndo() == Before + 2, "edits with the same key are one history entry" );
		C.Check( Document.Undo() && Near3( Document.Get().FindObject( Other )->Transform.Position, FVector3::Zero ) && Document.Get().FindObject( Other )->Name == "Renamed", "Undo reverts the whole drag" );
		C.Check( Document.Undo() && Document.Get().FindObject( Other )->Name == "Other" && Document.Get().FindObject( Other )->RawComponents.IsEmpty(), "and the rename" );
		C.Check( Document.Redo() && FJ( Document.Get().FindObject( Other )->Extra )["Keep"] == ParseJson( "[1,2,3]" ) && Document.Get().FindObject( Other )->RawComponents.Num() == 1, "Redo brings back unknown data too" );
		C.Check( Document.Redo() && Near3( Document.Get().FindObject( Other )->Transform.Position, FVector3( 3.0f, 0.0f, 0.0f ) ), "and the final value of the drag" );

		// 削除
		const int32 Doomed = NWorldEdit::AddObject( Document, "Doomed", Child );
		const int32 DoomedChild = NWorldEdit::AddObject( Document, "DoomedChild", Doomed );
		C.Check( NWorldEdit::RemoveObject( Document, Child ) && Document.Get().FindObject( Child ) == nullptr && Document.Get().FindObject( Grand ) == nullptr && Document.Get().FindObject( Doomed ) == nullptr && Document.Get().FindObject( DoomedChild ) == nullptr
					 && Document.Get().FindObject( Root ) != nullptr && Document.Get().FindObject( Other ) != nullptr,
				 "RemoveObject removes the object and everything below it" );
		const int32 NextAfterRemove = Document.Get().NextId;
		const int32 Fresh = NWorldEdit::AddObject( Document, "Fresh" );
		C.Check( Fresh == NextAfterRemove && Fresh > DoomedChild && Document.Get().FindObject( Child ) == nullptr, "a removed id is never used again (%d)", Fresh );
		const int32 UndoBefore = Document.GetNumUndo();
		C.Check( !NWorldEdit::RemoveObject( Document, 999 ) && Document.GetNumUndo() == UndoBefore, "removing a missing id does nothing" );
		Document.Undo();   // Fresh
		Document.Undo();   // RemoveObject
		C.Check( Document.Get().FindObject( Grand ) != nullptr && Document.Get().FindObject( Grand )->Parent == Child, "Undo of a removal restores the whole subtree" );

		// 親の付け替え
		Document.Reset( FWorldDefinition() );
		const int32 P = NWorldEdit::AddObject( Document, "P" );
		const int32 A = NWorldEdit::AddObject( Document, "A", P );
		const int32 B = NWorldEdit::AddObject( Document, "B", P );
		const int32 Cc = NWorldEdit::AddObject( Document, "C", P );
		const int32 AChild = NWorldEdit::AddObject( Document, "AChild", A );
		const int32 Q = NWorldEdit::AddObject( Document, "Q" );
		const int32 Hist = Document.GetNumUndo();
		C.Check( !NWorldEdit::Reparent( Document, P, AChild ) && !NWorldEdit::Reparent( Document, A, A ) && !NWorldEdit::Reparent( Document, P, A ) && !NWorldEdit::Reparent( Document, 99, Q ) && !NWorldEdit::Reparent( Document, A, 99 ) && Document.GetNumUndo() == Hist,
				 "an object cannot move under itself or its descendants; missing ids; none of it touches the history" );
		C.Check( !NWorldEdit::Reparent( Document, A, P, AChild ) && !NWorldEdit::Reparent( Document, A, P, A ) && !NWorldEdit::Reparent( Document, A, Q, B ), "the 'before' object must be a sibling under the new parent (and not the object itself)" );
		C.Check( NWorldEdit::Reparent( Document, Cc, P, A ) && IdsAre( Document.Get(), NWorld::GetChildIndices( Document.Get(), P ), { Cc, A, B } ), "moving C before A reorders the siblings" );
		C.Check( NWorldEdit::Reparent( Document, Cc, P ) && IdsAre( Document.Get(), NWorld::GetChildIndices( Document.Get(), P ), { A, B, Cc } ), "no 'before' means last" );
		C.Check( NWorldEdit::Reparent( Document, B, Q ) && Document.Get().FindObject( B )->Parent == Q && IdsAre( Document.Get(), NWorld::GetChildIndices( Document.Get(), P ), { A, Cc } ) && IdsAre( Document.Get(), NWorld::GetChildIndices( Document.Get(), Q ), { B } ),
				 "moving to another parent" );
		C.Check( NWorldEdit::Reparent( Document, AChild, WorldNoId ) && Document.Get().FindObject( AChild )->Parent == WorldNoId && IdsAre( Document.Get(), NWorld::GetChildIndices( Document.Get(), WorldNoId ), { P, Q, AChild } ), "moving to the top level" );
		C.Check( Document.Undo() && Document.Get().FindObject( AChild )->Parent == A, "Undo of a reparent" );

		// ワールドでの姿勢を保つ
		FWorldDefinition Spaces;
		FWorldObject Big = MakeObject( 1, "Big" );
		Big.Transform.Position = FVector3( 10.0f, 0.0f, 0.0f );
		Big.Transform.RotationDegrees = FVector3( 0.0f, 90.0f, 0.0f );
		Big.Transform.Scale = FVector3( 2.0f, 2.0f, 2.0f );
		Spaces.Objects.Add( Big );
		FWorldObject Small = MakeObject( 2, "Small" );
		Small.Transform.Position = FVector3( -4.0f, 3.0f, 0.0f );
		Spaces.Objects.Add( Small );
		FWorldObject Moving = MakeObject( 3, "Moving", 1 );
		Moving.Transform.Position = FVector3( 1.0f, 2.0f, 3.0f );
		Moving.Transform.RotationDegrees = FVector3( 0.0f, 30.0f, 0.0f );
		Moving.Transform.Scale = FVector3( 3.0f, 3.0f, 3.0f );
		Spaces.Objects.Add( Moving );
		Spaces.NextId = 4;
		TWorldDocument Keep;
		Keep.Reset( Spaces );
		const FTransform WorldBefore = NWorld::ComputeWorldTransform( Keep.Get(), 3 );
		C.Check( NWorldEdit::Reparent( Keep, 3, 2, WorldNoId, true ), "reparent keeping the world transform" );
		const FTransform WorldAfter = NWorld::ComputeWorldTransform( Keep.Get(), 3 );
		C.Check( Near3( WorldAfter.Position, WorldBefore.Position, 0.01f ) && Near3( WorldAfter.Scale, WorldBefore.Scale, 0.01f ) && Near3( WorldAfter.TransformDirection( FVector3::Forward ), WorldBefore.TransformDirection( FVector3::Forward ), 0.01f ),
				 "its world position, scale and direction are unchanged (%s -> %s)", *WorldBefore.Position.ToString(), *WorldAfter.Position.ToString() );
		TWorldDocument Plain;
		Plain.Reset( Spaces );
		NWorldEdit::Reparent( Plain, 3, 2 );
		C.Check( Near3( Plain.Get().FindObject( 3 )->Transform.Position, FVector3( 1.0f, 2.0f, 3.0f ) ) && Near3( Plain.Get().FindObject( 3 )->Transform.Scale, FVector3( 3.0f, 3.0f, 3.0f ) ), "without the option the local values stay as they were" );

		// 複製
		FWorldDefinition Source;
		Source.Objects.Add( MakeObject( 1, "Top" ) );
		FWorldObject Tree = MakeObject( 2, "Tree", 1 );
		Tree.AddTag( "Plant" );
		Tree.SetProp( FWorldProp::MakeObject( "Leaf", 3 ) );
		Tree.SetProp( FWorldProp::MakeObject( "Outside", 1 ) );
		Tree.Transform.Position = FVector3( 1.0f, 0.0f, 0.0f );
		Source.Objects.Add( Tree );
		FWorldObject Leaf = MakeObject( 3, "Leaf", 2 );
		Leaf.SetProp( FWorldProp::MakeObject( "Trunk", 2 ) );
		Source.Objects.Add( Leaf );
		Source.Objects.Add( MakeObject( 4, "After", 1 ) );
		Source.NextId = 5;
		TWorldDocument Copy;
		Copy.Reset( Source );
		const int32 Duplicated = NWorldEdit::DuplicateObject( Copy, 2 );
		const FWorldDefinition& CopyWorld = Copy.Get();
		C.Check( Duplicated == 5 && CopyWorld.Objects.Num() == 6 && CopyWorld.NextId == 7, "the copy gets new ids (root 5, child 6)" );
		const FWorldObject* CopyRoot = CopyWorld.FindObject( 5 );
		const FWorldObject* CopyLeaf = CopyWorld.FindObject( 6 );
		C.Check( CopyRoot != nullptr && CopyLeaf != nullptr && CopyRoot->Name == "Tree" && CopyRoot->Parent == 1 && CopyLeaf->Parent == 5 && CopyRoot->HasTag( "Plant" ) && Near3( CopyRoot->Transform.Position, FVector3( 1.0f, 0.0f, 0.0f ) ),
				 "the copy keeps names, tags and transforms; its root has the original's parent and its child the copy of the original's child" );
		C.Check( CopyRoot->FindProp( "Leaf" )->ObjectId == 6 && CopyLeaf->FindProp( "Trunk" )->ObjectId == 5 && CopyRoot->FindProp( "Outside" )->ObjectId == 1, "references inside the copy point to the copies; references outside are unchanged" );
		C.Check( CopyWorld.Objects[1].Id == 2 && CopyWorld.Objects[2].Id == 3 && CopyWorld.Objects[3].Id == 5 && CopyWorld.Objects[4].Id == 6 && CopyWorld.Objects[5].Id == 4, "the copy is placed right after the original subtree" );
		C.Check( CopyWorld.FindObject( 2 )->FindProp( "Leaf" )->ObjectId == 3 && CopyWorld.FindObject( 3 )->FindProp( "Trunk" )->ObjectId == 2, "the original is untouched" );
		C.Check( NWorldEdit::DuplicateObject( Copy, 99 ) == WorldNoId && Copy.Undo() && Copy.Get().Objects.Num() == 4, "duplicating a missing id does nothing; Undo removes the copy" );

		// タグ・プロパティ
		TWorldDocument Doc2;
		Doc2.Reset( FWorldDefinition() );
		const int32 T = NWorldEdit::AddObject( Doc2, "T" );
		const int32 H2 = Doc2.GetNumUndo();
		C.Check( NWorldEdit::AddTag( Doc2, T, " Enemy " ) && Doc2.Get().FindObject( T )->HasTag( "Enemy" ) && Doc2.GetNumUndo() == H2 + 1, "AddTag trims the tag" );
		C.Check( !NWorldEdit::AddTag( Doc2, T, "Enemy" ) && !NWorldEdit::AddTag( Doc2, T, "   " ) && !NWorldEdit::AddTag( Doc2, 99, "x" ) && Doc2.GetNumUndo() == H2 + 1, "a duplicate, empty or unknown-object tag is refused without touching the history" );
		C.Check( NWorldEdit::RemoveTag( Doc2, T, "Enemy" ) && !NWorldEdit::RemoveTag( Doc2, T, "Enemy" ) && Doc2.Get().FindObject( T )->Tags.IsEmpty(), "RemoveTag" );
		C.Check( NWorldEdit::SetProp( Doc2, T, FWorldProp::MakeInt( "Hp", 5 ) ) && NWorldEdit::SetProp( Doc2, T, FWorldProp::MakeInt( "Hp", 7 ) ) && Doc2.Get().FindObject( T )->Props.Num() == 1 && Doc2.Get().FindObject( T )->FindProp( "Hp" )->IntValue == 7, "SetProp replaces" );
		C.Check( !NWorldEdit::SetProp( Doc2, T, FWorldProp::MakeInt( "", 1 ) ) && !NWorldEdit::SetProp( Doc2, 99, FWorldProp::MakeInt( "a", 1 ) ), "a nameless property or an unknown object is refused" );
		C.Check( NWorldEdit::RemoveProp( Doc2, T, "Hp" ) && !NWorldEdit::RemoveProp( Doc2, T, "Hp" ), "RemoveProp" );

		// タグ規則
		C.Check( NWorldEdit::SetCollisionRule( Doc2, "Wall", true ) == 0 && NWorldEdit::SetCollisionRule( Doc2, " Decoration ", false ) == 1 && NWorldEdit::SetCollisionRule( Doc2, "Wall", false ) == 0 && Doc2.Get().Settings.CollisionRules.Num() == 2
					 && !Doc2.Get().Settings.CollisionRules[0].bCollide && Doc2.Get().Settings.CollisionRules[1].Tag == "Decoration",
				 "SetCollisionRule adds, trims, and replaces the action of an existing tag" );
		C.Check( NWorldEdit::SetCollisionRule( Doc2, "  ", true ) == INDEX_NONE && Doc2.Get().Settings.CollisionRules.Num() == 2, "an empty tag is refused" );
		C.Check( NWorldEdit::MoveCollisionRule( Doc2, 1, 0 ) && Doc2.Get().Settings.CollisionRules[0].Tag == "Decoration" && Doc2.Get().Settings.CollisionRules[1].Tag == "Wall" && !NWorldEdit::MoveCollisionRule( Doc2, 0, 5 ) && !NWorldEdit::MoveCollisionRule( Doc2, 0, 0 )
					 && !NWorldEdit::MoveCollisionRule( Doc2, 7, 0 ),
				 "MoveCollisionRule reorders; bad indices are refused" );
		C.Check( NWorldEdit::RemoveCollisionRule( Doc2, 0 ) && Doc2.Get().Settings.CollisionRules.Num() == 1 && Doc2.Get().Settings.CollisionRules[0].Tag == "Wall" && !NWorldEdit::RemoveCollisionRule( Doc2, 3 ), "RemoveCollisionRule" );

		// 保存済みの印
		TWorldDocument Saved;
		Saved.Reset( MakeRichWorld() );
		NWorldEdit::AddTag( Saved, 1, "New" );
		C.Check( Saved.IsDirty(), "an edit makes the document dirty" );
		Saved.Undo();
		C.Check( !Saved.IsDirty() && Saved.Get().ToJson() == MakeRichWorld().ToJson(), "undoing back to the saved state is clean, and identical (nothing was lost on the way: unknown parts included)" );
	}
}

void NWorldTest::RunLogicTests( NAssetTest::FChecker& C )
{
	TestColorAndNames( C );
	TestJson( C );
	TestJsonRepair( C );
	TestTransform( C );
	TestHierarchy( C );
	TestCollision( C );
	TestValidate( C );
	TestMeshCollider( C );
	TestCollisionSummary( C );
	TestEdit( C );
}
