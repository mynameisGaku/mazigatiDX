#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Animation/AnimationTimeline.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Vector3.h"

struct FModelAnimator;

//=============================================================================
// モデルのアニメーションの定義（独自のデータアセット。JSON と相互に変換できる。拡張子は .modelanim.json）
//   「どのモデルに、どのファイルのどのアニメーションを、何という名前・ループ・速さ・フェードで使い、
//    どの位置にどんな出来事（イベント）を置くか」をまとめたもの。モデルのアニメーションエディタ（デバッグ画面の Tools）で作って保存できる。
//
//   // 読み込んで、すぐ再生できるアニメーターを作る
//   TSharedPtr<CModelAnimationAsset> Robot = Assets->LoadModelAnimations( "Models/Robot.modelanim.json" );
//   FModelAnimator Animator = Robot->CreateAnimator();       // インスタンス + 全クリップ（出来事つき）が登録済み
//   Animator.Play( "Walk" );                                  // 定義に書いたループ・速さ・フェードで再生
//   AddData( Animator );
//
//   書式（省略した項目は既定値）:
//   {
//     "Version": 1,
//     "Model": "Models/Robot.fbx",                  // 表示するモデル。省略（空）なら、モデルを持たないアニメーション集（下の「モーション専用」）
//     "PreviewModel": "Models/Robot.fbx",           // エディタでプレビューに使うモデル（Model が空のときだけ。ゲームは使わない）
//     "UnitsPerSecond": 30,                         // モデルの時間 1 秒あたりの値（DxLib ではフレーム）
//     "Offset": [0, 0, 0], "Rotation": [0, 180, 0], "Scale": [1, 1, 1],   // 見た目の補正（Rotation は度）
//     "Clips": [
//       { "Name": "Walk",
//         "Source": "Models/Robot_Walk.fbx",        // アニメーションが入ったファイル。省略ならモデル自身（モーション専用ファイルの .mv1 / .fbx もここに書く）
//         "AnimIndex": 0, "AnimName": "mixamo.com", // 番号、またはファイル内の名前（名前があれば名前が優先）
//         "Loop": "Loop", "Speed": 1.0, "Fade": 0.2,
//         "Events": [ { "Time": 0.25, "Name": "Footstep" }, { "Time": 0.75, "Name": "Footstep" } ] }   // Time は 0..1
//     ]
//   }
//
//   モーション専用（モデルを持たないアニメーション集）:
//     Model を空にして、各クリップの Source に、メッシュの無いアニメーションだけのファイル（.mv1 / .fbx など）を書く。
//     ゲームでは、どのキャラクターにでも使える:
//       TSharedPtr<CModelAnimationAsset> Motions = Assets->LoadModelAnimations( "Motions/Humanoid.modelanim.json" );
//       FModelAnimator Animator = Motions->CreateAnimatorFor( Assets->LoadModel( "Models/Robot.mv1" ) );
//     アニメーションは名前が合う骨だけを動かすので、骨の名前が合うかは CModel::CompareBones で確かめられる。
//=============================================================================

// クリップの途中に置く出来事。Time はクリップの長さを 1 とした位置（0..1）
struct FModelEventDef
{
	float32 Time = 0.0f;
	FString Name;
};

struct FModelClipDef
{
	FString Name;
	FString SourcePath;          // アニメーションが入ったファイル（アセットのルートからのパス）。空ならモデル自身
	int32 AnimIndex = 0;         // ファイル内の番号
	FString AnimName;            // 空でなければ、ファイル内のアニメーション名で探す（AnimIndex より優先）
	EAnimLoopMode Loop = EAnimLoopMode::Loop;
	float32 Speed = 1.0f;        // 既定の速さの倍率（負で逆再生）
	float32 FadeTime = 0.2f;     // このクリップへ切り替えるときの既定のクロスフェード（秒）
	TArray<FModelEventDef> Events;
};

struct FModelAnimationDefinition
{
	FString ModelPath;           // 空なら、モデルを持たないアニメーション集（モーション専用）
	FString PreviewModelPath;    // エディタのプレビュー用（ModelPath が空のとき）。ゲームでは使わない
	float32 UnitsPerSecond = 30.0f;
	FVector3 Offset = FVector3::Zero;
	FVector3 RotationDegrees = FVector3::Zero;
	FVector3 Scale = FVector3::One;
	TArray<FModelClipDef> Clips;

	int32 FindClip( const FString& Name ) const;

	NlohmannJson ToJson() const;
	// JSON から。オブジェクトでなければ false（足りない項目は既定値になる）
	static bool FromJson( const NlohmannJson& Json, FModelAnimationDefinition& OutDefinition );
};

//=============================================================================
// CModelAnimationAsset
//   FModelAnimationDefinition と、それが指すモデルの読み込み結果（モデル本体・クリップごとのアニメーションの入ったモデル）。
//   CAssetManager::LoadModelAnimations / CreateModelAnimations で作る。
//=============================================================================
class CModelAnimationAsset : public CAsset
{
public:
	// ClipSources は Definition.Clips と同じ並び（モデル自身を使うクリップは、Model と同じもの）
	CModelAnimationAsset( const FString& InName, const FModelAnimationDefinition& InDefinition, const TSharedPtr<CModel>& InModel, const TArray<TSharedPtr<CModel>>& InClipSources );

	FORCEINLINE const FModelAnimationDefinition& GetDefinition() const { return Definition; }
	FORCEINLINE const TSharedPtr<CModel>& GetModel() const { return Model; }
	TSharedPtr<CModel> GetClipSource( int32 ClipIndex ) const;

	// 定義が指すモデルがすべて使える状態か（モデルを持たない集は、クリップのファイルだけ）
	bool IsReady() const override;
	FString Describe() const override;
	int64 GetMemoryBytes() const override;

	// モデルのインスタンスを作り、すべてのクリップ（ループ・速さ・フェード・出来事つき）と見た目の補正を登録したアニメーター。
	// モデルを作れなければ（モデルを持たない集・メッシュのないファイル・読み込みの失敗）、インスタンスのないアニメーター（何も描かれない）
	FModelAnimator CreateAnimator() const;
	// 別のモデル（Character）に使うアニメーターを作る。Source を省略したクリップは、Character 自身のアニメーションとして扱う。
	// モデルを持たない集（モーション専用）を、キャラクターに適用するときに使う
	FModelAnimator CreateAnimatorFor( const TSharedPtr<CModel>& Character ) const;
	// 既存のアニメーターに、クリップと見た目の補正を登録し直す（インスタンスは変えない）。登録できたクリップの数を返す。
	// FallbackSource: モデルも Source も無いクリップが使うモデル（CreateAnimatorFor が渡す）
	int32 ApplyTo( FModelAnimator& Animator, const TSharedPtr<CModel>& FallbackSource = TSharedPtr<CModel>() ) const;

private:
	FModelAnimationDefinition Definition;
	TSharedPtr<CModel> Model;
	TArray<TSharedPtr<CModel>> ClipSources;
};
