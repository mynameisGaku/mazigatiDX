#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Audio/AudioBackend.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Log/LogCategory.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Sound.h"
#include "Library/Framework/Audio/AudioTypes.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

DECLARE_LOG_CATEGORY_EXTERN( LogAudio, Log )

// 音楽の再生の指定
struct FMusicParams
{
	float32 Volume = 1.0f;
	float32 CrossFadeTime = 1.0f;   // 前の音楽を消しながら、新しい音楽を上げていく時間（秒）。0 なら即座に切り替える
	bool bLoop = true;
	bool bRestartIfSame = false;    // すでに同じ音楽が鳴っていても、最初からやり直す
};

// 環境音の再生の指定（ループする、長いフェードの音）
struct FAmbientParams
{
	float32 Volume = 1.0f;
	float32 Pitch = 1.0f;
	float32 FadeInTime = 2.0f;
	bool bSpatial = false;          // true なら Position に置く（風車の音、滝の音など）
	FVector3 Position = FVector3::Zero;
	float32 MinDistance = 3.0f;
	float32 MaxDistance = 40.0f;
	EAudioRolloff Rolloff = EAudioRolloff::Linear;
};

// 鳴らしている音 1 つの情報（デバッグ表示・確認用）
struct FVoiceInfo
{
	FSoundHandle Handle;
	FString SoundPath;
	EAudioBus Bus = EAudioBus::Sfx;
	float32 Volume = 0.0f;   // 最終的にバックエンドへ渡している音量（バス・フェード・距離をすべて掛けたもの）
	float32 Pan = 0.0f;
	float32 Pitch = 1.0f;
	bool bLooping = false;
	bool bPaused = false;
	bool bFading = false;
	bool bSpatial = false;
	float32 Age = 0.0f;      // 鳴らし始めてからの秒数
};

//=============================================================================
// CAudioSystem
//   サウンドを鳴らす・管理するシステム（アプリで 1 つ、自動で生成される）。
//
//   CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
//   TSharedPtr<CSound> Jump = Assets->LoadSound( "Sound/Jump.wav" );
//
//   // 効果音
//   Audio->Play( Jump );                                     // そのまま
//   FSoundPlayParams P;  P.Volume = 0.8f;  P.Pan = -0.5f;  P.Pitch = 1.2f;  P.PitchVariation = 0.05f;
//   Audio->Play( Jump, P );                                  // 音量・パン・ピッチ・ばらつき
//   Audio->PlayAt( Explosion, FVector3( 10, 0, 0 ) );        // 3D の位置（距離で音量、左右でパン）
//
//   // 音楽（自動でクロスフェード）
//   Audio->PlayMusic( Theme );
//   Audio->PlayMusic( BossTheme, { 1.0f, 2.5f } );           // 2.5 秒かけて入れ替える
//   Audio->StopMusic( 1.5f );
//
//   // 環境音（ループ。重ねて鳴らせる）
//   FSoundHandle Wind = Audio->PlayAmbient( WindLoop );
//   Audio->SetVolume( Wind, 0.3f, 4.0f );                    // 4 秒かけて小さくする
//   Audio->Stop( Wind, 3.0f );                               // 3 秒かけて止める
//
//   // 音量の調整（バスごと。Master は全体）
//   Audio->SetBusVolume( EAudioBus::Music, 0.5f );
//   Audio->SetBusVolume( EAudioBus::Master, 0.0f, 0.5f );    // 0.5 秒かけて無音に（フェードアウトして一時停止、など）
//   Audio->SetPaused( true );                                // ポーズ画面（bPausable = false の音は止まらない）
//
//   - 音の同時再生は、同じ CSound を重ねて鳴らせる（Stream で読み込んだ音は除く）。全体の数は SetMaxVoices、
//     同じ音の数は FSoundPlayParams::MaxInstances で制限でき、超えたら優先度の低いものから止める
//   - 戻り値の FSoundHandle で、鳴らしたあとの音を操作する。鳴り終わった音の FSoundHandle は無効になる
//   - 音を鳴らすのはメインスレッドだけ（DxLib はスレッドセーフではない）。フェードは毎フレームの更新で進む
//=============================================================================
class CAudioSystem : public CAppSubsystem, public ITickable
{
public:
	CAudioSystem();
	~CAudioSystem() override;

	//-------------------------------------------------------------------------
	// CAppSubsystem / ITickable
	//-------------------------------------------------------------------------
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;
	void Tick( float32 DeltaTime ) override;
	ETickGroup GetTickGroup() const override { return ETickGroup::PostUpdate; }

	// 時間を進める（Tick と同じ。バックエンドを差し替えた確認で、時間を自分で進めるために公開している）
	void Update( float32 DeltaSeconds );
	// 確認用: バックエンドを差し替える（nullptr で元に戻す）
	void SetBackendOverride( IAudioBackend* InBackend );

	//-------------------------------------------------------------------------
	// 鳴らす
	//-------------------------------------------------------------------------
	// 鳴らせなければ無効な FSoundHandle（読み込み中・数の制限・間隔の制限・バックエンドが無い、など）
	FSoundHandle Play( const TSharedPtr<CSound>& Sound, const FSoundPlayParams& Params = FSoundPlayParams() );
	// Position に置いて鳴らす（Params の bSpatial / Position を上書きする）
	FSoundHandle PlayAt( const TSharedPtr<CSound>& Sound, const FVector3& Position, const FSoundPlayParams& Params = FSoundPlayParams() );

	// 止める。FadeOut 秒かけて音量を下げてから止める（0 なら即座に）
	void Stop( FSoundHandle Handle, float32 FadeOut = 0.0f, EFadeCurve Curve = EFadeCurve::Linear );
	void StopAll( float32 FadeOut = 0.0f );
	void StopBus( EAudioBus Bus, float32 FadeOut = 0.0f );
	// その音（CSound）を鳴らしているものすべて
	void StopSound( const TSharedPtr<CSound>& Sound, float32 FadeOut = 0.0f );

	bool IsPlaying( FSoundHandle Handle ) const;

	//-------------------------------------------------------------------------
	// 鳴らしている音の操作（FadeTime 秒かけて変える。0 なら即座に）
	//-------------------------------------------------------------------------
	void SetVolume( FSoundHandle Handle, float32 Volume, float32 FadeTime = 0.0f, EFadeCurve Curve = EFadeCurve::Linear );
	void SetPitch( FSoundHandle Handle, float32 Pitch, float32 FadeTime = 0.0f, EFadeCurve Curve = EFadeCurve::Linear );
	void SetPan( FSoundHandle Handle, float32 Pan, float32 FadeTime = 0.0f, EFadeCurve Curve = EFadeCurve::Linear );
	// 3D の音の位置を動かす（bSpatial でない音は、これで空間の音になる）
	void SetPosition( FSoundHandle Handle, const FVector3& Position );
	// 一時停止と再開（位置は覚えている）
	void Pause( FSoundHandle Handle );
	void Resume( FSoundHandle Handle );

	//-------------------------------------------------------------------------
	// バス（音の種類ごとの音量）
	//-------------------------------------------------------------------------
	void SetBusVolume( EAudioBus Bus, float32 Volume, float32 FadeTime = 0.0f, EFadeCurve Curve = EFadeCurve::Linear );
	float32 GetBusVolume( EAudioBus Bus ) const;
	void SetBusMuted( EAudioBus Bus, bool bMuted );
	bool IsBusMuted( EAudioBus Bus ) const;
	// 全体の音量（Master バス）
	FORCEINLINE void SetMasterVolume( float32 Volume, float32 FadeTime = 0.0f ) { SetBusVolume( EAudioBus::Master, Volume, FadeTime ); }
	FORCEINLINE float32 GetMasterVolume() const { return GetBusVolume( EAudioBus::Master ); }

	// 全体の一時停止（bPausable が false の音は止まらない）。Resume で続きから再生する
	void SetPaused( bool bPaused );
	FORCEINLINE bool IsPaused() const { return bGlobalPaused; }
	// バスごとの一時停止
	void SetBusPaused( EAudioBus Bus, bool bPaused );

	//-------------------------------------------------------------------------
	// 音楽
	//-------------------------------------------------------------------------
	FSoundHandle PlayMusic( const TSharedPtr<CSound>& Music, const FMusicParams& Params = FMusicParams() );
	void StopMusic( float32 FadeOut = 1.0f );
	FORCEINLINE FSoundHandle GetCurrentMusic() const { return MusicHandle; }

	//-------------------------------------------------------------------------
	// 環境音
	//-------------------------------------------------------------------------
	FSoundHandle PlayAmbient( const TSharedPtr<CSound>& Sound, const FAmbientParams& Params = FAmbientParams() );

	//-------------------------------------------------------------------------
	// リスナー（聞き手。ふつうはカメラの位置と向き）
	//-------------------------------------------------------------------------
	void SetListener( const FVector3& Position, const FVector3& Forward = FVector3::Forward, const FVector3& Up = FVector3::Up );
	FORCEINLINE const FAudioListener& GetListener() const { return Listener; }

	//-------------------------------------------------------------------------
	// 設定・情報
	//-------------------------------------------------------------------------
	// 同時に鳴らせる音の数（既定 48）。超えたら、優先度が低く、小さく、古いものから止める
	void SetMaxVoices( int32 Max ) { MaxVoices = Max > 1 ? Max : 1; }
	// ばらつきの乱数の種（同じ種なら同じ結果になる）
	void SetRandomSeed( uint32 Seed ) { RandomState = Seed != 0 ? Seed : 1u; }
	int32 GetNumVoices() const { return NumActive; }
	TArray<FVoiceInfo> GetVoiceInfos() const;

private:
	struct FVoice
	{
		bool bInUse = false;
		uint32 Generation = 1;
		TSharedPtr<CSound> Sound;
		int32 BackendVoice = NAsset::InvalidHandle;
		EAudioBus Bus = EAudioBus::Sfx;
		int32 Priority = 0;
		bool bLoop = false;
		bool bPausable = true;
		bool bPaused = false;       // いま一時停止中（バックエンドは止めてある）
		bool bUserPaused = false;   // Pause( Handle ) で止められた
		bool bStopWhenFaded = false;

		FAudioFade Volume;   // 指定した音量
		FAudioFade Pitch;
		FAudioFade Pan;
		FAudioFade Gain;     // フェードイン・フェードアウト用（0..1）

		bool bSpatial = false;
		FVector3 Position = FVector3::Zero;
		float32 MinDistance = 1.0f;
		float32 MaxDistance = 30.0f;
		EAudioRolloff Rolloff = EAudioRolloff::Linear;
		float32 PanStrength = 1.0f;

		float64 StartTime = 0.0;
		// バックエンドへ最後に渡した値（変わったときだけ渡す）
		float32 SentVolume = -1.0f;
		float32 SentPan = 2.0f;
		float32 SentPitch = -1.0f;
		// 最後に計算した最終的な値（情報表示用）
		float32 FinalVolume = 0.0f;
		float32 FinalPan = 0.0f;
	};

	struct FBus
	{
		FAudioFade Volume;
		bool bMuted = false;
		bool bPaused = false;
	};

	FVoice* Resolve( FSoundHandle Handle );
	const FVoice* Resolve( FSoundHandle Handle ) const;
	FSoundHandle MakeHandle( int32 Index ) const;

	// 数や間隔の制限を確認する。鳴らしてよければ true（古い音を止める必要があれば、ここで止める）
	bool AdmitNewVoice( const TSharedPtr<CSound>& Sound, const FSoundPlayParams& Params );
	// 空きの場所を探す（無ければ、止めてよいものを止めて空ける）。見つからなければ INDEX_NONE
	int32 AllocateSlot( int32 NewPriority );
	void FreeVoice( int32 Index );

	// 音量・パン・ピッチを計算してバックエンドへ渡す。bForce なら、変わっていなくても渡す
	void Apply( FVoice& Voice, bool bForce );
	float32 GetBusGain( EAudioBus Bus ) const;
	bool ShouldBePaused( const FVoice& Voice ) const;
	void ApplyPauseState();

	float32 NextRandom();   // -1..1
	void ReleaseHandleSlots();

	IAudioBackend* GetBackend() const { return BackendOverride != nullptr ? BackendOverride : Backend; }

	IAudioBackend* Backend = nullptr;
	IAudioBackend* BackendOverride = nullptr;
	TArray<FVoice> Voices;
	FBus Buses[static_cast<int32>( EAudioBus::Count )];
	FAudioListener Listener;
	FSoundHandle MusicHandle;
	TWeakPtr<CSound> MusicSound;
	TMap<const CSound*, float64> LastPlayTime;

	int32 MaxVoices = 48;
	int32 NumActive = 0;
	bool bGlobalPaused = false;
	float64 Clock = 0.0;
	uint32 RandomState = 0x2545F491u;
};
