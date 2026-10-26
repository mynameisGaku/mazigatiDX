#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Animation/AnimationTimeline.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Render/ModelMaterialEffectRenderer.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Math/Quaternion.h"
#include "Library/Math/Vector3.h"

class FTransformView;

//=============================================================================
// FModelAnimator
//   3D モデルのアニメーションを再生する。クリップの登録・クロスフェード・速さ・ループ・出来事（イベント）に対応する。
//
//   // モデルを読み込み、自分用のインスタンスを作る
//   TSharedPtr<CModel> Robot = Assets->LoadModel( "Models/Robot.fbx" );
//   FModelAnimator Animator( Robot->CreateInstance() );
//
//   // クリップを登録する。モデル自身のアニメーションでも、動きだけが入った別ファイルでもよい（骨の名前が同じなら適用される）
//   Animator.AddClipsFrom( Robot );                                       // モデルに入っている全部。ファイル内の名前で登録
//   Animator.AddClip( "Walk", Assets->LoadModel( "Models/Robot_Walk.fbx" ), 0 );   // 別ファイルの 0 番を "Walk" として
//
//   Animator.Play( "Idle" );
//   Animator.Play( "Run", 0.25f );                                        // 0.25 秒かけてクロスフェード
//   Animator.Speed = 1.5f;                                                // 全体の速さ（クリップごとの速さは FModelPlayParams::Speed）
//
//   // オブジェクトに付けると、毎フレーム自動で更新・描画される（ECS のデータ。並列に処理される）
//   AddData( Animator );
//
//   // 自分で描く場合: 描画システムの中、または FDrawContext に記録して実行する
//   Animator.Record( Draw, World );
//
//   時間の単位: クリップの長さや再生位置は秒で扱う。DxLib のモデルの時間は「フレーム」なので、UnitsPerSecond（既定 30）で換算する
//   （FBX / MV1 / X はふつう 30 フレーム = 1 秒）。ファイルによって違う場合は UnitsPerSecond を変える。
//
//   出来事: AddEvent( "Run", 0.25f, "Footstep" ) のようにクリップの位置（0..1）に名前を付けておくと、
//   再生位置がそこを通った更新で GetEvents() / ConsumeEvent() に現れる（次の Update まで有効）。
//=============================================================================

// 再生の指定
struct FModelPlayParams
{
	float32 FadeTime = -1.0f;                // 前のアニメーションからのクロスフェードの長さ（秒）。0 なら即座に切り替える。負ならクリップの既定値
	float32 Speed = 1.0f;                    // 速さの倍率（このクリップの間だけ。クリップの既定の速さに掛かる。負で逆再生）
	TOptional<EAnimLoopMode> Loop;           // 指定すれば、登録時のループ設定の代わりに使う
	float32 StartNormalized = 0.0f;          // 開始位置（0..1。逆再生で 0 なら最後から）
	bool bRestartIfPlaying = false;          // すでに同じクリップを再生中でも、最初からやり直す
};

struct FModelAnimator
{
	FModelAnimator() = default;
	explicit FModelAnimator( const TSharedPtr<CModelInstance>& InInstance ) : Instance( InInstance ) {}

	//-------------------------------------------------------------------------
	// 設定（いつ変えてもよい）
	//-------------------------------------------------------------------------
	TSharedPtr<CModelInstance> Instance;
	// 選択した静的Model材質の共有資源と、このAnimator個体の効果時計。
	FModelMaterialEffectRenderer MaterialEffect;
	float32 UnitsPerSecond = 30.0f;     // DxLib のモデルの時間 1 秒あたりの値
	float32 Speed = 1.0f;               // 全体の速さの倍率
	bool bVisible = true;
	bool bAutoDraw = true;              // true なら、組み込みの描画システムが CTransformComponent のワールド行列で描く
	// モデルの見た目の補正（CTransformComponent のワールド行列より前に適用: モデルの向きや大きさがファイルと合わないときに）
	FVector3 ModelOffset = FVector3::Zero;
	FQuaternion ModelRotation = FQuaternion::Identity;
	FVector3 ModelScale = FVector3::One;

	//-------------------------------------------------------------------------
	// クリップの登録
	//-------------------------------------------------------------------------
	// Source の AnimIndex 番のアニメーションを、Name で登録する（長さは Source が持っているもの）。
	// DefaultLoop / DefaultSpeed / DefaultFade は、Play で指定しなかったときに使う値。
	// Source が読み込み中でも登録できる（長さは再生を始めるときに決まる）。同じ名前があれば置き換える
	void AddClip( const FString& Name, const TSharedPtr<CModel>& Source, int32 AnimIndex = 0, EAnimLoopMode DefaultLoop = EAnimLoopMode::Loop,
				  float32 DefaultSpeed = 1.0f, float32 DefaultFade = 0.2f );
	// Source に入っているアニメーションをすべて、ファイル内の名前（無ければ Prefix + 番号）で登録する。登録した数を返す
	int32 AddClipsFrom( const TSharedPtr<CModel>& Source, const FString& Prefix = FString() );
	bool HasClip( const FString& Name ) const;
	// 登録した順のクリップ名
	TArray<FString> GetClipNames() const;
	void ClearClips();
	// クリップの長さ（秒）。無ければ 0
	float32 GetClipLength( const FString& Name ) const;
	// クリップの NormalizedTime（0..1）の位置に、出来事の名前を付ける
	bool AddEvent( const FString& ClipName, float32 NormalizedTime, const FString& EventName );

	//-------------------------------------------------------------------------
	// 再生の操作
	//-------------------------------------------------------------------------
	// 前のアニメーションから FadeTime 秒かけて切り替える（負ならクリップの既定のフェード）。見つからなければ false（いまの再生は変えない）
	bool Play( const FString& ClipName, float32 FadeTime = -1.0f );
	bool Play( const FString& ClipName, const FModelPlayParams& Params );
	// 再生中のものを FadeTime 秒かけて止める（フェードが終わると、モデルは何も再生していない姿勢になる）
	void Stop( float32 FadeTime = 0.2f );
	void Pause() { bPaused = true; }
	void Resume() { bPaused = false; }
	FORCEINLINE bool IsPaused() const { return bPaused; }
	// 時間を進める（DeltaTime は秒）。重み（クロスフェード）・再生位置・出来事を更新する
	void Update( float32 DeltaTime );
	// 静的な選択材質効果を準備する。失敗時はnative Model描画へ戻る。
	bool PrepareMaterialEffect( int32 MaterialIndex, const FMeshMaterialEffectDefinition& Definition, const TCallable<TSharedPtr<CTexture>( const FString& )>& LoadTexture, const IDrawExecutor& Executor, bool bTwoSided = false );
	// 選択材質効果を解放し、native Model描画へ戻す。
	void ClearMaterialEffect();
	// このAnimator個体の材質効果制御口を返す。
	FModelMaterialEffectRenderer& GetMaterialEffect();
	const FModelMaterialEffectRenderer& GetMaterialEffect() const;
	// 主に再生中のクリップの再生位置を、0..1 の位置に動かす（出来事は起きない。エディタのシークバー用）。再生していなければ何もしない
	void Seek( float32 NormalizedTime );

	//-------------------------------------------------------------------------
	// 状態
	//-------------------------------------------------------------------------
	// いま主に再生しているクリップの名前（無ければ空）
	FString GetCurrentClip() const;
	bool IsPlaying() const;
	// 主に再生中のクリップが、Once で最後まで再生された（Play すると false に戻る）
	FORCEINLINE bool IsFinished() const { return bFinished; }
	FORCEINLINE bool DidFinishThisUpdate() const { return bFinishedThisUpdate; }
	// 主に再生中のクリップの位置（0..1）
	float32 GetNormalizedTime() const;
	// 主に再生中のクリップの再生位置（秒）。クロスフェード中は、重みの小さい方は別に持っている
	float32 GetPlayTime() const;
	// 重ねて再生している（フェードで消えかけのものも含む）アニメーションの数
	int32 GetNumActiveLayers() const;
	// アニメーション付けの状態（デバッグ・確認用）
	struct FLayerInfo
	{
		FString ClipName;
		float32 Weight = 0.0f;   // 正規化後（合計 1）
		float32 ClipTime = 0.0f; // モデルに設定する時間（DxLib の時間単位）
		int32 AttachIndex = -1;
	};
	TArray<FLayerInfo> GetLayerInfos() const;

	//-------------------------------------------------------------------------
	// 出来事
	//-------------------------------------------------------------------------
	FORCEINLINE const TArray<FString>& GetEvents() const { return Events; }
	FORCEINLINE bool HasEvent( const FString& Name ) const { return Events.Contains( Name ); }
	bool ConsumeEvent( const FString& Name );

	//-------------------------------------------------------------------------
	// 描画
	//-------------------------------------------------------------------------
	// 付けているアニメーションの時間とブレンド率、モデルの行列を記録する。World はモデルのワールド行列（ModelOffset などは内側に掛かる）
	void Record( FDrawContext& Draw, const FMatrix4x4& World ) const;
	void RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const;

	static constexpr int32 MaxLayers = 6;

private:
	struct FClip
	{
		FString Name;
		TSharedPtr<CModel> Source;
		int32 AnimIndex = 0;
		float32 LengthUnits = 0.0f;     // DxLib の時間単位。0 なら Source から読む
		EAnimLoopMode DefaultLoop = EAnimLoopMode::Loop;
		float32 DefaultSpeed = 1.0f;
		float32 DefaultFade = 0.2f;
		struct FEvent
		{
			float32 Normalized = 0.0f;
			FString Name;
		};
		TArray<FEvent> Events;
	};

	struct FLayer
	{
		int32 ClipSlot = INDEX_NONE;
		int32 AttachIndex = -1;
		float64 Playhead = 0.0;         // 秒（unbounded。速度が負なら減る）
		float32 LengthSeconds = 0.0f;
		float64 PrevPlayhead = 0.0;     // 前の Update の終わりの再生位置（出来事の判定用）
		float32 ClipSpeed = 1.0f;
		EAnimLoopMode Loop = EAnimLoopMode::Loop;
		float32 Weight = 0.0f;
		float32 TargetWeight = 0.0f;
		float32 FadeRate = 0.0f;        // 1 秒あたりの重みの変化量（TargetWeight に向かう）
		bool bAttached = false;         // Instance に付いている
	};

	FClip* FindClip( const FString& Name );
	const FClip* FindClip( const FString& Name ) const;
	// クリップの長さ（DxLib の時間単位）。0 以下なら使えない
	float32 ResolveLengthUnits( FClip& Clip ) const;
	// 使う層を決める（同じクリップがあれば再利用、無ければ空き、無ければ捨てる）
	FLayer* AcquireLayer( int32 ClipSlot );
	void DetachLayer( FLayer& Layer );
	int32 FindPrimaryLayer() const;

	TArray<FClip> Clips;
	TArray<FLayer> Layers;
	int32 PrimaryClipSlot = INDEX_NONE;   // 最後に Play したクリップ
	bool bPaused = false;
	bool bFinished = false;
	bool bFinishedThisUpdate = false;
	TArray<FString> Events;
};
