#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Effect/ParticleEffect.h"

//=============================================================================
// CEffectAsset
//   読み込んだパーティクルのエフェクト（.effect.json）。CAssetManager::LoadEffect で読み込む。
//   中身は FParticleEffectDefinition（純粋なデータ。ParticleEffect.h）。再生は IEffectBackend（EffectBackend.h）が受け持つ。
//
//   TSharedPtr<CEffectAsset> Fire = Assets->LoadEffect( "Effects/Fire.effect.json" );
//   NParticle::Start( Simulation, Fire->GetDefinitionShared() );
//
//   定義を作り直して保存するときは Assets->SaveEffect( Path, Definition )（保存したパスのキャッシュは捨てるので、次の LoadEffect は新しい内容を読む）。
//=============================================================================
class CEffectAsset : public CAsset
{
public:
	CEffectAsset( const FString& InPath, const FParticleEffectDefinition& InDefinition, const TArray<FString>& InLoadWarnings );

	FORCEINLINE const FParticleEffectDefinition& GetDefinition() const { return *Definition; }
	// シミュレーションが参照し続けるので、再生には、こちらを渡す
	FORCEINLINE const TSharedPtr<const FParticleEffectDefinition>& GetDefinitionShared() const { return Definition; }
	// 読み込みのとき、直したこと・読めなかった項目
	FORCEINLINE const TArray<FString>& GetLoadWarnings() const { return LoadWarnings; }

	FString Describe() const override;
	int64 GetMemoryBytes() const override;

private:
	TSharedPtr<const FParticleEffectDefinition> Definition;
	TArray<FString> LoadWarnings;
};
