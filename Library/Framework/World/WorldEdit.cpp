#include "WorldEdit.h"

#include "Library/Framework/World/WorldHierarchy.h"

int32 NWorldEdit::AddObject( TWorldDocument& Document, const FString& Name, int32 ParentId, const FWorldTransform& Transform )
{
	if ( ParentId != WorldNoId && Document.Get().FindObject( ParentId ) == nullptr ) return WorldNoId;

	FWorldDefinition& World = Document.BeginEdit();
	FWorldObject Object;
	Object.Id = World.AllocateId();
	Object.Name = Name;
	Object.Parent = ParentId;
	Object.Transform = Transform;
	World.Objects.Add( Object );
	return Object.Id;
}

bool NWorldEdit::RemoveObject( TWorldDocument& Document, int32 ObjectId )
{
	if ( Document.Get().FindObject( ObjectId ) == nullptr ) return false;

	// 先に、消す Id を集める（消しながら探すと、番号がずれる）
	TArray<int32> Doomed;
	for ( const int32 Index : NWorld::GetSubtreeIndices( Document.Get(), ObjectId ) ) Doomed.Add( Document.Get().Objects[Index].Id );

	FWorldDefinition& World = Document.BeginEdit();
	for ( int32 i = World.Objects.Num() - 1; i >= 0; --i )
	{
		if ( Doomed.Contains( World.Objects[i].Id ) ) World.Objects.RemoveAt( i );
	}
	return true;
}

FWorldObject* NWorldEdit::EditObject( TWorldDocument& Document, int32 ObjectId, const char* CoalesceKey )
{
	if ( Document.Get().FindObject( ObjectId ) == nullptr ) return nullptr;
	return Document.BeginEdit( CoalesceKey ).FindObject( ObjectId );
}

bool NWorldEdit::Reparent( TWorldDocument& Document, int32 ObjectId, int32 NewParentId, int32 BeforeSiblingId, bool bKeepWorldTransform )
{
	const FWorldDefinition& Current = Document.Get();
	if ( Current.FindObject( ObjectId ) == nullptr ) return false;
	if ( NewParentId != WorldNoId && Current.FindObject( NewParentId ) == nullptr ) return false;
	if ( NewParentId == ObjectId || NWorld::IsDescendantOf( Current, ObjectId, NewParentId ) ) return false;
	if ( BeforeSiblingId != WorldNoId )
	{
		const FWorldObject* Before = Current.FindObject( BeforeSiblingId );
		if ( Before == nullptr || Before->Parent != NewParentId || BeforeSiblingId == ObjectId ) return false;
	}

	// ワールドでの姿勢を保つなら、移す前に求めておく
	FTransform WorldTransform = FTransform::Identity;
	FTransform NewParentWorld = FTransform::Identity;
	if ( bKeepWorldTransform )
	{
		WorldTransform = NWorld::ComputeWorldTransform( Current, ObjectId );
		if ( NewParentId != WorldNoId ) NewParentWorld = NWorld::ComputeWorldTransform( Current, NewParentId );
	}

	FWorldDefinition& World = Document.BeginEdit();
	const int32 Index = World.FindIndexById( ObjectId );
	FWorldObject Moving = World.Objects[Index];
	Moving.Parent = NewParentId;
	if ( bKeepWorldTransform ) Moving.Transform = FWorldTransform::FromTransform( NWorld::WorldToLocal( NewParentWorld, WorldTransform ) );

	// 兄弟の順は、配列の中の前後で決まる。いったん外して、手前の兄弟の位置（無ければ最後）へ入れる
	World.Objects.RemoveAt( Index );
	const int32 BeforeIndex = BeforeSiblingId != WorldNoId ? World.FindIndexById( BeforeSiblingId ) : INDEX_NONE;
	if ( BeforeIndex != INDEX_NONE ) World.Objects.Insert( Moving, BeforeIndex );
	else World.Objects.Add( Moving );
	return true;
}

int32 NWorldEdit::DuplicateObject( TWorldDocument& Document, int32 ObjectId )
{
	if ( Document.Get().FindObject( ObjectId ) == nullptr ) return WorldNoId;
	const TArray<int32> SubtreeIndices = NWorld::GetSubtreeIndices( Document.Get(), ObjectId );
	TArray<FWorldObject> Copies;
	for ( const int32 Index : SubtreeIndices ) Copies.Add( Document.Get().Objects[Index] );

	FWorldDefinition& World = Document.BeginEdit();

	// 古い Id -> 新しい Id
	TArray<int32> OldIds;
	TArray<int32> NewIds;
	for ( const FWorldObject& Copy : Copies )
	{
		OldIds.Add( Copy.Id );
		NewIds.Add( World.AllocateId() );
	}
	const auto Remap = [&]( int32 OldId )
	{
		int32 Position = INDEX_NONE;
		return OldIds.Find( OldId, Position ) ? NewIds[Position] : OldId;
	};

	int32 LastIndexOfOriginal = 0;
	for ( const int32 Index : SubtreeIndices ) LastIndexOfOriginal = Index > LastIndexOfOriginal ? Index : LastIndexOfOriginal;

	for ( int32 i = 0; i < Copies.Num(); ++i )
	{
		FWorldObject& Copy = Copies[i];
		Copy.Id = NewIds[i];
		if ( i > 0 ) Copy.Parent = Remap( Copy.Parent );   // 根の親は、元と同じ（兄弟として並ぶ）
		for ( FWorldProp& Prop : Copy.Props )
		{
			if ( Prop.Type == EWorldPropType::Object ) Prop.ObjectId = Remap( Prop.ObjectId );
		}
		if ( Copy.Effect.IsSet() ) Copy.Effect.GetValue().Target = Remap( Copy.Effect.GetValue().Target );
	}

	// 元の部分木の、いちばん後ろの要素のすぐ後ろへ（元と同じ並びで）入れる
	for ( int32 i = 0; i < Copies.Num(); ++i ) World.Objects.Insert( Copies[i], LastIndexOfOriginal + 1 + i );
	return Copies[0].Id;
}

bool NWorldEdit::SetWorldTransform( TWorldDocument& Document, int32 ObjectId, const FTransform& World, const FWorldTransformMask& Mask, const char* CoalesceKey )
{
	if ( Document.Get().FindObject( ObjectId ) == nullptr ) return false;
	const FTransform Local = NWorld::WorldToLocal( NWorld::ComputeParentWorldTransform( Document.Get(), ObjectId ), World );
	FWorldObject* Object = EditObject( Document, ObjectId, CoalesceKey );
	if ( Mask.bPosition ) Object->Transform.Position = Local.Position;
	if ( Mask.bRotation ) Object->Transform.RotationDegrees = FWorldTransform::FromTransform( Local ).RotationDegrees;
	if ( Mask.bScale ) Object->Transform.Scale = Local.Scale;
	return true;
}

int32 NWorldEdit::RemoveObjects( TWorldDocument& Document, const TArray<int32>& Ids )
{
	const TArray<int32> Roots = NWorld::FilterToRoots( Document.Get(), Ids );
	TWorldDocument::FBatch Batch( Document );
	int32 NumRemoved = 0;
	for ( const int32 Id : Roots )
	{
		if ( RemoveObject( Document, Id ) ) ++NumRemoved;
	}
	return NumRemoved;
}

TArray<int32> NWorldEdit::DuplicateObjects( TWorldDocument& Document, const TArray<int32>& Ids )
{
	const TArray<int32> Roots = NWorld::FilterToRoots( Document.Get(), Ids );
	TWorldDocument::FBatch Batch( Document );
	TArray<int32> Copies;
	for ( const int32 Id : Roots )
	{
		const int32 Copy = DuplicateObject( Document, Id );
		if ( Copy != WorldNoId ) Copies.Add( Copy );
	}
	return Copies;
}

bool NWorldEdit::CanMoveObjects( const FWorldDefinition& World, const TArray<int32>& Ids, int32 TargetId, EWorldDropPosition Position )
{
	if ( World.FindObject( TargetId ) == nullptr ) return false;
	const TArray<int32> Roots = NWorld::FilterToRoots( World, Ids );
	if ( Roots.IsEmpty() ) return false;
	for ( const int32 Root : Roots )
	{
		if ( Root == TargetId || NWorld::IsDescendantOf( World, Root, TargetId ) ) return false;
	}
	return true;
}

bool NWorldEdit::MoveObjects( TWorldDocument& Document, const TArray<int32>& Ids, int32 TargetId, EWorldDropPosition Position )
{
	if ( !CanMoveObjects( Document.Get(), Ids, TargetId, Position ) ) return false;
	const TArray<int32> Roots = NWorld::FilterToRoots( Document.Get(), Ids );
	const int32 SiblingParent = Document.Get().FindObject( TargetId )->Parent;

	// 「後ろ」は、Target の次の兄弟（動かすものを除く）の手前へ入れる。無ければ最後
	int32 NextSibling = WorldNoId;
	if ( Position == EWorldDropPosition::After )
	{
		bool bPassedTarget = false;
		for ( const int32 Index : NWorld::GetChildIndices( Document.Get(), SiblingParent ) )
		{
			const int32 Id = Document.Get().Objects[Index].Id;
			if ( Id == TargetId ) bPassedTarget = true;
			else if ( bPassedTarget && !Roots.Contains( Id ) )
			{
				NextSibling = Id;
				break;
			}
		}
	}

	TWorldDocument::FBatch Batch( Document );
	bool bAll = true;
	for ( const int32 Root : Roots )
	{
		bool bMoved = false;
		switch ( Position )
		{
		case EWorldDropPosition::Inside: bMoved = Reparent( Document, Root, TargetId, WorldNoId, true ); break;
		case EWorldDropPosition::Before: bMoved = Reparent( Document, Root, SiblingParent, TargetId, true ); break;
		case EWorldDropPosition::After: bMoved = Reparent( Document, Root, SiblingParent, NextSibling, true ); break;
		}
		bAll = bAll && bMoved;
	}
	return bAll;
}

int32 NWorldEdit::GroupObjects( TWorldDocument& Document, const TArray<int32>& Ids, const FString& Name )
{
	const TArray<int32> Roots = NWorld::FilterToRoots( Document.Get(), Ids );
	if ( Roots.IsEmpty() ) return WorldNoId;

	// 親は、根の位置の中心。根が同じ親を持つなら、その親の子にする
	const int32 CommonParent = Document.Get().FindObject( Roots[0] )->Parent;
	bool bSameParent = true;
	FVector3 Center = FVector3::Zero;
	for ( const int32 Root : Roots )
	{
		if ( Document.Get().FindObject( Root )->Parent != CommonParent ) bSameParent = false;
		Center = Center + NWorld::ComputeWorldTransform( Document.Get(), Root ).Position;
	}
	Center = Center / static_cast<float32>( Roots.Num() );
	const int32 GroupParent = bSameParent && Document.Get().FindObject( CommonParent ) != nullptr ? CommonParent : WorldNoId;
	const FTransform ParentWorld = GroupParent != WorldNoId ? NWorld::ComputeWorldTransform( Document.Get(), GroupParent ) : FTransform::Identity;

	TWorldDocument::FBatch Batch( Document );
	const int32 Group = AddObject( Document, Name, GroupParent, FWorldTransform::FromTransform( NWorld::WorldToLocal( ParentWorld, FTransform( Center ) ) ) );
	if ( Group == WorldNoId ) return WorldNoId;
	// 親の並びでは、最初の根のあった位置へ
	if ( bSameParent ) Reparent( Document, Group, GroupParent, Roots[0], false );
	for ( const int32 Root : Roots ) Reparent( Document, Root, Group, WorldNoId, true );
	return Group;
}

bool NWorldEdit::UngroupObject( TWorldDocument& Document, int32 GroupId )
{
	const FWorldObject* Group = Document.Get().FindObject( GroupId );
	if ( Group == nullptr || Group->Model.IsSet() || Group->Billboard.IsSet() || Group->Sound.IsSet() || Group->Effect.IsSet() || Group->Collider.IsSet() || Group->Mesh.IsSet() || Group->Prefab.IsSet() || !Group->RawComponents.IsEmpty() ) return false;

	const int32 NewParent = Group->Parent;
	TArray<int32> Children;
	for ( const int32 Index : NWorld::GetChildIndices( Document.Get(), GroupId ) ) Children.Add( Document.Get().Objects[Index].Id );
	if ( Children.IsEmpty() ) return false;   // 子の無い空のオブジェクトは、グループではない（外すと、消えてしまう）

	TWorldDocument::FBatch Batch( Document );
	for ( const int32 Child : Children )
	{
		// グループのあった位置（グループの手前）へ。グループが最上位でなくても、同じ兄弟として並ぶ
		if ( !Reparent( Document, Child, NewParent, GroupId, true ) ) return false;
	}
	return RemoveObject( Document, GroupId );
}

NWorldEdit::FWorldClipboard NWorldEdit::CopyObjects( const FWorldDefinition& World, const TArray<int32>& Ids )
{
	FWorldClipboard Clipboard;
	for ( const int32 Root : NWorld::FilterToRoots( World, Ids ) )
	{
		bool bFirst = true;
		for ( const int32 Index : NWorld::GetSubtreeIndices( World, Root ) )
		{
			FWorldObject Copy = World.Objects[Index];
			if ( bFirst )
			{
				// 根は、ワールドでの姿勢にする（別の親の下へ貼っても、同じ場所に出るように）。親のいない根は、そのまま
				if ( Copy.Parent != WorldNoId ) Copy.Transform = FWorldTransform::FromTransform( NWorld::ComputeWorldTransform( World, Root ) );
				Copy.Parent = WorldNoId;
				bFirst = false;
			}
			Clipboard.Objects.Add( Copy );
		}
	}
	return Clipboard;
}

FWorldDefinition NWorldEdit::ExtractSubWorld( const FWorldDefinition& World, const TArray<int32>& Ids, const FVector3& Origin, const FString& Name )
{
	FWorldDefinition Empty;
	Empty.Name = Name;
	const FWorldClipboard Clipboard = CopyObjects( World, Ids );
	if ( Clipboard.IsEmpty() ) return Empty;
	TWorldDocument Scratch;
	Scratch.Reset( Empty );
	PasteObjects( Scratch, Clipboard, WorldNoId, FVector3::Zero - Origin );
	return Scratch.Get();
}

int32 NWorldEdit::ReplaceWithPrefab( TWorldDocument& Document, const TArray<int32>& Ids, const FVector3& Origin, const FString& PrefabPath, const FString& Name )
{
	const TArray<int32> Roots = NWorld::FilterToRoots( Document.Get(), Ids );
	if ( Roots.IsEmpty() ) return WorldNoId;
	int32 CommonParent = Document.Get().FindObject( Roots[0] )->Parent;
	for ( const int32 Root : Roots )
	{
		if ( Document.Get().FindObject( Root )->Parent != CommonParent ) CommonParent = WorldNoId;
	}
	TWorldDocument::FBatch Batch( Document );
	const int32 Id = AddObject( Document, Name, CommonParent );
	if ( Id == WorldNoId ) return WorldNoId;
	FWorldPrefab Prefab;
	Prefab.Path = PrefabPath;
	EditObject( Document, Id )->Prefab = Prefab;
	SetWorldTransform( Document, Id, FTransform( Origin ) );
	RemoveObjects( Document, Roots );
	return Id;
}

TArray<int32> NWorldEdit::UnpackPrefab( TWorldDocument& Document, int32 ObjectId, const FWorldDefinition& Prefab )
{
	const FWorldObject* Host = Document.Get().FindObject( ObjectId );
	if ( Host == nullptr || !Host->Prefab.IsSet() ) return TArray<int32>();
	const FTransform HostWorld = NWorld::ComputeWorldTransform( Document.Get(), ObjectId );

	// 中身の根（親のいないもの）。根の姿勢を、ホストを掛けたワールドでの姿勢にして貼ると、貼り付けが、ホストからの相対（元のまま）に直す
	TArray<int32> RootIds;
	for ( const FWorldObject& Object : Prefab.Objects )
	{
		if ( Object.Parent == WorldNoId ) RootIds.Add( Object.Id );
	}
	FWorldClipboard Clipboard = CopyObjects( Prefab, RootIds );
	for ( FWorldObject& Object : Clipboard.Objects )
	{
		if ( Object.Parent == WorldNoId ) Object.Transform = FWorldTransform::FromTransform( FTransform::Combine( HostWorld, Object.Transform.ToTransform() ) );
	}
	TWorldDocument::FBatch Batch( Document );
	const TArray<int32> NewRoots = PasteObjects( Document, Clipboard, ObjectId );
	EditObject( Document, ObjectId )->Prefab.Reset();
	return NewRoots;
}

TArray<int32> NWorldEdit::PasteObjects( TWorldDocument& Document, const FWorldClipboard& Clipboard, int32 ParentId, const FVector3& WorldOffset )
{
	TArray<int32> NewRoots;
	if ( Clipboard.IsEmpty() ) return NewRoots;
	if ( ParentId != WorldNoId && Document.Get().FindObject( ParentId ) == nullptr ) return NewRoots;
	const FTransform ParentWorld = ParentId != WorldNoId ? NWorld::ComputeWorldTransform( Document.Get(), ParentId ) : FTransform::Identity;

	FWorldDefinition& World = Document.BeginEdit();

	TArray<int32> OldIds;
	TArray<int32> NewIds;
	for ( const FWorldObject& Object : Clipboard.Objects )
	{
		OldIds.Add( Object.Id );
		NewIds.Add( World.AllocateId() );
	}
	const auto Remap = [&]( int32 OldId, bool& bOutInside )
	{
		int32 Position = INDEX_NONE;
		bOutInside = OldIds.Find( OldId, Position );
		return bOutInside ? NewIds[Position] : OldId;
	};

	for ( int32 i = 0; i < Clipboard.Objects.Num(); ++i )
	{
		FWorldObject Copy = Clipboard.Objects[i];
		Copy.Id = NewIds[i];
		bool bParentInside = false;
		const int32 Parent = Remap( Copy.Parent, bParentInside );
		if ( bParentInside ) Copy.Parent = Parent;
		else
		{
			// 根: 貼る先の親の下へ。ワールドでの位置をずらして、親の中のローカルの値にする
			Copy.Parent = ParentId;
			if ( ParentId == WorldNoId ) Copy.Transform.Position = Copy.Transform.Position + WorldOffset;
			else
			{
				FTransform RootWorld = Copy.Transform.ToTransform();
				RootWorld.Position = RootWorld.Position + WorldOffset;
				Copy.Transform = FWorldTransform::FromTransform( NWorld::WorldToLocal( ParentWorld, RootWorld ) );
			}
			NewRoots.Add( Copy.Id );
		}
		for ( FWorldProp& Prop : Copy.Props )
		{
			bool bInside = false;
			if ( Prop.Type == EWorldPropType::Object ) Prop.ObjectId = Remap( Prop.ObjectId, bInside );
		}
		if ( Copy.Effect.IsSet() )
		{
			bool bTargetInside = false;
			Copy.Effect.GetValue().Target = Remap( Copy.Effect.GetValue().Target, bTargetInside );
		}
		World.Objects.Add( Copy );
	}
	return NewRoots;
}

bool NWorldEdit::AddTag( TWorldDocument& Document, int32 ObjectId, const FString& Tag )
{
	const FWorldObject* Object = Document.Get().FindObject( ObjectId );
	if ( Object == nullptr ) return false;
	const FString Trimmed = Tag.TrimStartAndEnd();
	if ( Trimmed.IsEmpty() || Object->HasTag( Trimmed ) ) return false;
	return EditObject( Document, ObjectId )->AddTag( Trimmed );
}

bool NWorldEdit::RemoveTag( TWorldDocument& Document, int32 ObjectId, const FString& Tag )
{
	const FWorldObject* Object = Document.Get().FindObject( ObjectId );
	if ( Object == nullptr || !Object->HasTag( Tag ) ) return false;
	return EditObject( Document, ObjectId )->RemoveTag( Tag );
}

bool NWorldEdit::SetProp( TWorldDocument& Document, int32 ObjectId, const FWorldProp& Prop, const char* CoalesceKey )
{
	if ( Prop.Name.IsEmpty() ) return false;
	FWorldObject* Object = EditObject( Document, ObjectId, CoalesceKey );
	if ( Object == nullptr ) return false;
	Object->SetProp( Prop );
	return true;
}

bool NWorldEdit::RemoveProp( TWorldDocument& Document, int32 ObjectId, const FString& PropName )
{
	const FWorldObject* Object = Document.Get().FindObject( ObjectId );
	if ( Object == nullptr || Object->FindProp( PropName ) == nullptr ) return false;
	return EditObject( Document, ObjectId )->RemoveProp( PropName );
}

int32 NWorldEdit::SetCollisionRule( TWorldDocument& Document, const FString& Tag, bool bCollide )
{
	const FString Trimmed = Tag.TrimStartAndEnd();
	if ( Trimmed.IsEmpty() ) return INDEX_NONE;

	FWorldDefinition& World = Document.BeginEdit();
	for ( int32 i = 0; i < World.Settings.CollisionRules.Num(); ++i )
	{
		if ( World.Settings.CollisionRules[i].Tag == Trimmed )
		{
			World.Settings.CollisionRules[i].bCollide = bCollide;
			return i;
		}
	}
	FWorldCollisionRule Rule;
	Rule.Tag = Trimmed;
	Rule.bCollide = bCollide;
	World.Settings.CollisionRules.Add( Rule );
	return World.Settings.CollisionRules.Num() - 1;
}

bool NWorldEdit::RemoveCollisionRule( TWorldDocument& Document, int32 RuleIndex )
{
	if ( !Document.Get().Settings.CollisionRules.IsValidIndex( RuleIndex ) ) return false;
	Document.BeginEdit().Settings.CollisionRules.RemoveAt( RuleIndex );
	return true;
}

bool NWorldEdit::MoveCollisionRule( TWorldDocument& Document, int32 From, int32 To )
{
	const int32 Num = Document.Get().Settings.CollisionRules.Num();
	if ( !Document.Get().Settings.CollisionRules.IsValidIndex( From ) || To < 0 || To >= Num || From == To ) return false;
	TArray<FWorldCollisionRule>& Rules = Document.BeginEdit().Settings.CollisionRules;
	const FWorldCollisionRule Moving = Rules[From];
	Rules.RemoveAt( From );
	Rules.Insert( Moving, To );
	return true;
}
