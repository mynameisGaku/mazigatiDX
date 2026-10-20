#include "EditorSession.h"

#include <algorithm>

#include "Library/Framework/World/WorldHierarchy.h"

//=============================================================================
// ワールドエディタのセッション: 編集メッシュの編集（EditorSession.h の「編集メッシュの編集」）
//=============================================================================
namespace
{
	using NEditMesh::EEditMeshElement;

	// 選択の集合を、ESelectMode で合成する
	template<typename T>
	void Combine( TArray<T>& Target, const TArray<T>& Picked, CWorldEditorSession::ESelectMode Mode )
	{
		switch ( Mode )
		{
		case CWorldEditorSession::ESelectMode::Replace:
			Target = Picked;
			break;
		case CWorldEditorSession::ESelectMode::Add:
			for ( const T& Item : Picked ) NEditMesh::AddSorted( Target, Item );
			break;
		case CWorldEditorSession::ESelectMode::Toggle:
			for ( const T& Item : Picked )
			{
				if ( !NEditMesh::RemoveSorted( Target, Item ) ) NEditMesh::AddSorted( Target, Item );
			}
			break;
		case CWorldEditorSession::ESelectMode::Remove:
			for ( const T& Item : Picked ) NEditMesh::RemoveSorted( Target, Item );
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// 作る・変える（オブジェクト）
//-----------------------------------------------------------------------------
int32 CWorldEditorSession::CreateMesh( EEditMeshPrimitive Kind, const FVector3& WorldPosition, int32 ParentId, bool bCollider )
{
	if ( Drag.IsActive() ) return WorldNoId;
	const int32 Id = NWorldEditor::AddMeshObject( Document, Kind, ParentId, WorldPosition, bCollider );
	if ( Id != WorldNoId ) SelectNew( TArray<int32>{ Id } );
	return Id;
}

int32 CWorldEditorSession::ConvertSelectionToMeshes( const IEditorAssetInfo& Assets )
{
	if ( Drag.IsActive() ) return 0;
	TWorldDocument::FBatch Batch( Document );
	int32 NumConverted = 0;
	for ( const int32 Id : TArray<int32>( Selected ) )
	{
		if ( NWorldEditor::ConvertModelToMesh( Document, Id, Assets ) ) ++NumConverted;
	}
	return NumConverted;
}

//-----------------------------------------------------------------------------
// 編集の始め・終わり・同期
//-----------------------------------------------------------------------------
bool CWorldEditorSession::BeginMeshEdit( int32 ObjectId )
{
	if ( Drag.IsActive() ) return false;
	const FWorldObject* Object = Document.Get().FindObject( ObjectId );
	if ( Object == nullptr || !Object->Mesh.IsSet() ) return false;
	MeshObjectId = ObjectId;
	MeshSelection.Clear();
	Select( ObjectId );
	return true;
}

void CWorldEditorSession::EndMeshEdit()
{
	if ( Drag.IsActive() && IsMeshEditing() ) CancelGizmoDrag();
	MeshObjectId = WorldNoId;
	MeshSelection.Clear();
	MeshDragVertices.Reset();
	MeshDragStart.Reset();
}

void CWorldEditorSession::SyncMeshEdit()
{
	if ( !IsMeshEditing() ) return;
	const FWorldObject* Object = Document.Get().FindObject( MeshObjectId );
	// 編集しているオブジェクトが無くなった・メッシュを失った・選択から外れた: 編集を終える
	if ( Object == nullptr || !Object->Mesh.IsSet() || !Selected.Contains( MeshObjectId ) )
	{
		EndMeshEdit();
		return;
	}
	NEditMesh::CleanSelection( Object->Mesh.GetValue(), MeshSelection );
}

const FEditMesh* CWorldEditorSession::GetEditedMesh() const
{
	if ( !IsMeshEditing() ) return nullptr;
	const FWorldObject* Object = Document.Get().FindObject( MeshObjectId );
	return Object != nullptr && Object->Mesh.IsSet() ? &Object->Mesh.GetValue() : nullptr;
}

FTransform CWorldEditorSession::GetEditedWorldTransform() const
{
	return IsMeshEditing() ? NWorld::ComputeWorldTransform( Document.Get(), MeshObjectId ) : FTransform::Identity;
}

void CWorldEditorSession::SetMeshSelection( const FEditMeshSelection& Selection )
{
	MeshSelection = Selection;
	if ( const FEditMesh* Mesh = GetEditedMesh() ) NEditMesh::CleanSelection( *Mesh, MeshSelection );
	else MeshSelection.Clear();
}

void CWorldEditorSession::SetMeshElement( NEditMesh::EEditMeshElement Element )
{
	if ( Element == MeshElement ) return;
	if ( const FEditMesh* Mesh = GetEditedMesh() ) MeshSelection = NEditMesh::ConvertSelection( *Mesh, MeshSelection, Element );
	MeshElement = Element;
}

//-----------------------------------------------------------------------------
// 選ぶ
//-----------------------------------------------------------------------------
void CWorldEditorSession::ApplyMeshSelection( const FEditMeshSelection& Picked, ESelectMode Mode )
{
	switch ( MeshElement )
	{
	case EEditMeshElement::Vertex: Combine( MeshSelection.Vertices, Picked.Vertices, Mode ); break;
	case EEditMeshElement::Edge: Combine( MeshSelection.Edges, Picked.Edges, Mode ); break;
	case EEditMeshElement::Face: Combine( MeshSelection.Faces, Picked.Faces, Mode ); break;
	}
}

bool CWorldEditorSession::MeshClickSelect( const FEditorViewport& Viewport, float32 X, float32 Y, ESelectMode Mode )
{
	const FEditMesh* Mesh = GetEditedMesh();
	if ( Mesh == nullptr ) return false;
	const FEditMeshHit Hit = NWorldEditor::PickMeshElement( *Mesh, GetEditedWorldTransform(), Camera, Viewport, X, Y, MeshElement );
	FEditMeshSelection Picked;
	if ( Hit.bHit )
	{
		switch ( MeshElement )
		{
		case EEditMeshElement::Vertex: Picked.Vertices.Add( Hit.Index ); break;
		case EEditMeshElement::Edge: Picked.Edges.Add( Hit.Edge ); break;
		case EEditMeshElement::Face: Picked.Faces.Add( Hit.Index ); break;
		}
	}
	// 何にも当たらなかったときの、置き換えは、選択を空にする
	ApplyMeshSelection( Picked, Mode );
	return Hit.bHit;
}

void CWorldEditorSession::MeshRectSelect( const FEditorViewport& Viewport, float32 X0, float32 Y0, float32 X1, float32 Y1, ESelectMode Mode, bool bOnlyVisible )
{
	const FEditMesh* Mesh = GetEditedMesh();
	if ( Mesh == nullptr ) return;
	ApplyMeshSelection( NWorldEditor::FindMeshElementsInRect( *Mesh, GetEditedWorldTransform(), Camera, Viewport, X0, Y0, X1, Y1, MeshElement, bOnlyVisible ), Mode );
}

void CWorldEditorSession::MeshSelectAll()
{
	const FEditMesh* Mesh = GetEditedMesh();
	if ( Mesh == nullptr ) return;
	FEditMeshSelection Everything;
	for ( int32 Face = 0; Face < Mesh->GetNumFaces(); ++Face )
	{
		if ( Mesh->IsFaceValid( Face ) ) Everything.Faces.Add( Face );
	}
	MeshSelection = NEditMesh::ConvertSelection( *Mesh, Everything, MeshElement );
	if ( MeshElement == EEditMeshElement::Face ) MeshSelection.Faces = Everything.Faces;
}

void CWorldEditorSession::MeshSelectNone()
{
	MeshSelection.Clear();
}

void CWorldEditorSession::MeshInvertSelection()
{
	const FEditMesh* Mesh = GetEditedMesh();
	if ( Mesh == nullptr ) return;
	const FEditMeshSelection Before = MeshSelection;
	MeshSelectAll();
	const FEditMeshSelection All = MeshSelection;
	MeshSelection.Clear();
	for ( const int32 Item : All.Vertices )
	{
		if ( !NEditMesh::ContainsSorted( Before.Vertices, Item ) ) MeshSelection.Vertices.Add( Item );
	}
	for ( const int64 Item : All.Edges )
	{
		if ( !NEditMesh::ContainsSorted( Before.Edges, Item ) ) MeshSelection.Edges.Add( Item );
	}
	for ( const int32 Item : All.Faces )
	{
		if ( !NEditMesh::ContainsSorted( Before.Faces, Item ) ) MeshSelection.Faces.Add( Item );
	}
}

void CWorldEditorSession::MeshSelectLinked()
{
	const FEditMesh* Mesh = GetEditedMesh();
	if ( Mesh == nullptr ) return;
	const TArray<int32> Touching = NEditMesh::GetFacesUsingVertices( *Mesh, NEditMesh::GetAffectedVertices( *Mesh, MeshSelection ) );
	FEditMeshSelection Linked;
	Linked.Faces = NEditMesh::GetLinkedFaces( *Mesh, Touching );
	MeshSelection = NEditMesh::ConvertSelection( *Mesh, Linked, MeshElement );
	if ( MeshElement == EEditMeshElement::Face ) MeshSelection.Faces = Linked.Faces;
}

//-----------------------------------------------------------------------------
// 操作
//-----------------------------------------------------------------------------
template<typename TOp>
bool CWorldEditorSession::ApplyMeshOp( TOp&& Op )
{
	const FEditMesh* Current = GetEditedMesh();
	if ( Current == nullptr || Drag.IsActive() ) return false;
	FEditMesh Work = *Current;
	FEditMeshSelection NewSelection = MeshSelection;
	if ( !Op( Work, NewSelection ) ) return false;
	if ( Work.ComputeHash() == Current->ComputeHash() ) return false;
	NWorldEdit::EditObject( Document, MeshObjectId )->Mesh = Work;
	MeshSelection = NewSelection;
	if ( const FEditMesh* Edited = GetEditedMesh() ) NEditMesh::CleanSelection( *Edited, MeshSelection );
	return true;
}

// 選んでいるものの、頂点がすべて選ばれている面（面の選択ならその面）
TArray<int32> CWorldEditorSession::GetMeshFaceSelection() const
{
	const FEditMesh* Mesh = GetEditedMesh();
	if ( Mesh == nullptr ) return TArray<int32>();
	if ( MeshElement == EEditMeshElement::Face )
	{
		TArray<int32> Faces;
		for ( const int32 Face : MeshSelection.Faces )
		{
			if ( Mesh->IsFaceValid( Face ) ) Faces.Add( Face );
		}
		return Faces;
	}
	return NEditMesh::ConvertSelection( *Mesh, MeshSelection, EEditMeshElement::Face ).Faces;
}

bool CWorldEditorSession::MeshExtrude( float32 Distance, bool bIndividual )
{
	const TArray<int32> Faces = GetMeshFaceSelection();
	if ( Faces.IsEmpty() ) return false;
	const bool bChanged = ApplyMeshOp( [&]( FEditMesh& Mesh, FEditMeshSelection& Selection )
	{
		const FEditMeshSelection Result = NEditMesh::ExtrudeFaces( Mesh, Faces, Distance, bIndividual );
		Selection.Clear();
		Selection.Faces = Result.Faces;
		return !Result.Faces.IsEmpty();
	} );
	if ( bChanged ) MeshElement = EEditMeshElement::Face;
	return bChanged;
}

bool CWorldEditorSession::MeshInset( float32 Amount )
{
	const TArray<int32> Faces = GetMeshFaceSelection();
	if ( Faces.IsEmpty() ) return false;
	const bool bChanged = ApplyMeshOp( [&]( FEditMesh& Mesh, FEditMeshSelection& Selection )
	{
		const FEditMeshSelection Result = NEditMesh::InsetFaces( Mesh, Faces, Amount );
		Selection.Clear();
		Selection.Faces = Result.Faces;
		return !Result.Faces.IsEmpty();
	} );
	if ( bChanged ) MeshElement = EEditMeshElement::Face;
	return bChanged;
}

bool CWorldEditorSession::MeshDeleteSelection()
{
	if ( MeshSelection.IsEmpty() ) return false;
	return ApplyMeshOp( [&]( FEditMesh& Mesh, FEditMeshSelection& Selection )
	{
		switch ( MeshElement )
		{
		case EEditMeshElement::Face:
			NEditMesh::DeleteFaces( Mesh, Selection.Faces );
			break;
		case EEditMeshElement::Vertex:
			NEditMesh::DeleteVertices( Mesh, Selection.Vertices );
			break;
		case EEditMeshElement::Edge:
		{
			TArray<int32> Faces;
			for ( const int64 Edge : Selection.Edges )
			{
				for ( const int32 Face : NEditMesh::GetFacesUsingEdge( Mesh, Edge ) ) Faces.Add( Face );
			}
			NEditMesh::DeleteFaces( Mesh, Faces );
			break;
		}
		}
		Selection.Clear();   // 番号が詰まったので、選び直す
		return true;
	} );
}

bool CWorldEditorSession::MeshFlip()
{
	const TArray<int32> Faces = GetMeshFaceSelection();
	if ( Faces.IsEmpty() ) return false;
	return ApplyMeshOp( [&]( FEditMesh& Mesh, FEditMeshSelection& ) { NEditMesh::FlipFaces( Mesh, Faces ); return true; } );
}

bool CWorldEditorSession::MeshMerge()
{
	const FEditMesh* Mesh = GetEditedMesh();
	if ( Mesh == nullptr ) return false;
	const TArray<int32> Vertices = NEditMesh::GetAffectedVertices( *Mesh, MeshSelection );
	if ( Vertices.Num() < 2 ) return false;
	const bool bChanged = ApplyMeshOp( [&]( FEditMesh& Work, FEditMeshSelection& Selection )
	{
		const int32 Merged = NEditMesh::MergeVertices( Work, Vertices );
		Selection.Clear();
		if ( Merged >= 0 ) Selection.Vertices.Add( Merged );
		return Merged >= 0;
	} );
	if ( bChanged ) MeshElement = EEditMeshElement::Vertex;
	return bChanged;
}

bool CWorldEditorSession::MeshWeld( float32 Distance )
{
	const FEditMesh* Mesh = GetEditedMesh();
	if ( Mesh == nullptr ) return false;
	const TArray<int32> Vertices = NEditMesh::GetAffectedVertices( *Mesh, MeshSelection );
	return ApplyMeshOp( [&]( FEditMesh& Work, FEditMeshSelection& Selection )
	{
		const int32 NumRemoved = NEditMesh::WeldVertices( Work, Vertices, Distance );
		Selection.Clear();
		return NumRemoved > 0;
	} );
}

bool CWorldEditorSession::MeshSubdivide()
{
	const TArray<int32> Faces = GetMeshFaceSelection();
	if ( Faces.IsEmpty() ) return false;
	return ApplyMeshOp( [&]( FEditMesh& Mesh, FEditMeshSelection& Selection )
	{
		const int32 NumBefore = Mesh.GetNumFaces();
		NEditMesh::SubdivideFaces( Mesh, Faces );
		Selection.Clear();
		Selection.Faces = Faces;   // 割った面（元の番号の面が、最初の 1 つ）。足した面も選ぶ
		for ( int32 Face = NumBefore; Face < Mesh.GetNumFaces(); ++Face ) NEditMesh::AddSorted( Selection.Faces, Face );
		return true;
	} );
}

bool CWorldEditorSession::MeshTriangulate()
{
	const TArray<int32> Faces = GetMeshFaceSelection();
	if ( Faces.IsEmpty() ) return false;
	return ApplyMeshOp( [&]( FEditMesh& Mesh, FEditMeshSelection& Selection )
	{
		const int32 NumBefore = Mesh.GetNumFaces();
		NEditMesh::TriangulateFaces( Mesh, Faces );
		Selection.Clear();
		Selection.Faces = Faces;
		for ( int32 Face = NumBefore; Face < Mesh.GetNumFaces(); ++Face ) NEditMesh::AddSorted( Selection.Faces, Face );
		return true;
	} );
}

bool CWorldEditorSession::MeshSetMaterial( int32 Material )
{
	const TArray<int32> Faces = GetMeshFaceSelection();
	if ( Faces.IsEmpty() || Material < 0 ) return false;
	return ApplyMeshOp( [&]( FEditMesh& Mesh, FEditMeshSelection& ) { NEditMesh::SetFaceMaterial( Mesh, Faces, Material ); return true; } );
}

bool CWorldEditorSession::MeshAppendPrimitive( EEditMeshPrimitive Kind, const FVector3& WorldPosition )
{
	if ( !IsMeshEditing() ) return false;
	const FVector3 LocalPosition = GetEditedWorldTransform().InverseTransformPoint( WorldPosition );
	const bool bChanged = ApplyMeshOp( [&]( FEditMesh& Mesh, FEditMeshSelection& Selection )
	{
		const int32 First = NEditMesh::AppendMesh( Mesh, NEditMesh::MakePrimitive( Kind ), FTransform( LocalPosition ) );
		Selection.Clear();
		for ( int32 Face = First; Face < Mesh.GetNumFaces(); ++Face ) Selection.Faces.Add( Face );
		return Mesh.GetNumFaces() > First;
	} );
	if ( bChanged ) MeshElement = EEditMeshElement::Face;
	return bChanged;
}
