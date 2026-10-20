#include <algorithm>
#include <cmath>

#include "Library/Core/Collection/Map.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Framework/World/Editor/EditorPlacement.h"
#include "Library/Framework/World/Editor/EditorSession.h"
#include "Library/Framework/World/Editor/EditorShapes.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Framework/World/WorldHierarchy.h"
#include "Library/Framework/World/WorldSpawner.h"
#include "Library/Framework/World/WorldValidate.h"
#include "Library/Physics/RigidBody.h"
#include "Library/Physics/WorldPhysics.h"
#include "WorldEditorSelfTestSupport.h"
#include "WorldMeshSelfTestSupport.h"
#include "WorldSelfTestSupport.h"

using namespace NAssetTest;
using namespace NWorldMeshTest;

//=============================================================================
// 入れ子のワールド（Prefab 部品）の確認
//   純粋な部分（JSON・検証・取り出し / 置き換え / 展開）と、シーンへの設置（入れ子の位置・階層・タグ・結び付け・循環・深さ・片付け）。
//=============================================================================
namespace
{
	bool Near3( const FVector3& A, const FVector3& B, float32 Tolerance = 1.0e-3f )
	{
		return std::fabs( A.x - B.x ) <= Tolerance && std::fabs( A.y - B.y ) <= Tolerance && std::fabs( A.z - B.z ) <= Tolerance;
	}

	FWorldObject MakeObject( int32 Id, const char* Name, const FVector3& Position = FVector3::Zero, int32 Parent = WorldNoId )
	{
		FWorldObject Object;
		Object.Id = Id;
		Object.Name = Name;
		Object.Parent = Parent;
		Object.Transform.Position = Position;
		return Object;
	}

	FWorldObject MakeHost( int32 Id, const char* Name, const char* Path, const FVector3& Position = FVector3::Zero )
	{
		FWorldObject Object = MakeObject( Id, Name, Position );
		FWorldPrefab Prefab;
		Prefab.Path = Path;
		Object.Prefab = Prefab;
		return Object;
	}

	FWorldDefinition MakeWorld( const char* Name, std::initializer_list<FWorldObject> Objects )
	{
		FWorldDefinition World;
		World.Name = Name;
		for ( const FWorldObject& Object : Objects )
		{
			World.Objects.Add( Object );
			World.NextId = std::max( World.NextId, Object.Id + 1 );
		}
		return World;
	}

	// 入れ子のワールドを答える、偽のアセットの窓口（登録した定義だけがある）
	class FWorldQuery final : public IWorldAssetQuery
	{
	public:
		TMap<FString, FWorldDefinition> Worlds;
		bool Exists( const FString& Path ) const override { return Worlds.Find( Path ) != nullptr; }
		const FWorldDefinition* GetWorld( const FString& Path ) const override { return Worlds.Find( Path ); }
	};

	int32 CountIssues( const TArray<FWorldIssue>& Issues, EWorldIssueCode Code, EWorldIssueSeverity Severity, const char* Contains = nullptr )
	{
		int32 Count = 0;
		for ( const FWorldIssue& Issue : Issues )
		{
			if ( Issue.Code == Code && Issue.Severity == Severity && ( Contains == nullptr || Issue.Message.Contains( Contains ) ) ) ++Count;
		}
		return Count;
	}

	//=========================================================================
	// データと JSON
	//=========================================================================
	void TestData( FChecker& C )
	{
		C.Group = "prefab-data";
		FWorldDefinition World = MakeWorld( "Room", { MakeHost( 1, "Door", "Prefabs/Door.world", FVector3( 2, 0, 0 ) ), MakeObject( 2, "Plain" ) } );
		const NlohmannJson Json = World.ToJson();
		const FJson J( Json );
		C.Check( J["Objects"][0]["Components"]["Prefab"]["Path"] == "Prefabs/Door.world" && !J["Objects"][1].contains( "Components" ), "a Prefab component is written under Components with its path (an object without one has no Components)" );
		FWorldDefinition Back;
		TArray<FString> Warnings;
		C.Check( FWorldDefinition::FromJson( ParseJsonText( Json.dump().c_str() ), Back, &Warnings ) && Warnings.IsEmpty() && Back.Objects.Num() == 2 && Back.Objects[0].Prefab.IsSet() && Back.Objects[0].Prefab.GetValue().Path == "Prefabs/Door.world"
					 && !Back.Objects[1].Prefab.IsSet() && Back.Objects[0].RawComponents.IsEmpty() && Back.ToJson() == Json,
				 "it reads back as a Prefab (not an unknown component), and writes the same (%d warnings)", Warnings.Num() );
		C.Check( Back.Objects[0].GetComponentNames().Contains( "Prefab" ) && !Back.Objects[1].GetComponentNames().Contains( "Prefab" ), "the component names include Prefab" );

		Warnings.Reset();
		FWorldDefinition Wrong;
		C.Check( FWorldDefinition::FromJson( ParseJsonText( "{\"Objects\":[{\"Id\":1,\"Components\":{\"Prefab\":5}}]}" ), Wrong, &Warnings ) && !Wrong.Objects[0].Prefab.IsSet() && Wrong.Objects[0].RawComponents.Num() == 1 && Warnings.Num() == 1,
				 "a Prefab component that is not an object is kept as it is, with a warning" );
		FWorldDefinition NoPath;
		C.Check( FWorldDefinition::FromJson( ParseJsonText( "{\"Objects\":[{\"Id\":1,\"Components\":{\"Prefab\":{}}}]}" ), NoPath ) && NoPath.Objects[0].Prefab.IsSet() && NoPath.Objects[0].Prefab.GetValue().Path.IsEmpty(), "a Prefab without a path is a Prefab with an empty path (validation reports it)" );
		C.Check( NWorld::NormalizeAssetPath( " Prefabs\\Door.WORLD " ) == "prefabs/door.world" && NWorld::NormalizeAssetPath( "a.world" ) == NWorld::NormalizeAssetPath( "A.World" ), "asset paths are compared without case and with / as the separator" );
	}

	//=========================================================================
	// 検証
	//=========================================================================
	void TestValidate( FChecker& C )
	{
		C.Group = "prefab-validate";
		FWorldQuery Query;
		Query.Worlds.Add( "b.world", MakeWorld( "B", { MakeObject( 1, "X" ) } ) );
		Query.Worlds.Add( "Models/x.mv1", FWorldDefinition() );
		const auto Validate = [&]( const FWorldObject& Host, const char* WorldPath = "" ) { return NWorld::ValidateWorld( MakeWorld( "Host", { Host } ), &Query, WorldPath ); };

		C.Check( Validate( MakeHost( 1, "H", "b.world" ) ).IsEmpty(), "a prefab whose file exists has no issues" );
		TArray<FWorldIssue> Issues = Validate( MakeHost( 1, "H", "" ) );
		C.Check( CountIssues( Issues, EWorldIssueCode::EmptyAssetPath, EWorldIssueSeverity::Error ) == 1 && NWorld::HasErrors( Issues ), "a prefab with no file is an error" );
		Issues = Validate( MakeHost( 1, "H", "nope.world" ) );
		C.Check( CountIssues( Issues, EWorldIssueCode::MissingAsset, EWorldIssueSeverity::Error, "nope.world" ) == 1, "a prefab whose file is missing is an error" );
		Query.Worlds.Add( "Prefabs/DOOR.WORLD", FWorldDefinition() );
		C.Check( Validate( MakeHost( 1, "H", "Prefabs/DOOR.WORLD" ) ).IsEmpty(), "the .world extension is recognised in any case" );
		Issues = Validate( MakeHost( 1, "H", "Models/x.mv1" ) );
		C.Check( CountIssues( Issues, EWorldIssueCode::BadPrefab, EWorldIssueSeverity::Warning, "not a .world" ) == 1 && !NWorld::HasErrors( Issues ), "a prefab that is not a .world file is a warning" );
		C.Check( NWorld::ValidateWorld( MakeWorld( "Host", { MakeHost( 1, "H", "Whatever.world" ) } ), nullptr ).IsEmpty(), "without an asset query, nothing about the file is checked" );

		// 循環
		Query.Worlds.Add( "a.world", MakeWorld( "A", { MakeHost( 1, "ToB", "b2.world" ) } ) );
		Query.Worlds.Add( "b2.world", MakeWorld( "B2", { MakeHost( 1, "ToA", "a.world" ) } ) );
		Query.Worlds.Add( "self.world", MakeWorld( "Self", { MakeHost( 1, "ToSelf", "self.world" ) } ) );
		Issues = NWorld::ValidateWorld( *Query.GetWorld( "a.world" ), &Query, "a.world" );
		C.Check( CountIssues( Issues, EWorldIssueCode::BadPrefab, EWorldIssueSeverity::Error, "(a.world -> b2.world -> a.world)" ) == 1, "a loop through the world itself is an error, with the way round (a -> b2 -> a)" );
		Issues = NWorld::ValidateWorld( *Query.GetWorld( "a.world" ), &Query );
		C.Check( CountIssues( Issues, EWorldIssueCode::BadPrefab, EWorldIssueSeverity::Error, "leads back to itself" ) == 1, "a loop is found even when the world's own file is not known (the loop is found on the second visit)" );
		Issues = NWorld::ValidateWorld( *Query.GetWorld( "self.world" ), &Query, "self.world" );
		C.Check( CountIssues( Issues, EWorldIssueCode::BadPrefab, EWorldIssueSeverity::Error, "(self.world -> self.world)" ) == 1, "a world that places itself is a loop" );
		Issues = NWorld::ValidateWorld( *Query.GetWorld( "a.world" ), &Query, "A.WORLD" );
		C.Check( CountIssues( Issues, EWorldIssueCode::BadPrefab, EWorldIssueSeverity::Error, "leads back" ) == 1, "paths are compared without case" );

		// 深さ: 8 段までは置ける。9 段目は、エラー
		FWorldQuery Deep;
		for ( int32 i = 1; i <= 9; ++i )
		{
			const FString Name = FString::Printf( "d%d.world", i );
			if ( i < 9 ) Deep.Worlds.Add( Name, MakeWorld( "D", { MakeHost( 1, "Next", *FString::Printf( "d%d.world", i + 1 ) ) } ) );
			else Deep.Worlds.Add( Name, MakeWorld( "D", { MakeObject( 1, "End" ) } ) );
		}
		FWorldDefinition Top = MakeWorld( "Top", { MakeHost( 1, "Go", "d1.world" ) } );
		Issues = NWorld::ValidateWorld( Top, &Deep );
		C.Check( CountIssues( Issues, EWorldIssueCode::BadPrefab, EWorldIssueSeverity::Error, "deeper than 8" ) == 1, "nesting 9 levels deep is an error (d1 .. d9)" );
		Deep.Worlds.Add( "d8.world", MakeWorld( "D", { MakeObject( 1, "End" ) } ) );   // d8 で終わる: 8 段
		Issues = NWorld::ValidateWorld( Top, &Deep );
		C.Check( Issues.IsEmpty(), "8 levels (d1 .. d8) is allowed" );
	}

	//=========================================================================
	// 編集: 取り出す・置き換える・展開する
	//=========================================================================
	void TestEdit( FChecker& C )
	{
		C.Group = "prefab-edit";
		// グループ（Id 1）とその子（Id 2）、外のもの（Id 3）
		FWorldObject Group = MakeObject( 1, "Group", FVector3( 10, 0, 5 ) );
		Group.Transform.RotationDegrees = FVector3( 0, 90, 0 );
		Group.Tags.Add( "Door" );
		FWorldObject Child = MakeObject( 2, "Child", FVector3( 1, 0, 0 ), 1 );
		Child.Props.Add( FWorldProp::MakeObject( "Owner", 1 ) );
		FWorldObject Outside = MakeObject( 3, "Outside", FVector3( 20, 0, 0 ) );
		const FWorldDefinition Source = MakeWorld( "Source", { Group, Child, Outside } );

		// 取り出す
		TArray<int32> Ids;
		Ids.Add( 1 );
		FWorldDefinition Extracted = NWorldEdit::ExtractSubWorld( Source, Ids, FVector3( 10, 0, 5 ), "Door" );
		const FWorldObject* ExRoot = Extracted.FindObjectByName( "Group" );
		const FWorldObject* ExChild = Extracted.FindObjectByName( "Child" );
		C.Check( Extracted.Name == "Door" && Extracted.Objects.Num() == 2 && ExRoot != nullptr && ExChild != nullptr && ExRoot->Parent == WorldNoId && ExChild->Parent == ExRoot->Id && Extracted.NextId > ExChild->Id && ExRoot->Id >= 1 && ExChild->Id >= 1 && ExRoot->Id != ExChild->Id,
				 "extracting a group and its child: 2 objects, the child under the root, new ids, a name for the world" );
		C.Check( ExRoot != nullptr && Near3( ExRoot->Transform.Position, FVector3::Zero ) && Near3( ExRoot->Transform.RotationDegrees, FVector3( 0, 90, 0 ) ) && ExChild != nullptr && Near3( ExChild->Transform.Position, FVector3( 1, 0, 0 ) ), "the origin becomes the root's position (the rotation and the child's own placement are kept)" );
		C.Check( ExChild != nullptr && ExRoot != nullptr && ExChild->FindProp( "Owner" ) != nullptr && ExChild->FindProp( "Owner" )->ObjectId == ExRoot->Id, "a reference between the extracted objects points at the new id" );
		C.Check( Source.Objects.Num() == 3 && Source.FindObject( 1 ) != nullptr, "the world it was taken from is not changed" );
		FWorldDefinition ExtractedAway = NWorldEdit::ExtractSubWorld( Source, Ids, FVector3::Zero, "Door" );
		C.Check( ExtractedAway.FindObjectByName( "Group" ) != nullptr && Near3( ExtractedAway.FindObjectByName( "Group" )->Transform.Position, FVector3( 10, 0, 5 ) ), "with the origin at the world's origin the positions are kept as they are" );
		C.Check( NWorldEdit::ExtractSubWorld( Source, TArray<int32>(), FVector3::Zero, "Empty" ).Objects.IsEmpty() && NWorldEdit::ExtractSubWorld( Source, TArray<int32>{ 99 }, FVector3::Zero, "Empty" ).Objects.IsEmpty(), "nothing to extract gives an empty world" );
		FWorldDefinition Twice = NWorldEdit::ExtractSubWorld( Source, TArray<int32>{ 1, 2 }, FVector3( 10, 0, 5 ), "Door" );
		C.Check( Twice.Objects.Num() == 2, "an object chosen together with its parent is taken once (with the parent)" );

		// 置き換える
		TWorldDocument Document;
		Document.Reset( Source );
		const int32 Instance = NWorldEdit::ReplaceWithPrefab( Document, Ids, FVector3( 10, 0, 5 ), "Prefabs/Door.world", "Door" );
		const FWorldObject* Placed = Document.Get().FindObject( Instance );
		C.Check( Instance != WorldNoId && Instance > 3 && Placed != nullptr && Placed->Prefab.IsSet() && Placed->Prefab.GetValue().Path == "Prefabs/Door.world" && Placed->Name == "Door" && Placed->Parent == WorldNoId && Near3( NWorld::ComputeWorldTransform( Document.Get(), Instance ).Position, FVector3( 10, 0, 5 ) )
					 && Document.Get().FindObject( 1 ) == nullptr && Document.Get().FindObject( 2 ) == nullptr && Document.Get().FindObject( 3 ) != nullptr,
				 "replacing the group with a prefab: the group and its child are gone, one object with the Prefab is at the origin (the other object stays) (%d)", Instance );
		C.Check( Document.Undo() && Document.Get().Objects.Num() == 3 && Document.Get().FindObject( 1 ) != nullptr && Document.Get().FindObject( Instance ) == nullptr, "and it is one undo step" );
		Document.Redo();
		C.Check( NWorldEdit::ReplaceWithPrefab( Document, TArray<int32>(), FVector3::Zero, "x.world", "X" ) == WorldNoId, "nothing chosen replaces nothing" );

		// 親の下のものを置き換えると、同じ親の下に置く
		FWorldDefinition Nested = MakeWorld( "Nested", { MakeObject( 1, "Root", FVector3( 5, 0, 0 ) ), MakeObject( 2, "A", FVector3( 1, 0, 0 ), 1 ), MakeObject( 3, "B", FVector3( 2, 0, 0 ), 1 ) } );
		TWorldDocument NestedDocument;
		NestedDocument.Reset( Nested );
		const int32 Under = NWorldEdit::ReplaceWithPrefab( NestedDocument, TArray<int32>{ 2, 3 }, FVector3( 8, 0, 0 ), "p.world", "P" );
		const FWorldObject* UnderObject = NestedDocument.Get().FindObject( Under );
		C.Check( UnderObject != nullptr && UnderObject->Parent == 1 && Near3( NWorld::ComputeWorldTransform( NestedDocument.Get(), Under ).Position, FVector3( 8, 0, 0 ) ) && Near3( UnderObject->Transform.Position, FVector3( 3, 0, 0 ) ), "when the chosen objects share a parent, the instance goes under it (at the origin in the world: local 3 under a parent at 5)" );

		// 親の違うものを置き換えると、最上位に置く
		TWorldDocument MixedDocument;
		MixedDocument.Reset( MakeWorld( "Mixed", { MakeObject( 1, "Root", FVector3( 5, 0, 0 ) ), MakeObject( 2, "A", FVector3( 1, 0, 0 ), 1 ), MakeObject( 3, "Other", FVector3( 9, 0, 0 ) ) } ) );
		const int32 Mixed = NWorldEdit::ReplaceWithPrefab( MixedDocument, TArray<int32>{ 2, 3 }, FVector3( 7, 0, 0 ), "p.world", "P" );
		C.Check( Mixed != WorldNoId && MixedDocument.Get().FindObject( Mixed )->Parent == WorldNoId && Near3( MixedDocument.Get().FindObject( Mixed )->Transform.Position, FVector3( 7, 0, 0 ) ), "when the chosen objects do not share a parent (a child of Root, and another top-level object), the instance goes at the top level" );

		// 展開する
		FWorldObject Host = MakeHost( 1, "Host", "Prefabs/Door.world", FVector3( 5, 0, 0 ) );
		Host.Transform.RotationDegrees = FVector3( 0, 90, 0 );
		Host.Transform.Scale = FVector3( 2, 2, 2 );
		TWorldDocument UnpackDocument;
		UnpackDocument.Reset( MakeWorld( "Level", { Host, MakeObject( 2, "Other" ) } ) );
		FWorldObject InnerRoot = MakeObject( 1, "InnerRoot", FVector3( 1, 0, 0 ) );
		InnerRoot.Tags.Add( "Inside" );
		FWorldDefinition Inner = MakeWorld( "Inner", { InnerRoot, MakeObject( 2, "InnerChild", FVector3( 0, 0, 1 ), 1 ) } );
		const FTransform HostWorld = NWorld::ComputeWorldTransform( UnpackDocument.Get(), 1 );
		const uint32 RevisionBefore = UnpackDocument.GetRevision();
		C.Check( NWorldEdit::UnpackPrefab( UnpackDocument, 2, Inner ).IsEmpty() && NWorldEdit::UnpackPrefab( UnpackDocument, 99, Inner ).IsEmpty() && UnpackDocument.GetRevision() == RevisionBefore, "an object without a Prefab, or one that does not exist, is left alone (no history)" );
		const TArray<int32> Roots = NWorldEdit::UnpackPrefab( UnpackDocument, 1, Inner );
		const FWorldObject* NewRoot = Roots.Num() == 1 ? UnpackDocument.Get().FindObject( Roots[0] ) : nullptr;
		const FWorldObject* NewChild = UnpackDocument.Get().FindObjectByName( "InnerChild" );
		C.Check( Roots.Num() == 1 && NewRoot != nullptr && NewRoot->Name == "InnerRoot" && NewRoot->Parent == 1 && NewRoot->HasTag( "Inside" ) && NewChild != nullptr && NewChild->Parent == NewRoot->Id && !UnpackDocument.Get().FindObject( 1 )->Prefab.IsSet() && UnpackDocument.Get().Objects.Num() == 4,
				 "unpacking adds the contents as children of the host (new ids), and the host no longer has a Prefab (4 objects)" );
		C.Check( NewRoot != nullptr && Near3( NewRoot->Transform.Position, FVector3( 1, 0, 0 ) ) && NewChild != nullptr && Near3( NWorld::ComputeWorldTransform( UnpackDocument.Get(), NewChild->Id ).Position, HostWorld.TransformPoint( FVector3( 1, 0, 1 ) ) ) && Near3( NWorld::ComputeWorldTransform( UnpackDocument.Get(), NewRoot->Id ).Position, HostWorld.TransformPoint( FVector3( 1, 0, 0 ) ) ),
				 "the contents keep their placement relative to the host (so, in the world, they are where the host's rotation and scale put them)" );
		C.Check( UnpackDocument.Undo() && UnpackDocument.Get().Objects.Num() == 2 && UnpackDocument.Get().FindObject( 1 )->Prefab.IsSet(), "and one undo gives the prefab back" );
	}

	//=========================================================================
	// エディタ: 図形・置く・セッション
	//=========================================================================
	void TestEditor( FChecker& C )
	{
		C.Group = "prefab-editor";
		C.Check( NWorldEditor::ClassifyAsset( "Prefabs/Door.world" ) == EEditorAssetKind::World && NWorldEditor::ClassifyAsset( "Levels/A.WORLD" ) == EEditorAssetKind::World && NWorldEditor::ClassifyAsset( "x.world.bak" ) == EEditorAssetKind::Unknown, "a .world file is a world asset (any case); other extensions are not" );

		NWorldEditorTest::FFakeEditorAssets Assets;
		// 扉: 箱 2 x 4 x 1（枠）と、(5, 0, 0) の 1 m の箱、(100, 0, 0) の無効な箱（数えない）
		FWorldObject Frame = MakeObject( 1, "Frame" );
		Frame.Mesh = NEditMesh::MakeBox( FVector3( 2, 4, 1 ) );
		FWorldObject Side = MakeObject( 2, "Side", FVector3( 5, 0, 0 ) );
		Side.Mesh = NEditMesh::MakeBox( FVector3::One );
		FWorldObject Far = MakeObject( 3, "Far", FVector3( 100, 0, 0 ) );
		Far.Mesh = NEditMesh::MakeBox( FVector3::One );
		Far.bEnabled = false;
		Assets.Worlds.Add( { "Prefabs/Door.world", MakeWorld( "Door", { Frame, Side, Far } ) } );
		Assets.Worlds.Add( { "Prefabs/Marks.world", MakeWorld( "Marks", { MakeObject( 1, "A", FVector3( -3, 0, 0 ) ), MakeObject( 2, "B", FVector3( 3, 2, 0 ) ) } ) } );
		Assets.Worlds.Add( { "Prefabs/Empty.world", MakeWorld( "Empty", {} ) } );
		Assets.Worlds.Add( { "Prefabs/Loop.world", MakeWorld( "Loop", { MakeHost( 1, "Again", "Prefabs/Loop.world" ), MakeObject( 2, "Wall", FVector3( 1, 0, 0 ) ) } ) } );
		FEditorShapeContext Context;
		Context.Assets = &Assets;

		// 図形: 中身を囲む箱（入れ子のワールドの座標の箱を、ホストの姿勢で置く）
		FWorldObject Host = MakeHost( 1, "Door", "Prefabs/Door.world" );
		const FTransform HostAt( FVector3( 10, 0, 0 ), FQuaternion::Identity, FVector3( 1, 2, 1 ) );
		TArray<FEditorShape> Shapes = NWorldEditor::GatherShapes( Host, HostAt, Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].Kind == EEditorShapeKind::Box && Shapes[0].Role == EEditorShapeRole::Model && !Shapes[0].bApproximate && Near3( Shapes[0].Center, FVector3( 12.25f, 4.0f, 0.0f ) ) && Near3( Shapes[0].Half, FVector3( 3.25f, 4.0f, 0.5f ) ),
				 "a Prefab is the box round what is inside (x -1..5.5, y 0..4, z -0.5..0.5; the disabled box is not counted), placed by the host's transform (scale 2 in y)" );
		Shapes = NWorldEditor::GatherShapes( MakeHost( 1, "Marks", "Prefabs/Marks.world" ), FTransform::Identity, Context );
		C.Check( Shapes.Num() == 1 && !Shapes[0].bApproximate && Near3( Shapes[0].Center, FVector3( 0.0f, 1.0f, 0.0f ) ) && Near3( Shapes[0].Half, FVector3( 3.0f, 1.0f, 0.0f ) ), "a world of empty objects is the box round their positions" );
		Shapes = NWorldEditor::GatherShapes( MakeHost( 1, "Lost", "Prefabs/Nope.world" ), FTransform::Identity, Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].bApproximate && Near3( Shapes[0].Half, FVector3( 0.5f, 0.5f, 0.5f ) ), "a world that cannot be read is a flagged 1 m box" );
		Shapes = NWorldEditor::GatherShapes( MakeHost( 1, "None", "Prefabs/Empty.world" ), FTransform::Identity, Context );
		C.Check( Shapes.Num() == 1 && Shapes[0].bApproximate, "and so is an empty world" );
		Shapes = NWorldEditor::GatherShapes( MakeHost( 1, "Looped", "Prefabs/Loop.world" ), FTransform::Identity, Context );
		C.Check( Shapes.Num() == 1 && !Shapes[0].bApproximate, "a world that places itself still gives a box (the loop is cut at the depth limit)" );
		FEditorShapeContext NoAssets;
		Shapes = NWorldEditor::GatherShapes( Host, HostAt, NoAssets );
		C.Check( Shapes.Num() == 1 && Shapes[0].bApproximate, "without asset information it is a flagged 1 m box" );

		// 置く
		TWorldDocument Document;
		Document.Reset( FWorldDefinition() );
		const int32 Placed = NWorldEditor::AddAssetObject( Document, "Prefabs\\Door.world", WorldNoId, FVector3( 1, 0, 2 ), &Assets );
		const FWorldObject* PlacedObject = Document.Get().FindObject( Placed );
		C.Check( Placed != WorldNoId && PlacedObject != nullptr && PlacedObject->Name == "Door" && PlacedObject->Prefab.IsSet() && PlacedObject->Prefab.GetValue().Path == "Prefabs/Door.world" && !PlacedObject->Model.IsSet() && Near3( PlacedObject->Transform.Position, FVector3( 1, 0, 2 ) ),
				 "placing a .world asset makes an object with a Prefab (the path with / separators, named after the file)" );

		// セッション: 選んだものを Prefab に置き換える・展開する
		CWorldEditorSession Session;
		FWorldObject Group = MakeObject( 1, "Group", FVector3( 4, 0, 0 ) );
		FWorldObject Part = MakeObject( 2, "Part", FVector3( 1, 0, 0 ), 1 );
		Session.Reset( MakeWorld( "Level", { Group, Part, MakeObject( 3, "Other" ) } ), "Levels/Level.world" );
		Session.SetSelection( TArray<int32>{ 1 } );
		const int32 Instance = Session.ReplaceSelectionWithPrefab( "Prefabs/Group.world", FVector3( 4, 0, 0 ), "Group" );
		C.Check( Instance != WorldNoId && Session.GetSelection() == TArray<int32>{ Instance } && Session.Document.Get().FindObject( Instance )->Prefab.IsSet() && Session.Document.Get().Objects.Num() == 2, "replacing the selection with a prefab selects the instance (the group and its part are gone)" );
		C.Check( Session.Undo() && Session.Document.Get().Objects.Num() == 3 && Session.Document.Get().FindObject( 1 ) != nullptr, "and one undo brings them back" );
		Session.Redo();

		Assets.Worlds.Add( { "Prefabs/Group.world", MakeWorld( "Group", { MakeObject( 1, "Group" ), MakeObject( 2, "Part", FVector3( 1, 0, 0 ), 1 ) } ) } );
		const uint32 RevisionBefore = Session.Document.GetRevision();
		C.Check( Session.UnpackPrefabs( TArray<int32>{ 3 }, Assets ) == 0 && Session.UnpackPrefabs( TArray<int32>{ 99 }, Assets ) == 0 && Session.Document.GetRevision() == RevisionBefore, "unpacking an object without a Prefab, or one that does not exist, does nothing (no history)" );
		C.Check( Session.UnpackPrefabs( TArray<int32>{ Instance }, Assets ) == 1 && Session.Document.Get().Objects.Num() == 4 && !Session.Document.Get().FindObject( Instance )->Prefab.IsSet(), "unpacking the instance adds the contents (2 objects) under it (4 in all), and removes its Prefab" );
		C.Check( Session.Undo() && Session.Document.Get().Objects.Num() == 2 && Session.Document.Get().FindObject( Instance )->Prefab.IsSet(), "and that is one undo step too" );
		FWorldObject Lost = MakeHost( 1, "Lost", "Prefabs/Nope.world" );
		Session.Reset( MakeWorld( "Level", { Lost } ), "" );
		C.Check( Session.UnpackPrefabs( TArray<int32>{ 1 }, Assets ) == 0 && Session.Document.Get().FindObject( 1 )->Prefab.IsSet(), "a prefab whose world cannot be read is left as it is" );
		Assets.Worlds.Add( { "Prefabs/Nothing.world", MakeWorld( "Nothing", {} ) } );
		FWorldObject Hollow = MakeHost( 1, "Hollow", "Prefabs/Nothing.world" );
		Session.Reset( MakeWorld( "Level", { Hollow } ), "" );
		C.Check( Session.UnpackPrefabs( TArray<int32>{ 1 }, Assets ) == 1 && !Session.Document.Get().FindObject( 1 )->Prefab.IsSet() && Session.Document.Get().Objects.Num() == 1, "a prefab with nothing in it is unpacked to nothing: the Prefab component is just removed" );
	}

	//=========================================================================
	// 設置の確認用のワールドを、アセットとして書く
	//=========================================================================
	bool SaveTestWorlds( CAssetManager& Assets )
	{
		bool bOk = true;
		// 扉: 枠（タグ Door・当たり判定の箱）・扉板（タグ Leaf・枠の子）・結び付ける置き場（タグ Spawner）
		FWorldObject Frame = MakeObject( 1, "Frame" );
		Frame.Tags.Add( "Door" );
		FWorldObject Leaf = MakeObject( 2, "Leaf", FVector3( 1, 0, 0 ), 1 );
		Leaf.Tags.Add( "Leaf" );
		FWorldCollider Box;
		Box.Shape = EWorldColliderShape::Box;
		Box.Size = FVector3( 0.5f, 0.5f, 0.5f );
		Leaf.Collider = Box;
		FWorldObject Marker = MakeObject( 3, "Marker", FVector3( 0, 2, 0 ), 1 );
		Marker.Tags.Add( "Spawner" );
		bOk = Assets.SaveWorld( "prefabs/door.world", MakeWorld( "Door", { Frame, Leaf, Marker } ) ) && bOk;
		// 部屋: 扉を 2 つ（2 つめは、Y に 90 度）
		FWorldObject Door2 = MakeHost( 2, "Door2", "prefabs/door.world", FVector3( 0, 0, 10 ) );
		Door2.Transform.RotationDegrees = FVector3( 0, 90, 0 );
		bOk = Assets.SaveWorld( "prefabs/room.world", MakeWorld( "Room", { MakeHost( 1, "Door1", "prefabs/door.world", FVector3( 2, 0, 0 ) ), Door2 } ) ) && bOk;
		// 循環
		bOk = Assets.SaveWorld( "prefabs/loopa.world", MakeWorld( "LoopA", { MakeHost( 1, "ToB", "prefabs/loopb.world" ) } ) ) && bOk;
		bOk = Assets.SaveWorld( "prefabs/loopb.world", MakeWorld( "LoopB", { MakeHost( 1, "ToA", "prefabs/loopa.world" ) } ) ) && bOk;
		bOk = Assets.SaveWorld( "prefabs/selfref.world", MakeWorld( "SelfRef", { MakeHost( 1, "ToSelf", "prefabs/selfref.world" ) } ) ) && bOk;
		// 深い入れ子: deep0 -> deep1 -> ... -> deep11
		for ( int32 i = 0; i < 12; ++i )
		{
			FWorldObject Object = i < 11 ? MakeHost( 1, "Next", *FString::Printf( "prefabs/deep%d.world", i + 1 ) ) : MakeObject( 1, "End" );
			bOk = Assets.SaveWorld( *FString::Printf( "prefabs/deep%d.world", i ), MakeWorld( "Deep", { Object } ) ) && bOk;
		}
		return bOk;
	}
}

void NWorldTest::RunPrefabLogicTests( FChecker& C )
{
	TestData( C );
	TestValidate( C );
	TestEdit( C );
	TestEditor( C );
}

void NWorldTest::RunPrefabSceneTests( FChecker& C, CScene& Scene, CAssetManager& Assets, const FWorldSpawnerRegistry& Registry )
{
	C.Group = "world-prefab";
	C.Check( SaveTestWorlds( Assets ), "the prefab worlds are saved" );

	FWorldBindings Bindings;
	Bindings.Add( "Spawner", []( const FWorldBindContext& Context ) { return TSharedPtr<CGameObject>( Context.Scene.SpawnObject<CGameObject>() ); } );
	FWorldSpawnOptions Options;
	Options.Registry = &Registry;
	Options.WorldPath = "host.world";

	// ホスト: 部屋を、(100, 0, 0) に置く
	FWorldObject Room = MakeHost( 1, "Room", "prefabs/room.world", FVector3( 100, 0, 0 ) );
	const TSharedPtr<FWorldDefinition> HostDefinition = MakeShared<FWorldDefinition>( MakeWorld( "Host", { Room, MakeObject( 2, "Marker" ) } ) );
	FWorldInstance Level = NWorld::Spawn( Scene, HostDefinition, Bindings, Options );
	const FWorldSpawnReport& Report = Level.GetReport();
	// 数: ホスト 2 + 部屋の中の扉 2 + 扉の中身 3 x 2 = 10。入れ子の置き物（部屋 1・扉 2）= 3
	C.Check( Report.NumSpawned == 10 && Report.NumPrefabs == 3 && Report.NumBound == 2 && Report.Warnings.IsEmpty() && Level.GetNumSpawned() == 2 && Level.GetNumNested() == 1,
			 "a room of two doors: 10 objects in all (2 + 2 + 3 x 2), 3 prefabs, 2 bound, no warnings (%d placed, %d prefabs, %d bound, %d warnings)", Report.NumSpawned, Report.NumPrefabs, Report.NumBound, Report.Warnings.Num() );
	const FWorldInstance* RoomInstance = Level.FindNested( 1 );
	const FWorldInstance* Door1Instance = RoomInstance != nullptr ? RoomInstance->FindNested( 1 ) : nullptr;
	const FWorldInstance* Door2Instance = RoomInstance != nullptr ? RoomInstance->FindNested( 2 ) : nullptr;
	C.Check( RoomInstance != nullptr && Door1Instance != nullptr && Door2Instance != nullptr && Level.FindNested( 2 ) == nullptr && RoomInstance->GetNumSpawned() == 2 && Door1Instance->GetNumSpawned() == 3 && Door2Instance->GetNumSpawned() == 3, "each prefab has its own instance: room (2 objects), door 1 (3), door 2 (3); an object without a prefab has none" );

	const auto WorldPosition = [&]( const TSharedPtr<CGameObject>& Object ) { return Object.IsValid() && Object->GetComponent<CTransformComponent>() != nullptr ? Object->GetComponent<CTransformComponent>()->GetWorldPosition() : FVector3( -999, -999, -999 ); };
	const TSharedPtr<CGameObject> RoomHost = Level.FindById( 1 );
	const TSharedPtr<CGameObject> Door1Host = RoomInstance != nullptr ? RoomInstance->FindById( 1 ) : TSharedPtr<CGameObject>();
	const TSharedPtr<CGameObject> Door2Host = RoomInstance != nullptr ? RoomInstance->FindById( 2 ) : TSharedPtr<CGameObject>();
	C.Check( Near3( WorldPosition( RoomHost ), FVector3( 100, 0, 0 ) ) && Near3( WorldPosition( Door1Host ), FVector3( 102, 0, 0 ) ) && Near3( WorldPosition( Door2Host ), FVector3( 100, 0, 10 ) ), "the doors are at the room's position plus their own: (102, 0, 0) and (100, 0, 10)" );
	const TSharedPtr<CGameObject> Leaf1 = Door1Instance != nullptr ? Door1Instance->FindById( 2 ) : TSharedPtr<CGameObject>();
	const TSharedPtr<CGameObject> Leaf2 = Door2Instance != nullptr ? Door2Instance->FindById( 2 ) : TSharedPtr<CGameObject>();
	C.Check( Near3( WorldPosition( Leaf1 ), FVector3( 103, 0, 0 ) ) && Near3( WorldPosition( Leaf2 ), FVector3( 100, 0, 9 ) ), "what is inside follows: leaf 1 at (103, 0, 0), and leaf 2 of the door turned 90 degrees at (100, 0, 9) (%s)", *WorldPosition( Leaf2 ).ToString() );

	// 階層: 入れ子の最上位は、ホストの子
	const TSharedPtr<CGameObject> Frame1 = Door1Instance != nullptr ? Door1Instance->FindById( 1 ) : TSharedPtr<CGameObject>();
	C.Check( Door1Host.IsValid() && RoomHost.IsValid() && Door1Host->GetParent() == RoomHost.Get() && Frame1.IsValid() && Frame1->GetParent() == Door1Host.Get() && Leaf1.IsValid() && Leaf1->GetParent() == Frame1.Get() && RoomHost->GetParent() == nullptr,
			 "the top objects of a prefab are children of the host (room > door > frame > leaf)" );

	// タグ・結び付け
	C.Check( Level.FindAll( "Door" ).Num() == 2 && Level.FindAll( "Leaf" ).Num() == 2 && Level.FindFirst( "Leaf" ).IsValid() && Level.FindAll( "Nope" ).IsEmpty() && Level.FindByName( "Leaf" ).IsValid() && Level.FindByName( "Marker" ) == Level.FindById( 2 ), "tags are found inside the prefabs too (2 frames, 2 leaves); a name is looked for in this world first" );
	C.Check( Level.FindById( 3 ) == nullptr && Door1Instance != nullptr && Door1Instance->FindById( 1 ).IsValid(), "an id belongs to its own world: id 3 is not in the host world" );
	TArray<TSharedPtr<CGameObject>> Placeholders;
	for ( const FWorldInstance* Inner : { Door1Instance, Door2Instance } )
	{
		if ( Inner != nullptr && Inner->FindById( 3 ).IsValid() && Inner->FindById( 3 )->GetChildren().Num() == 1 ) Placeholders.Add( Inner->FindById( 3 ) );
	}
	C.Check( Placeholders.Num() == 2, "the tag bindings work inside a prefab (each Spawner marker has the object that was made for it)" );

	// ホストを動かすと、中身も動く
	if ( RoomHost.IsValid() && RoomHost->GetComponent<CTransformComponent>() != nullptr )
	{
		RoomHost->GetComponent<CTransformComponent>()->SetLocalTransform( FTransform( FVector3( 200, 0, 0 ) ) );
		C.Check( Near3( WorldPosition( Leaf1 ), FVector3( 203, 0, 0 ) ), "moving the host moves what is inside (leaf 1 follows to (203, 0, 0))" );
	}

	// 物理: 入れ子の中の当たり判定の箱は、ワールドでの位置（ホストの位置を含む）に置かれる
	const CWorldColliderComponent* Collider1 = Leaf1.IsValid() ? Leaf1->GetComponent<CWorldColliderComponent>() : nullptr;
	const CWorldColliderComponent* Collider2 = Leaf2.IsValid() ? Leaf2->GetComponent<CWorldColliderComponent>() : nullptr;
	C.Check( Collider1 != nullptr && Collider1->GetBody() != nullptr && Near3( Collider1->GetBody()->GetPosition(), FVector3( 103, 0, 0 ) ) && Collider2 != nullptr && Collider2->GetBody() != nullptr && Near3( Collider2->GetBody()->GetPosition(), FVector3( 100, 0, 9 ) ),
			 "a collider inside a prefab has its body where the world placement puts it ((103, 0, 0) and (100, 0, 9)) (the body does not follow later moves: it is placed once)" );

	// 片付け: ホストの Despawn で、中身も消える
	Level.Despawn();
	C.Check( Level.FindAll( "Door" ).IsEmpty() && Level.GetNumNested() == 0 && Level.FindById( 1 ) == nullptr, "despawning the host drops the prefabs' records too" );
	C.Check( Leaf1.IsValid() && !Leaf1->IsAlive(), "and what was placed inside is destroyed with it" );

	// 循環・深さ・無いファイル
	{
		FWorldSpawnOptions Plain;
		Plain.Registry = &Registry;
		Plain.WorldPath = "prefabs/loopa.world";
		FWorldInstance Loop = NWorld::Spawn( Scene, Assets.LoadWorld( "prefabs/loopa.world" )->GetDefinitionShared(), Bindings, Plain );
		bool bLoopWarning = false;
		for ( const FString& Warning : Loop.GetReport().Warnings ) bLoopWarning = bLoopWarning || Warning.Contains( "leads back to itself" );
		C.Check( bLoopWarning && Loop.GetReport().NumSpawned == 2 && Loop.GetReport().NumPrefabs == 1, "a loop (a -> b -> a) is cut where it comes back: a and b are placed once each, with a warning (%d placed)", Loop.GetReport().NumSpawned );
		Loop.Despawn();

		Plain.WorldPath = "prefabs/selfref.world";
		FWorldInstance Self = NWorld::Spawn( Scene, Assets.LoadWorld( "prefabs/selfref.world" )->GetDefinitionShared(), Bindings, Plain );
		C.Check( Self.GetReport().NumSpawned == 1 && Self.GetReport().NumPrefabs == 0 && Self.GetReport().Warnings.Num() == 1, "a world that places itself places nothing more, with a warning" );
		Self.Despawn();

		Plain.WorldPath = FString();
		FWorldInstance Unknown = NWorld::Spawn( Scene, Assets.LoadWorld( "prefabs/loopa.world" )->GetDefinitionShared(), Bindings, Plain );
		C.Check( Unknown.GetReport().NumSpawned >= 2 && Unknown.GetReport().NumSpawned <= 3 && !Unknown.GetReport().Warnings.IsEmpty(), "even when the world's own file is not given, a loop ends (%d placed)", Unknown.GetReport().NumSpawned );
		Unknown.Despawn();

		FWorldInstance Deep = NWorld::Spawn( Scene, Assets.LoadWorld( "prefabs/deep0.world" )->GetDefinitionShared(), Bindings, Plain );
		bool bDeepWarning = false;
		for ( const FString& Warning : Deep.GetReport().Warnings ) bDeepWarning = bDeepWarning || Warning.Contains( "deeper than 8" );
		C.Check( bDeepWarning && Deep.GetReport().NumPrefabs == WorldMaxPrefabDepth && Deep.GetReport().NumSpawned == 1 + WorldMaxPrefabDepth, "a nesting deeper than 8 levels stops at 8 (deep1 .. deep8), with a warning (%d prefabs, %d placed)", Deep.GetReport().NumPrefabs, Deep.GetReport().NumSpawned );
		Deep.Despawn();

		FWorldObject Lost = MakeHost( 1, "Lost", "prefabs/nope.world" );
		FWorldObject Empty = MakeHost( 2, "Empty", "" );
		FWorldInstance Broken = NWorld::Spawn( Scene, MakeWorld( "Broken", { Lost, Empty } ), Bindings, Plain );
		bool bLoadWarning = false, bEmptyWarning = false;
		for ( const FString& Warning : Broken.GetReport().Warnings )
		{
			bLoadWarning = bLoadWarning || ( Warning.Contains( "'Lost'" ) && Warning.Contains( "could not be loaded" ) );
			bEmptyWarning = bEmptyWarning || ( Warning.Contains( "'Empty'" ) && Warning.Contains( "no file" ) );
		}
		C.Check( bLoadWarning && bEmptyWarning && Broken.GetReport().NumSpawned == 2 && Broken.GetReport().NumPrefabs == 0, "a prefab that cannot be loaded, or has no file, is a warning (the host is still placed)" );
		Broken.Despawn();

		// 入れ子の中の警告は、ホストの警告に、ファイル名つきで入る。無効なオブジェクト（bEnabled = false）は、置かない
		// 入れ子の中に、無効なオブジェクトと、親が循環しているオブジェクトがあるときの数
		FWorldObject Quiet = MakeObject( 2, "Quiet" );
		Quiet.bEnabled = false;
		FWorldObject LoopA = MakeObject( 3, "LoopA" );
		LoopA.Parent = 4;
		FWorldObject LoopB = MakeObject( 4, "LoopB" );
		LoopB.Parent = 3;
		C.Check( Assets.SaveWorld( "prefabs/messy.world", MakeWorld( "Messy", { MakeObject( 1, "Fine" ), Quiet, LoopA, LoopB } ) ), "a world with a disabled object and a parent loop is saved" );
		FWorldInstance Messy = NWorld::Spawn( Scene, MakeWorld( "Messy host", { MakeHost( 1, "Host", "prefabs/messy.world" ) } ), Bindings, Plain );
		C.Check( Messy.GetReport().NumSpawned == 2 && Messy.GetReport().NumDisabled == 1 && Messy.GetReport().NumSkipped == 2, "what the prefab could not place is counted in the host's report too (2 placed, 1 disabled, 2 skipped: %d, %d, %d)", Messy.GetReport().NumSpawned, Messy.GetReport().NumDisabled, Messy.GetReport().NumSkipped );
		Messy.Despawn();

		FWorldObject Off = MakeHost( 1, "Off", "prefabs/door.world" );
		Off.bEnabled = false;
		FWorldInstance Disabled = NWorld::Spawn( Scene, MakeWorld( "Off", { Off } ), Bindings, Plain );
		C.Check( Disabled.GetReport().NumSpawned == 0 && Disabled.GetReport().NumDisabled == 1 && Disabled.GetNumNested() == 0, "a disabled host places no prefab" );
		Disabled.Despawn();
		FWorldInstance InnerWarnings = NWorld::Spawn( Scene, MakeWorld( "Inner", { MakeHost( 1, "Loopy", "prefabs/loopa.world" ) } ), Bindings, Plain );
		bool bPrefixed = false;
		for ( const FString& Warning : InnerWarnings.GetReport().Warnings ) bPrefixed = bPrefixed || Warning.StartsWith( "prefab 'prefabs/loopa.world': " );
		C.Check( bPrefixed, "a warning from inside a prefab says which file it came from" );
		InnerWarnings.Despawn();
	}
}
