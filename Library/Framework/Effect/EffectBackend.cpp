#include "EffectBackend.h"

#include "Library/Framework/Effect/ParticleEffectBackend.h"

//=============================================================================
// FEffectBackendRegistry
//=============================================================================
void FEffectBackendRegistry::Add( const TSharedPtr<IEffectBackend>& Backend )
{
	if ( !Backend.IsValid() ) return;
	for ( TSharedPtr<IEffectBackend>& Existing : Backends )
	{
		if ( FString( Existing->GetName() ) == FString( Backend->GetName() ) )
		{
			Existing = Backend;
			return;
		}
	}
	Backends.Add( Backend );
}

bool FEffectBackendRegistry::Remove( const FString& Name )
{
	for ( int32 i = 0; i < Backends.Num(); ++i )
	{
		if ( FString( Backends[i]->GetName() ) == Name )
		{
			Backends.RemoveAt( i );
			return true;
		}
	}
	return false;
}

IEffectBackend* FEffectBackendRegistry::Find( const FString& Path ) const
{
	for ( const TSharedPtr<IEffectBackend>& Backend : Backends )
	{
		if ( Backend->CanPlay( Path ) ) return Backend.Get();
	}
	return nullptr;
}

TSharedPtr<IEffectPlayback> FEffectBackendRegistry::Play( const FString& Path, CAssetManager* Assets, const FEffectPlayParams& Params, const FTransform& World ) const
{
	// 扱えると言ったバックエンドが読み込めなかったら、次のバックエンドに聞く（拡張子が重なる外部ライブラリの置き換えのため）
	for ( const TSharedPtr<IEffectBackend>& Backend : Backends )
	{
		if ( !Backend->CanPlay( Path ) ) continue;
		if ( TSharedPtr<IEffectPlayback> Playback = Backend->Play( Path, Assets, Params, World ); Playback.IsValid() ) return Playback;
	}
	return nullptr;
}

FEffectBackendRegistry& FEffectBackendRegistry::Global()
{
	static FEffectBackendRegistry Registry = CreateWithBuiltins();
	return Registry;
}

FEffectBackendRegistry FEffectBackendRegistry::CreateWithBuiltins()
{
	FEffectBackendRegistry Registry;
	NEffect::RegisterBuiltinBackends( Registry );
	return Registry;
}

void NEffect::RegisterBuiltinBackends( FEffectBackendRegistry& Registry )
{
	Registry.Add( MakeShared<FParticleEffectBackend>() );
}
