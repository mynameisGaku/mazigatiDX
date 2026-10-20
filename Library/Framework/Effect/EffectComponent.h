#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Component/Component.h"
#include "Library/Framework/Effect/EffectBackend.h"

//=============================================================================
// FEffectPlayer
//   オブジェクトに付く、再生中のエフェクト（ECS のデータ）。描画システム（"Effect.Draw"）が、これを描く。
//   CEffectComponent が付ける・外す（自分で付けなくてよい）。
//=============================================================================
struct FEffectPlayer
{
	TSharedPtr<IEffectPlayback> Playback;
	bool bAutoDraw = true;
};

//=============================================================================
// CEffectComponent
//   このオブジェクトの位置に出るエフェクト（炎・煙・火花・魔法の光など）。オブジェクトに付いて動く（毎フレーム、位置・向き・大きさに追従する）。
//   オブジェクトがシーンから外れる・破棄されると、止まって消える。CTransformComponent が必要。
//
//   CEffectComponent* Fire = Campfire->AddComponent<CEffectComponent>( "Effects/Fire.effect.json" );
//   Fire->SetScale( 2.0f );
//   Fire->Play();                  // AutoPlay が true（既定）なら、シーンに入った時点で始まる
//   Fire->Stop();                  // 出すのをやめる（残りの粒子は、自然に消える）
//
//   1 つのオブジェクトに付けるエフェクトの部品は 1 つ（描画のデータが 1 つだけのため）。いくつも出したいときは、子のオブジェクトに付ける。
//   使い切りの演出（爆発など）は、Loop を切ったエフェクトを、SetDestroyOwnerWhenFinished( true ) で置くと、終わったときにオブジェクトごと消える。
//   中身は IEffectBackend（EffectBackend.h）が決める: 既定は内蔵のパーティクル（.effect.json）。SetBackends で、別の一覧（外部のライブラリ入り）に変えられる。
//=============================================================================
class CEffectComponent : public CComponent, public ITickable
{
public:
	explicit CEffectComponent( const FString& InPath = FString() ) : Path( InPath ) {}

	// 設定（Play の前に）
	void SetPath( const FString& InPath ) { Path = InPath; }
	const FString& GetPath() const { return Path; }
	void SetParams( const FEffectPlayParams& InParams ) { Params = InParams; }
	const FEffectPlayParams& GetParams() const { return Params; }
	void SetScale( float32 Scale ) { Params.Scale = Scale; }
	void SetSpeed( float32 Speed ) { Params.Speed = Speed; }
	void SetAutoPlay( bool bInAutoPlay ) { bAutoPlay = bInAutoPlay; }
	void SetDestroyOwnerWhenFinished( bool bInDestroy ) { bDestroyOwnerWhenFinished = bInDestroy; }
	// 使うバックエンドの一覧（nullptr で、アプリ全体の FEffectBackendRegistry::Global()）。一覧は、このコンポーネントより長く生きていること
	void SetBackends( const FEffectBackendRegistry* InBackends ) { Backends = InBackends; }

	// 最初から始める（再生中なら、捨てて作り直す）。読み込めなければ false
	bool Play();
	// 出すのをやめる。bImmediate なら、出ている粒子もすぐ消す。止めたものは、自動で始め直さない
	void Stop( bool bImmediate = false );
	bool IsPlaying() const;
	// 出し終えて、残りも消えた
	bool IsFinished() const;
	TSharedPtr<IEffectPlayback> GetPlayback() const { return Playback; }

	//-------------------------------------------------------------------------
	// CComponent / ITickable
	//-------------------------------------------------------------------------
	void OnDetach() override;
	void OnOwnerSceneChanged( CScene* NewScene ) override;
	void Tick( float32 DeltaTime ) override;
	ETickGroup GetTickGroup() const override { return ETickGroup::PostUpdate; }

private:
	void Release();

	FString Path;
	FEffectPlayParams Params;
	const FEffectBackendRegistry* Backends = nullptr;
	TSharedPtr<IEffectPlayback> Playback;
	bool bAutoPlay = true;
	bool bStarted = false;   // 自動で始める処理をしたか
	bool bDestroyOwnerWhenFinished = false;
};
