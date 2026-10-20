#include <cmath>

#include "Library/Framework/World/Editor/EditorOutliner.h"
#include "Library/Framework/World/Editor/EditorPlacement.h"
#include "Library/Framework/World/Editor/EditorSession.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Framework/World/WorldHierarchy.h"
#include "WorldEditorSelfTestSupport.h"

using namespace NAssetTest;
using namespace NWorldEditorTest;

//=============================================================================
// ワールドエディタの「編集の状態」（選択・ギズモの結果・複数の編集・アウトライナー・置き方）の確認
//   DxLib も ImGui も使わない。期待する値は、手で求めた数か、元に戻す・保存し直すなどの別の経路で求めた値。
//=============================================================================
namespace
{
	int32 Add( TWorldDocument& Document, const char* Name, int32 Parent, const FVector3& Position )
	{
		FWorldTransform Transform;
		Transform.Position = Position;
		return NWorldEdit::AddObject( Document, Name, Parent, Transform );
	}

	FVector3 WorldPosition( const TWorldDocument& Document, int32 Id )
	{
		return NWorld::ComputeWorldTransform( Document.Get(), Id ).Position;
	}

	FVector3 LocalPosition( const TWorldDocument& Document, int32 Id )
	{
		return ObjectOrEmpty( Document.Get(), Id ).Transform.Position;
	}

	// 表示している軸（カメラ側へ反転していることがある）の上の、原点から Fraction * Length の点
	FVector3 OnAxis( const FEditorGizmoFrame& Frame, int32 Axis, float32 Fraction )
	{
		FVector3 Start, End;
		NWorldEditor::GetGizmoAxisSegment( Frame, Axis, Start, End );
		return Start + ( End - Start ) * Fraction;
	}

	//=========================================================================
	// 履歴のまとめ
	//=========================================================================
	void TestDocumentBatch( NAssetTest::FChecker& C )
	{
		C.Group = "editor-batch";
		TWorldDocument Document;
		Document.Reset( FWorldDefinition() );
		Add( Document, "First", WorldNoId, FVector3::Zero );
		const int32 Base = Document.GetNumUndo();

		{
			TWorldDocument::FBatch Batch( Document );
			Add( Document, "A", WorldNoId, FVector3::Zero );
			Add( Document, "B", WorldNoId, FVector3::Zero );
			Add( Document, "C", WorldNoId, FVector3::Zero );
		}
		C.Check( Document.GetNumUndo() == Base + 1 && Document.Get().Objects.Num() == 4, "three edits in a batch are one history entry" );
		C.Check( Document.Undo() && Document.Get().Objects.Num() == 1 && Document.GetNumUndo() == Base, "and one Undo reverts all three" );
		C.Check( Document.Redo() && Document.Get().Objects.Num() == 4, "and one Redo brings them back" );

		const int32 Before = Document.GetNumUndo();
		{
			TWorldDocument::FBatch Outer( Document );
			{
				TWorldDocument::FBatch Inner( Document );
				Add( Document, "Inner", WorldNoId, FVector3::Zero );
			}
			Add( Document, "Outer", WorldNoId, FVector3::Zero );
		}
		C.Check( Document.GetNumUndo() == Before + 1, "nested batches are still one entry" );

		const int32 BeforeEmpty = Document.GetNumUndo();
		const uint32 RevisionBefore = Document.GetRevision();
		{
			TWorldDocument::FBatch Empty( Document );
		}
		C.Check( Document.GetNumUndo() == BeforeEmpty && Document.GetRevision() == RevisionBefore, "a batch without edits leaves no entry" );

		// 連続した編集のキーは、まとめ編集の中では使わず、終わったあとにまとまりを引き継がない
		const int32 BeforeKey = Document.GetNumUndo();
		{
			TWorldDocument::FBatch Batch( Document );
			NWorldEdit::EditObject( Document, 1, "k" )->Name = "x1";
			NWorldEdit::EditObject( Document, 1, "k" )->Name = "x2";
		}
		NWorldEdit::EditObject( Document, 1, "k" )->Name = "x3";
		Document.EndEditSession();
		C.Check( Document.GetNumUndo() == BeforeKey + 2, "a coalescing key does not merge a later edit into the batch (%d)", Document.GetNumUndo() - BeforeKey );

		// 中では元に戻せない
		bool bUndoInside = true;
		bool bRedoInside = true;
		bool bForgetInside = true;
		{
			TWorldDocument::FBatch Batch( Document );
			Add( Document, "Inside", WorldNoId, FVector3::Zero );
			bUndoInside = Document.Undo();
			bRedoInside = Document.Redo();
			bForgetInside = Document.UndoAndForget();
		}
		C.Check( !bUndoInside && !bRedoInside && !bForgetInside, "Undo / Redo / UndoAndForget are refused inside a batch" );

		// 元に戻して捨てる
		TWorldDocument Other;
		Other.Reset( FWorldDefinition() );
		C.Check( !Other.UndoAndForget(), "UndoAndForget with no history does nothing" );
		Add( Other, "One", WorldNoId, FVector3::Zero );
		Add( Other, "Two", WorldNoId, FVector3::Zero );
		C.Check( Other.UndoAndForget() && Other.Get().Objects.Num() == 1 && !Other.CanRedo() && Other.GetNumUndo() == 1, "UndoAndForget reverts and leaves nothing to redo" );
	}

	//=========================================================================
	// 階層の補助
	//=========================================================================
	void TestHierarchyExtras( NAssetTest::FChecker& C )
	{
		C.Group = "editor-hierarchy";
		FWorldDefinition World;
		const auto Place = [&]( int32 Id, int32 Parent, const FVector3& Position, const FVector3& Rotation, const FVector3& Scale )
		{
			FWorldObject Object;
			Object.Id = Id;
			Object.Name = "O";
			Object.Parent = Parent;
			Object.Transform.Position = Position;
			Object.Transform.RotationDegrees = Rotation;
			Object.Transform.Scale = Scale;
			World.Objects.Add( Object );
		};
		Place( 1, WorldNoId, FVector3( 1, 2, 3 ), FVector3( 10, 20, 30 ), FVector3( 2, 2, 2 ) );
		Place( 2, 1, FVector3( 1, 0, 0 ), FVector3( 0, 45, 0 ), FVector3( 1, 2, 1 ) );
		Place( 3, 2, FVector3( 0, 1, 0 ), FVector3::Zero, FVector3( 3, 1, 1 ) );
		Place( 4, 99, FVector3( 5, 5, 5 ), FVector3::Zero, FVector3::One );
		Place( 5, 6, FVector3( 1, 0, 0 ), FVector3::Zero, FVector3::One );
		Place( 6, 5, FVector3( 0, 1, 0 ), FVector3::Zero, FVector3::One );
		World.NextId = 7;

		const TArray<FTransform> All = NWorld::ComputeAllWorldTransforms( World );
		bool bSame = All.Num() == 6;
		for ( int32 Id = 1; Id <= 4 && bSame; ++Id )
		{
			const FTransform One = NWorld::ComputeWorldTransform( World, Id );
			const FTransform& Batch = All[World.FindIndexById( Id )];
			if ( !Near3( One.Position, Batch.Position, 1e-3f ) || !Near3( One.Scale, Batch.Scale, 1e-4f ) || !One.Rotation.Equals( Batch.Rotation, 1e-4f ) ) bSame = false;
		}
		C.Check( bSame, "ComputeAllWorldTransforms equals ComputeWorldTransform for a nested tree and an orphan" );
		C.Check( Near3( All[3].Position, FVector3( 5, 5, 5 ) ), "an orphan is placed as a top-level object" );
		bool bFinite = true;
		for ( const int32 Index : { 4, 5 } )
		{
			const FVector3 P = All[Index].Position;
			if ( !std::isfinite( P.x ) || !std::isfinite( P.y ) || !std::isfinite( P.z ) ) bFinite = false;
		}
		C.Check( bFinite, "objects in a parent cycle get finite values and the walk ends" );

		// ワールド <-> ローカル
		const FTransform Parent( FVector3( 3, 0, -2 ), FQuaternion::FromAxisAngle( FVector3::Up, 0.52f ), FVector3( 2, 3, 0.5f ) );
		const FTransform Child( FVector3( 1, 2, 3 ), FQuaternion::EulerToQuat( FVector3( 0.17f, 0.35f, 0.09f ) ), FVector3( 1.5f, 1.0f, 0.7f ) );
		const FTransform Combined = FTransform::Combine( Parent, Child );
		const FTransform Back = NWorld::WorldToLocal( Parent, Combined );
		C.Check( Near3( Back.Position, Child.Position, 1e-3f ) && Back.Rotation.Equals( Child.Rotation, 1e-4f ) && Near3( Back.Scale, Child.Scale, 1e-4f ), "WorldToLocal undoes Combine, even under a parent with a non-uniform scale and a rotation" );

		// 親を付け替えても、ワールドの位置が変わらない（新しい親が回って非一様に拡大されていても）
		TWorldDocument Document;
		Document.Reset( FWorldDefinition() );
		FWorldTransform ParentSpec;
		ParentSpec.Position = FVector3( 5, 0, 0 );
		ParentSpec.RotationDegrees = FVector3( 0, 90, 0 );
		ParentSpec.Scale = FVector3( 1, 2, 3 );
		const int32 P = NWorldEdit::AddObject( Document, "P", WorldNoId, ParentSpec );
		const int32 X = Add( Document, "X", WorldNoId, FVector3( 10, 1, 1 ) );
		NWorldEdit::EditObject( Document, X )->Transform.Scale = FVector3( 1, 1, 1 );
		C.Check( NWorldEdit::Reparent( Document, X, P, WorldNoId, true ) && Near3( WorldPosition( Document, X ), FVector3( 10, 1, 1 ), 1e-3f ), "Reparent keeping the world transform is exact under a rotated, non-uniformly scaled parent (%s)", *WorldPosition( Document, X ).ToString() );

		// 根
		FWorldDefinition Tree;
		for ( int32 Id = 1; Id <= 4; ++Id )
		{
			FWorldObject Object;
			Object.Id = Id;
			Object.Parent = Id == 2 ? 1 : Id == 3 ? 2 : WorldNoId;
			Tree.Objects.Add( Object );
		}
		Tree.NextId = 5;
		const auto Ids = []( std::initializer_list<int32> List ) { TArray<int32> Result; for ( const int32 Id : List ) Result.Add( Id ); return Result; };
		C.Check( NWorld::FilterToRoots( Tree, Ids( { 3, 1, 2, 99 } ) ) == Ids( { 1 } ), "FilterToRoots keeps only the ancestor of a selected chain, and drops ids that do not exist" );
		C.Check( NWorld::FilterToRoots( Tree, Ids( { 3, 2 } ) ) == Ids( { 2 } ), "a child of a selected parent is dropped" );
		C.Check( NWorld::FilterToRoots( Tree, Ids( { 4, 1, 4 } ) ) == Ids( { 1, 4 } ), "roots come in array order, once each" );
		C.Check( NWorld::FilterToRoots( Tree, TArray<int32>() ).IsEmpty(), "no ids, no roots" );
	}

	//=========================================================================
	// 複数のオブジェクトの編集
	//=========================================================================
	void TestMultiEdit( NAssetTest::FChecker& C )
	{
		C.Group = "editor-multiedit";
		TWorldDocument Document;
		Document.Reset( FWorldDefinition() );
		const int32 A = Add( Document, "A", WorldNoId, FVector3( 0, 0, 0 ) );
		const int32 A1 = Add( Document, "A1", A, FVector3( 1, 0, 0 ) );
		const int32 A2 = Add( Document, "A2", A, FVector3( 2, 0, 0 ) );
		const int32 B = Add( Document, "B", WorldNoId, FVector3( 10, 0, 0 ) );
		const int32 D = Add( Document, "D", WorldNoId, FVector3( 20, 0, 0 ) );
		const auto Ids = []( std::initializer_list<int32> List ) { TArray<int32> Result; for ( const int32 Id : List ) Result.Add( Id ); return Result; };

		// 削除
		const int32 UndoBase = Document.GetNumUndo();
		C.Check( NWorldEdit::RemoveObjects( Document, Ids( { A1, B, A } ) ) == 2 && Document.Get().FindObject( A ) == nullptr && Document.Get().FindObject( A1 ) == nullptr && Document.Get().FindObject( A2 ) == nullptr && Document.Get().FindObject( B ) == nullptr && Document.Get().FindObject( D ) != nullptr,
				 "RemoveObjects removes each root with its subtree (A1 is counted with A)" );
		C.Check( Document.GetNumUndo() == UndoBase + 1 && Document.Undo() && Document.Get().Objects.Num() == 5, "and is one history entry" );
		C.Check( NWorldEdit::RemoveObjects( Document, Ids( { 999 } ) ) == 0 && Document.GetNumUndo() == UndoBase, "removing nothing leaves no history" );

		// 複製
		const TArray<int32> Copies = NWorldEdit::DuplicateObjects( Document, Ids( { A2, B, A1 } ) );
		C.Check( Copies.Num() == 3 && Document.GetNumUndo() == UndoBase + 1 && Document.Get().Objects.Num() == 8, "DuplicateObjects copies each root as one entry" );
		C.Check( ObjectOrEmpty( Document.Get(), IdAtOrNone( Copies, 0 ) ).Name == "A1" && ObjectOrEmpty( Document.Get(), IdAtOrNone( Copies, 1 ) ).Name == "A2" && ObjectOrEmpty( Document.Get(), IdAtOrNone( Copies, 2 ) ).Name == "B", "the copies come in the order of the world, not the order of the selection" );
		C.Check( Document.Undo() && Document.Get().Objects.Num() == 5, "one Undo removes all copies" );

		// 付け替えの可否
		C.Check( !NWorldEdit::CanMoveObjects( Document.Get(), Ids( { B } ), 999, NWorldEdit::EWorldDropPosition::Inside ), "cannot move onto a missing target" );
		C.Check( !NWorldEdit::CanMoveObjects( Document.Get(), Ids( { B } ), B, NWorldEdit::EWorldDropPosition::Before ), "cannot move onto itself" );
		C.Check( !NWorldEdit::CanMoveObjects( Document.Get(), Ids( { A } ), A2, NWorldEdit::EWorldDropPosition::Inside ), "cannot move an object into its own subtree" );
		C.Check( !NWorldEdit::CanMoveObjects( Document.Get(), Ids( { A } ), A2, NWorldEdit::EWorldDropPosition::After ), "nor next to something inside its subtree" );
		C.Check( !NWorldEdit::CanMoveObjects( Document.Get(), TArray<int32>(), B, NWorldEdit::EWorldDropPosition::Inside ) && !NWorldEdit::CanMoveObjects( Document.Get(), Ids( { 999 } ), B, NWorldEdit::EWorldDropPosition::Inside ), "nothing to move" );
		C.Check( NWorldEdit::CanMoveObjects( Document.Get(), Ids( { B, D } ), A, NWorldEdit::EWorldDropPosition::Inside ), "moving two roots into another object is allowed" );
		const int32 UndoBeforeRefuse = Document.GetNumUndo();
		C.Check( !NWorldEdit::MoveObjects( Document, Ids( { A } ), A1, NWorldEdit::EWorldDropPosition::Inside ) && Document.GetNumUndo() == UndoBeforeRefuse, "a refused move changes nothing" );

		// 付け替え: ワールドの姿勢が変わらない（回って拡大された親の中へ）
		FWorldTransform Spec;
		Spec.Position = FVector3( 100, 0, 0 );
		Spec.RotationDegrees = FVector3( 0, 90, 0 );
		Spec.Scale = FVector3( 2, 2, 2 );
		const int32 Holder = NWorldEdit::AddObject( Document, "Holder", WorldNoId, Spec );
		const FVector3 BWorld = WorldPosition( Document, B );
		const FVector3 DWorld = WorldPosition( Document, D );
		const int32 UndoBeforeMove = Document.GetNumUndo();
		C.Check( NWorldEdit::MoveObjects( Document, Ids( { D, B } ), Holder, NWorldEdit::EWorldDropPosition::Inside ) && ObjectOrEmpty( Document.Get(), B ).Parent == Holder && ObjectOrEmpty( Document.Get(), D ).Parent == Holder, "MoveObjects Inside makes them children" );
		C.Check( Near3( WorldPosition( Document, B ), BWorld, 1e-3f ) && Near3( WorldPosition( Document, D ), DWorld, 1e-3f ), "and keeps their world positions (%s)", *WorldPosition( Document, B ).ToString() );
		C.Check( Document.GetNumUndo() == UndoBeforeMove + 1, "as one entry" );
		const TArray<int32> Kids = NWorld::GetChildIndices( Document.Get(), Holder );
		C.Check( Kids.Num() == 2 && Document.Get().Objects[Kids[0]].Id == B && Document.Get().Objects[Kids[1]].Id == D, "the children keep the order of the world (B before D), not the order given" );

		// 順番: Before / After
		C.Check( NWorldEdit::MoveObjects( Document, Ids( { D } ), A, NWorldEdit::EWorldDropPosition::Before ), "MoveObjects Before" );
		TArray<int32> Tops;
		for ( const int32 Index : NWorld::GetChildIndices( Document.Get(), WorldNoId ) ) Tops.Add( Document.Get().Objects[Index].Id );
		C.Check( Tops == Ids( { D, A, Holder } ), "D is now the sibling just before A (top level: D, A, Holder)" );
		C.Check( NWorldEdit::MoveObjects( Document, Ids( { D, Holder } ), A, NWorldEdit::EWorldDropPosition::After ), "MoveObjects After with two" );
		Tops.Reset();
		for ( const int32 Index : NWorld::GetChildIndices( Document.Get(), WorldNoId ) ) Tops.Add( Document.Get().Objects[Index].Id );
		C.Check( Tops == Ids( { A, D, Holder } ), "both land after A, in the order of the world (A, D, Holder)" );
		C.Check( NWorldEdit::MoveObjects( Document, Ids( { A1 } ), D, NWorldEdit::EWorldDropPosition::After ) && ObjectOrEmpty( Document.Get(), A1 ).Parent == WorldNoId, "After a top-level object makes it a top-level object" );

		// グループ
		TWorldDocument Group;
		Group.Reset( FWorldDefinition() );
		const int32 Before = Add( Group, "Before", WorldNoId, FVector3( -9, 0, 0 ) );
		const int32 X = Add( Group, "X", WorldNoId, FVector3( 0, 0, 0 ) );
		const int32 Y = Add( Group, "Y", WorldNoId, FVector3( 2, 0, 0 ) );
		const int32 Z = Add( Group, "Z", WorldNoId, FVector3( 4, 0, 0 ) );
		const int32 W = Add( Group, "W", Z, FVector3( 1, 0, 0 ) );
		const int32 After = Add( Group, "After", WorldNoId, FVector3( 9, 0, 0 ) );
		const int32 GroupBase = Group.GetNumUndo();
		const int32 G = NWorldEdit::GroupObjects( Group, Ids( { Z, X, Y, W } ), "Set" );
		C.Check( G != WorldNoId && ObjectOrEmpty( Group.Get(), G ).Name == "Set" && ObjectOrEmpty( Group.Get(), G ).Parent == WorldNoId && Near3( LocalPosition( Group, G ), FVector3( 2, 0, 0 ) ), "GroupObjects makes a parent at the centre of the roots (2,0,0)" );
		C.Check( ObjectOrEmpty( Group.Get(), X ).Parent == G && ObjectOrEmpty( Group.Get(), Y ).Parent == G && ObjectOrEmpty( Group.Get(), Z ).Parent == G && ObjectOrEmpty( Group.Get(), W ).Parent == Z, "the roots become its children; W stays under Z" );
		C.Check( Near3( WorldPosition( Group, X ), FVector3( 0, 0, 0 ), 1e-3f ) && Near3( WorldPosition( Group, Y ), FVector3( 2, 0, 0 ), 1e-3f ) && Near3( WorldPosition( Group, Z ), FVector3( 4, 0, 0 ), 1e-3f ) && Near3( WorldPosition( Group, W ), FVector3( 5, 0, 0 ), 1e-3f ), "and nothing moved in the world" );
		C.Check( Near3( LocalPosition( Group, X ), FVector3( -2, 0, 0 ) ) && Near3( LocalPosition( Group, Z ), FVector3( 2, 0, 0 ) ), "the children's local positions are relative to the group" );
		TArray<int32> TopLevel;
		for ( const int32 Index : NWorld::GetChildIndices( Group.Get(), WorldNoId ) ) TopLevel.Add( Group.Get().Objects[Index].Id );
		C.Check( Group.Get().Objects.Num() == 7 && TopLevel == Ids( { Before, G, After } ), "the group takes the place of the first root among its siblings (before / group / after)" );
		C.Check( Group.GetNumUndo() == GroupBase + 1 && Group.Undo() && Group.Get().Objects.Num() == 6 && ObjectOrEmpty( Group.Get(), X ).Parent == WorldNoId, "grouping is one history entry" );
		C.Check( NWorldEdit::GroupObjects( Group, TArray<int32>(), "x" ) == WorldNoId && NWorldEdit::GroupObjects( Group, Ids( { 999 } ), "x" ) == WorldNoId, "grouping nothing gives no group" );

		// 親の下のグループ
		TWorldDocument Nested;
		Nested.Reset( FWorldDefinition() );
		FWorldTransform HolderSpec;
		HolderSpec.Position = FVector3( 10, 0, 0 );
		HolderSpec.RotationDegrees = FVector3( 0, 90, 0 );
		HolderSpec.Scale = FVector3( 2, 2, 2 );
		const int32 NestedHolder = NWorldEdit::AddObject( Nested, "Holder", WorldNoId, HolderSpec );
		const int32 N1 = Add( Nested, "N1", NestedHolder, FVector3( 1, 0, 0 ) );
		const int32 N2 = Add( Nested, "N2", NestedHolder, FVector3( 0, 0, 1 ) );
		const FVector3 N1World = WorldPosition( Nested, N1 );
		const FVector3 N2World = WorldPosition( Nested, N2 );
		const int32 NG = NWorldEdit::GroupObjects( Nested, Ids( { N1, N2 } ), "Pair" );
		C.Check( ObjectOrEmpty( Nested.Get(), NG ).Parent == NestedHolder && Near3( WorldPosition( Nested, NG ), ( N1World + N2World ) * 0.5f, 1e-3f ), "roots that share a parent are grouped under it, at their world centre" );
		C.Check( Near3( WorldPosition( Nested, N1 ), N1World, 1e-3f ) && Near3( WorldPosition( Nested, N2 ), N2World, 1e-3f ), "and stay where they were in the world, under a rotated, scaled parent" );

		// グループを外す
		const int32 UngroupBase = Nested.GetNumUndo();
		C.Check( NWorldEdit::UngroupObject( Nested, NG ) && Nested.Get().FindObject( NG ) == nullptr && ObjectOrEmpty( Nested.Get(), N1 ).Parent == NestedHolder && ObjectOrEmpty( Nested.Get(), N2 ).Parent == NestedHolder, "UngroupObject returns the children to the group's parent and removes the group" );
		C.Check( Near3( WorldPosition( Nested, N1 ), N1World, 1e-3f ) && Near3( WorldPosition( Nested, N2 ), N2World, 1e-3f ) && Nested.GetNumUndo() == UngroupBase + 1, "without moving them, as one entry" );
		const int32 Model = Add( Nested, "Mesh", WorldNoId, FVector3::Zero );
		FWorldModel Spec2;
		Spec2.Path = "Models/a.mv1";
		NWorldEdit::EditObject( Nested, Model )->Model = Spec2;
		Add( Nested, "Under", Model, FVector3::Zero );
		C.Check( !NWorldEdit::UngroupObject( Nested, Model ) && Nested.Get().FindObject( Model ) != nullptr, "an object with a component is not ungrouped (it would be lost)" );
		C.Check( !NWorldEdit::UngroupObject( Nested, 999 ), "a missing id" );
		const int32 Lonely = Add( Nested, "Lonely", WorldNoId, FVector3::Zero );
		const int32 UndoBeforeLonely = Nested.GetNumUndo();
		C.Check( !NWorldEdit::UngroupObject( Nested, Lonely ) && Nested.Get().FindObject( Lonely ) != nullptr && Nested.GetNumUndo() == UndoBeforeLonely, "an object without children is not a group: ungrouping it must not delete it" );

		// コピーと貼り付け
		TWorldDocument Source;
		Source.Reset( FWorldDefinition() );
		FWorldTransform RSpec;
		RSpec.Position = FVector3( 1, 2, 3 );
		RSpec.RotationDegrees = FVector3( 0, 90, 0 );
		const int32 R = NWorldEdit::AddObject( Source, "R", WorldNoId, RSpec );
		const int32 K = Add( Source, "K", R, FVector3( 1, 0, 0 ) );
		const int32 S = Add( Source, "S", WorldNoId, FVector3( 7, 0, 0 ) );
		NWorldEdit::SetProp( Source, K, FWorldProp::MakeObject( "Inside", R ) );
		NWorldEdit::SetProp( Source, K, FWorldProp::MakeObject( "Outside", S ) );
		NWorldEdit::SetProp( Source, K, FWorldProp::MakeInt( "Hp", 3 ) );
		const NWorldEdit::FWorldClipboard Copied = NWorldEdit::CopyObjects( Source.Get(), Ids( { K, R } ) );
		C.Check( Copied.Objects.Num() == 2 && Copied.Objects[0].Id == R && Copied.Objects[0].Parent == WorldNoId && Copied.Objects[1].Id == K && Copied.Objects[1].Parent == R, "CopyObjects copies the root and its subtree, parent first (the child K is not a root of its own)" );
		C.Check( Near3( Copied.Objects[0].Transform.Position, FVector3( 1, 2, 3 ) ) && Near3( Copied.Objects[0].Transform.RotationDegrees, FVector3( 0, 90, 0 ) ), "a top-level root keeps its transform exactly" );
		const NWorldEdit::FWorldClipboard CopiedChild = NWorldEdit::CopyObjects( Source.Get(), Ids( { K } ) );
		C.Check( CopiedChild.Objects.Num() == 1 && CopiedChild.Objects[0].Parent == WorldNoId && Near3( CopiedChild.Objects[0].Transform.Position, WorldPosition( Source, K ), 1e-3f ), "a copied child becomes a root at its world position" );
		C.Check( NWorldEdit::CopyObjects( Source.Get(), Ids( { 999 } ) ).IsEmpty(), "copying nothing gives an empty clipboard" );

		TWorldDocument Target;
		Target.Reset( FWorldDefinition() );
		for ( int32 i = 0; i < 4; ++i ) Add( Target, "Existing", WorldNoId, FVector3::Zero );   // 新しい Id が、コピー元の Id（S = 3）とぶつからないように
		const int32 TargetBase = Target.GetNumUndo();
		const TArray<int32> Pasted = NWorldEdit::PasteObjects( Target, Copied, WorldNoId, FVector3( 1, 0, 0 ) );
		C.Check( Pasted.Num() == 1 && Target.Get().Objects.Num() == 6 && Target.GetNumUndo() == TargetBase + 1 && IdAtOrNone( Pasted, 0 ) > 4, "PasteObjects adds new objects with new ids as one entry" );
		const FWorldObject* PastedRoot = Target.Get().FindObject( IdAtOrNone( Pasted, 0 ) );
		const FWorldObject* PastedChild = nullptr;
		for ( const FWorldObject& Object : Target.Get().Objects )
		{
			if ( Object.Name == "K" ) PastedChild = &Object;
		}
		C.Check( PastedRoot != nullptr && PastedChild != nullptr && PastedChild->Parent == PastedRoot->Id, "the child's parent is the pasted root" );
		const FWorldProp* Inside = PastedChild->FindProp( "Inside" );
		const FWorldProp* Outside = PastedChild->FindProp( "Outside" );
		C.Check( Inside != nullptr && Inside->ObjectId == PastedRoot->Id && Outside != nullptr && Outside->ObjectId == S && PastedChild->FindProp( "Hp" )->IntValue == 3, "an object reference inside the pasted set follows the paste; one pointing outside is kept; other props are kept" );
		C.Check( Near3( PastedRoot->Transform.Position, FVector3( 2, 2, 3 ) ) && Near3( PastedRoot->Transform.RotationDegrees, FVector3( 0, 90, 0 ) ), "the offset moves the pasted root in the world" );

		const TArray<int32> Beside = NWorldEdit::PasteObjects( Target, Copied, WorldNoId, FVector3::Zero );
		C.Check( Beside.Num() == 1 && IdAtOrNone( Beside, 0 ) != IdAtOrNone( Pasted, 0 ) && Target.Get().Objects.Num() == 8, "pasting again makes another copy" );

		// 親の下へ貼ると、ワールドの位置が（オフセットのぶんだけ）同じになる
		FWorldTransform PSpec;
		PSpec.Position = FVector3( 50, 0, 0 );
		PSpec.RotationDegrees = FVector3( 0, 90, 0 );
		PSpec.Scale = FVector3( 2, 3, 2 );
		const int32 PasteParent = NWorldEdit::AddObject( Target, "PasteParent", WorldNoId, PSpec );
		const TArray<int32> Under = NWorldEdit::PasteObjects( Target, Copied, PasteParent, FVector3( 0, 1, 0 ) );
		C.Check( Under.Num() == 1 && ObjectOrEmpty( Target.Get(), IdAtOrNone( Under, 0 ) ).Parent == PasteParent && Near3( WorldPosition( Target, IdAtOrNone( Under, 0 ) ), FVector3( 1, 3, 3 ), 5e-3f ), "pasted under a rotated, scaled parent, the root is at its copied world position plus the offset (%s)", *WorldPosition( Target, IdAtOrNone( Under, 0 ) ).ToString() );
		const int32 UndoBeforeBad = Target.GetNumUndo();
		C.Check( NWorldEdit::PasteObjects( Target, Copied, 999 ).IsEmpty() && NWorldEdit::PasteObjects( Target, NWorldEdit::FWorldClipboard() ).IsEmpty() && Target.GetNumUndo() == UndoBeforeBad, "a missing parent or an empty clipboard pastes nothing and leaves no history" );

		// 姿勢を直接指定
		TWorldDocument Pose;
		Pose.Reset( FWorldDefinition() );
		FWorldTransform QSpec;
		QSpec.Position = FVector3( 3, 0, 0 );
		QSpec.RotationDegrees = FVector3( 0, 400, 0 );
		QSpec.Scale = FVector3( 2, 2, 2 );
		const int32 Q = NWorldEdit::AddObject( Pose, "Q", WorldNoId, QSpec );
		const int32 Child = Add( Pose, "Child", Q, FVector3( 1, 0, 0 ) );
		NWorldEdit::FWorldTransformMask OnlyPosition;
		OnlyPosition.bRotation = false;
		OnlyPosition.bScale = false;
		const FTransform ChildWorldNow = NWorld::ComputeWorldTransform( Pose.Get(), Child );
		FTransform Wanted = ChildWorldNow;
		Wanted.Position = Wanted.Position + FVector3( 0, 5, 0 );
		C.Check( NWorldEdit::SetWorldTransform( Pose, Child, Wanted, OnlyPosition ) && Near3( WorldPosition( Pose, Child ), ChildWorldNow.Position + FVector3( 0, 5, 0 ), 1e-3f ), "SetWorldTransform places the object at the wanted world position under a rotated, scaled parent" );
		C.Check( Near3( ObjectOrEmpty( Pose.Get(), Child ).Transform.RotationDegrees, FVector3::Zero, 0.0f ) && Near3( ObjectOrEmpty( Pose.Get(), Child ).Transform.Scale, FVector3::One, 0.0f ), "and with the mask, the rotation and scale values are untouched (bit for bit)" );
		NWorldEdit::FWorldTransformMask OnlyRotation;
		OnlyRotation.bPosition = false;
		OnlyRotation.bScale = false;
		FTransform Spun = NWorld::ComputeWorldTransform( Pose.Get(), Child );
		Spun.Rotation = FQuaternion::EulerToQuat( FVector3( 0, 1.0f, 0 ) ) * Spun.Rotation;
		const FVector3 PositionBeforeSpin = LocalPosition( Pose, Child );
		C.Check( NWorldEdit::SetWorldTransform( Pose, Child, Spun, OnlyRotation ) && Near3( LocalPosition( Pose, Child ), PositionBeforeSpin, 0.0f ) && Near3( NWorld::ComputeWorldTransform( Pose.Get(), Child ).Rotation.Rotate( FVector3::Forward ), Spun.Rotation.Rotate( FVector3::Forward ), 1e-4f )
					 && Near3( NWorld::ComputeWorldTransform( Pose.Get(), Child ).Rotation.Rotate( FVector3::Right ), Spun.Rotation.Rotate( FVector3::Right ), 1e-4f ), "only the rotation mask changes the rotation" );
		C.Check( !NWorldEdit::SetWorldTransform( Pose, 999, Wanted ), "a missing id" );
	}

	//=========================================================================
	// セッション: 選択
	//=========================================================================
	struct FSessionFixture
	{
		CWorldEditorSession Session;
		FFakeEditorAssets Assets;
		FEditorViewport View = MakeViewport();
		int32 Near = 0, Far = 0, Side = 0;

		FSessionFixture()
		{
			Session.Reset( FWorldDefinition(), "" );
			Session.Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
			Assets.Models.Add( { "Models/Box.mv1", FVector3( -1, 0, -1 ), FVector3( 1, 2, 1 ) } );
			Assets.Textures.Add( { "Tex/a.png", 64, 32 } );
		}

		FEditorShapeContext Context() const
		{
			FEditorShapeContext Result;
			Result.Assets = &Assets;
			Result.Camera = &Session.Camera;
			Result.Viewport = View;
			return Result;
		}
		FEditorRay RayAt( const FVector3& Point ) const { return RayThrough( Session.Camera, View, Point ); }

		// 重なった 2 つ（Near の真後ろに Far）と、横に Side
		void MakeColliders()
		{
			Near = Session.CreateCollider( EWorldColliderShape::Box, false, FVector3( 0, 0, 0 ) );
			Far = Session.CreateCollider( EWorldColliderShape::Box, false, FVector3( 0, 0, 5 ) );
			Side = Session.CreateCollider( EWorldColliderShape::Box, false, FVector3( 3, 0, 0 ) );
			Session.ClearSelection();
		}
	};

	void TestSelection( NAssetTest::FChecker& C )
	{
		C.Group = "editor-selection";
		FSessionFixture F;
		CWorldEditorSession& S = F.Session;
		const int32 A = S.CreateEmpty( FVector3( 0, 0, 0 ), "A" );
		const int32 B = S.CreateEmpty( FVector3( 1, 0, 0 ), "B" );
		const int32 D = S.CreateEmpty( FVector3( 2, 0, 0 ), "D" );
		const auto Ids = []( std::initializer_list<int32> List ) { TArray<int32> Result; for ( const int32 Id : List ) Result.Add( Id ); return Result; };

		C.Check( S.GetSelection() == Ids( { D } ) && S.GetPrimary() == D, "creating an object selects it" );
		S.Select( A );
		C.Check( S.GetSelection() == Ids( { A } ) && S.GetPrimary() == A, "Replace selects only that" );
		S.Select( B, CWorldEditorSession::ESelectMode::Add );
		S.Select( D, CWorldEditorSession::ESelectMode::Toggle );
		C.Check( S.GetSelection() == Ids( { A, B, D } ) && S.GetPrimary() == D && S.IsSelected( B ), "Add and Toggle add; the last is the primary" );
		S.Select( B, CWorldEditorSession::ESelectMode::Toggle );
		C.Check( S.GetSelection() == Ids( { A, D } ), "Toggle of a selected one removes it" );
		S.Select( A, CWorldEditorSession::ESelectMode::Add );
		C.Check( S.GetSelection() == Ids( { D, A } ) && S.GetPrimary() == A, "Add of one already selected makes it the primary" );
		S.Select( D, CWorldEditorSession::ESelectMode::Remove );
		C.Check( S.GetSelection() == Ids( { A } ), "Remove" );
		S.Select( 999, CWorldEditorSession::ESelectMode::Add );
		C.Check( S.GetSelection() == Ids( { A } ), "an id that does not exist is not selected" );
		S.Select( 999 );
		C.Check( S.GetSelection().IsEmpty() && S.GetPrimary() == WorldNoId, "Replace with a missing id selects nothing" );
		S.SetSelection( Ids( { A, 999, B, A } ) );
		C.Check( S.GetSelection() == Ids( { B, A } ), "SetSelection keeps the existing ids, once each (A moved to the end by its second mention)" );
		S.SelectAll();
		C.Check( S.GetSelection().Num() == 3, "SelectAll" );
		S.ClearSelection();
		C.Check( S.GetSelection().IsEmpty(), "ClearSelection" );

		// 外からの削除は、SyncSelection で選択から外れる
		S.SelectAll();
		NWorldEdit::RemoveObject( S.Document, B );
		S.SyncSelection();
		C.Check( !S.IsSelected( B ) && S.GetSelection().Num() == 2, "an object removed behind the session's back leaves the selection after SyncSelection" );

		// 根
		const int32 Kid = S.CreateEmpty( FVector3::Zero, "Kid", A );
		S.SetSelection( Ids( { Kid, A } ) );
		C.Check( S.GetSelectedRoots() == Ids( { A } ), "the roots of the selection leave out a child whose parent is selected" );
	}

	//=========================================================================
	// セッション: クリックと矩形
	//=========================================================================
	void TestSessionPicking( NAssetTest::FChecker& C )
	{
		C.Group = "editor-click";
		FSessionFixture F;
		CWorldEditorSession& S = F.Session;
		F.MakeColliders();
		const auto Mode = CWorldEditorSession::ESelectMode::Replace;
		const auto Ids = []( std::initializer_list<int32> List ) { TArray<int32> Result; for ( const int32 Id : List ) Result.Add( Id ); return Result; };

		C.Check( S.ClickSelect( F.RayAt( FVector3( 3, 0, 0 ) ), F.Context(), Mode ) == F.Side && S.GetSelection() == Ids( { F.Side } ), "clicking on an object selects it" );
		C.Check( S.ClickSelect( S.Camera.ScreenToRay( F.View, 10, 10 ), F.Context(), CWorldEditorSession::ESelectMode::Add ) == WorldNoId && S.GetSelection() == Ids( { F.Side } ), "clicking on empty space with Add keeps the selection" );
		C.Check( S.ClickSelect( S.Camera.ScreenToRay( F.View, 10, 10 ), F.Context(), Mode ) == WorldNoId && S.GetSelection().IsEmpty(), "clicking on empty space with Replace clears it" );
		S.ClickSelect( F.RayAt( FVector3( 3, 0, 0 ) ), F.Context(), Mode );
		S.ClickSelect( F.RayAt( FVector3( 0, 0, 0 ) ), F.Context(), CWorldEditorSession::ESelectMode::Toggle );
		C.Check( S.GetSelection() == Ids( { F.Side, F.Near } ), "Toggle adds a second object" );
		S.ClickSelect( F.RayAt( FVector3( 0, 0, 0 ) ), F.Context(), CWorldEditorSession::ESelectMode::Toggle );
		C.Check( S.GetSelection() == Ids( { F.Side } ), "and removes it on the next click" );

		// 重なったもの
		const FEditorRay Center = S.Camera.ScreenToRay( F.View, 400, 300 );
		S.ClearSelection();
		C.Check( S.ClickSelect( Center, F.Context(), Mode, true ) == F.Near, "cycling: the first click picks the nearest" );
		C.Check( S.ClickSelect( Center, F.Context(), Mode, true ) == F.Far && S.GetPrimary() == F.Far, "the next click on the same place picks the one behind" );
		C.Check( S.ClickSelect( Center, F.Context(), Mode, true ) == F.Near, "and then back to the first" );
		C.Check( S.ClickSelect( Center, F.Context(), Mode, false ) == F.Near, "without cycling it is always the nearest" );

		// 矩形
		S.ClearSelection();
		S.RectSelect( F.Context(), 0, 0, 799, 599, Mode );
		C.Check( S.GetSelection().Num() == 3, "a rectangle over everything selects all three" );
		S.ClearSelection();
		FVector2 SidePixel;
		S.Camera.WorldToScreen( F.View, FVector3( 3, 0, 0 ), SidePixel );
		S.RectSelect( F.Context(), SidePixel.x - 10, SidePixel.y - 10, SidePixel.x + 10, SidePixel.y + 10, Mode );
		C.Check( S.GetSelection() == Ids( { F.Side } ), "a small rectangle around one object selects it" );
		S.RectSelect( F.Context(), 395, 295, 405, 305, CWorldEditorSession::ESelectMode::Add );
		C.Check( S.GetSelection().Num() == 3, "Add: the objects under the centre (Near and Far) are added" );
	}

	//=========================================================================
	// セッション: 編集
	//=========================================================================
	void TestSessionEdit( NAssetTest::FChecker& C )
	{
		C.Group = "editor-session";
		FSessionFixture F;
		CWorldEditorSession& S = F.Session;
		const auto Ids = []( std::initializer_list<int32> List ) { TArray<int32> Result; for ( const int32 Id : List ) Result.Add( Id ); return Result; };

		// 作る
		const int32 UndoBase = S.Document.GetNumUndo();
		const int32 Empty = S.CreateEmpty( FVector3( 1, 2, 3 ) );
		C.Check( Empty != WorldNoId && ObjectOrEmpty( S.Document.Get(), Empty ).Name == "Empty" && Near3( LocalPosition( S.Document, Empty ), FVector3( 1, 2, 3 ) ) && S.GetSelection() == Ids( { Empty } ) && S.Document.GetNumUndo() == UndoBase + 1, "CreateEmpty: named, placed, selected, one entry" );
		const int32 Trigger = S.CreateCollider( EWorldColliderShape::Box, true, FVector3( 0, 1, 0 ) );
		C.Check( ObjectOrEmpty( S.Document.Get(), Trigger ).Name == "Trigger" && ObjectOrEmpty( S.Document.Get(), Trigger ).Collider.IsSet() && ObjectOrEmpty( S.Document.Get(), Trigger ).Collider.Get( FWorldCollider() ).bTrigger, "CreateCollider makes a trigger" );
		const int32 Wall = S.CreateCollider( EWorldColliderShape::Capsule, false, FVector3::Zero );
		C.Check( ObjectOrEmpty( S.Document.Get(), Wall ).Name == "Capsule" && ObjectOrEmpty( S.Document.Get(), Wall ).Collider.Get( FWorldCollider() ).Shape == EWorldColliderShape::Capsule, "and a capsule" );
		const int32 Crate = S.CreateAsset( "Models\\Box.mv1", FVector3( 4, 0, 4 ), &F.Assets );
		C.Check( Crate != WorldNoId && ObjectOrEmpty( S.Document.Get(), Crate ).Name == "Box" && ObjectOrEmpty( S.Document.Get(), Crate ).Model.IsSet() && ObjectOrEmpty( S.Document.Get(), Crate ).Model.Get( FWorldModel() ).Path == "Models/Box.mv1", "CreateAsset: a model, named after the file, with the path in forward slashes" );
		const int32 Sign = S.CreateAsset( "Tex/a.png", FVector3( 0, 0, 0 ), &F.Assets );
		const FWorldBillboard& SignBoard = ObjectOrEmpty( S.Document.Get(), Sign ).Billboard.Get( FWorldBillboard() );
		C.Check( std::fabs( SignBoard.Size.x - 2.0f ) < 1e-5f && std::fabs( SignBoard.Size.y - 1.0f ) < 1e-5f && SignBoard.Texture == "Tex/a.png" && std::fabs( SignBoard.Pivot.y - 1.0f ) < 1e-5f, "an image becomes a billboard with the image's aspect (64x32 -> 2x1) and its pivot at the feet" );
		const int32 Voice = S.CreateAsset( "Sounds/Wind.ogg", FVector3::Zero, &F.Assets );
		C.Check( ObjectOrEmpty( S.Document.Get(), Voice ).Sound.IsSet() && ObjectOrEmpty( S.Document.Get(), Voice ).Sound.Get( FWorldSound() ).Path == "Sounds/Wind.ogg", "a sound file becomes a sound object" );
		const int32 UndoBeforeBad = S.Document.GetNumUndo();
		const TArray<int32> SelectionBefore = S.GetSelection();
		C.Check( S.CreateAsset( "Docs/readme.txt", FVector3::Zero, &F.Assets ) == WorldNoId && S.CreateAsset( "", FVector3::Zero, &F.Assets ) == WorldNoId && S.Document.GetNumUndo() == UndoBeforeBad && S.GetSelection() == SelectionBefore, "an unknown kind of file creates nothing and changes neither history nor selection" );

		// 削除と元に戻す
		S.SetSelection( Ids( { Crate, Sign, Voice } ) );
		const int32 Count = S.Document.Get().Objects.Num();
		const int32 Base = S.Document.GetNumUndo();
		C.Check( S.DeleteSelection() == 3 && S.Document.Get().Objects.Num() == Count - 3 && S.GetSelection().IsEmpty() && S.Document.GetNumUndo() == Base + 1, "DeleteSelection removes all selected as one entry and clears the selection" );
		C.Check( S.Undo() && S.Document.Get().Objects.Num() == Count && S.GetSelection().IsEmpty(), "Undo brings them back (the selection is not restored: it stays valid, i.e. empty)" );
		C.Check( S.Redo() && S.Document.Get().Objects.Num() == Count - 3, "Redo" );

		// 元に戻して無くなったオブジェクトは、選択から外れる
		FSessionFixture Fresh;
		const int32 Keep = Fresh.Session.CreateEmpty( FVector3::Zero, "Keep" );
		const int32 Gone = Fresh.Session.CreateEmpty( FVector3::Zero, "Gone" );
		C.Check( Fresh.Session.GetSelection() == Ids( { Gone } ) && Fresh.Session.Undo() && Fresh.Session.GetSelection().IsEmpty(), "undoing the creation of the selected object removes it from the selection" );
		C.Check( Fresh.Session.Redo() && Fresh.Session.Document.Get().FindObject( Gone ) != nullptr && Fresh.Session.GetSelection().IsEmpty(), "and Redo does not select it again" );
		(void)Keep;

		// 複製
		FSessionFixture G;
		CWorldEditorSession& T = G.Session;
		const int32 First = T.CreateEmpty( FVector3( 1, 0, 0 ), "First" );
		const int32 Second = T.CreateEmpty( FVector3( 2, 0, 0 ), "Second" );
		T.SetSelection( Ids( { First, Second } ) );
		const int32 DupBase = T.Document.GetNumUndo();
		const TArray<int32> Dups = T.DuplicateSelection();
		C.Check( Dups.Num() == 2 && T.GetSelection() == Dups && T.Document.GetNumUndo() == DupBase + 1 && ObjectOrEmpty( T.Document.Get(), IdAtOrNone( Dups, 0 ) ).Name == "First", "DuplicateSelection selects the copies, as one entry" );

		// コピーと貼り付け
		T.SetSelection( Ids( { First } ) );
		T.CopySelection();
		C.Check( T.CanPaste(), "after CopySelection a paste is possible" );
		T.ClearSelection();
		T.CopySelection();
		C.Check( T.CanPaste(), "copying an empty selection keeps the earlier clipboard" );
		const TArray<int32> Pasted = T.Paste( FVector3( 0, 0, 3 ) );
		C.Check( Pasted.Num() == 1 && T.GetSelection() == Pasted && Near3( LocalPosition( T.Document, IdAtOrNone( Pasted, 0 ) ), FVector3( 1, 0, 3 ) ), "Paste selects the pasted object, offset in the world" );

		// ワールドを入れ替えても、コピーしたものは残る（別のワールドへ貼れる）
		FSessionFixture H;
		C.Check( !H.Session.CanPaste(), "a fresh session has an empty clipboard" );
		const FWorldDefinition Replacement = T.Document.Get();
		T.Reset( Replacement, "keep.world" );
		C.Check( T.CanPaste() && T.GetSelection().IsEmpty() && T.FilePath == "keep.world" && !T.Document.CanUndo(), "Reset clears history and selection but keeps the clipboard" );

		// グループ
		FSessionFixture J;
		CWorldEditorSession& U = J.Session;
		const int32 X = U.CreateEmpty( FVector3( 0, 0, 0 ), "X" );
		const int32 Y = U.CreateEmpty( FVector3( 4, 0, 0 ), "Y" );
		U.SetSelection( Ids( { X, Y } ) );
		const int32 Group = U.GroupSelection( "Pair" );
		C.Check( Group != WorldNoId && U.GetSelection() == Ids( { Group } ) && ObjectOrEmpty( U.Document.Get(), X ).Parent == Group, "GroupSelection selects the new group" );
		C.Check( U.UngroupSelection() && U.GetSelection().Num() == 2 && U.IsSelected( X ) && U.IsSelected( Y ) && U.Document.Get().FindObject( Group ) == nullptr && Near3( WorldPosition( U.Document, Y ), FVector3( 4, 0, 0 ), 1e-3f ), "UngroupSelection frees the children, selects them and keeps their positions" );
		C.Check( !U.UngroupSelection() && U.Document.Get().FindObject( X ) != nullptr && U.Document.Get().FindObject( Y ) != nullptr, "ungrouping ordinary objects does nothing (and deletes nothing)" );

		// アウトライナーの移動
		U.SetSelection( Ids( { Y } ) );
		C.Check( U.MoveSelection( X, NWorldEdit::EWorldDropPosition::Inside ) && ObjectOrEmpty( U.Document.Get(), Y ).Parent == X, "MoveSelection puts the selection under the target" );
		C.Check( !U.MoveSelection( Y, NWorldEdit::EWorldDropPosition::Inside ) || ObjectOrEmpty( U.Document.Get(), Y ).Parent == X, "and cannot put it under itself" );

		// 寄る
		FSessionFixture K;
		CWorldEditorSession& V = K.Session;
		const int32 Block = V.CreateAsset( "Models/Box.mv1", FVector3( 10, 0, 0 ), &K.Assets );
		V.Camera = MakeCamera( 0.6f, 0.45f, 50.0f, FVector3::Zero );
		C.Check( V.FocusSelection( K.View, &K.Assets ) && Near3( V.Camera.Target, FVector3( 10, 1, 0 ), 1e-3f ) && V.Camera.Distance < 20.0f, "FocusSelection centres the camera on the selection (%s)", *V.Camera.Target.ToString() );
		V.ClearSelection();
		C.Check( !V.FocusSelection( K.View, &K.Assets ), "and does nothing without a selection" );
		(void)Block;
	}

	//=========================================================================
	// セッション: ギズモ
	//=========================================================================
	void TestSessionGizmo( NAssetTest::FChecker& C )
	{
		C.Group = "editor-gizmo-session";
		FSessionFixture F;
		CWorldEditorSession& S = F.Session;
		S.Camera = MakeCamera( 0.6f, 0.45f, 10.0f );
		const auto Ids = []( std::initializer_list<int32> List ) { TArray<int32> Result; for ( const int32 Id : List ) Result.Add( Id ); return Result; };

		FWorldTransform ASpec;
		ASpec.Position = FVector3( 1, 0, 0 );
		ASpec.RotationDegrees = FVector3( 0, 400, 0 );
		ASpec.Scale = FVector3( 1, 2, 3 );
		const int32 A = NWorldEdit::AddObject( S.Document, "A", WorldNoId, ASpec );
		const int32 B = Add( S.Document, "B", WorldNoId, FVector3( -3, 0, 0 ) );
		FEditorGizmoFrame Frame;
		FEditorGizmoFrame Dummy;
		C.Check( !S.GetGizmoFrame( F.View, Dummy ) && !S.BeginGizmoDrag( F.View, EEditorGizmoHandle::AxisX, S.Camera.ScreenToRay( F.View, 400, 300 ) ), "no selection: no gizmo, no drag" );

		S.Select( A );
		C.Check( S.GetGizmoFrame( F.View, Frame ) && Near3( Frame.Origin, FVector3( 1, 0, 0 ) ) && Near3( Frame.Axes[0], FVector3::Right ), "the gizmo sits on the selected object, along the world axes" );
		S.GizmoSpace = EEditorGizmoSpace::Local;
		S.GetGizmoFrame( F.View, Frame );
		C.Check( Near3( Frame.Axes[0], NWorld::ComputeWorldTransform( S.Document.Get(), A ).Rotation.Rotate( FVector3::Right ), 1e-4f ), "in local space the axes follow the object" );
		S.GizmoSpace = EEditorGizmoSpace::World;
		S.GetGizmoFrame( F.View, Frame );

		// 移動（World）
		const int32 Base = S.Document.GetNumUndo();
		const FVector3 Grab = OnAxis( Frame, 0, 0.5f );
		C.Check( S.BeginGizmoDrag( F.View, EEditorGizmoHandle::AxisX, F.RayAt( Grab ) ) && S.IsGizmoDragging(), "a drag begins" );
		C.Check( !S.Undo() && !S.DeleteSelection(), "no Undo or delete while dragging" );
		S.UpdateGizmoDrag( F.RayAt( Grab ) );
		C.Check( S.Document.GetNumUndo() == Base && Near3( LocalPosition( S.Document, A ), FVector3( 1, 0, 0 ) ), "an update without movement makes no history" );
		S.UpdateGizmoDrag( F.RayAt( Grab + FVector3( 1, 0, 0 ) ) );
		S.UpdateGizmoDrag( F.RayAt( Grab + FVector3( 2, 0, 0 ) ) );
		C.Check( Near3( LocalPosition( S.Document, A ), FVector3( 3, 0, 0 ), 2e-3f ) && S.Document.GetNumUndo() == Base + 1, "dragging 2 along X moves the object there, with one history entry however many frames" );
		C.Check( Near3( ObjectOrEmpty( S.Document.Get(), A ).Transform.RotationDegrees, FVector3( 0, 400, 0 ), 0.0f ) && Near3( ObjectOrEmpty( S.Document.Get(), A ).Transform.Scale, FVector3( 1, 2, 3 ), 0.0f ), "a move leaves the rotation (400 degrees) and the scale bit for bit as they were" );
		S.EndGizmoDrag();
		C.Check( !S.IsGizmoDragging() && S.Undo() && Near3( LocalPosition( S.Document, A ), FVector3( 1, 0, 0 ) ), "after the drag, one Undo returns it" );
		S.Redo();

		// 続けての 2 回目は別の編集
		S.GetGizmoFrame( F.View, Frame );
		const int32 Base2 = S.Document.GetNumUndo();
		const FVector3 Grab2 = OnAxis( Frame, 0, 0.5f );
		S.BeginGizmoDrag( F.View, EEditorGizmoHandle::AxisX, F.RayAt( Grab2 ) );
		S.UpdateGizmoDrag( F.RayAt( Grab2 + FVector3( 1, 0, 0 ) ) );
		S.EndGizmoDrag();
		C.Check( S.Document.GetNumUndo() == Base2 + 1 && Near3( LocalPosition( S.Document, A ), FVector3( 4, 0, 0 ), 2e-3f ), "a second drag is its own history entry" );

		// 動かさずに離す
		S.GetGizmoFrame( F.View, Frame );
		const int32 Base3 = S.Document.GetNumUndo();
		const bool bDirty = S.Document.IsDirty();
		S.BeginGizmoDrag( F.View, EEditorGizmoHandle::AxisX, F.RayAt( OnAxis( Frame, 0, 0.5f ) ) );
		S.UpdateGizmoDrag( F.RayAt( OnAxis( Frame, 0, 0.5f ) ) );
		S.EndGizmoDrag();
		C.Check( S.Document.GetNumUndo() == Base3 && S.Document.IsDirty() == bDirty, "a click on the handle without moving is no edit" );
		S.BeginGizmoDrag( F.View, EEditorGizmoHandle::AxisX, F.RayAt( OnAxis( Frame, 0, 0.5f ) ) );
		S.CancelGizmoDrag();
		C.Check( !S.IsGizmoDragging() && S.Document.GetNumUndo() == Base3, "cancelling a drag that did not move pops no history" );

		// 取り消し
		S.GetGizmoFrame( F.View, Frame );
		const FVector3 Before = LocalPosition( S.Document, A );
		const FVector3 Grab3 = OnAxis( Frame, 0, 0.5f );
		S.BeginGizmoDrag( F.View, EEditorGizmoHandle::AxisX, F.RayAt( Grab3 ) );
		S.UpdateGizmoDrag( F.RayAt( Grab3 + FVector3( 5, 0, 0 ) ) );
		C.Check( Near3( LocalPosition( S.Document, A ), Before + FVector3( 5, 0, 0 ), 5e-3f ), "the drag moves the object (before cancelling)" );
		S.CancelGizmoDrag();
		C.Check( Near3( LocalPosition( S.Document, A ), Before, 0.0f ) && S.Document.GetNumUndo() == Base3 && !S.Document.CanRedo(), "cancelling restores the position exactly, leaves no history and nothing to redo" );

		// スナップ
		S.bSnap = true;
		S.MoveSnap = 0.5f;
		S.GetGizmoFrame( F.View, Frame );
		const FVector3 Grab4 = OnAxis( Frame, 0, 0.5f );
		const FVector3 Start4 = LocalPosition( S.Document, A );
		S.BeginGizmoDrag( F.View, EEditorGizmoHandle::AxisX, F.RayAt( Grab4 ) );
		S.UpdateGizmoDrag( F.RayAt( Grab4 + FVector3( 1.3f, 0, 0 ) ) );
		C.Check( Near3( LocalPosition( S.Document, A ), Start4 + FVector3( 1.5f, 0, 0 ), 3e-3f ), "with snap on, 1.3 becomes 1.5" );
		S.UpdateGizmoDrag( F.RayAt( Grab4 + FVector3( 1.3f, 0, 0 ) ), true );
		C.Check( Near3( LocalPosition( S.Document, A ), Start4 + FVector3( 1.3f, 0, 0 ), 3e-3f ), "and holding the invert key turns the snap off for that update" );
		S.CancelGizmoDrag();
		S.bSnap = false;

		// 子: 回って拡大された親の下を、ワールドの X へ 2 動かす
		FSessionFixture G;
		CWorldEditorSession& T = G.Session;
		T.Camera = MakeCamera( 0.6f, 0.45f, 10.0f );
		FWorldTransform PSpec;
		PSpec.RotationDegrees = FVector3( 0, 90, 0 );
		PSpec.Scale = FVector3( 2, 2, 2 );
		const int32 P = NWorldEdit::AddObject( T.Document, "P", WorldNoId, PSpec );
		FWorldTransform KSpec;
		KSpec.Position = FVector3( 1, 0, 0 );
		KSpec.RotationDegrees = FVector3( 0, 400, 0 );
		KSpec.Scale = FVector3( 1.5f, 1.5f, 1.5f );
		const int32 K = NWorldEdit::AddObject( T.Document, "K", P, KSpec );
		const FVector3 KWorld = WorldPosition( T.Document, K );
		T.Select( K );
		T.GetGizmoFrame( G.View, Frame );
		const FVector3 GrabK = OnAxis( Frame, 0, 0.5f );
		T.BeginGizmoDrag( G.View, EEditorGizmoHandle::AxisX, G.RayAt( GrabK ) );
		T.UpdateGizmoDrag( G.RayAt( GrabK + FVector3( 2, 0, 0 ) ) );
		T.EndGizmoDrag();
		C.Check( Near3( WorldPosition( T.Document, K ), KWorld + FVector3( 2, 0, 0 ), 5e-3f ), "a child of a rotated, scaled parent moves exactly 2 along the world X (%s -> %s)", *KWorld.ToString(), *WorldPosition( T.Document, K ).ToString() );
		C.Check( Near3( ObjectOrEmpty( T.Document.Get(), K ).Transform.RotationDegrees, FVector3( 0, 400, 0 ), 0.0f ) && Near3( ObjectOrEmpty( T.Document.Get(), K ).Transform.Scale, FVector3( 1.5f, 1.5f, 1.5f ), 0.0f ) && ObjectOrEmpty( T.Document.Get(), K ).Parent == P, "and keeps its own rotation, scale and parent" );

		// 親と子を一緒に選ぶと、子は二重に動かない
		T.SetSelection( Ids( { P, K } ) );
		const FVector3 KWorld2 = WorldPosition( T.Document, K );
		const FVector3 PWorld2 = WorldPosition( T.Document, P );
		T.GetGizmoFrame( G.View, Frame );
		C.Check( Near3( Frame.Origin, PWorld2, 1e-3f ), "the gizmo origin counts only the roots" );
		const FVector3 GrabP = OnAxis( Frame, 0, 0.5f );
		T.BeginGizmoDrag( G.View, EEditorGizmoHandle::AxisX, G.RayAt( GrabP ) );
		T.UpdateGizmoDrag( G.RayAt( GrabP + FVector3( 1, 0, 0 ) ) );
		T.EndGizmoDrag();
		C.Check( Near3( WorldPosition( T.Document, P ), PWorld2 + FVector3( 1, 0, 0 ), 3e-3f ) && Near3( WorldPosition( T.Document, K ), KWorld2 + FVector3( 1, 0, 0 ), 3e-3f ), "a parent and its selected child move together by 1, not 2" );

		// 複数: それぞれ同じだけ動く
		FSessionFixture M;
		CWorldEditorSession& U = M.Session;
		U.Camera = MakeCamera( 0.6f, 0.45f, 10.0f );
		const int32 M1 = Add( U.Document, "M1", WorldNoId, FVector3( 2, 0, 0 ) );
		const int32 M2 = Add( U.Document, "M2", WorldNoId, FVector3( -2, 1, 0 ) );
		U.SetSelection( Ids( { M1, M2 } ) );
		U.GetGizmoFrame( M.View, Frame );
		C.Check( Near3( Frame.Origin, FVector3( 0, 0.5f, 0 ) ), "for several objects the gizmo is at the centre of their positions" );
		const FVector3 GrabM = OnAxis( Frame, 0, 0.5f );
		U.BeginGizmoDrag( M.View, EEditorGizmoHandle::AxisX, M.RayAt( GrabM ) );
		U.UpdateGizmoDrag( M.RayAt( GrabM + FVector3( 1.5f, 0, 0 ) ) );
		U.EndGizmoDrag();
		C.Check( Near3( LocalPosition( U.Document, M1 ), FVector3( 3.5f, 0, 0 ), 3e-3f ) && Near3( LocalPosition( U.Document, M2 ), FVector3( -0.5f, 1, 0 ), 3e-3f ), "each selected object moves by the same 1.5" );

		// 回転: 中心のまわりを回る
		U.GizmoMode = EEditorGizmoMode::Rotate;
		U.bSnap = true;
		U.RotateSnapDegrees = 90.0f;
		U.GetGizmoFrame( M.View, Frame );
		const FVector3 Pivot = Frame.Origin;
		const float32 Radius1 = FVector3::Distance( WorldPosition( U.Document, M1 ), Pivot );
		const float32 Radius2 = FVector3::Distance( WorldPosition( U.Document, M2 ), Pivot );
		FVector3 V0 = FVector3::Zero;
		float32 Best = -1.0e9f;
		for ( int32 i = 0; i < 72; ++i )
		{
			const float32 Angle = NMath::TWO_PI * static_cast<float32>( i ) / 72.0f;
			const FVector3 Point = ( Frame.Axes[2] * std::cos( Angle ) + Frame.Axes[0] * std::sin( Angle ) ) * NWorldEditor::GetGizmoRingRadius( Frame );
			if ( FVector3::Dot( Point, U.Camera.GetEye() ) > Best )
			{
				Best = FVector3::Dot( Point, U.Camera.GetEye() );
				V0 = Point;
			}
		}
		const float32 R = NWorldEditor::GetGizmoRingRadius( Frame );
		const FVector3 Perp = FVector3::Cross( FVector3::Up, V0 ).Normalized();
		U.BeginGizmoDrag( M.View, EEditorGizmoHandle::AxisY, M.RayAt( Pivot + V0 ) );
		U.UpdateGizmoDrag( M.RayAt( Pivot + V0 * std::cos( NMath::HALF_PI ) + Perp * ( R * std::sin( NMath::HALF_PI ) ) ) );
		U.EndGizmoDrag();
		C.Check( std::fabs( FVector3::Distance( WorldPosition( U.Document, M1 ), Pivot ) - Radius1 ) < 2e-3f && std::fabs( FVector3::Distance( WorldPosition( U.Document, M2 ), Pivot ) - Radius2 ) < 2e-3f, "rotating several objects keeps each at its distance from the centre" );
		C.Check( !Near3( WorldPosition( U.Document, M1 ), FVector3( 3.5f, 0, 0 ), 0.1f ) && std::fabs( std::fabs( ObjectOrEmpty( U.Document.Get(), M1 ).Transform.RotationDegrees.y ) - 90.0f ) < 0.1f, "M1 orbited and turned by 90 degrees about Y (rotation %s)", *ObjectOrEmpty( U.Document.Get(), M1 ).Transform.RotationDegrees.ToString() );

		// 拡大（Local）
		FSessionFixture N;
		CWorldEditorSession& W = N.Session;
		W.Camera = MakeCamera( 0.6f, 0.45f, 10.0f );
		FWorldTransform SSpec;
		SSpec.Position = FVector3( 0, 0, 0 );
		SSpec.RotationDegrees = FVector3( 0, 90, 0 );
		SSpec.Scale = FVector3( 1, 2, 3 );
		const int32 Scaled = NWorldEdit::AddObject( W.Document, "Scaled", WorldNoId, SSpec );
		W.Select( Scaled );
		W.GizmoMode = EEditorGizmoMode::Scale;
		W.GizmoSpace = EEditorGizmoSpace::Local;
		W.GetGizmoFrame( N.View, Frame );
		const FVector3 GrabS = OnAxis( Frame, 0, 0.5f );
		W.BeginGizmoDrag( N.View, EEditorGizmoHandle::AxisX, N.RayAt( GrabS ) );
		W.UpdateGizmoDrag( N.RayAt( OnAxis( Frame, 0, 1.0f ) ) );
		W.EndGizmoDrag();
		C.Check( Near3( ObjectOrEmpty( W.Document.Get(), Scaled ).Transform.Scale, FVector3( 1.5f, 2, 3 ), 5e-3f ) && Near3( ObjectOrEmpty( W.Document.Get(), Scaled ).Transform.Position, FVector3::Zero, 0.0f ) && Near3( ObjectOrEmpty( W.Document.Get(), Scaled ).Transform.RotationDegrees, FVector3( 0, 90, 0 ), 0.0f ), "scaling along the local X by half the gizmo length makes X 1.5x; position and rotation are untouched" );
	}

	//=========================================================================
	// アウトライナー
	//=========================================================================
	void TestOutliner( NAssetTest::FChecker& C )
	{
		C.Group = "editor-outliner";
		TWorldDocument Document;
		Document.Reset( FWorldDefinition() );
		const int32 Level = Add( Document, "Level", WorldNoId, FVector3::Zero );
		const int32 Enemies = Add( Document, "Enemies", Level, FVector3::Zero );
		const int32 Goblin = Add( Document, "Goblin", Enemies, FVector3::Zero );
		const int32 Orc = Add( Document, "Orc", Enemies, FVector3::Zero );
		const int32 Lights = Add( Document, "Lights", Level, FVector3::Zero );
		const int32 Torch = Add( Document, "Torch", Lights, FVector3::Zero );
		const int32 Player = Add( Document, "PlayerStart", WorldNoId, FVector3::Zero );
		NWorldEdit::AddTag( Document, Goblin, "Enemy" );
		NWorldEdit::AddTag( Document, Orc, "Enemy" );
		NWorldEdit::AddTag( Document, Orc, "Boss" );
		NWorldEdit::EditObject( Document, Torch )->Model = FWorldModel();
		NWorldEdit::EditObject( Document, Goblin )->Collider = FWorldCollider();

		const TSet<int32> None;
		TArray<FEditorOutlinerRow> Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), None, FString() );
		const auto IdAt = [&]( int32 Index ) { return RowAt( Rows, Index ).ObjectId; };
		C.Check( Rows.Num() == 7 && IdAt( 0 ) == Level && IdAt( 1 ) == Enemies && IdAt( 2 ) == Goblin && IdAt( 3 ) == Orc && IdAt( 4 ) == Lights && IdAt( 5 ) == Torch && IdAt( 6 ) == Player, "rows are depth first, siblings in the order of the world" );
		C.Check( RowAt( Rows, 0 ).Depth == 0 && RowAt( Rows, 1 ).Depth == 1 && RowAt( Rows, 2 ).Depth == 2 && RowAt( Rows, 5 ).Depth == 2 && RowAt( Rows, 6 ).Depth == 0, "with their depth" );
		C.Check( RowAt( Rows, 0 ).bHasChildren && RowAt( Rows, 0 ).bExpanded && !RowAt( Rows, 2 ).bHasChildren && !RowAt( Rows, 2 ).bExpanded && RowAt( Rows, 2 ).bMatch, "has-children / expanded flags" );

		TSet<int32> Collapsed;
		Collapsed.Add( Enemies );
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), Collapsed, FString() );
		C.Check( Rows.Num() == 5 && RowAt( Rows, 1 ).ObjectId == Enemies && RowAt( Rows, 1 ).bHasChildren && !RowAt( Rows, 1 ).bExpanded && RowAt( Rows, 2 ).ObjectId == Lights, "a collapsed object hides its subtree and says it has children" );
		Collapsed.Add( Level );
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), Collapsed, FString() );
		C.Check( Rows.Num() == 2 && RowAt( Rows, 0 ).ObjectId == Level && !RowAt( Rows, 0 ).bExpanded && RowAt( Rows, 1 ).ObjectId == Player, "collapsing the root hides everything below it" );

		// 絞り込み
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), Collapsed, "tag:Enemy" );
		C.Check( Rows.Num() == 4 && IdAt( 0 ) == Level && IdAt( 1 ) == Enemies && IdAt( 2 ) == Goblin && IdAt( 3 ) == Orc, "a tag filter shows the matches and their ancestors, ignoring the collapsed state" );
		C.Check( !RowAt( Rows, 0 ).bMatch && !RowAt( Rows, 1 ).bMatch && RowAt( Rows, 2 ).bMatch && RowAt( Rows, 3 ).bMatch && RowAt( Rows, 0 ).bExpanded, "the ancestors are shown as a path, not as matches" );
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), None, "tag:enemy" );
		C.Check( Rows.IsEmpty(), "tags are case sensitive" );
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), None, "TORCH" );
		C.Check( Rows.Num() == 3 && IdAt( 2 ) == Torch, "names match without regard to case" );
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), None, "comp:model" );
		C.Check( Rows.Num() == 3 && IdAt( 2 ) == Torch, "component filter (case-insensitive)" );
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), None, "comp:Collider tag:Enemy" );
		C.Check( Rows.Num() == 3 && IdAt( 2 ) == Goblin, "several terms are ANDed (only Goblin has a collider and the Enemy tag)" );
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), None, FString::Printf( "#%d", Orc ) );
		C.Check( Rows.Num() == 3 && IdAt( 2 ) == Orc, "#id matches an id" );
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), None, "   " );
		C.Check( Rows.Num() == 7, "a filter of only spaces shows everything" );
		Rows = NWorldEditor::BuildOutlinerRows( Document.Get(), None, "tag:" );
		C.Check( Rows.IsEmpty(), "an empty tag term matches nothing" );

		// 壊れたデータ: 存在しない親、循環
		FWorldDefinition Broken = Document.Get();
		FWorldObject Orphan;
		Orphan.Id = 50;
		Orphan.Name = "Orphan";
		Orphan.Parent = 999;
		Broken.Objects.Add( Orphan );
		FWorldObject CycleA, CycleB;
		CycleA.Id = 51;
		CycleA.Name = "CycleA";
		CycleA.Parent = 52;
		CycleB.Id = 52;
		CycleB.Name = "CycleB";
		CycleB.Parent = 51;
		Broken.Objects.Add( CycleA );
		Broken.Objects.Add( CycleB );
		Rows = NWorldEditor::BuildOutlinerRows( Broken, None, FString() );
		bool bOrphanTop = false, bCycleShown = false;
		for ( const FEditorOutlinerRow& Row : Rows )
		{
			if ( Row.ObjectId == 50 && Row.Depth == 0 ) bOrphanTop = true;
			if ( Row.ObjectId == 51 || Row.ObjectId == 52 ) bCycleShown = true;
		}
		C.Check( bOrphanTop && bCycleShown && Rows.Num() == 10, "an orphan is shown at the top; objects in a cycle are shown too (so they can be found and deleted) (%d rows)", Rows.Num() );

		// 大きなワールド
		FWorldDefinition Big;
		int32 Last = WorldNoId;
		for ( int32 i = 1; i <= 3000; ++i )
		{
			FWorldObject Object;
			Object.Id = i;
			Object.Name = "Obj";
			Object.Parent = i % 10 == 0 ? WorldNoId : Last;
			Last = i;
			Big.Objects.Add( Object );
		}
		Big.NextId = 3001;
		Rows = NWorldEditor::BuildOutlinerRows( Big, None, FString() );
		C.Check( Rows.Num() == 3000, "a world of 3000 objects in chains of 10 lists every object once" );
	}

	//=========================================================================
	// 置き方
	//=========================================================================
	void TestPlacement( NAssetTest::FChecker& C )
	{
		C.Group = "editor-placement";
		C.Check( NWorldEditor::ClassifyAsset( "Models/Torch.mv1" ) == EEditorAssetKind::Model && NWorldEditor::ClassifyAsset( "a.FBX" ) == EEditorAssetKind::Model && NWorldEditor::ClassifyAsset( "a.obj" ) == EEditorAssetKind::Model && NWorldEditor::ClassifyAsset( "a.pmx" ) == EEditorAssetKind::Model, "model extensions" );
		C.Check( NWorldEditor::ClassifyAsset( "Tex/a.png" ) == EEditorAssetKind::Texture && NWorldEditor::ClassifyAsset( "Tex/b.JPG" ) == EEditorAssetKind::Texture && NWorldEditor::ClassifyAsset( "c.bmp" ) == EEditorAssetKind::Texture && NWorldEditor::ClassifyAsset( "d.tga" ) == EEditorAssetKind::Texture, "image extensions, in any case" );
		C.Check( NWorldEditor::ClassifyAsset( "s/a.wav" ) == EEditorAssetKind::Sound && NWorldEditor::ClassifyAsset( "b.OGG" ) == EEditorAssetKind::Sound && NWorldEditor::ClassifyAsset( "c.mp3" ) == EEditorAssetKind::Sound, "sound extensions" );
		C.Check( NWorldEditor::ClassifyAsset( "readme.txt" ) == EEditorAssetKind::Unknown && NWorldEditor::ClassifyAsset( "noext" ) == EEditorAssetKind::Unknown && NWorldEditor::ClassifyAsset( "" ) == EEditorAssetKind::Unknown && NWorldEditor::ClassifyAsset( "dir.png/file" ) == EEditorAssetKind::Unknown, "other files (and a dot only in a folder name)" );
		C.Check( NWorldEditor::GetAssetDisplayName( "Models/Torch.mv1" ) == "Torch" && NWorldEditor::GetAssetDisplayName( "Models\\Props\\Big.crate.fbx" ) == "Big.crate" && NWorldEditor::GetAssetDisplayName( "plain" ) == "plain", "display names drop the folders and the last extension" );

		FSessionFixture F;
		const FEditorShapeContext Context = F.Context();
		FWorldDefinition World;
		World.Objects.Add( [] { FWorldObject Object; Object.Id = 1; Object.Name = "Crate"; FWorldCollider Collider; Collider.Shape = EWorldColliderShape::Box; Collider.Size = FVector3( 2, 2, 2 ); Collider.Offset = FVector3( 0, 1, 0 ); Object.Collider = Collider; Object.Transform.Position = FVector3( 0, 0, 10 ); return Object; }() );
		World.NextId = 2;

		// 地面
		FEditorCamera Camera = MakeCamera( 0.0f, 0.5f, 10.0f );
		FEditorPlacementOptions Options;
		FEditorPlacement Placement = NWorldEditor::ComputePlacement( Camera.ScreenToRay( F.View, 400, 300 ), nullptr, Context, Options );
		C.Check( Placement.bHitGround && !Placement.bHitObject && std::fabs( Placement.Position.y ) < 1e-6f && std::fabs( Placement.Position.x ) < 1e-3f && std::fabs( Placement.Position.z ) < 1e-3f, "looking at the target on the ground places it at the target (%s)", *Placement.Position.ToString() );
		Options.GroundY = 3.0f;
		Placement = NWorldEditor::ComputePlacement( Camera.ScreenToRay( F.View, 400, 100 ), nullptr, Context, Options );
		C.Check( Placement.bHitGround && Placement.Position.y == 3.0f, "a different ground height" );
		Options = FEditorPlacementOptions();
		Options.SnapStep = 1.0f;
		const FEditorRay Slanted = Camera.ScreenToRay( F.View, 480, 280 );
		Placement = NWorldEditor::ComputePlacement( Slanted, nullptr, Context, Options );
		C.Check( std::fabs( Placement.Position.x - std::round( Placement.Position.x ) ) < 1e-6f && std::fabs( Placement.Position.z - std::round( Placement.Position.z ) ) < 1e-6f && Placement.Position.y == 0.0f, "snapping rounds X and Z to the step (%s)", *Placement.Position.ToString() );

		// 空の方を向いている: 地面に当たらないので、一定の距離
		const FEditorCamera Up = MakeCamera( 0.0f, -0.5f, 10.0f, FVector3( 0, 20, 0 ) );   // 地面より高いところから、空を見上げる
		Placement = NWorldEditor::ComputePlacement( Up.ScreenToRay( F.View, 400, 300 ), nullptr, Context, FEditorPlacementOptions() );
		C.Check( !Placement.bHitGround && !Placement.bHitObject && std::fabs( FVector3::Distance( Placement.Position, Up.GetEye() ) - 8.0f ) < 1e-3f, "a ray that misses the ground is placed 8 units from the eye" );

		// 既存のものの表面（地面より手前）
		const FEditorCamera Level = MakeCamera( 0.0f, 0.2f, 20.0f, FVector3( 0, 1, 10 ) );
		Placement = NWorldEditor::ComputePlacement( Level.ScreenToRay( F.View, 400, 300 ), &World, Context, FEditorPlacementOptions() );
		C.Check( Placement.bHitObject && Placement.HitObjectId == 1 && Placement.Position.y > 1.0f, "aiming at a crate puts the new object on the crate's surface, not on the ground behind it (%s)", *Placement.Position.ToString() );
		FEditorPlacementOptions NoSurface;
		NoSurface.bOnSurfaces = false;
		Placement = NWorldEditor::ComputePlacement( Level.ScreenToRay( F.View, 400, 300 ), &World, Context, NoSurface );
		C.Check( Placement.bHitGround && !Placement.bHitObject, "with surfaces off, only the ground counts" );

		// アセットを置く
		TWorldDocument Document;
		Document.Reset( FWorldDefinition() );
		FFakeEditorAssets Assets;
		Assets.Textures.Add( { "Tex/tall.png", 32, 64 } );
		const int32 Base = Document.GetNumUndo();
		const int32 Model = NWorldEditor::AddAssetObject( Document, "Models\\Props\\Torch.mv1", WorldNoId, FVector3( 1, 2, 3 ), &Assets );
		C.Check( Model != WorldNoId && ObjectOrEmpty( Document.Get(), Model ).Name == "Torch" && ObjectOrEmpty( Document.Get(), Model ).Model.Get( FWorldModel() ).Path == "Models/Props/Torch.mv1" && Near3( LocalPosition( Document, Model ), FVector3( 1, 2, 3 ) ) && Document.GetNumUndo() == Base + 1, "AddAssetObject: a model, one history entry" );
		const int32 Tall = NWorldEditor::AddAssetObject( Document, "Tex/tall.png", WorldNoId, FVector3::Zero, &Assets );
		C.Check( std::fabs( ObjectOrEmpty( Document.Get(), Tall ).Billboard.Get( FWorldBillboard() ).Size.x - 0.5f ) < 1e-5f && std::fabs( ObjectOrEmpty( Document.Get(), Tall ).Billboard.Get( FWorldBillboard() ).Size.y - 1.0f ) < 1e-5f, "a tall image is 0.5 wide and 1 high" );
		const int32 NoInfo = NWorldEditor::AddAssetObject( Document, "Tex/unknown.png", WorldNoId, FVector3::Zero, nullptr );
		C.Check( std::fabs( ObjectOrEmpty( Document.Get(), NoInfo ).Billboard.Get( FWorldBillboard() ).Size.x - 1.0f ) < 1e-5f, "without a size the billboard is square" );
		const int32 UndoBefore = Document.GetNumUndo();
		C.Check( NWorldEditor::AddAssetObject( Document, "x.txt", WorldNoId, FVector3::Zero, &Assets ) == WorldNoId && NWorldEditor::AddAssetObject( Document, "Models/a.mv1", 999, FVector3::Zero, &Assets ) == WorldNoId && Document.GetNumUndo() == UndoBefore, "an unknown kind or a missing parent adds nothing and leaves no history" );

		// 親の下に置くと、ワールドの位置が指定どおり
		FWorldTransform HolderSpec;
		HolderSpec.Position = FVector3( 100, 0, 0 );
		HolderSpec.RotationDegrees = FVector3( 0, 90, 0 );
		HolderSpec.Scale = FVector3( 2, 4, 2 );
		const int32 Holder = NWorldEdit::AddObject( Document, "Holder", WorldNoId, HolderSpec );
		const int32 Under = NWorldEditor::AddAssetObject( Document, "Models/a.mv1", Holder, FVector3( 101, 4, 2 ), &Assets );
		C.Check( Under != WorldNoId && ObjectOrEmpty( Document.Get(), Under ).Parent == Holder && Near3( WorldPosition( Document, Under ), FVector3( 101, 4, 2 ), 5e-3f ), "placed under a rotated, scaled parent, the object is at the requested world position (%s)", *WorldPosition( Document, Under ).ToString() );
		const int32 EmptyUnder = NWorldEditor::AddEmptyObject( Document, "Spawn", Holder, FVector3( 100, 1, 1 ) );
		C.Check( Near3( WorldPosition( Document, EmptyUnder ), FVector3( 100, 1, 1 ), 5e-3f ) && NWorldEditor::AddEmptyObject( Document, "x", 999, FVector3::Zero ) == WorldNoId, "an empty object likewise; a missing parent fails" );
		const int32 Box = NWorldEditor::AddColliderObject( Document, EWorldColliderShape::Sphere, false, WorldNoId, FVector3::Zero );
		C.Check( ObjectOrEmpty( Document.Get(), Box ).Name == "Sphere" && ObjectOrEmpty( Document.Get(), Box ).Collider.Get( FWorldCollider() ).Shape == EWorldColliderShape::Sphere && !ObjectOrEmpty( Document.Get(), Box ).Collider.Get( FWorldCollider() ).bTrigger, "a collider object is named for its shape" );
	}
}

namespace
{
	void TestColliderTools( NAssetTest::FChecker& C )
	{
		C.Group = "editor-collider-tools";
		FSessionFixture F;
		CWorldEditorSession& S = F.Session;
		F.Assets.Models.Add( { "Models/Off.mv1", FVector3( 0, 0, 0 ), FVector3( 4, 2, 2 ) } );

		FWorldDefinition World;
		const auto Add = [&]( int32 Id, const char* Name, const char* ModelPath, const char* Tag, bool bEnabled = true )
		{
			FWorldObject Object;
			Object.Id = Id;
			Object.Name = Name;
			Object.bEnabled = bEnabled;
			if ( ModelPath != nullptr )
			{
				FWorldModel Model;
				Model.Path = ModelPath;
				Object.Model = Model;
			}
			if ( Tag != nullptr ) Object.AddTag( Tag );
			World.Objects.Add( Object );
		};
		Add( 1, "Crate", "Models/Box.mv1", "Wall" );
		Add( 2, "Rock", "Models/Off.mv1", "Wall" );
		FWorldCollider Sphere;
		Sphere.Shape = EWorldColliderShape::Sphere;
		Sphere.Radius = 9.0f;
		Sphere.bTrigger = true;
		World.Objects[1].Collider = Sphere;
		Add( 3, "Empty", nullptr, "Wall" );
		Add( 4, "Hidden", "Models/Box.mv1", "Wall", false );
		Add( 5, "Meshy", nullptr, nullptr );
		FWorldCollider Mesh;
		Mesh.Shape = EWorldColliderShape::Mesh;
		Mesh.Mesh = "Models/Off.mv1";
		World.Objects[4].Collider = Mesh;
		Add( 6, "Other", nullptr, "Floor" );
		World.NextId = 7;
		S.Reset( World, "" );
		const auto Ids = []( std::initializer_list<int32> List ) { TArray<int32> Result; for ( const int32 Id : List ) Result.Add( Id ); return Result; };

		// タグで選ぶ
		C.Check( S.SelectByTag( "Wall" ) == 4 && S.GetSelection() == Ids( { 1, 2, 3, 4 } ), "select by tag: every object with the tag, including a disabled one" );
		C.Check( S.SelectByTag( "wall" ) == 0 && S.GetSelection().IsEmpty(), "tags are case sensitive; nothing found clears the selection" );
		S.Select( 6 );
		C.Check( S.SelectByTag( "Nothing", CWorldEditorSession::ESelectMode::Add ) == 0 && S.GetSelection() == Ids( { 6 } ), "adding nothing keeps the selection" );
		C.Check( S.SelectByTag( "Wall", CWorldEditorSession::ESelectMode::Add ) == 4 && S.GetSelection().Num() == 5, "adding a tag's objects to the selection" );
		C.Check( !S.Document.CanUndo(), "selecting is not an undo step" );

		// モデルに合わせる
		S.SetSelection( Ids( { 1, 2, 3 } ) );
		C.Check( S.FitSelectionColliders( EWorldColliderShape::Box, F.Assets ) == 2, "fit a box to the selection: the 2 objects that have a model" );
		const FWorldCollider Crate = ObjectOrEmpty( S.Document.Get(), 1 ).Collider.Get( FWorldCollider() );
		C.Check( ObjectOrEmpty( S.Document.Get(), 1 ).Collider.IsSet() && Crate.Shape == EWorldColliderShape::Box && Near3( Crate.Size, FVector3( 2, 2, 2 ) ) && Near3( Crate.Offset, FVector3( 0, 1, 0 ) ) && !Crate.bTrigger, "a crate gets a box of its bounds" );
		const FWorldCollider Rock = ObjectOrEmpty( S.Document.Get(), 2 ).Collider.Get( FWorldCollider() );
		C.Check( Rock.Shape == EWorldColliderShape::Box && Near3( Rock.Size, FVector3( 4, 2, 2 ) ) && Near3( Rock.Offset, FVector3( 2, 1, 1 ) ) && Rock.bTrigger, "a rock's existing collider changes shape, and stays a trigger" );
		C.Check( !ObjectOrEmpty( S.Document.Get(), 3 ).Collider.IsSet(), "an object with no model is left alone" );
		C.Check( S.Document.Undo() && !ObjectOrEmpty( S.Document.Get(), 1 ).Collider.IsSet() && ObjectOrEmpty( S.Document.Get(), 2 ).Collider.Get( FWorldCollider() ).Shape == EWorldColliderShape::Sphere
					 && std::fabs( ObjectOrEmpty( S.Document.Get(), 2 ).Collider.Get( FWorldCollider() ).Radius - 9.0f ) < 1e-5f && !S.Document.CanUndo(),
				 "one undo takes both back" );

		const uint32 Revision = S.Document.GetRevision();
		S.SetSelection( Ids( { 3, 6 } ) );
		C.Check( S.FitSelectionColliders( EWorldColliderShape::Sphere, F.Assets ) == 0 && S.Document.GetRevision() == Revision && !S.Document.CanUndo(), "nothing to fit: nothing changes, and there is no undo step" );

		S.SetSelection( Ids( { 1, 2 } ) );
		S.FitSelectionColliders( EWorldColliderShape::Sphere, F.Assets );
		C.Check( std::fabs( ObjectOrEmpty( S.Document.Get(), 1 ).Collider.Get( FWorldCollider() ).Radius - 1.0f ) < 1e-5f && std::fabs( ObjectOrEmpty( S.Document.Get(), 2 ).Collider.Get( FWorldCollider() ).Radius - 2.0f ) < 1e-5f
					 && Near3( ObjectOrEmpty( S.Document.Get(), 2 ).Collider.Get( FWorldCollider() ).Offset, FVector3( 2, 1, 1 ) ),
				 "a sphere: half of the longest side, centred on the bounds" );
		S.FitSelectionColliders( EWorldColliderShape::Capsule, F.Assets );
		const FWorldCollider RockCapsule = ObjectOrEmpty( S.Document.Get(), 2 ).Collider.Get( FWorldCollider() );
		C.Check( RockCapsule.Shape == EWorldColliderShape::Capsule && std::fabs( RockCapsule.Radius - 2.0f ) < 1e-5f && std::fabs( RockCapsule.Height - 4.0f ) < 1e-5f, "a capsule (%.2f, %.2f)", RockCapsule.Radius, RockCapsule.Height );

		// メッシュ: 三角形は実行時にモデルから取るので、大きさは要らない（モデル、またはファイルが要る）
		S.SetSelection( Ids( { 1, 3, 5 } ) );
		C.Check( S.FitSelectionColliders( EWorldColliderShape::Mesh, F.Assets ) == 2, "mesh: the object with a model and the one with its own file; not the one with neither" );
		C.Check( ObjectOrEmpty( S.Document.Get(), 1 ).Collider.Get( FWorldCollider() ).Shape == EWorldColliderShape::Mesh && ObjectOrEmpty( S.Document.Get(), 1 ).Collider.Get( FWorldCollider() ).Mesh.IsEmpty()
					 && ObjectOrEmpty( S.Document.Get(), 5 ).Collider.Get( FWorldCollider() ).Mesh == "Models/Off.mv1" && !ObjectOrEmpty( S.Document.Get(), 3 ).Collider.IsSet(),
				 "the model's own triangles are used unless a file is named" );

		// メッシュだったものを箱にすると、そのファイルの大きさになり、ファイルの指定は消える
		S.SetSelection( Ids( { 5 } ) );
		C.Check( S.FitSelectionColliders( EWorldColliderShape::Box, F.Assets ) == 1, "a mesh collider can be turned into a box" );
		const FWorldCollider Meshy = ObjectOrEmpty( S.Document.Get(), 5 ).Collider.Get( FWorldCollider() );
		C.Check( Meshy.Shape == EWorldColliderShape::Box && Near3( Meshy.Size, FVector3( 4, 2, 2 ) ) && Meshy.Mesh.IsEmpty(), "using the bounds of the file it took its triangles from, and forgetting the file" );
	}
}

void NWorldTest::RunEditorSessionTests( NAssetTest::FChecker& C )
{
	TestDocumentBatch( C );
	TestHierarchyExtras( C );
	TestMultiEdit( C );
	TestSelection( C );
	TestSessionPicking( C );
	TestSessionEdit( C );
	TestSessionGizmo( C );
	TestOutliner( C );
	TestPlacement( C );
	TestColliderTools( C );
}
