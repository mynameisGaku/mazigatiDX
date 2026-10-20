#include "SubsystemCollection.h"

#include <typeinfo>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Profiler/ProfilerScope.h"

DEFINE_LOG_CATEGORY_STATIC( LogSubsystem, Log )

void FSubsystemCollection::InitializeAll()
{
	for ( int32 i = 0; i < Entries.Num(); ++i )
	{
		InitializeEntry( i );
	}

	// 初期化された順に並べて 1 行で残す（どのサブシステムが有効か、順番はどうか、を後から確認できる）
	TArray<FString> Names;
	for ( const CSubsystem* Subsystem : InitializedSubsystems ) Names.Add( Subsystem->GetTypeName() );
	// 何も無いコレクション（サブシステムを使わないシーンなど）は目立たせない
	const ELogVerbosity Verbosity = Names.IsEmpty() ? ELogVerbosity::Verbose : ELogVerbosity::Log;
	if ( !LogSubsystem.IsSuppressed( Verbosity ) )
	{
		CLogManager::Get().Log( LogSubsystem, Verbosity, __FILE__, __LINE__, __FUNCTION__, "[%s] %d subsystem(s) initialized: %s",
								*Label, Names.Num(), Names.IsEmpty() ? "(none)" : *FString::Join( Names, ", " ) );
	}
}

void FSubsystemCollection::NotifySkipped( const CSubsystem& Subsystem ) const
{
	LOG( LogSubsystem, Verbose, "[%s] Skipped: %s (ShouldCreateSubsystem returned false)", *Label, *Subsystem.GetTypeName() );
}

CSubsystem* FSubsystemCollection::InitializeEntry( int32 Index )
{
	FEntry& Entry = Entries[Index];
	CSubsystem* Subsystem = Entry.Subsystem.Get();

	switch ( Entry.State )
	{
	case EState::Initialized:
		return Subsystem;

	case EState::Initializing:
		LOG( LogSubsystem, Error, "[%s] Circular dependency detected: %s", *Label, *Subsystem->GetTypeName() );
		return nullptr;

	case EState::Uninitialized:
		break;
	}

	Entry.State = EState::Initializing;
	// 依存先の Initialize で Entries は増減しないので Entry の参照は有効なまま
	Subsystem->Initialize( *this );
	Entry.State = EState::Initialized;

	InitializedSubsystems.Add( Subsystem );
	if ( ITickable* Tickable = dynamic_cast<ITickable*>( Subsystem ) ) Tickables.Add( Tickable );

	LOG( LogSubsystem, Verbose, "[%s] Initialized: %s", *Label, *Subsystem->GetTypeName() );
	return Subsystem;
}

CSubsystem* FSubsystemCollection::InitializeMatching( bool ( *Predicate )( CSubsystem* ) )
{
	for ( int32 i = 0; i < Entries.Num(); ++i )
	{
		if ( Predicate( Entries[i].Subsystem.Get() ) ) return InitializeEntry( i );
	}
	return nullptr;
}

void FSubsystemCollection::Deinitialize()
{
	if ( !InitializedSubsystems.IsEmpty() )
	{
		LOG( LogSubsystem, Log, "[%s] Deinitializing %d subsystem(s)", *Label, InitializedSubsystems.Num() );
	}

	for ( int32 i = InitializedSubsystems.Num() - 1; i >= 0; --i )
	{
		InitializedSubsystems[i]->Deinitialize();
		LOG( LogSubsystem, Verbose, "[%s] Deinitialized: %s", *Label, *InitializedSubsystems[i]->GetTypeName() );
	}

	// 依存される側を後に残すため、初期化と逆の順番で破棄する
	for ( int32 i = InitializedSubsystems.Num() - 1; i >= 0; --i )
	{
		const int32 Index = Entries.IndexOfByPredicate( [&]( const FEntry& Entry ) { return Entry.Subsystem.Get() == InitializedSubsystems[i]; } );
		if ( Index != INDEX_NONE ) Entries.RemoveAt( Index );
	}

	Tickables.Reset();
	InitializedSubsystems.Reset();
	Entries.Reset();
}

void FSubsystemCollection::Tick( float32 DeltaTime, ETickGroup Group )
{
	// 記録中なら、持ち主の名前を ID にしておく（計測区間の Owner になる）
	const uint32 OwnerId = NProfiler::GRecorder ? NProfiler::GRecorder->InternString( *Label ) : 0u;

	for ( int32 i = 0; i < Tickables.Num(); ++i )
	{
		ITickable* Tickable = Tickables[i];
		if ( Tickable->GetTickGroup() != Group || !Tickable->IsTickable() ) continue;

		// typeid().name() はプログラム中ずっと有効なポインタ。型名が Name になる
		PROFILE_SCOPE_LAZY( SubsystemScope, FProfileScope( "Subsystem", OwnerId, typeid( *Tickable ).name() ) );
		Tickable->Tick( DeltaTime );
	}
}
