#include <algorithm>
#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/World/Editor/EditorGizmo.h"
#include "Library/Framework/World/Editor/EditorMeshPicking.h"
#include "Library/Framework/World/Editor/EditorPlacement.h"
#include "Library/Framework/World/Editor/EditorSession.h"
#include "Library/Framework/World/Mesh/EditMeshBuild.h"
#include "Library/Framework/World/Mesh/EditMeshOps.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldCollision.h"
#include "Library/Framework/World/WorldHierarchy.h"
#include "WorldEditorSelfTestSupport.h"
#include "WorldMeshSelfTestSupport.h"

using namespace NAssetTest;
using namespace NWorldEditorTest;
using namespace NWorldMeshTest;
using NEditMesh::EEditMeshElement;
using ESelectMode = CWorldEditorSession::ESelectMode;

//=============================================================================
// 編集メッシュの、エディタの部分（作る・モデルから変える・始める / 終わる・選ぶ・操作・ギズモ）の確認
//   DxLib も ImGui も使わない。選ぶのは、カメラで投影した画面の点をクリックする形で確かめる。
//=============================================================================
namespace
{
	bool Near( float32 A, float32 B, float32 Tolerance = 1.0e-3f ) { return NMath::Abs( A - B ) <= Tolerance; }

	TArray<int32> Ints( std::initializer_list<int32> List )
	{
		TArray<int32> Result;
		for ( const int32 Value : List ) Result.Add( Value );
		return Result;
	}

	int32 FindVertex( const FEditMesh& Mesh, const FVector3& Position )
	{
		for ( int32 i = 0; i < Mesh.GetNumVertices(); ++i )
		{
			if ( Near3( Mesh.Vertices[i], Position, 1.0e-3f ) ) return i;
		}
		return INDEX_NONE;
	}

	FEditMeshSelection Chosen( std::initializer_list<int32> Vertices, std::initializer_list<int32> Faces = {} )
	{
		FEditMeshSelection Selection;
		for ( const int32 Item : Vertices ) Selection.Vertices.Add( Item );
		for ( const int32 Item : Faces ) Selection.Faces.Add( Item );
		return Selection;
	}

	// 箱（2 x 2 x 2）を 1 つ作って、編集を始めたセッション
	struct FMeshFixture
	{
		CWorldEditorSession Session;
		FEditorViewport View = MakeViewport();
		int32 Id = WorldNoId;

		explicit FMeshFixture( const FVector3& Position = FVector3::Zero )
		{
			Session.Reset( FWorldDefinition(), "" );
			Session.Camera = MakeCamera( 0.0f, 0.5f, 10.0f, Position + FVector3( 0.0f, 1.0f, 0.0f ) );
			Id = Session.CreateMesh( EEditMeshPrimitive::Box, Position );
			Session.BeginMeshEdit( Id );
		}

		const FEditMesh& Mesh() const { return Session.Document.Get().FindObject( Id )->Mesh.GetValue(); }
		FVector3 ToWorld( const FVector3& Local ) const { return Session.GetEditedWorldTransform().TransformPoint( Local ); }
		FVector2 Project( const FVector3& Local ) const
		{
			FVector2 Pixel( -1.0f, -1.0f );
			Session.Camera.WorldToScreen( View, ToWorld( Local ), Pixel );
			return Pixel;
		}
		int32 V( float32 X, float32 Y, float32 Z ) const { return FindVertex( Mesh(), FVector3( X, Y, Z ) ); }
	};

	//=========================================================================
	// 作る・モデルから変える
	//=========================================================================
	void TestCreate( FChecker& C )
	{
		C.Group = "mesh-editor-create";
		CWorldEditorSession S;
		S.Reset( FWorldDefinition(), "" );
		const int32 Id = S.CreateMesh( EEditMeshPrimitive::Box, FVector3( 3, 0, 2 ) );
		const FWorldObject& Object = ObjectOrEmpty( S.Document.Get(), Id );
		C.Check( Id != WorldNoId && Object.Name == "Box" && Object.Mesh.IsSet() && Object.Mesh.GetValue().GetNumVertices() == 8 && Object.Collider.IsSet() && Object.Collider.GetValue().Shape == EWorldColliderShape::Mesh && NWorld::UsesEditMeshCollider( Object ) && Near3( Object.Transform.ToTransform().Position, FVector3( 3, 0, 2 ) )
					 && S.GetSelection() == Ints( { Id } ),
				 "a mesh object is a box on its own, with a mesh collider that uses it, at the position, and selected" );
		C.Check( S.Document.Undo() && S.Document.Get().Objects.IsEmpty() && !S.Document.CanUndo(), "creating it is one undo step" );
		S.Document.Redo();
		const int32 NoCollider = S.CreateMesh( EEditMeshPrimitive::Sphere, FVector3::Zero, WorldNoId, false );
		C.Check( ObjectOrEmpty( S.Document.Get(), NoCollider ).Name == "Sphere" && !ObjectOrEmpty( S.Document.Get(), NoCollider ).Collider.IsSet() && ObjectOrEmpty( S.Document.Get(), NoCollider ).Mesh.IsSet(), "a mesh without a collider can be asked for" );
		C.Check( S.CreateMesh( EEditMeshPrimitive::Box, FVector3::Zero, 99 ) == WorldNoId, "a parent that does not exist makes nothing" );
		bool bAll = true;
		for ( int32 i = 0; i < NEditMesh::NumPrimitives; ++i )
		{
			const int32 Made = S.CreateMesh( static_cast<EEditMeshPrimitive>( i ), FVector3::Zero );
			bAll = bAll && ObjectOrEmpty( S.Document.Get(), Made ).Mesh.IsSet() && ObjectOrEmpty( S.Document.Get(), Made ).Name == NEditMesh::ToString( static_cast<EEditMeshPrimitive>( i ) );
		}
		C.Check( bAll, "every kind of primitive can be created, named after its kind" );

		// 親の下: ワールドの位置を保つ
		const int32 Parent = S.CreateEmpty( FVector3( 10, 0, 0 ) );
		NWorldEdit::EditObject( S.Document, Parent )->Transform.Scale = FVector3( 2, 2, 2 );
		const int32 Child = S.CreateMesh( EEditMeshPrimitive::Box, FVector3( 12, 0, 0 ), Parent );
		C.Check( Near3( NWorld::ComputeWorldTransform( S.Document.Get(), Child ).Position, FVector3( 12, 0, 0 ) ) && ObjectOrEmpty( S.Document.Get(), Child ).Parent == Parent, "under a parent, the world position stays (a parent at (10,0,0) with scale 2 gives a local x of 1)" );

		// モデルから変える
		C.Group = "mesh-editor-convert";
		FFakeEditorAssets Assets;
		const FEditMeshBuild Cube = NEditMesh::BuildRenderMesh( NEditMesh::MakeBox( FVector3( 2, 2, 2 ) ) );
		FEditorCollisionMesh CubeMesh;
		for ( const FMeshVertex& Vertex : Cube.Vertices ) CubeMesh.Vertices.Add( FVector3( Vertex.Position[0], Vertex.Position[1], Vertex.Position[2] ) );
		CubeMesh.Indices = Cube.Indices;
		Assets.Meshes.Add( { "Models/Cube.mv1", CubeMesh } );
		CWorldEditorSession T;
		T.Reset( FWorldDefinition(), "" );
		const int32 Crate = T.CreateEmpty( FVector3( 1, 0, 0 ), "Crate" );
		FWorldModel Model;
		Model.Path = "Models/Cube.mv1";
		Model.Clip = "Idle";
		NWorldEdit::EditObject( T.Document, Crate )->Model = Model;
		FWorldCollider Collider;
		Collider.Shape = EWorldColliderShape::Mesh;
		NWorldEdit::EditObject( T.Document, Crate )->Collider = Collider;
		T.Document.EndEditSession();
		const FWorldObject* Before = T.Document.Get().FindObject( Crate );
		C.Check( Before != nullptr && !NWorld::UsesEditMeshCollider( *Before ) && NWorld::GetColliderMeshPath( *Before ) == "Models/Cube.mv1", "before: the mesh collider uses the model's file" );
		C.Check( NWorldEditor::ConvertModelToMesh( T.Document, Crate, Assets ), "a model with triangles converts" );
		const FWorldObject& After = ObjectOrEmpty( T.Document.Get(), Crate );
		C.Check( After.Mesh.IsSet() && !After.Model.IsSet() && After.Mesh.GetValue().GetNumVertices() == 8 && After.Mesh.GetValue().GetNumFaces() == 12 && Near( After.Mesh.GetValue().ComputeVolume(), 8.0f ) && IsClosed( After.Mesh.GetValue() ) && After.Collider.IsSet() && NWorld::UsesEditMeshCollider( After ) && After.Name == "Crate",
				 "the 24 corner vertices of the model weld into 8, 12 triangles, closed, volume 8; the Model is gone, the collider now uses the Mesh, the rest is kept (%d vertices)", After.Mesh.IsSet() ? After.Mesh.GetValue().GetNumVertices() : -1 );
		C.Check( T.Document.Undo() && ObjectOrEmpty( T.Document.Get(), Crate ).Model.IsSet() && !ObjectOrEmpty( T.Document.Get(), Crate ).Mesh.IsSet(), "one undo gives the model back" );
		T.Document.Redo();
		C.Check( !NWorldEditor::ConvertModelToMesh( T.Document, Crate, Assets ) && !NWorldEditor::ConvertModelToMesh( T.Document, 99, Assets ), "a second time (no Model now), or an object that is not there, does nothing" );
		const int32 Plain = T.CreateEmpty( FVector3::Zero, "Plain" );
		const int32 NoMesh = T.CreateEmpty( FVector3::Zero, "NoMesh" );
		FWorldModel Unknown;
		Unknown.Path = "Models/Unknown.mv1";
		NWorldEdit::EditObject( T.Document, NoMesh )->Model = Unknown;
		const int32 Twice = T.CreateEmpty( FVector3::Zero, "Twice" );
		FWorldModel Again;
		Again.Path = "Models/Cube.mv1";
		NWorldEdit::EditObject( T.Document, Twice )->Model = Again;
		NWorldEdit::EditObject( T.Document, Twice )->Mesh = NEditMesh::MakeBox( FVector3::One );
		C.Check( !NWorldEditor::ConvertModelToMesh( T.Document, Plain, Assets ) && !NWorldEditor::ConvertModelToMesh( T.Document, NoMesh, Assets ) && !NWorldEditor::ConvertModelToMesh( T.Document, Twice, Assets ) && ObjectOrEmpty( T.Document.Get(), Twice ).Model.IsSet(),
				 "no Model, a Model whose triangles are unknown, or an object that already has a Mesh: refused, nothing changed" );
		FEditorCollisionMesh Flat;   // 一直線に並んだ 3 つの頂点: 面積のない三角形だけ
		Flat.Vertices.Add( FVector3( 0, 0, 0 ) );
		Flat.Vertices.Add( FVector3( 1, 0, 0 ) );
		Flat.Vertices.Add( FVector3( 2, 0, 0 ) );
		for ( const uint32 Index : { 0u, 1u, 2u } ) Flat.Indices.Add( Index );
		Assets.Meshes.Add( { "Models/Flat.mv1", Flat } );
		const int32 Line = T.CreateEmpty( FVector3::Zero, "Line" );
		FWorldModel LineModel;
		LineModel.Path = "Models/Flat.mv1";
		NWorldEdit::EditObject( T.Document, Line )->Model = LineModel;
		C.Check( !NWorldEditor::ConvertModelToMesh( T.Document, Line, Assets ) && ObjectOrEmpty( T.Document.Get(), Line ).Model.IsSet() && !ObjectOrEmpty( T.Document.Get(), Line ).Mesh.IsSet(), "a model whose triangles have no area gives no mesh: refused, and the model stays" );
		// 選んだものを、まとめて 1 回で
		CWorldEditorSession U;
		U.Reset( FWorldDefinition(), "" );
		TArray<int32> Ids;
		for ( int32 i = 0; i < 3; ++i )
		{
			const int32 Object = U.CreateEmpty( FVector3( static_cast<float32>( i ), 0, 0 ) );
			if ( i != 1 ) NWorldEdit::EditObject( U.Document, Object )->Model = Again;
			Ids.Add( Object );
		}
		U.SetSelection( Ids );
		U.Document.EndEditSession();
		C.Check( U.ConvertSelectionToMeshes( Assets ) == 2 && ObjectOrEmpty( U.Document.Get(), Ids[0] ).Mesh.IsSet() && !ObjectOrEmpty( U.Document.Get(), Ids[1] ).Mesh.IsSet() && ObjectOrEmpty( U.Document.Get(), Ids[2] ).Mesh.IsSet(), "converting the selection: the 2 with a model (and not the middle one)" );
		C.Check( U.Document.Undo() && !ObjectOrEmpty( U.Document.Get(), Ids[0] ).Mesh.IsSet() && !ObjectOrEmpty( U.Document.Get(), Ids[2] ).Mesh.IsSet(), "and one undo takes both back" );
	}

	//=========================================================================
	// 始める・終わる
	//=========================================================================
	void TestBeginEnd( FChecker& C )
	{
		C.Group = "mesh-editor-session";
		FMeshFixture F;
		CWorldEditorSession& S = F.Session;
		C.Check( S.IsMeshEditing() && S.GetMeshObjectId() == F.Id && S.GetSelection() == Ints( { F.Id } ) && S.GetEditedMesh() != nullptr && S.GetMeshSelection().IsEmpty() && S.GetMeshElement() == EEditMeshElement::Vertex, "editing starts on the mesh object, selects it, with nothing chosen, in vertex mode" );
		const int32 Empty = S.CreateEmpty( FVector3( 5, 0, 0 ) );
		C.Check( !S.BeginMeshEdit( Empty ) && !S.BeginMeshEdit( 99 ) && S.GetMeshObjectId() == F.Id, "an object without a Mesh, or one that is not there, cannot be edited (and the edit in progress stays)" );
		S.EndMeshEdit();
		C.Check( !S.IsMeshEditing() && S.GetEditedMesh() == nullptr && S.GetMeshSelection().IsEmpty() && S.GetMeshObjectId() == WorldNoId, "ending leaves nothing" );
		C.Check( S.BeginMeshEdit( F.Id ) && !S.MeshExtrude( 1.0f ) && !S.MeshDeleteSelection() && !S.MeshFlip() && !S.MeshMerge() && !S.MeshSubdivide() && !S.MeshTriangulate() && !S.MeshSetMaterial( 1 ) && !S.MeshWeld( 0.01f ), "with nothing chosen, operations do nothing" );
		const bool bNoHistory = !S.Document.CanUndo() || S.Document.Get().Objects.Num() == 2;
		C.Check( bNoHistory, "and make no history of their own" );

		// 編集中のオブジェクトが無くなった・メッシュを失った・選択から外れた
		FMeshFixture Gone;
		Gone.Session.Document.Undo();   // 作ったことを取り消す
		Gone.Session.SyncSelection();
		C.Check( !Gone.Session.IsMeshEditing(), "when the object disappears (undo of its creation), editing ends" );
		FMeshFixture Lost;
		NWorldEdit::EditObject( Lost.Session.Document, Lost.Id )->Mesh.Reset();
		Lost.Session.SyncSelection();
		C.Check( !Lost.Session.IsMeshEditing(), "when the object loses its Mesh, editing ends" );
		FMeshFixture Left;
		Left.Session.CreateEmpty( FVector3::Zero );
		Left.Session.SyncSelection();
		C.Check( !Left.Session.IsMeshEditing(), "when another object is selected, editing ends" );
		// 番号が減ったあと、選択を直す
		FMeshFixture Shrunk;
		Shrunk.Session.SetMeshElement( EEditMeshElement::Vertex );
		Shrunk.Session.MeshSelectAll();
		NWorldEdit::EditObject( Shrunk.Session.Document, Shrunk.Id )->Mesh.GetValue() = NEditMesh::MakeRamp( FVector3::One );
		Shrunk.Session.SyncSelection();
		C.Check( Shrunk.Session.IsMeshEditing() && Shrunk.Session.GetMeshSelection().Vertices.Num() == 6, "if the mesh changes under the selection (fewer vertices), the selection is cleaned to what still exists (6 of 8)" );
		Shrunk.Session.Reset( FWorldDefinition(), "" );
		C.Check( !Shrunk.Session.IsMeshEditing() && Shrunk.Session.GetMeshSelection().IsEmpty(), "Reset ends editing" );
	}

	//=========================================================================
	// 選ぶ
	//=========================================================================
	void TestPicking( FChecker& C )
	{
		C.Group = "mesh-editor-pick";
		FMeshFixture F;
		CWorldEditorSession& S = F.Session;
		const FEditMesh Mesh = F.Mesh();
		const FTransform World = S.GetEditedWorldTransform();
		// 視点が箱のどちら側（z の符号）にあるか。手前の面（-z か +z）と、奥の底の角は、それで決まる（視点は x = 0 の上）
		const float32 FrontZ = S.Camera.GetEye().z < 0.0f ? -1.0f : 1.0f, BackZ = -FrontZ;
		const int32 FrontFace = FrontZ < 0.0f ? 5 : 4;

		// 頂点: 画面の頂点の位置をクリックする
		const int32 FrontTop = F.V( -1, 2, FrontZ );
		const FVector2 Pixel = F.Project( FVector3( -1, 2, FrontZ ) );
		FEditMeshHit Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Pixel.x + 2.0f, Pixel.y - 1.0f, EEditMeshElement::Vertex );
		C.Check( Hit.bHit && Hit.Index == FrontTop && Hit.Distance < 3.0f && Hit.Element == EEditMeshElement::Vertex, "clicking next to a vertex picks it (%d)", Hit.Index );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Pixel.x + 40.0f, Pixel.y + 40.0f, EEditMeshElement::Vertex );
		C.Check( !Hit.bHit, "clicking far from every vertex picks nothing" );
		// 近い方
		FEditMeshPickOptions Wide;
		Wide.VertexRadius = 300.0f;
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Pixel.x, Pixel.y, EEditMeshElement::Vertex, Wide );
		C.Check( Hit.bHit && Hit.Index == FrontTop, "with a big radius the nearest vertex is still the one clicked" );

		// 隠れた頂点（奥の底の角）は、既定では選ばない
		const int32 Hidden = F.V( 1, 0, BackZ );
		const FVector2 HiddenPixel = F.Project( FVector3( 1, 0, BackZ ) );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, HiddenPixel.x, HiddenPixel.y, EEditMeshElement::Vertex );
		C.Check( !Hit.bHit || Hit.Index != Hidden, "a vertex hidden behind the box is not picked by default" );
		FEditMeshPickOptions XRay;
		XRay.bOnlyVisible = false;
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, HiddenPixel.x, HiddenPixel.y, EEditMeshElement::Vertex, XRay );
		C.Check( Hit.bHit && Hit.Index == Hidden, "but it is with visibility switched off" );
		C.Check( !NWorldEditor::IsMeshPointVisible( Mesh, World, S.Camera.GetEye(), World.TransformPoint( FVector3( 1, 0, BackZ ) ) ) && NWorldEditor::IsMeshPointVisible( Mesh, World, S.Camera.GetEye(), World.TransformPoint( FVector3( -1, 2, FrontZ ) ) )
					 && NWorldEditor::IsMeshPointVisible( Mesh, World, S.Camera.GetEye(), World.TransformPoint( FVector3( 0, 2, 0 ) ) ) && NWorldEditor::IsMeshPointVisible( Mesh, World, S.Camera.GetEye(), World.TransformPoint( FVector3( 0, 5, 0 ) ) ),
				 "visibility: the hidden corner is hidden; a corner that faces the eye, the middle of the top, and a point in the air are visible" );

		// 辺: 辺の真ん中をクリックする
		const int64 TopFrontEdge = NEditMesh::MakeEdgeKey( F.V( -1, 2, FrontZ ), F.V( 1, 2, FrontZ ) );
		const FVector2 Middle = F.Project( FVector3( 0, 2, FrontZ ) );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Middle.x, Middle.y + 3.0f, EEditMeshElement::Edge );
		C.Check( Hit.bHit && Hit.Edge == TopFrontEdge && Hit.Distance < 4.0f && Hit.Element == EEditMeshElement::Edge, "clicking near the middle of an edge picks that edge (%d pixels)", static_cast<int32>( Hit.Distance ) );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Middle.x, Middle.y + 3.0f, EEditMeshElement::Edge, FEditMeshPickOptions() );
		const FVector2 HiddenMiddle = F.Project( FVector3( 0, 0, BackZ ) );
		const FEditMeshHit HiddenEdge = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, HiddenMiddle.x, HiddenMiddle.y, EEditMeshElement::Edge );
		C.Check( !HiddenEdge.bHit || HiddenEdge.Edge != NEditMesh::MakeEdgeKey( F.V( -1, 0, BackZ ), F.V( 1, 0, BackZ ) ), "an edge hidden behind the box (the bottom back edge) is not picked" );

		// 面: 光線が当たる、手前の面
		const FVector2 TopCenter = F.Project( FVector3( 0, 2, 0 ) );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, TopCenter.x, TopCenter.y, EEditMeshElement::Face );
		C.Check( Hit.bHit && Hit.Index == 0 && Hit.Distance > 1.0f && Hit.Element == EEditMeshElement::Face, "clicking the middle of the top picks the top face (%d)", Hit.Index );
		const FVector2 FrontCenter = F.Project( FVector3( 0, 1, FrontZ ) );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, FrontCenter.x, FrontCenter.y, EEditMeshElement::Face );
		C.Check( Hit.bHit && Hit.Index == FrontFace, "and the middle of the front picks the face towards the eye (%d)", Hit.Index );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, 2.0f, 2.0f, EEditMeshElement::Face );
		C.Check( !Hit.bHit, "a click on the empty corner of the screen picks no face" );

		// 半径: 頂点は 9、辺は 6 ピクセル
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Pixel.x + 7.5f, Pixel.y, EEditMeshElement::Vertex );
		C.Check( Hit.bHit && Hit.Index == FrontTop, "a vertex is picked from 7.5 pixels away (the radius is 9)" );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Pixel.x + 10.5f, Pixel.y, EEditMeshElement::Vertex );
		C.Check( !Hit.bHit, "but not from 10.5 pixels away" );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Middle.x, Middle.y + 5.0f, EEditMeshElement::Edge );
		C.Check( Hit.bHit && Hit.Edge == TopFrontEdge, "an edge is picked from 5 pixels away (the radius is 6)" );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Middle.x, Middle.y + 7.5f, EEditMeshElement::Edge );
		C.Check( !Hit.bHit || Hit.Edge != TopFrontEdge, "but not from 7.5 pixels away" );
		// 辺の延長線上（端から 2 m 先）は、辺に当たらない
		const FVector2 Beyond = F.Project( FVector3( 3, 2, FrontZ ) );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, Beyond.x, Beyond.y, EEditMeshElement::Edge );
		C.Check( !Hit.bHit, "the extension of an edge beyond its end does not pick it" );
		// 面: 見える判定を切っても選べる・距離は視点から当たった点まで
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, TopCenter.x, TopCenter.y, EEditMeshElement::Face, XRay );
		C.Check( Hit.bHit && Hit.Index == 0, "a face is picked with visibility switched off too" );
		Hit = NWorldEditor::PickMeshElement( Mesh, World, S.Camera, F.View, TopCenter.x, TopCenter.y, EEditMeshElement::Face );
		C.Check( Hit.bHit && Near( Hit.Distance, FVector3::Distance( S.Camera.GetEye(), World.TransformPoint( FVector3( 0, 2, 0 ) ) ), 0.05f ), "the distance of a picked face is from the eye to the point hit (%.2f)", Hit.Distance );
		// 回したオブジェクト: 光線は、オブジェクト自身の軸へ移して当てる
		FMeshFixture Turned;
		NWorldEdit::EditObject( Turned.Session.Document, Turned.Id )->Transform.RotationDegrees = FVector3( 0, 90, 0 );
		Turned.Session.Document.EndEditSession();
		const FVector2 TurnedTop = Turned.Project( FVector3( 0, 2, 0 ) );
		Hit = NWorldEditor::PickMeshElement( Turned.Mesh(), Turned.Session.GetEditedWorldTransform(), Turned.Session.Camera, Turned.View, TurnedTop.x, TurnedTop.y, EEditMeshElement::Face );
		C.Check( Hit.bHit && Hit.Index == 0, "a click on the top of a turned object picks the top face (the ray goes into the object's own axes)" );
		// 視点の後ろの面は、当たらず、前の点を隠さない
		const FEditorRay CenterRay = S.Camera.ScreenToRay( F.View, F.View.Width * 0.5f, F.View.Height * 0.5f );
		const FVector3 SideRaw = FVector3::Cross( FVector3::Up, CenterRay.Direction );
		const FVector3 RayRight = SideRaw / SideRaw.Length();
		const FVector3 RayUp = FVector3::Cross( CenterRay.Direction, RayRight );
		const FVector3 BehindCenter = CenterRay.Origin - CenterRay.Direction * 2.5f;
		FEditMesh Behind;
		Behind.Vertices.Add( BehindCenter - RayRight * 3.0f - RayUp * 3.0f );
		Behind.Vertices.Add( BehindCenter - RayRight * 3.0f + RayUp * 3.0f );
		Behind.Vertices.Add( BehindCenter + RayRight * 3.0f + RayUp * 3.0f );
		Behind.Vertices.Add( BehindCenter + RayRight * 3.0f - RayUp * 3.0f );
		FEditMeshFace BehindFace;
		for ( int32 i = 0; i < 4; ++i ) BehindFace.Indices.Add( i );
		Behind.Faces.Add( BehindFace );
		Hit = NWorldEditor::PickMeshElement( Behind, FTransform::Identity, S.Camera, F.View, F.View.Width * 0.5f, F.View.Height * 0.5f, EEditMeshElement::Face );
		C.Check( !Hit.bHit, "a face behind the eye is not picked" );
		C.Check( NWorldEditor::IsMeshPointVisible( Behind, FTransform::Identity, CenterRay.Origin, CenterRay.Origin + CenterRay.Direction * 5.0f ), "and it does not hide a point in front of the eye" );

		// 矩形
		FVector2 TopLeft( 1.0e9f, 1.0e9f ), BottomRight( -1.0e9f, -1.0e9f );   // 8 つの頂点の画面での範囲
		for ( const FVector3& Corner : Mesh.Vertices )
		{
			const FVector2 P = F.Project( Corner );
			TopLeft = FVector2( NMath::Min( TopLeft.x, P.x ), NMath::Min( TopLeft.y, P.y ) );
			BottomRight = FVector2( NMath::Max( BottomRight.x, P.x ), NMath::Max( BottomRight.y, P.y ) );
		}
		FEditMeshSelection InRect = NWorldEditor::FindMeshElementsInRect( Mesh, World, S.Camera, F.View, BottomRight.x + 20.0f, BottomRight.y + 20.0f, TopLeft.x - 20.0f, TopLeft.y - 20.0f, EEditMeshElement::Vertex, false );
		C.Check( InRect.Vertices.Num() == 8 && InRect.Edges.IsEmpty() && InRect.Faces.IsEmpty(), "a rectangle round the whole box (corners given in either order) takes all 8 vertices when everything counts" );
		InRect = NWorldEditor::FindMeshElementsInRect( Mesh, World, S.Camera, F.View, TopLeft.x - 20.0f, TopLeft.y - 20.0f, BottomRight.x + 20.0f, BottomRight.y + 20.0f, EEditMeshElement::Vertex, true );
		C.Check( InRect.Vertices.Num() == 6 && !NEditMesh::ContainsSorted( InRect.Vertices, Hidden ), "with only the visible ones: 6 (the two hidden bottom-back corners are left out) (%d)", InRect.Vertices.Num() );
		InRect = NWorldEditor::FindMeshElementsInRect( Mesh, World, S.Camera, F.View, TopLeft.x - 20.0f, TopLeft.y - 20.0f, BottomRight.x + 20.0f, BottomRight.y + 20.0f, EEditMeshElement::Edge, false );
		C.Check( InRect.Edges.Num() == 12 && InRect.Vertices.IsEmpty(), "edges: all 12 have both ends inside" );
		InRect = NWorldEditor::FindMeshElementsInRect( Mesh, World, S.Camera, F.View, TopLeft.x - 20.0f, TopLeft.y - 20.0f, BottomRight.x + 20.0f, BottomRight.y + 20.0f, EEditMeshElement::Face, false );
		C.Check( InRect.Faces.Num() == 6 && InRect.Vertices.IsEmpty(), "faces: all 6 centres are inside" );
		InRect = NWorldEditor::FindMeshElementsInRect( Mesh, World, S.Camera, F.View, TopLeft.x - 20.0f, TopLeft.y - 20.0f, BottomRight.x + 20.0f, BottomRight.y + 20.0f, EEditMeshElement::Face, true );
		C.Check( ( FrontFace == 5 ? InRect.Faces == Ints( { 0, 5 } ) : InRect.Faces == Ints( { 0, 4 } ) ), "faces that can be seen from here: the top and the front (%d)", InRect.Faces.Num() );
		InRect = NWorldEditor::FindMeshElementsInRect( Mesh, World, S.Camera, F.View, 0.0f, 0.0f, 3.0f, 3.0f, EEditMeshElement::Vertex, false );
		C.Check( InRect.IsEmpty(), "a rectangle that holds nothing gives nothing" );
		FVector2 TopMin( 1.0e9f, 1.0e9f ), TopMax( -1.0e9f, -1.0e9f );   // 上の 4 つの頂点だけの範囲
		for ( const FVector3& Corner : { FVector3( -1, 2, -1 ), FVector3( -1, 2, 1 ), FVector3( 1, 2, 1 ), FVector3( 1, 2, -1 ) } )
		{
			const FVector2 P = F.Project( Corner );
			TopMin = FVector2( NMath::Min( TopMin.x, P.x ), NMath::Min( TopMin.y, P.y ) );
			TopMax = FVector2( NMath::Max( TopMax.x, P.x ), NMath::Max( TopMax.y, P.y ) );
		}
		InRect = NWorldEditor::FindMeshElementsInRect( Mesh, World, S.Camera, F.View, TopMin.x - 4.0f, TopMin.y - 4.0f, TopMax.x + 4.0f, TopMax.y + 4.0f, EEditMeshElement::Vertex, false );
		C.Check( InRect.Vertices.Num() == 4, "a rectangle round the 4 corners of the top takes only those (the lower ones are outside) (%d)", InRect.Vertices.Num() );
		InRect = NWorldEditor::FindMeshElementsInRect( Mesh, World, S.Camera, F.View, TopMin.x - 4.0f, TopMin.y - 4.0f, TopMax.x + 4.0f, TopMax.y + 4.0f, EEditMeshElement::Edge, false );
		C.Check( InRect.Edges.Num() == 4, "...and only the 4 edges of the top: a vertical edge has just one end inside (%d)", InRect.Edges.Num() );

		// セッションを通して: クリックの合成
		S.SetMeshElement( EEditMeshElement::Vertex );
		C.Check( S.MeshClickSelect( F.View, Pixel.x, Pixel.y, ESelectMode::Replace ) && S.GetMeshSelection().Vertices == Ints( { FrontTop } ), "a click selects the vertex" );
		const FVector2 Other = F.Project( FVector3( 1, 2, FrontZ ) );
		const int32 OtherVertex = F.V( 1, 2, FrontZ );
		S.MeshClickSelect( F.View, Other.x, Other.y, ESelectMode::Add );
		TArray<int32> Both = Ints( { FrontTop, OtherVertex } );
		Both.Sort();
		C.Check( S.GetMeshSelection().Vertices == Both, "adding another keeps both (kept in order)" );
		S.MeshClickSelect( F.View, Other.x, Other.y, ESelectMode::Toggle );
		C.Check( S.GetMeshSelection().Vertices == Ints( { FrontTop } ), "toggling the second one takes it out" );
		S.MeshClickSelect( F.View, Pixel.x, Pixel.y, ESelectMode::Remove );
		C.Check( S.GetMeshSelection().IsEmpty(), "removing the first empties the selection" );
		S.MeshClickSelect( F.View, Pixel.x, Pixel.y, ESelectMode::Replace );
		C.Check( !S.MeshClickSelect( F.View, 3.0f, 3.0f, ESelectMode::Replace ) && S.GetMeshSelection().IsEmpty(), "clicking on nothing (replace) clears the selection" );
		S.MeshClickSelect( F.View, Pixel.x, Pixel.y, ESelectMode::Replace );
		S.MeshClickSelect( F.View, 3.0f, 3.0f, ESelectMode::Add );
		C.Check( S.GetMeshSelection().Vertices == Ints( { FrontTop } ), "clicking on nothing while adding keeps what is chosen" );

		// 辺・面のモード、種類の切り替え
		S.MeshSelectNone();
		S.SetMeshElement( EEditMeshElement::Face );
		S.MeshClickSelect( F.View, TopCenter.x, TopCenter.y, ESelectMode::Replace );
		C.Check( S.GetMeshSelection().Faces == Ints( { 0 } ) && S.GetMeshSelection().Vertices.IsEmpty(), "in face mode a click on the top selects face 0" );
		S.SetMeshElement( EEditMeshElement::Vertex );
		C.Check( S.GetMeshSelection().Vertices.Num() == 4 && S.GetMeshSelection().Faces.IsEmpty(), "switching to vertex mode keeps the 4 corners as vertices" );
		S.SetMeshElement( EEditMeshElement::Edge );
		C.Check( S.GetMeshSelection().Edges.Num() == 4 && S.GetMeshSelection().Vertices.IsEmpty(), "to edge mode: the 4 edges of the top" );
		S.SetMeshElement( EEditMeshElement::Face );
		C.Check( S.GetMeshSelection().Faces == Ints( { 0 } ), "and back to a face" );
		S.SetMeshElement( EEditMeshElement::Edge );
		S.MeshClickSelect( F.View, Middle.x, Middle.y + 3.0f, ESelectMode::Replace );
		C.Check( S.GetMeshSelection().Edges.Num() == 1 && S.GetMeshSelection().Edges[0] == TopFrontEdge, "in edge mode a click selects the edge" );

		// 全部・無し・反転・つながった
		S.SetMeshElement( EEditMeshElement::Face );
		S.MeshSelectAll();
		C.Check( S.GetMeshSelection().Faces.Num() == 6, "select all (faces)" );
		S.MeshSelectNone();
		C.Check( S.GetMeshSelection().IsEmpty(), "select none" );
		S.MeshClickSelect( F.View, TopCenter.x, TopCenter.y, ESelectMode::Replace );
		S.MeshInvertSelection();
		C.Check( S.GetMeshSelection().Faces == Ints( { 1, 2, 3, 4, 5 } ), "invert: the other 5 faces" );
		S.SetMeshElement( EEditMeshElement::Vertex );
		S.MeshSelectAll();
		C.Check( S.GetMeshSelection().Vertices.Num() == 8, "select all (vertices)" );
		S.MeshInvertSelection();
		C.Check( S.GetMeshSelection().Vertices.IsEmpty(), "inverting everything gives nothing" );
		S.SetMeshElement( EEditMeshElement::Edge );
		S.MeshSelectAll();
		C.Check( S.GetMeshSelection().Edges.Num() == 12, "select all (edges)" );
		S.MeshClickSelect( F.View, Middle.x, Middle.y + 3.0f, ESelectMode::Toggle );   // 上の手前の辺を外す
		C.Check( S.GetMeshSelection().Edges.Num() == 11, "(all edges but the top front one)" );
		S.MeshInvertSelection();
		C.Check( S.GetMeshSelection().Edges.Num() == 1 && S.GetMeshSelection().Edges[0] == TopFrontEdge, "invert in edge mode: only the one that was left out" );
		FMeshFixture Broken;
		FEditMeshFace TwoCorners;
		TwoCorners.Indices.Add( 0 );
		TwoCorners.Indices.Add( 1 );
		NWorldEdit::EditObject( Broken.Session.Document, Broken.Id )->Mesh.GetValue().Faces.Add( TwoCorners );
		Broken.Session.SetMeshElement( EEditMeshElement::Face );
		Broken.Session.MeshSelectAll();
		C.Check( Broken.Session.GetMeshSelection().Faces.Num() == 6, "select all leaves out a broken face (%d)", Broken.Session.GetMeshSelection().Faces.Num() );
		// つながっている面: 2 つの島
		S.MeshAppendPrimitive( EEditMeshPrimitive::Box, FVector3( 8, 0, 0 ) );
		S.SetMeshElement( EEditMeshElement::Face );
		S.MeshSelectNone();
		S.MeshClickSelect( F.View, TopCenter.x, TopCenter.y, ESelectMode::Replace );
		S.MeshSelectLinked();
		C.Check( S.GetMeshSelection().Faces == Ints( { 0, 1, 2, 3, 4, 5 } ), "select linked: the whole first box, not the one appended beside it (%d)", S.GetMeshSelection().Faces.Num() );
	}

	//=========================================================================
	// 操作
	//=========================================================================
	void TestOperations( FChecker& C )
	{
		C.Group = "mesh-editor-ops";
		// 押し出し: 上の面（面 0）
		{
			FMeshFixture F;
			CWorldEditorSession& S = F.Session;
			S.SetMeshElement( EEditMeshElement::Face );
			S.SetMeshSelection( Chosen( {}, { 0 } ) );
			const uint32 Revision = S.Document.GetRevision();
			C.Check( S.MeshExtrude( 1.0f ) && F.Mesh().GetNumFaces() == 10 && F.Mesh().GetNumVertices() == 12 && S.GetMeshSelection().Faces == Ints( { 0 } ) && S.GetMeshElement() == EEditMeshElement::Face && S.Document.GetRevision() != Revision,
					 "extruding the top face: 10 faces, 12 vertices, the face stays chosen" );
			C.Check( Near( F.Mesh().ComputeVolume(), 12.0f ) && IsClosed( F.Mesh() ), "still closed, volume 12" );
			C.Check( S.Document.Undo() && S.Document.Get().FindObject( F.Id ) != nullptr && F.Mesh().GetNumFaces() == 6, "one undo step takes the extrusion back (the object, made earlier, is still there)" );
			S.Document.Redo();
			S.SyncSelection();
			C.Check( S.IsMeshEditing() && S.MeshExtrude( 0.0f ) && F.Mesh().GetNumFaces() == 14, "extruding the (still chosen) face again" );
		}
		// 頂点モードでの選択から、面の操作（4 つの頂点で面になる）
		{
			FMeshFixture F;
			CWorldEditorSession& S = F.Session;
			FEditMeshSelection Corners;
			for ( const int32 Index : F.Mesh().Faces[0].Indices ) NEditMesh::AddSorted( Corners.Vertices, Index );
			S.SetMeshSelection( Corners );
			C.Check( S.MeshInset( 0.5f ) && F.Mesh().GetNumFaces() == 10 && S.GetMeshElement() == EEditMeshElement::Face && S.GetMeshSelection().Faces == Ints( { 0 } ) && Near( F.Mesh().ComputeFaceArea( 0 ), 1.0f ),
					 "vertex mode with the 4 corners of the top chosen: insetting works on that face, and the mode becomes face" );
		}
		// 消す: 面・頂点・辺
		{
			FMeshFixture F;
			CWorldEditorSession& S = F.Session;
			S.SetMeshElement( EEditMeshElement::Face );
			S.SetMeshSelection( Chosen( {}, { 0 } ) );
			C.Check( S.MeshDeleteSelection() && F.Mesh().GetNumFaces() == 5 && S.GetMeshSelection().IsEmpty(), "deleting the top face leaves 5, and the selection is cleared" );
			S.SetMeshElement( EEditMeshElement::Vertex );
			S.SetMeshSelection( Chosen( { 0 } ) );
			const int32 FacesBefore = F.Mesh().GetNumFaces();
			C.Check( S.MeshDeleteSelection() && F.Mesh().GetNumFaces() < FacesBefore, "deleting a vertex removes the faces that use it" );
			S.SetMeshElement( EEditMeshElement::Edge );
			FEditMeshSelection OneEdge;
			OneEdge.Edges.Add( NEditMesh::GetAllEdges( F.Mesh() )[0] );
			S.SetMeshSelection( OneEdge );
			const int32 Before = F.Mesh().GetNumFaces();
			C.Check( S.MeshDeleteSelection() && F.Mesh().GetNumFaces() < Before, "deleting an edge removes the faces that use it" );
		}
		// 裏返し・材質・割る・三角形・つなぐ・溶接
		{
			FMeshFixture F;
			CWorldEditorSession& S = F.Session;
			S.SetMeshElement( EEditMeshElement::Face );
			S.MeshSelectAll();
			C.Check( S.MeshFlip() && F.Mesh().ComputeVolume() < -7.9f, "flipping all faces turns the box inside out" );
			S.MeshFlip();
			C.Check( F.Mesh().ComputeVolume() > 7.9f, "and back" );
			S.SetMeshSelection( Chosen( {}, { 2 } ) );
			NWorldEdit::EditObject( S.Document, F.Id )->Mesh.GetValue().Materials.Add( FEditMeshMaterial() );
			NWorldEdit::EditObject( S.Document, F.Id )->Mesh.GetValue().Materials.Add( FEditMeshMaterial() );
			C.Check( S.MeshSetMaterial( 1 ) && F.Mesh().Faces[2].Material == 1 && F.Mesh().Faces[0].Material == 0 && !S.MeshSetMaterial( 1 ) && !S.MeshSetMaterial( -1 ), "setting the material of the chosen face (and doing it again changes nothing; a negative number is refused)" );
			C.Check( S.MeshSubdivide() && F.Mesh().GetNumFaces() == 9 + 0 && S.GetMeshSelection().Faces.Num() == 4 && IsClosed( F.Mesh() ), "subdividing a face: 4 pieces, all chosen, still closed (%d faces)", F.Mesh().GetNumFaces() );
			C.Check( S.MeshTriangulate() && F.Mesh().GetNumFaces() == 9 + 4 && S.GetMeshSelection().Faces.Num() == 8, "triangulating those 4 pieces gives 8 triangles, all chosen (%d)", S.GetMeshSelection().Faces.Num() );
		}
		{
			FMeshFixture F;
			CWorldEditorSession& S = F.Session;
			const int32 A = F.V( -1, 2, -1 ), B = F.V( 1, 2, -1 );
			S.SetMeshSelection( Chosen( { NMath::Min( A, B ), NMath::Max( A, B ) } ) );
			C.Check( S.MeshMerge() && F.Mesh().GetNumVertices() == 7 && S.GetMeshSelection().Vertices.Num() == 1 && Near3( F.Mesh().Vertices[S.GetMeshSelection().Vertices[0]], FVector3( 0, 2, -1 ) ), "merging two vertices: 7 left, the merged one (in the middle of the edge) is the one chosen" );
			C.Check( !S.MeshMerge(), "one vertex alone cannot be merged" );
			S.MeshSelectNone();
			C.Check( !S.MeshWeld( 0.01f ) && S.GetMeshSelection().IsEmpty(), "welding finds nothing to join in a box" );
		}
		{
			// 隣に、頂点を共有しない平面を並べる: 縫い目の頂点が、溶接でつながる
			FMeshFixture F;
			CWorldEditorSession& S = F.Session;
			FEditMesh Seam = NEditMesh::MakePlane( 2, 2, 1, 1 );
			NEditMesh::AppendMesh( Seam, NEditMesh::MakePlane( 2, 2, 1, 1 ), FTransform( FVector3( 2, 0, 0 ) ) );
			NWorldEdit::EditObject( S.Document, F.Id )->Mesh = Seam;
			S.Document.EndEditSession();
			S.SetMeshSelection( Chosen( { 0, 1, 2, 3 } ) );
			C.Check( !S.MeshWeld( 0.01f ), "welding only the first plane's vertices finds nothing to join (they are far from each other)" );
			S.MeshSelectNone();
			C.Check( S.MeshWeld( 0.01f ) && F.Mesh().GetNumVertices() == 6 && S.GetMeshSelection().IsEmpty() && S.Document.Undo() && F.Mesh().GetNumVertices() == 8, "welding with nothing chosen looks at all vertices: the seam joins (8 -> 6), one undo step" );
		}
		// 基本の形を足す: オブジェクトの変換（位置・拡大）を通してローカルに
		{
			FMeshFixture F( FVector3( 10, 0, 0 ) );
			CWorldEditorSession& S = F.Session;
			NWorldEdit::EditObject( S.Document, F.Id )->Transform.Scale = FVector3( 2, 1, 1 );
			S.Document.EndEditSession();
			C.Check( S.MeshAppendPrimitive( EEditMeshPrimitive::Box, FVector3( 14, 0, 0 ) ) && F.Mesh().GetNumFaces() == 12 && F.Mesh().GetNumVertices() == 16 && S.GetMeshSelection().Faces.Num() == 6 && S.GetMeshSelection().Faces[0] == 6 && S.GetMeshElement() == EEditMeshElement::Face,
					 "appending a box: 6 new faces, chosen, in face mode" );
			FVector3 Min, Max;
			F.Mesh().GetBounds( Min, Max );
			// 世界の (14, 0, 0) は、位置 10・拡大 2 のオブジェクトの x = 2。足した箱の中心は、ローカルの x = 2（幅 2）
			C.Check( Near( Max.x, 3.0f ) && Near( Min.x, -1.0f ), "its world position becomes local through the object's position and scale: spanning x -1 .. 3 together (%.2f .. %.2f)", Min.x, Max.x );
			C.Check( S.Document.Undo() && F.Mesh().GetNumFaces() == 6, "one undo" );
		}
	}

	//=========================================================================
	// ギズモで頂点を動かす
	//=========================================================================
	void TestGizmo( FChecker& C )
	{
		C.Group = "mesh-editor-gizmo";
		// ギズモの点への適用
		FEditorGizmoFrame Frame;
		Frame.Origin = FVector3( 1, 0, 0 );
		FEditorGizmoDelta Delta;
		Delta.Translation = FVector3( 0, 2, 0 );
		C.Check( Near3( NWorldEditor::ApplyGizmoDeltaToPoint( FVector3( 3, 1, 0 ), Delta, EEditorGizmoMode::Translate, Frame ), FVector3( 3, 3, 0 ) ), "a translation moves a point" );
		Delta = FEditorGizmoDelta();
		Delta.Rotation = FQuaternion::FromAxisAngle( FVector3::Up, 1.5707964f );
		C.Check( Near3( NWorldEditor::ApplyGizmoDeltaToPoint( FVector3( 3, 1, 0 ), Delta, EEditorGizmoMode::Rotate, Frame ), Frame.Origin + Delta.Rotation.Rotate( FVector3( 2, 1, 0 ) ) ) && Near3( NWorldEditor::ApplyGizmoDeltaToPoint( Frame.Origin, Delta, EEditorGizmoMode::Rotate, Frame ), Frame.Origin ),
				 "a rotation turns it about the gizmo's origin (the origin itself stays)" );
		Delta = FEditorGizmoDelta();
		Delta.Scale = FVector3( 2, 1, 3 );
		C.Check( Near3( NWorldEditor::ApplyGizmoDeltaToPoint( FVector3( 3, 1, 1 ), Delta, EEditorGizmoMode::Scale, Frame ), FVector3( 5, 1, 3 ) ), "a scale of (2, 1, 3) about the origin along the frame's axes" );
		FEditorGizmoFrame Turned = Frame;   // 軸を回した枠: 第 1 軸が +Z、第 3 軸が -X
		Turned.Axes[0] = FVector3( 0, 0, 1 );
		Turned.Axes[2] = FVector3( -1, 0, 0 );
		// 点 (2,0,0) は、原点 (1,0,0) から (1,0,0)。第 3 軸（-X）の成分は -1 で、その倍率 3 を掛けて、-3 -> 位置は原点 + (-X) * (-3) = (4,0,0)
		C.Check( Near3( NWorldEditor::ApplyGizmoDeltaToPoint( FVector3( 2, 0, 0 ), Delta, EEditorGizmoMode::Scale, Turned ), FVector3( 4, 0, 0 ) ), "with the frame's axes turned, the scale follows them (the third axis is -x: the factor 3 applies along x)" );

		// セッションで、面を動かす
		FMeshFixture F( FVector3( 10, 0, 0 ) );
		CWorldEditorSession& S = F.Session;
		NWorldEdit::EditObject( S.Document, F.Id )->Transform.Scale = FVector3( 2, 3, 1 );
		NWorldEdit::EditObject( S.Document, F.Id )->Transform.RotationDegrees = FVector3( 0, 30, 0 );
		S.Document.EndEditSession();
		S.SetMeshElement( EEditMeshElement::Face );
		FEditorGizmoFrame Out;
		C.Check( !S.GetGizmoFrame( F.View, Out ), "with nothing chosen there is no gizmo" );
		S.SetMeshSelection( Chosen( {}, { 0 } ) );
		const FEditMesh Start = F.Mesh();
		C.Check( S.GetGizmoFrame( F.View, Out ) && Near3( Out.Origin, F.ToWorld( FVector3( 0, 2, 0 ) ) ) && Near3( Out.Axes[0], FVector3::Right ), "the gizmo is at the middle of the chosen face, in world axes" );
		S.GizmoSpace = EEditorGizmoSpace::Local;
		C.Check( S.GetGizmoFrame( F.View, Out ) && Near3( Out.Axes[0], S.GetEditedWorldTransform().Rotation.Normalized().Rotate( FVector3::Right ) ), "in local space it follows the object's rotation" );
		S.GizmoSpace = EEditorGizmoSpace::World;
		S.GizmoMode = EEditorGizmoMode::Translate;
		S.GetGizmoFrame( F.View, Out );
		FVector3 AxisStart, AxisEnd;
		NWorldEditor::GetGizmoAxisSegment( Out, 1, AxisStart, AxisEnd );
		const FVector3 Grab = ( AxisStart + AxisEnd ) * 0.5f;
		C.Check( S.BeginGizmoDrag( F.View, EEditorGizmoHandle::AxisY, RayThrough( S.Camera, F.View, Grab ) ) && S.IsGizmoDragging(), "the up arrow of the gizmo can be grabbed" );
		// 世界の上へ 1.5 動かす
		const FVector3 Up = Grab + FVector3( 0.0f, 1.5f, 0.0f );
		S.UpdateGizmoDrag( RayThrough( S.Camera, F.View, Up ) );
		const FEditMesh& Moved = F.Mesh();
		bool bFaceMoved = true, bRestStays = true;
		for ( int32 i = 0; i < Moved.GetNumVertices(); ++i )
		{
			const FVector3 Before = F.ToWorld( Start.Vertices[i] ), After = F.ToWorld( Moved.Vertices[i] );
			if ( Start.Faces[0].Indices.Contains( i ) ) bFaceMoved = bFaceMoved && Near3( After, Before + FVector3( 0.0f, 1.5f, 0.0f ), 2.0e-2f );
			else bRestStays = bRestStays && Near3( After, Before );
		}
		C.Check( bFaceMoved && bRestStays, "dragging up by 1.5 in the world moves the 4 corners of the face by exactly that in world space (through the object's scale 3 in y and its 30-degree turn); the other vertices stay" );
		S.EndGizmoDrag();
		C.Check( S.Document.Undo() && S.Document.Get().FindObject( F.Id ) != nullptr && Near3( F.Mesh().Vertices[Start.Faces[0].Indices[0]], Start.Vertices[Start.Faces[0].Indices[0]] ), "the whole drag is one undo step (the mesh is as before, and the object is still there)" );

		// 取り消し
		S.GetGizmoFrame( F.View, Out );
		NWorldEditor::GetGizmoAxisSegment( Out, 1, AxisStart, AxisEnd );
		const uint64 BeforeHash = F.Mesh().ComputeHash();
		S.BeginGizmoDrag( F.View, EEditorGizmoHandle::AxisY, RayThrough( S.Camera, F.View, ( AxisStart + AxisEnd ) * 0.5f ) );
		S.UpdateGizmoDrag( RayThrough( S.Camera, F.View, ( AxisStart + AxisEnd ) * 0.5f + FVector3( 0, 1, 0 ) ) );
		C.Check( F.Mesh().ComputeHash() != BeforeHash, "(the drag changed the mesh)" );
		S.CancelGizmoDrag();
		C.Check( F.Mesh().ComputeHash() == BeforeHash && !S.IsGizmoDragging(), "cancelling puts the mesh back, and leaves nothing to redo" );
		const bool bNoRedo = !S.Document.CanRedo();
		C.Check( bNoRedo, "(no redo entry)" );

		// 回転・拡大
		S.GizmoMode = EEditorGizmoMode::Scale;
		S.GetGizmoFrame( F.View, Out );
		C.Check( S.BeginGizmoDrag( F.View, EEditorGizmoHandle::Free, RayThrough( S.Camera, F.View, Out.Origin ) ), "scale mode: the centre handle" );
		S.UpdateGizmoDrag( RayThrough( S.Camera, F.View, Out.Origin + Out.Axes[0] * Out.Length * 0.5f + Out.Axes[1] * Out.Length * 0.5f ) );
		S.EndGizmoDrag();
		const FEditMesh Scaled = F.Mesh();
		const FVector3 Centre = F.ToWorld( FVector3( 0, 2, 0 ) );
		float32 SpreadBefore = 0.0f, SpreadAfter = 0.0f;
		for ( const int32 Index : Start.Faces[0].Indices )
		{
			SpreadBefore += FVector3::Distance( F.ToWorld( Start.Vertices[Index] ), Centre );
			SpreadAfter += FVector3::Distance( F.ToWorld( Scaled.Vertices[Index] ), Centre );
		}
		C.Check( SpreadAfter > SpreadBefore * 1.05f && Near3( Centre, F.ToWorld( ( Scaled.Vertices[Start.Faces[0].Indices[0]] + Scaled.Vertices[Start.Faces[0].Indices[2]] ) * 0.5f ), 0.05f ), "dragging the centre handle outwards spreads the face's corners about its middle (%.2f -> %.2f), and the middle stays", SpreadBefore, SpreadAfter );
		S.Document.Undo();

		// 動かせないとき: 編集中でなければ、オブジェクトを動かす（これまでどおり）
		S.EndMeshEdit();
		S.GizmoMode = EEditorGizmoMode::Translate;
		C.Check( S.GetGizmoFrame( F.View, Out ) && Near3( Out.Origin, FVector3( 10, 0, 0 ) ), "after the editing ends the gizmo is on the object again (at its position)" );
	}

	//=========================================================================
	// 始める・終わる・操作の細かいところ（ドラッグ中・選択の掃除・要素の切り替え）
	//=========================================================================
	void TestSessionDetails( FChecker& C )
	{
		C.Group = "mesh-editor-session";
		{
			FMeshFixture G;
			CWorldEditorSession& T = G.Session;
			T.SetMeshElement( EEditMeshElement::Face );
			T.SetMeshSelection( Chosen( {}, { 0 } ) );
			T.BeginMeshEdit( G.Id );
			C.Check( T.GetMeshSelection().IsEmpty(), "beginning again clears what was chosen" );
			const int32 Other = T.CreateEmpty( FVector3( 5, 0, 0 ) );
			T.EndMeshEdit();
			T.SetSelection( Ints( { Other } ) );
			C.Check( T.BeginMeshEdit( G.Id ) && T.IsSelected( G.Id ) && !T.IsSelected( Other ) && T.GetPrimary() == G.Id, "beginning selects the mesh object, and only it" );
			T.SetMeshElement( EEditMeshElement::Vertex );
			T.SetMeshSelection( Chosen( { 0, 99, 5 } ) );
			C.Check( T.GetMeshSelection().Vertices == Ints( { 0, 5 } ), "setting a selection drops the numbers the mesh does not have" );

			// ギズモのドラッグ中: 始め直せない・操作も、オブジェクトの作成もできない。終わりは、ドラッグを取り消して、選択を空にする
			T.SetMeshElement( EEditMeshElement::Face );
			T.SetMeshSelection( Chosen( {}, { 0 } ) );
			T.GizmoMode = EEditorGizmoMode::Translate;
			FEditorGizmoFrame Frame;
			T.GetGizmoFrame( G.View, Frame );
			FVector3 AxisStart, AxisEnd;
			NWorldEditor::GetGizmoAxisSegment( Frame, 1, AxisStart, AxisEnd );
			const FVector3 Grab = ( AxisStart + AxisEnd ) * 0.5f;
			const bool bGrabbed = T.BeginGizmoDrag( G.View, EEditorGizmoHandle::AxisY, RayThrough( T.Camera, G.View, Grab ) );
			T.UpdateGizmoDrag( RayThrough( T.Camera, G.View, Grab + FVector3( 0, 1, 0 ) ) );
			const uint64 DraggedHash = G.Mesh().ComputeHash();
			const int32 NumObjects = T.Document.Get().Objects.Num();
			C.Check( bGrabbed && !T.BeginMeshEdit( G.Id ) && !T.MeshExtrude( 1.0f ) && !T.MeshFlip() && G.Mesh().ComputeHash() == DraggedHash, "while the gizmo is dragged, editing cannot begin again and no operation is applied" );
			C.Check( T.CreateMesh( EEditMeshPrimitive::Box, FVector3::Zero ) == WorldNoId && T.Document.Get().Objects.Num() == NumObjects, "...and no object is created" );
			T.EndMeshEdit();
			C.Check( !T.IsGizmoDragging() && !T.IsMeshEditing() && T.GetMeshSelection().IsEmpty() && G.Mesh().ComputeHash() != DraggedHash, "ending the edit cancels the drag (the mesh goes back) and clears what was chosen" );
		}

		C.Group = "mesh-editor-ops";
		{
			FMeshFixture F2;
			CWorldEditorSession& T = F2.Session;
			T.SetMeshElement( EEditMeshElement::Face );
			T.SetMeshSelection( Chosen( {}, { 0 } ) );
			C.Check( T.MeshMerge() && T.GetMeshElement() == EEditMeshElement::Vertex, "merging the corners of a face switches to vertex mode" );
			FMeshFixture F3;
			CWorldEditorSession& W = F3.Session;
			W.MeshAppendPrimitive( EEditMeshPrimitive::Box, FVector3::Zero );
			W.SetMeshElement( EEditMeshElement::Vertex );
			W.MeshSelectAll();
			C.Check( !W.GetMeshSelection().IsEmpty() && W.MeshWeld( 0.01f ) && F3.Mesh().GetNumVertices() == 8 && W.GetMeshSelection().IsEmpty(), "welding a box laid over itself joins 16 vertices into 8, and clears what was chosen" );
		}
	}
}

void NWorldTest::RunMeshEditorTests( FChecker& C )
{
	TestCreate( C );
	TestBeginEnd( C );
	TestPicking( C );
	TestOperations( C );
	TestGizmo( C );
	TestSessionDetails( C );
}
