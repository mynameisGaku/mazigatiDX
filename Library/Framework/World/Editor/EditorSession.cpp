#include "EditorSession.h"

#include <algorithm>
#include <cmath>

#include "Library/Framework/World/WorldHierarchy.h"

namespace
{
	constexpr const char* GizmoEditKey = "WorldEditor.Gizmo";

	// ギズモの変化量が「何も変えない」か
	bool IsIdentityDelta( const FEditorGizmoDelta& Delta, EEditorGizmoMode Mode )
	{
		switch ( Mode )
		{
		case EEditorGizmoMode::Translate: return Delta.Translation.LengthSquared() == 0.0f;
		case EEditorGizmoMode::Rotate: return Delta.AngleRadians == 0.0f;
		case EEditorGizmoMode::Scale: return Delta.Scale.x == 1.0f && Delta.Scale.y == 1.0f && Delta.Scale.z == 1.0f;
		}
		return true;
	}
}

void CWorldEditorSession::Reset( const FWorldDefinition& World, const FString& InFilePath )
{
	if ( Drag.IsActive() ) Drag.End();
	DragObjects.Reset();
	bDragEdited = false;
	MeshObjectId = WorldNoId;
	MeshSelection.Clear();
	MeshDragVertices.Reset();
	MeshDragStart.Reset();
	Document.Reset( World );
	FilePath = InFilePath;
	Selected.Reset();
}

//-----------------------------------------------------------------------------
// 選択
//-----------------------------------------------------------------------------
TArray<int32> CWorldEditorSession::GetSelectedRoots() const
{
	return NWorld::FilterToRoots( Document.Get(), Selected );
}

void CWorldEditorSession::Select( int32 ObjectId, ESelectMode Mode )
{
	TArray<int32> Ids;
	if ( ObjectId != WorldNoId ) Ids.Add( ObjectId );
	SetSelection( Ids, Mode );
}

void CWorldEditorSession::SetSelection( const TArray<int32>& Ids, ESelectMode Mode )
{
	if ( Mode == ESelectMode::Replace ) Selected.Reset();
	for ( const int32 Id : Ids )
	{
		const bool bExists = Document.Get().FindObject( Id ) != nullptr;
		const int32 Position = Selected.Find( Id );
		switch ( Mode )
		{
		case ESelectMode::Replace:
		case ESelectMode::Add:
			if ( !bExists ) break;
			if ( Position != INDEX_NONE ) Selected.RemoveAt( Position );   // 選び直したら、主な選択（末尾）にする
			Selected.Add( Id );
			break;
		case ESelectMode::Toggle:
			if ( Position != INDEX_NONE ) Selected.RemoveAt( Position );
			else if ( bExists ) Selected.Add( Id );
			break;
		case ESelectMode::Remove:
			if ( Position != INDEX_NONE ) Selected.RemoveAt( Position );
			break;
		}
	}
}

void CWorldEditorSession::ClearSelection()
{
	Selected.Reset();
}

void CWorldEditorSession::SelectAll()
{
	Selected.Reset();
	for ( const FWorldObject& Object : Document.Get().Objects ) Selected.Add( Object.Id );
}

void CWorldEditorSession::SyncSelection()
{
	for ( int32 i = Selected.Num() - 1; i >= 0; --i )
	{
		if ( Document.Get().FindObject( Selected[i] ) == nullptr ) Selected.RemoveAt( i );
	}
	SyncMeshEdit();
}

void CWorldEditorSession::SelectNew( const TArray<int32>& Ids )
{
	SetSelection( Ids, ESelectMode::Replace );
}

int32 CWorldEditorSession::ClickSelect( const FEditorRay& Ray, const FEditorShapeContext& Context, ESelectMode Mode, bool bCycle )
{
	const TArray<FEditorHit> Hits = NWorldEditor::PickObjects( Document.Get(), Ray, Context );
	if ( Hits.IsEmpty() )
	{
		if ( Mode == ESelectMode::Replace ) ClearSelection();
		return WorldNoId;
	}
	const int32 Chosen = bCycle ? NWorldEditor::PickNext( Hits, GetPrimary() ) : Hits[0].ObjectId;
	Select( Chosen, Mode );
	return Chosen;
}

int32 CWorldEditorSession::SelectByTag( const FString& Tag, ESelectMode Mode )
{
	TArray<int32> Ids;
	for ( const FWorldObject& Object : Document.Get().Objects )
	{
		if ( Object.HasTag( Tag ) ) Ids.Add( Object.Id );
	}
	SetSelection( Ids, Mode );
	return Ids.Num();
}

void CWorldEditorSession::RectSelect( const FEditorShapeContext& Context, float32 X0, float32 Y0, float32 X1, float32 Y1, ESelectMode Mode )
{
	SetSelection( NWorldEditor::FindObjectsInRect( Document.Get(), Context, X0, Y0, X1, Y1 ), Mode );
}

//-----------------------------------------------------------------------------
// 編集
//-----------------------------------------------------------------------------
bool CWorldEditorSession::Undo()
{
	if ( Drag.IsActive() || !Document.Undo() ) return false;
	SyncSelection();
	return true;
}

bool CWorldEditorSession::Redo()
{
	if ( Drag.IsActive() || !Document.Redo() ) return false;
	SyncSelection();
	return true;
}

int32 CWorldEditorSession::DeleteSelection()
{
	if ( Drag.IsActive() ) return 0;
	const int32 NumRemoved = NWorldEdit::RemoveObjects( Document, Selected );
	SyncSelection();
	return NumRemoved;
}

TArray<int32> CWorldEditorSession::DuplicateSelection()
{
	if ( Drag.IsActive() ) return TArray<int32>();
	const TArray<int32> Copies = NWorldEdit::DuplicateObjects( Document, Selected );
	if ( !Copies.IsEmpty() ) SelectNew( Copies );
	return Copies;
}

void CWorldEditorSession::CopySelection()
{
	const NWorldEdit::FWorldClipboard Copied = NWorldEdit::CopyObjects( Document.Get(), Selected );
	if ( !Copied.IsEmpty() ) Clipboard = Copied;   // 何も選んでいないときは、前にコピーしたものを残す
}

TArray<int32> CWorldEditorSession::Paste( const FVector3& WorldOffset, int32 ParentId )
{
	if ( Drag.IsActive() ) return TArray<int32>();
	const TArray<int32> Pasted = NWorldEdit::PasteObjects( Document, Clipboard, ParentId, WorldOffset );
	if ( !Pasted.IsEmpty() ) SelectNew( Pasted );
	return Pasted;
}

int32 CWorldEditorSession::GroupSelection( const FString& Name )
{
	if ( Drag.IsActive() ) return WorldNoId;
	const int32 Group = NWorldEdit::GroupObjects( Document, Selected, Name );
	if ( Group != WorldNoId ) SelectNew( TArray<int32>{ Group } );
	return Group;
}

bool CWorldEditorSession::UngroupSelection()
{
	if ( Drag.IsActive() ) return false;
	TArray<int32> Freed;
	TWorldDocument::FBatch Batch( Document );
	for ( const int32 Group : GetSelectedRoots() )
	{
		TArray<int32> Children;
		for ( const int32 Index : NWorld::GetChildIndices( Document.Get(), Group ) ) Children.Add( Document.Get().Objects[Index].Id );
		if ( NWorldEdit::UngroupObject( Document, Group ) ) Freed.Append( Children );
	}
	SyncSelection();
	if ( Freed.IsEmpty() ) return false;
	SelectNew( Freed );
	return true;
}

bool CWorldEditorSession::MoveSelection( int32 TargetId, NWorldEdit::EWorldDropPosition Position )
{
	if ( Drag.IsActive() ) return false;
	return NWorldEdit::MoveObjects( Document, Selected, TargetId, Position );
}

int32 CWorldEditorSession::FitSelectionColliders( EWorldColliderShape Shape, const IEditorAssetInfo& Assets )
{
	TWorldDocument::FBatch Batch( Document );
	int32 NumFitted = 0;
	for ( const int32 Id : TArray<int32>( Selected ) )
	{
		if ( NWorldEditor::FitColliderToModel( Document, Id, Shape, Assets ) ) ++NumFitted;
	}
	return NumFitted;
}

int32 CWorldEditorSession::ReplaceSelectionWithPrefab( const FString& PrefabPath, const FVector3& Origin, const FString& Name )
{
	if ( Drag.IsActive() ) return WorldNoId;
	EndMeshEdit();
	const int32 Id = NWorldEdit::ReplaceWithPrefab( Document, Selected, Origin, PrefabPath, Name );
	if ( Id != WorldNoId ) SelectNew( TArray<int32>{ Id } );
	return Id;
}

int32 CWorldEditorSession::UnpackPrefabs( const TArray<int32>& Ids, const IEditorAssetInfo& Assets )
{
	if ( Drag.IsActive() ) return 0;
	TWorldDocument::FBatch Batch( Document );
	int32 NumUnpacked = 0;
	for ( const int32 Id : Ids )
	{
		const FWorldObject* Object = Document.Get().FindObject( Id );
		if ( Object == nullptr || !Object->Prefab.IsSet() ) continue;
		const FWorldDefinition* Contents = Assets.GetWorld( Object->Prefab.GetValue().Path );
		if ( Contents == nullptr ) continue;
		if ( !NWorldEdit::UnpackPrefab( Document, Id, *Contents ).IsEmpty() || Contents->Objects.IsEmpty() ) ++NumUnpacked;
		if ( Contents->Objects.IsEmpty() ) NWorldEdit::EditObject( Document, Id )->Prefab.Reset();   // 中身が空なら、Prefab 部品を外すだけ
	}
	return NumUnpacked;
}

int32 CWorldEditorSession::CreateEmpty( const FVector3& WorldPosition, const FString& Name, int32 ParentId )
{
	const int32 Id = NWorldEditor::AddEmptyObject( Document, Name, ParentId, WorldPosition );
	if ( Id != WorldNoId ) SelectNew( TArray<int32>{ Id } );
	return Id;
}

int32 CWorldEditorSession::CreateAsset( const FString& AssetPath, const FVector3& WorldPosition, const IEditorAssetInfo* Info, int32 ParentId )
{
	const int32 Id = NWorldEditor::AddAssetObject( Document, AssetPath, ParentId, WorldPosition, Info );
	if ( Id != WorldNoId ) SelectNew( TArray<int32>{ Id } );
	return Id;
}

int32 CWorldEditorSession::CreateCollider( EWorldColliderShape Shape, bool bTrigger, const FVector3& WorldPosition, int32 ParentId )
{
	const int32 Id = NWorldEditor::AddColliderObject( Document, Shape, bTrigger, ParentId, WorldPosition );
	if ( Id != WorldNoId ) SelectNew( TArray<int32>{ Id } );
	return Id;
}

bool CWorldEditorSession::FocusSelection( const FEditorViewport& Viewport, const IEditorAssetInfo* Assets )
{
	// カメラの距離で大きさが変わる目印を使わないよう、カメラなしの図形で測る（寄るたびに近づきすぎないように）
	FEditorShapeContext Context;
	Context.Assets = Assets;
	FVector3 Center;
	float32 Radius = 0.0f;
	if ( !NWorldEditor::ComputeBoundingSphere( Document.Get(), Selected, Context, Center, Radius ) ) return false;
	Camera.Focus( Center, std::max( Radius, 0.5f ), Viewport );
	return true;
}

//-----------------------------------------------------------------------------
// ギズモ
//-----------------------------------------------------------------------------
bool CWorldEditorSession::GetGizmoFrame( const FEditorViewport& Viewport, FEditorGizmoFrame& OutFrame ) const
{
	// 編集メッシュ: 選んだ頂点（辺・面の頂点）の中心
	if ( IsMeshEditing() )
	{
		const FEditMesh* Mesh = GetEditedMesh();
		if ( Mesh == nullptr ) return false;
		const FTransform World = GetEditedWorldTransform();
		const FVector3 Center = World.TransformPoint( NEditMesh::ComputeSelectionCenter( *Mesh, MeshSelection ) );
		if ( NEditMesh::GetAffectedVertices( *Mesh, MeshSelection ).IsEmpty() ) return false;
		const FQuaternion Orientation = GizmoSpace == EEditorGizmoSpace::Local ? World.Rotation.Normalized() : FQuaternion::Identity;
		OutFrame = NWorldEditor::MakeGizmoFrame( Camera, Viewport, Center, Orientation );
		return true;
	}

	const TArray<int32> Roots = GetSelectedRoots();
	if ( Roots.IsEmpty() ) return false;

	FVector3 Center = FVector3::Zero;
	for ( const int32 Root : Roots ) Center = Center + NWorld::ComputeWorldTransform( Document.Get(), Root ).Position;
	Center = Center / static_cast<float32>( Roots.Num() );

	FQuaternion Orientation = FQuaternion::Identity;
	if ( GizmoSpace == EEditorGizmoSpace::Local && GetPrimary() != WorldNoId ) Orientation = NWorld::ComputeWorldTransform( Document.Get(), GetPrimary() ).Rotation.Normalized();
	OutFrame = NWorldEditor::MakeGizmoFrame( Camera, Viewport, Center, Orientation );
	return true;
}

float32 CWorldEditorSession::GetActiveSnap( bool bInvert ) const
{
	if ( bSnap == bInvert ) return 0.0f;
	switch ( GizmoMode )
	{
	case EEditorGizmoMode::Translate: return MoveSnap;
	case EEditorGizmoMode::Rotate: return RotateSnapDegrees * NMath::DEG2RAD;
	case EEditorGizmoMode::Scale: return ScaleSnap;
	}
	return 0.0f;
}

bool CWorldEditorSession::BeginGizmoDrag( const FEditorViewport& Viewport, EEditorGizmoHandle Handle, const FEditorRay& Ray )
{
	if ( Drag.IsActive() ) return false;
	if ( !GetGizmoFrame( Viewport, DragFrame ) ) return false;
	if ( !Drag.Begin( DragFrame, GizmoMode, Handle, Ray, Camera ) ) return false;

	// 編集メッシュ: 動かす頂点の、開始のときのワールドでの位置
	DragObjects.Reset();
	MeshDragVertices.Reset();
	MeshDragStart.Reset();
	if ( IsMeshEditing() )
	{
		const FEditMesh* Mesh = GetEditedMesh();
		MeshDragWorld = GetEditedWorldTransform();
		MeshDragVertices = NEditMesh::GetAffectedVertices( *Mesh, MeshSelection );
		for ( const int32 Vertex : MeshDragVertices ) MeshDragStart.Add( MeshDragWorld.TransformPoint( Mesh->Vertices[Vertex] ) );
		bDragEdited = false;
		LastDelta = FEditorGizmoDelta();
		return true;
	}

	// 動かす根の、開始のときのワールドでの姿勢
	for ( const int32 Root : GetSelectedRoots() )
	{
		FDragObject Object;
		Object.ObjectId = Root;
		Object.StartWorld = NWorld::ComputeWorldTransform( Document.Get(), Root );
		DragObjects.Add( Object );
	}
	bDragEdited = false;
	LastDelta = FEditorGizmoDelta();
	return true;
}

void CWorldEditorSession::UpdateGizmoDrag( const FEditorRay& Ray, bool bInvertSnap )
{
	if ( !Drag.IsActive() ) return;
	const FEditorGizmoDelta Delta = Drag.Update( Ray, GetActiveSnap( bInvertSnap ) );
	LastDelta = Delta;
	if ( IsIdentityDelta( Delta, GizmoMode ) && !bDragEdited ) return;   // まだ動いていない: 履歴を積まない

	if ( IsMeshEditing() )
	{
		FWorldObject* Object = NWorldEdit::EditObject( Document, MeshObjectId, GizmoEditKey );
		if ( Object == nullptr || !Object->Mesh.IsSet() ) return;
		FEditMesh& Mesh = Object->Mesh.GetValue();
		for ( int32 i = 0; i < MeshDragVertices.Num(); ++i )
		{
			if ( !Mesh.Vertices.IsValidIndex( MeshDragVertices[i] ) ) continue;
			Mesh.Vertices[MeshDragVertices[i]] = MeshDragWorld.InverseTransformPoint( NWorldEditor::ApplyGizmoDeltaToPoint( MeshDragStart[i], Delta, GizmoMode, DragFrame ) );
		}
		bDragEdited = true;
		return;
	}

	NWorldEdit::FWorldTransformMask Mask;
	Mask.bPosition = GizmoMode != EEditorGizmoMode::Scale;
	Mask.bRotation = GizmoMode == EEditorGizmoMode::Rotate;
	Mask.bScale = GizmoMode == EEditorGizmoMode::Scale;
	for ( const FDragObject& Object : DragObjects )
	{
		const FTransform Moved = NWorldEditor::ApplyGizmoDelta( Object.StartWorld, Delta, GizmoMode, DragFrame );
		NWorldEdit::SetWorldTransform( Document, Object.ObjectId, Moved, Mask, GizmoEditKey );
	}
	bDragEdited = true;
}

void CWorldEditorSession::EndGizmoDrag()
{
	if ( !Drag.IsActive() ) return;
	Drag.End();
	Document.EndEditSession();
	DragObjects.Reset();
	MeshDragVertices.Reset();
	MeshDragStart.Reset();
	bDragEdited = false;
}

void CWorldEditorSession::CancelGizmoDrag()
{
	if ( !Drag.IsActive() ) return;
	Drag.End();
	Document.EndEditSession();
	if ( bDragEdited ) Document.UndoAndForget();
	DragObjects.Reset();
	MeshDragVertices.Reset();
	MeshDragStart.Reset();
	bDragEdited = false;
}
