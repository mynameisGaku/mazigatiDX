#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Framework/World/WorldDefinition.h"

//=============================================================================
// 当たり判定を付けるかどうかの決定
//   純粋な関数（DxLib・物理・シーンを使わない）。実際に物理の剛体を作るのも、エディタが形を重ねて表示するのも、この結果に従う。
//   エディタには、なぜそうなったか（Reason / Detail）も見せられる。
//
//   優先順位（上が強い）:
//     1. オブジェクトごとの指定（FWorldObject::Collision が On / Off）
//     2. ワールドのタグ規則（FWorldSettings::CollisionRules の、最初に一致したもの）
//     3. 既定（Collider 部品があれば、付ける）
//   Collider 部品（形）が無いオブジェクトは、どの指定でも付かない（付ける形が無いので）。
//
//   FWorldCollisionDecision Decision = NWorld::ResolveCollision( World.Settings, Object );
//   if ( Decision.bEnabled ) { /* 剛体を作る */ }
//=============================================================================
enum class EWorldCollisionReason : uint8
{
	NoCollider,        // Collider 部品が無い（付ける形が無い）
	ObjectOverride,    // オブジェクトの Collision の指定（Detail = "On" / "Off"）
	TagRule,           // ワールドのタグ規則（Detail = 一致したタグ）
	Default,           // どの指定も無く、既定に従った
};

struct FWorldCollisionDecision
{
	bool bEnabled = false;
	EWorldCollisionReason Reason = EWorldCollisionReason::NoCollider;
	FString Detail;

	// 「Collision on (tag rule 'Wall')」のような、人が読む説明
	FString ToDisplayString() const;
};

// ワールド全体の、当たり判定の集計（エディタの表示用。置かれるオブジェクト = 有効で、親も有効なものだけを数える）
struct FWorldCollisionSummary
{
	int32 NumPlaced = 0;           // 置かれるオブジェクトの数
	int32 NumWithCollider = 0;     // そのうち、Collider 部品を持つもの
	int32 NumEnabled = 0;          // Collider を持ち、剛体になるもの（トリガー・メッシュを含む）
	int32 NumDisabled = 0;         // Collider を持つが、剛体にならないもの
	int32 NumTriggers = 0;         // 剛体になるもののうち、トリガー
	int32 NumMeshes = 0;           // 剛体になるもののうち、メッシュ
	int32 NumByOverride = 0;       // Collider を持つものの、決まった理由の内訳
	int32 NumByRule = 0;
	int32 NumByDefault = 0;
	TArray<int32> NumDecidedByRule;   // ワールドの規則（CollisionRules）ごとの、その規則で決まったオブジェクトの数（規則と同じ並び）
};

namespace NWorld
{
	FWorldCollisionDecision ResolveCollision( const FWorldSettings& Settings, const FWorldObject& Object );

	// Mesh の当たり判定が、三角形を取るモデルのファイル（Collider の Mesh）。空なら、同じオブジェクトの Mesh 部品（編集メッシュ。UsesEditMeshCollider）、
	// それも無ければ、Model 部品のファイル。どれも無ければ空
	FString GetColliderMeshPath( const FWorldObject& Object );
	// Mesh の当たり判定が、オブジェクト自身の Mesh 部品（編集メッシュ）の三角形を使うか（Collider にファイルの指定が無く、Mesh 部品がある）
	bool UsesEditMeshCollider( const FWorldObject& Object );

	FWorldCollisionSummary SummarizeCollision( const FWorldDefinition& World );
}
