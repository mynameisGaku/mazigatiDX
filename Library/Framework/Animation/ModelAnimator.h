// SPDX-License-Identifier: MIT
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
class CModelAnimationSampler;
class IDrawExecutor;

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
//   時間の単位: クリップの長さや再生位置は秒で扱う。DxLib のモデルの時間はフレームなので、UnitsPerSecond（既定 30）で換算する。
//   ファイルによって違う場合は UnitsPerSecond を変える。
//
//   体の一部だけ別の動き: PlayPart( "Wave", "UpperBody" ) で、骨 UpperBody とその子だけを Wave で動かす（ほかは Play のまま）。StopPart で戻す。
//
//   アニメーターは描画の実装（DxLib など）を知らない。アニメーションの付け外しはアセットの窓口（IAssetBackend）、
//   時間と混ぜる割合は描画の命令（ModelAnim / ModelAnimFrame）で伝える。割合の計算は NAnimationBlend。
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
	float32 UnitsPerSecond = 30.0f;     // モデルの時間 1 秒あたりの値
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
	// 再生中のものを FadeTime 秒かけて止める（体の一部の動きも。フェードが終わると、モデルは何も再生していない姿勢になる）
	void Stop( float32 FadeTime = 0.2f );
	// 骨 BoneName とその子だけを、ClipName で動かす（ほかの部分は Play のアニメーションのまま。上半身で手を振る、など）。
	// 前の一部の動きからは、フェードで切り替える。クリップ・骨が見つからなければ false
	bool PlayPart( const FString& ClipName, const FString& BoneName, const FModelPlayParams& Params = FModelPlayParams() );
	// 体の一部の動きを、FadeTime 秒かけてやめる（Play のアニメーションへ戻る）
	void StopPart( float32 FadeTime = 0.2f );
	// 体の一部を動かしているクリップと骨（無ければ空）
	FString GetPartClip() const;
	FString GetPartBone() const;
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
	// メインスレッドで指定骨の親基準姿勢を選ぶ。失敗時は選択を変更しない。
	bool ConfigureRootMotionBone( const FString& BoneName );
	// メインスレッドでroot-motion骨の選択と未消費差分を解除する。
	void ClearRootMotionBone();
	// 主スレッドで未消費差分を一度だけ取得する。native失敗ではOutDeltaと差分を保ち、再試行できる。
	bool ConsumeRootMotion( FMatrix4x4& OutDelta );
	// メインスレッドで基準行列へ差分を補正し、剛体として変換できた場合だけ移動・回転を一度取得する。
	// native sampleまたはbasis変換の失敗では両出力と未消費区間を保ち、再試行できる。
	bool ConsumeRootMotion( const FMatrix4x4& ReferenceToActorBasis, FVector3& OutTranslation, FQuaternion& OutRotation );

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
		float32 LengthUnits = 0.0f;     // モデルの時間の単位での長さ。0 なら Source から読む
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
		float32 UnitsPerSecondAtStart = 30.0f; // 再生開始時の有効時間単位。途中変更の検出に使う。
		EAnimLoopMode Loop = EAnimLoopMode::Loop;
		float32 Weight = 0.0f;
		float32 TargetWeight = 0.0f;
		float32 FadeRate = 0.0f;        // 1 秒あたりの重みの変化量（TargetWeight に向かう）
		bool bAttached = false;         // Instance に付いている
		TWeakPtr<CModelInstance> AttachedInstance; // 添付先。公開Instance差し替えを検出する。
		int32 PartFrame = -1;           // 体の一部だけに効かせる骨の番号（-1 なら体全体）
	};
	// root-motion計算へ渡す、連続した全身再生区間の値。
	struct FRootMotionInterval
	{
		bool bValid = false; // 同じFrom/Toでも未消費区間として扱う。
		float64 From = 0.0; // 連続区間の開始時刻。
		float64 To = 0.0; // 最後に記録した終了時刻。
		int32 ClipSlot = INDEX_NONE; // 区間を作った全身クリップ。
		TSharedPtr<CModel> Source; // クリップ資源の寿命を保つ。
		int32 AnimIndex = INDEX_NONE; // Source内のアニメーション番号。
		float32 LengthUnits = 0.0f; // native時間の長さ。
		float32 LengthSeconds = 0.0f; // Updateと同じ秒単位の長さ。
		float32 UnitsPerSecond = 0.0f; // Play時に使った時間換算率。
		EAnimLoopMode Loop = EAnimLoopMode::Loop; // 区間を進めた再生方式。
		int32 Frame = INDEX_NONE; // 親基準姿勢を読む骨番号。
		TWeakPtr<CModelInstance> Instance; // 再生を開始したModel instance。
	};

	FClip* FindClip( const FString& Name );
	const FClip* FindClip( const FString& Name ) const;
	// クリップの長さ（モデルの時間の単位）。0 以下なら使えない
	float32 ResolveLengthUnits( FClip& Clip ) const;
	// 有限かつ正の時間単位。公開値が不正なら従来の既定 30 を使う
	float32 ResolveUnitsPerSecond() const;
	// 使う層を決める（同じクリップ・同じ骨があれば再利用、無ければ空き、無ければ捨てる）
	FLayer* AcquireLayer( int32 ClipSlot, int32 InPartFrame );
	// クリップを層に付けて、再生の位置・速さ・繰り返しを決める（重みは変えない）。OutFade はフェードの長さ
	FLayer* StartClip( FClip& Clip, int32 InPartFrame, const FModelPlayParams& Params, float32& OutFade );
	// Layer を重み 1 へ、同じ組（体全体か、体の一部か）のほかの層を 0 へ、Fade 秒で
	void CrossFadeTo( FLayer& Layer, float32 Fade );
	void DetachLayer( FLayer& Layer );
	int32 FindPrimaryLayer() const;
	// 未消費区間の値だけを消す。samplerの寿命には触れない。
	void ClearPendingRootMotion();
	// 不連続を記録し、必要なら新しい再生までroot取得を止める。
	void InvalidateRootMotion( bool bRequireRestart );
	// worker Updateから、一次全身層の連続区間だけを値として蓄積する。
	void AccumulateRootMotion( const FLayer& Layer, float64 From, float64 To );
	// 選択骨・添付先・単位などのroot取得条件を検査する。
	bool IsRootMotionIdentityValid() const;
	// pending区間を検証・sampleし、消費前のfull affine差分だけを返す。
	bool TryExtractPendingRootMotion( FMatrix4x4& OutDelta );

	TArray<FClip> Clips;
	TArray<FLayer> Layers;
	TArray<int32> PartParents;				// 部分再生の骨階層（Record はゲームスレッド外からも読む）
	TArray<uint8> PartTouchedFrames;		// 一度でも部分率を書いた骨。DxLib の保持状態を毎フレーム戻す
	int32 PrimaryClipSlot = INDEX_NONE;   // 最後に Play したクリップ
	int32 PartClipSlot = INDEX_NONE;      // 最後に PlayPart したクリップ
	int32 PartFrame = -1;                 // その骨
	bool bPaused = false;
	bool bFinished = false;
	bool bFinishedThisUpdate = false;
	TArray<FString> Events;
	int32 RootMotionFrame = INDEX_NONE; // 選択した骨番号。
	FRootMotionInterval PendingRootMotion; // Updateでまとめた未消費区間。
	TWeakPtr<CModelInstance> RootMotionConfiguredInstance; // 骨選択時のinstance。
	TWeakPtr<CModelInstance> PlaybackInstance; // 最初のanimation添付先。
	bool bHasPlaybackInstance = false; // PlaybackInstanceの有効性。
	bool bRootMotionRequiresRestart = false; // 単位変更後は新規Playまで取得を止める。
	bool bRootMotionClipReplacementRequiresClear = false; // active clip置換後はClearClipsまで取得を止める。
	bool bRootMotionInstanceMismatch = false; // Instance差し替え後は新Animatorを要求する。
	TSharedPtr<CModelAnimationSampler> RootMotionSampler; // main-thread Consume用の再利用sampler。
	TSharedPtr<CModel> RootMotionSamplerTarget; // sampler作成時の対象Model。
	TSharedPtr<CModel> RootMotionSamplerSource; // sampler作成時のanimation source。
	int32 RootMotionSamplerAnimIndex = INDEX_NONE; // sampler作成時のclip番号。
};
