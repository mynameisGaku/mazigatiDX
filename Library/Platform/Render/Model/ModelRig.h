// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Render/StaticModelDrawView.h"
#include "Library/Framework/Asset/Model/Import/ImportedScene.h"
#include "Library/Framework/Asset/Model/Import/StaticModelDefinition.h"
#include "Library/Math/Matrix4x4.h"

class IJobSystem;

//=============================================================================
// モデルの骨組み（リグ）と、ポーズの計算。GPU・描画 API・DxLib を知らない（CPU でスキニングする）。
//
//   FModelRig   取り込んだモデルの不変の骨組み: 骨（ノード）の階層・メッシュの頂点と骨の影響・アニメーションのクリップ。共有する。
//   FModelPose  個体ごとの状態: 付けたアニメーション（時間・割合・骨ごとの割合）、骨の上書き、骨・メッシュの表示。
//   NModelRig   骨組みを作る・ポーズ（骨の行列）を求める・頂点を変形する。
//
//   規約（DxLib の MV1 と同じ）:
//   - 行列は行ベクトル（v * M）。A * B は A を適用してから B
//   - アニメーションの時間は「30 = 1 秒」の単位（GetModelAnimUnitsPerSecond が 30）。クリップは、毎秒 Fps コマで行列を持つ
//   - 付けたアニメーションは、割合（Rate。骨ごとに上書きできる）の重みで混ぜる。重みの合計が 1 に満たない骨は、元の姿へ寄せる。1 を超えたら、合計が 1 になるよう割る
//   - 骨の行列の混ぜ方は、移動・拡大を線形、回転を（向きを揃えた）正規化つき線形補間にする
//   - 骨の上書き（ユーザーの行列）は、その骨の「親から見た行列」を置き換える
//=============================================================================

// 頂点 1 つに影響する骨（スキンの重み）の並び
struct FRigInfluenceList
{
	TArray<int32> Start;     // 頂点 i の影響は Bone[Start[i] .. Start[i+1]) （頂点の数 + 1 個）
	TArray<int32> Cluster;   // 影響ごとの、メッシュの Clusters の番号
	TArray<float32> Weight;  // 影響ごとの重み
};

// メッシュ 1 つぶんの、変形元（基準の姿）
struct FRigMesh
{
	int32 Node = -1;                  // 付いている骨（ノード）。-1 なら、モデルの原点
	TArray<FVector3> Positions;       // メッシュの座標での、基準の位置
	TArray<FVector3> Normals;         // 同じ並びの法線
	TArray<int32> ClusterBone;        // クラスターごとの骨（ノード）の番号
	TArray<FMatrix4x4> ClusterOffset; // クラスターごとの、メッシュの座標 -> 骨の座標（バインド時）
	FRigInfluenceList Influences;     // スキンがあるときだけ使う
	bool bSkinned = false;
};

// 描画用の頂点 1 つが、どのメッシュのどの頂点から来たか
using FRigVertexSource = FModelVertexSource;

struct FModelRig
{
	TArray<int32> Parent;                // ノードの親（-1 でルート）
	TArray<FMatrix4x4> BaseLocal;        // ノードの、アニメーションの無い親からの行列
	TArray<FString> NodeNames;
	TArray<int32> Order;                 // 親が先に来る、ノードの並び
	TMap<FString, int32> NodeByName;     // 名前の重なりは、最初のノード
	TArray<FRigMesh> Meshes;
	TArray<FImportedAnimation> Clips;

	int32 GetNumNodes() const { return Parent.Num(); }
};

// 付けたアニメーション 1 つ
struct FModelAttach
{
	bool bUsed = false;
	TSharedPtr<const FModelRig> Source;  // クリップを持つリグ（自分自身のことも、別のモデルのこともある）
	int32 Clip = -1;
	float32 Time = 0.0f;                 // 30 = 1 秒
	float32 Rate = 0.0f;                 // 全体の割合
	TArray<int32> NodeTrack;             // このモデルのノード -> クリップのトラック（無ければ -1）。名前で結びつける
	TArray<float32> FrameRate;           // ノードごとの割合の上書き（負なら、Rate を使う）
};

// 個体ごとのポーズの状態
struct FModelPose
{
	TArray<FModelAttach> Attaches;
	TArray<uint8> bOverride;             // ノードごとの上書きの有無
	TArray<FMatrix4x4> Override;
	TArray<uint8> FrameHidden;           // ノードごとの「表示しない」
	TArray<uint8> MeshHidden;            // メッシュごとの「表示しない」
	FMatrix4x4 World;                    // 最後に描いたときの、モデルの配置
	uint64 Version = 1;                  // ポーズに関わる状態を変えるたびに増やす（変形の作り直しの判断）
};

namespace NModelRig
{
	// 取り込んだ（左手系にした）シーンから、骨組みを作る。メッシュの数・頂点の数が合わなければ false
	bool Build( const FImportedScene& Scene, FModelRig& Out, FString& Error );

	// ポーズの状態を、骨組みに合わせて空にする
	void InitPose( const FModelRig& Rig, FModelPose& Pose );

	// クリップの長さ（30 = 1 秒の単位）。範囲外は 0
	float32 GetClipLength( const FModelRig& Rig, int32 Clip );

	// 付ける（空いている番号を使う。付けられなければ -1）。Source のクリップ Clip を、名前で、この骨組みのノードに結びつける
	int32 Attach( const FModelRig& Rig, FModelPose& Pose, const TSharedPtr<const FModelRig>& Source, int32 Clip );
	void Detach( FModelPose& Pose, int32 AttachIndex );

	// クリップのトラック Track の、Time（30 = 1 秒）の行列（前後のコマの間は補間。範囲の外は、端のコマ）
	FMatrix4x4 SampleTrack( const FImportedAnimation& Clip, int32 Track, float32 Time );

	// 2 つ以上の行列を、重みで混ぜる（移動・拡大は線形、回転は向きを揃えた正規化つき線形補間）
	FMatrix4x4 BlendMatrices( const TArray<FMatrix4x4>& Matrices, const TArray<float32>& Weights );

	// ノードごとの「親から見た行列」（アニメーションの混合と上書きを反映）
	void EvaluateLocals( const FModelRig& Rig, const FModelPose& Pose, TArray<FMatrix4x4>& OutLocal );
	// モデルの座標での、ノードごとの行列（Order の順に、親の行列を掛ける）
	void ComputeGlobals( const FModelRig& Rig, const TArray<FMatrix4x4>& Local, TArray<FMatrix4x4>& OutGlobal );

	// Vertices の位置と法線を、Globals で変形した値にする（それ以外の値は触らない）。Sources は Vertices と同じ並び
	// Jobs を渡すと、頂点の数が多いとき、頂点を範囲に分けて並列に変形する（各頂点は自分の出力だけを書くので、結果は、ワーカーの数によらず同じ）。
	void Deform( const FModelRig& Rig, const TArray<FMatrix4x4>& Globals, const TArray<FRigVertexSource>& Sources, FDrawVertex3D* Vertices, int32 NumVertices, IJobSystem* Jobs = nullptr );
}
