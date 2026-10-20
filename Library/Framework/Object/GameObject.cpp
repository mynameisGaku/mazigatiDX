#include "GameObject.h"

#include <typeinfo>

#include "Library/Core/Interfaces/Inspector/IPropertyInspector.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Ecs/TransformWorld.h"
#include "Library/Framework/Scene/Scene.h"

DEFINE_LOG_CATEGORY( LogGameObject )

FString CGameObject::GetDebugName() const
{
	return HasCustomName()
		? FString::Printf( "%s '%s' #%llu", *GetTypeName(), *GetName(), static_cast<unsigned long long>( Id ) )
		: FString::Printf( "%s #%llu", *GetTypeName(), static_cast<unsigned long long>( Id ) );
}

#pragma region Lifetime

void CGameObject::SetTickEnabled( bool bEnabled )
{
	bTickEnabled = bEnabled;
	if ( FEcsWorld* World = GetEcsWorldIfAny() ) World->GetRecordMutable( EntitySlot ).bTickEnabled = bEnabled;
}

void CGameObject::SetVisible( bool bInVisible )
{
	bVisible = bInVisible;
	if ( FEcsWorld* World = GetEcsWorldIfAny() ) World->GetRecordMutable( EntitySlot ).bVisible = bInVisible;
}

void CGameObject::SetTransformSlotInternal( int32 Slot )
{
	TransformSlot = Slot;
	if ( FEcsWorld* World = GetEcsWorldIfAny() )
	{
		FEcsWorld::FEntityRecord& Record = World->GetRecordMutable( EntitySlot );
		Record.TransformSlot = Slot;
		Record.TransformNode = Slot != INDEX_NONE ? &Scene->GetTransformWorld().GetNode( Slot ) : nullptr;
	}
}

void CGameObject::Destroy()
{
	if ( bPendingDestroy ) return;
	bPendingDestroy = true;
	if ( FEcsWorld* World = GetEcsWorldIfAny() ) World->GetRecordMutable( EntitySlot ).bPendingDestroy = true;

	for ( CGameObject* Child : Children ) Child->Destroy();
}

void CGameObject::DetachAll()
{
	// コンポーネントは追加と逆の順番で外す
	for ( int32 i = Components.Num() - 1; i >= 0; --i )
	{
		Components[i].Component->OnDetach();
	}
	Components.Reset();

	if ( Parent != nullptr ) Parent->Children.RemoveSingle( this );
	Parent = nullptr;

	for ( CGameObject* Child : Children ) Child->Parent = nullptr;
	Children.Reset();

	if ( Scene != nullptr )
	{
		Scene->GetTransformWorld().MarkHierarchyDirty();
		// エンティティを返す（持っていたデータコンポーネントもすべて消える）
		if ( EntitySlot != INDEX_NONE )
		{
			Scene->GetEcsWorld().DestroyEntity( EntitySlot );
			EntitySlot = INDEX_NONE;
		}
	}
}

FEcsWorld* CGameObject::GetEcsWorldIfAny() const
{
	return Scene != nullptr && EntitySlot != INDEX_NONE ? &Scene->GetEcsWorld() : nullptr;
}

FEcsWorld* CGameObject::GetEcsWorldForData( const char* What ) const
{
	FEcsWorld* World = GetEcsWorldIfAny();
	if ( World == nullptr ) LOG( LogGameObject, Warning, "%s: %s is not in a scene yet. Data components can be used from OnSpawn onwards.", What, *GetDebugName() );
	return World;
}

void CGameObject::NotifySceneAssigned()
{
	// OnOwnerSceneChanged の中でコンポーネントが増えても大丈夫なよう、件数を固定してインデックスで回す
	const int32 NumComponents = Components.Num();
	for ( int32 i = 0; i < NumComponents; ++i ) Components[i].Component->OnOwnerSceneChanged( Scene );
}

#pragma endregion

#pragma region Hierarchy

bool CGameObject::IsAncestorOf( const CGameObject* Object ) const
{
	for ( const CGameObject* Ancestor = Object ? Object->Parent : nullptr; Ancestor != nullptr; Ancestor = Ancestor->Parent )
	{
		if ( Ancestor == this ) return true;
	}
	return false;
}

bool CGameObject::AttachToParent( CGameObject* NewParent )
{
	if ( NewParent == Parent ) return true;

	if ( NewParent != nullptr )
	{
		if ( NewParent == this || IsAncestorOf( NewParent ) )
		{
			LOG( LogGameObject, Warning, "AttachToParent: %s cannot be attached to its own descendant.", *GetName() );
			return false;
		}
		if ( Scene == nullptr || NewParent->Scene != Scene )
		{
			LOG( LogGameObject, Warning, "AttachToParent: %s and %s are not in the same scene.", *GetName(), *NewParent->GetName() );
			return false;
		}
		if ( bPendingDestroy || NewParent->bPendingDestroy )
		{
			LOG( LogGameObject, Warning, "AttachToParent: %s or its new parent is being destroyed.", *GetName() );
			return false;
		}
	}

	if ( Parent != nullptr ) Parent->Children.RemoveSingle( this );
	Parent = NewParent;
	if ( Parent != nullptr ) Parent->Children.Add( this );
	if ( Scene != nullptr ) Scene->GetTransformWorld().MarkHierarchyDirty();   // ワールド値の階層を組み直す

	if ( Parent != nullptr ) LOG( LogGameObject, Verbose, "%s attached to %s", *GetDebugName(), *Parent->GetDebugName() );
	else LOG( LogGameObject, Verbose, "%s detached from its parent", *GetDebugName() );
	return true;
}

#pragma endregion

#pragma region Components

void CGameObject::AddComponentInternal( TUniquePtr<CComponent> Component )
{
	CComponent* Raw = Component.Get();
	Raw->Owner = this;
	Components.Add( { std::move( Component ), dynamic_cast<ITickable*>( Raw ) } );
	Raw->OnAttach();
	LOG( LogGameObject, Verbose, "Component %s added to %s%s", *Raw->GetTypeName(), *GetDebugName(), Components.Last().Tickable ? " (ticks)" : "" );
}

void CGameObject::TickComponents( float32 DeltaTime, ETickGroup Group )
{
	// Tick 中に AddComponent されても大丈夫なよう、件数を固定してインデックスで回す
	const int32 NumComponents = Components.Num();
	for ( int32 i = 0; i < NumComponents; ++i )
	{
		ITickable* Tickable = Components[i].Tickable;
		if ( Tickable == nullptr || Components[i].Component->IsPendingDestroy() ) continue;
		if ( Tickable->GetTickGroup() != Group || !Tickable->IsTickable() ) continue;

		PROFILE_SCOPE_LAZY( ComponentScope, FProfileScope( "Component", Scene ? Scene->GetProfilerOwnerId() : 0u,
														   typeid( *Components[i].Component.Get() ).name(), Id, GetProfilerLabelId( *NProfiler::GRecorder ) ) );
		Tickable->Tick( DeltaTime );
	}
}

void CGameObject::TickObject( float32 DeltaTime )
{
	if ( bTickEnabled )
	{
		PROFILE_SCOPE_LAZY( ObjectScope, MakeProfileScope( "Object.Tick" ) );
		TickComponents( DeltaTime, ETickGroup::PreUpdate );
		Tick( DeltaTime );
		TickComponents( DeltaTime, ETickGroup::PostUpdate );
	}
	RemoveDestroyedComponents();
}

void CGameObject::DrawObject()
{
	PROFILE_SCOPE_LAZY( ObjectScope, MakeProfileScope( "Object.Draw" ) );
	Draw();
}

FProfileScope CGameObject::MakeProfileScope( const char* Category ) const
{
	IProfiler& Recorder = *NProfiler::GRecorder;
	// typeid().name() はプログラム中ずっと有効なポインタなので、ポインタで ID に変換できる
	return FProfileScope( Recorder.InternLiteral( Category ), Scene ? Scene->GetProfilerOwnerId() : 0u,
						  Recorder.InternLiteral( typeid( *this ).name() ), Id, GetProfilerLabelId( Recorder ) );
}

uint32 CGameObject::GetProfilerLabelId( IProfiler& Recorder ) const
{
	// SetName された名前だけを表示名にする（無ければ型名が Name の欄に出るので 0）
	if ( ProfilerLabelRevision != GetNameRevision() )
	{
		ProfilerLabelId = HasCustomName() ? Recorder.InternString( *GetName() ) : 0u;
		ProfilerLabelRevision = GetNameRevision();
	}
	return ProfilerLabelId;
}

void CGameObject::RemoveDestroyedComponents()
{
	for ( int32 i = Components.Num() - 1; i >= 0; --i )
	{
		if ( !Components[i].Component->IsPendingDestroy() ) continue;
		LOG( LogGameObject, Verbose, "Component %s removed from %s", *Components[i].Component->GetTypeName(), *GetDebugName() );
		Components[i].Component->OnDetach();
		Components.RemoveAt( i );
	}
}

#pragma endregion

#pragma region Properties

void CGameObject::SetDrawOrder( int32 InDrawOrder )
{
	if ( DrawOrder == InDrawOrder ) return;
	DrawOrder = InDrawOrder;
	if ( Scene != nullptr ) Scene->MarkDrawOrderDirty();
}

void CGameObject::InspectProperties( IPropertyInspector& Inspector )
{
	Inspector.Text( "Id", FString::FromInt( static_cast<int64>( Id ) ) );
	// システム用の記録にも写すので、メンバを直接ではなく setter 経由で変える
	bool bNewTickEnabled = bTickEnabled;
	if ( Inspector.Bool( "Tick Enabled", bNewTickEnabled ) ) SetTickEnabled( bNewTickEnabled );
	bool bNewVisible = bVisible;
	if ( Inspector.Bool( "Visible", bNewVisible ) ) SetVisible( bNewVisible );

	int32 NewDrawOrder = DrawOrder;
	if ( Inspector.Int( "Draw Order", NewDrawOrder ) ) SetDrawOrder( NewDrawOrder );
}

#pragma endregion

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

void CGameObject::OnSpawn()
{
}

void CGameObject::Tick( float32 DeltaTime )
{
}

void CGameObject::Draw()
{
}

void CGameObject::OnDestroy()
{
}

#pragma endregion
