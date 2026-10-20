#pragma once
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Sound.h"
#include "Library/Framework/Audio/AudioTypes.h"
#include "Library/Framework/Component/Component.h"

//=============================================================================
// CAudioListenerComponent
//   このオブジェクトの位置と向きを、毎フレーム CAudioSystem のリスナー（聞き手）にする。カメラのオブジェクトに付ける。
//   CTransformComponent が必要。
//
//   Camera->AddComponent<CTransformComponent>();
//   Camera->AddComponent<CAudioListenerComponent>();
//=============================================================================
class CAudioListenerComponent : public CComponent, public ITickable
{
public:
	void Tick( float32 DeltaTime ) override;
	ETickGroup GetTickGroup() const override { return ETickGroup::PostUpdate; }
};

//=============================================================================
// CAudioSourceComponent
//   このオブジェクトに付く音。オブジェクトの位置で鳴り（毎フレーム追従する）、オブジェクトが破棄されると止まる。
//   焚き火・機械・エンジン音など、物にくっついた音に使う。CTransformComponent が必要。
//
//   CAudioSourceComponent* Source = Campfire->AddComponent<CAudioSourceComponent>( FireLoop );
//   Source->SetLoop( true );
//   Source->SetRange( 2.0f, 25.0f );
//   Source->Play();            // AutoPlay が true（既定）なら、シーンに入った時点で鳴る
//   Source->Stop( 0.5f );
//
//   バスは Ambient（既定。SetBus で変える）。距離による減衰と左右の定位が付く。
//=============================================================================
class CAudioSourceComponent : public CComponent, public ITickable
{
public:
	explicit CAudioSourceComponent( const TSharedPtr<CSound>& InSound ) : Sound( InSound ) {}

	// 設定（Play の前に）
	void SetSound( const TSharedPtr<CSound>& InSound ) { Sound = InSound; }
	void SetLoop( bool bInLoop ) { Params.bLoop = bInLoop; }
	void SetVolume( float32 Volume ) { Params.Volume = Volume; }
	void SetPitch( float32 Pitch ) { Params.Pitch = Pitch; }
	void SetBus( EAudioBus Bus ) { Params.Bus = Bus; }
	void SetRange( float32 MinDistance, float32 MaxDistance ) { Params.MinDistance = MinDistance; Params.MaxDistance = MaxDistance; }
	void SetRolloff( EAudioRolloff Rolloff ) { Params.Rolloff = Rolloff; }
	void SetFadeInTime( float32 Seconds ) { Params.FadeInTime = Seconds; }
	void SetAutoPlay( bool bInAutoPlay ) { bAutoPlay = bInAutoPlay; }
	// 全部の指定（FSoundPlayParams の bSpatial / Position は、オブジェクトの位置で上書きされる）
	void SetParams( const FSoundPlayParams& InParams ) { Params = InParams; }

	// 鳴らす（すでに鳴っていれば何もしない）／止める
	void Play();
	void Stop( float32 FadeOut = 0.0f );
	bool IsPlaying() const;
	// 鳴らしている音。動かしたい（音量のフェードなど）ときは CAudioSystem の関数に渡す
	FORCEINLINE FSoundHandle GetHandle() const { return Handle; }

	//-------------------------------------------------------------------------
	// CComponent / ITickable
	//-------------------------------------------------------------------------
	void OnAttach() override;
	void OnDetach() override;
	void OnOwnerSceneChanged( CScene* NewScene ) override;
	void Tick( float32 DeltaTime ) override;
	ETickGroup GetTickGroup() const override { return ETickGroup::PostUpdate; }

private:
	TSharedPtr<CSound> Sound;
	FSoundPlayParams Params = DefaultParams();
	FSoundHandle Handle;
	bool bAutoPlay = true;
	bool bStarted = false;   // 自動で鳴らす処理をしたか

	static FSoundPlayParams DefaultParams()
	{
		FSoundPlayParams Result;
		Result.Bus = EAudioBus::Ambient;
		Result.bSpatial = true;
		return Result;
	}
};
