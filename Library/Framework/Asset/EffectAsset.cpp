#include "EffectAsset.h"

CEffectAsset::CEffectAsset( const FString& InPath, const FParticleEffectDefinition& InDefinition, const TArray<FString>& InLoadWarnings )
	: CAsset( EAssetType::Effect, InPath )
	, Definition( MakeShared<FParticleEffectDefinition>( InDefinition ) )
	, LoadWarnings( InLoadWarnings )
{
}

FString CEffectAsset::Describe() const
{
	int32 MaxParticles = 0;
	for ( const FParticleEmitterDefinition& Emitter : Definition->Emitters ) MaxParticles += Emitter.MaxParticles;
	return FString::Printf( "%d emitter(s), up to %d particle(s), %s", Definition->Emitters.Num(), MaxParticles, Definition->Duration > 0.0f ? *FString::Printf( "%.1f s%s", Definition->Duration, Definition->bLoop ? " (loop)" : "" ) : "endless" );
}

int64 CEffectAsset::GetMemoryBytes() const
{
	// 定義そのものの大きさ（粒子は、再生しているものが持つ）
	int64 Bytes = static_cast<int64>( sizeof( FParticleEffectDefinition ) );
	for ( const FParticleEmitterDefinition& Emitter : Definition->Emitters )
	{
		Bytes += static_cast<int64>( sizeof( FParticleEmitterDefinition ) ) + Emitter.Name.Len() + Emitter.Texture.Len() + static_cast<int64>( Emitter.Bursts.Num() ) * static_cast<int64>( sizeof( FParticleBurst ) );
	}
	return Bytes;
}
