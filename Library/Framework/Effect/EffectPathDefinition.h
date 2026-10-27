// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Effect/EffectLightningTreeDefinition.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Vector3.h"

// 粒子を帯や複数種類の雷として描くための、保存可能な設定。
struct FEffectPathDefinition
{
	// 点の粒子、移動の軌跡、分岐する雷を選ぶ。
	enum class EKind : uint8
	{
		// 既存の四角い粒子を描く。
		None,
		// 生まれた粒子を順に帯でつなぐ。
		Ribbon,
		// 始点と終点の間に雷を作る。
		Lightning,
		// 親子の枝と成長・放電を持つ雷を作る。
		TreeLightning,
		// Treeを伴う未知のkindをUndo後も不正値として保つ。
		InvalidTreeLightning = 0xFF,
	};
	// 主幹を分ける区間数の上限。
	static constexpr int32 MaxSegments = 128;
	// 主幹から分かれる枝の上限。
	static constexpr int32 MaxBranches = 16;
	// 点をどの方法でつなぐか。
	EKind Kind = EKind::None;
	// 雷の始点から終点への、エフェクト内の移動量。
	FVector3 End = FVector3( 0.0f, 3.0f, 0.0f );
	// 再生側から動的ワールド終点が渡されたとき、雷の終点をそちらへ接続するか（未指定時は End へ戻す）。
	bool bUseTarget = false;
	// 雷の主幹を分ける区間数。
	int32 Segments = 32;
	// 主幹の途中から分かれる枝の数。
	int32 Branches = 4;
	// 幹の長さに対する、横へ揺れる幅の比率。
	float32 Jitter = 0.3f;
	// 幹の長さに対する、枝の長さの比率。
	float32 BranchLength = 0.45f;
	// 雷の形の変化を一秒間に何回補間するか。0 なら静止。
	float32 RefreshHz = 12.0f;
	// ワールド距離 1 単位あたりの画像の繰り返し回数。累積距離へ掛けて使う。
	float32 UVRepeat = 1.0f;
	// 軌跡の粒子間がこの距離を超えたら、帯を切る。
	float32 BreakDistance = 2.0f;
	// TreeLightningだけが使う枝数・成長・放電設定。他kindでも値を保持する。
	FEffectLightningTreeDefinition Tree;

	// UseTarget に真偽値以外の不正な型が読み込まれたか返す。
	bool HasInvalidUseTarget() const { return bInvalidUseTarget; }
	// 型不正フラグを消去し、リセットや再編集後の保存検証を通せるようにする。
	void ClearInvalidFlags() { bInvalidUseTarget = false; }

	// 全項目を精度を落とさず JSON にする。
	NlohmannJson ToJson() const;
	// オブジェクトを読み、不足項目は既定値にする。オブジェクトでなければ false。
	static bool FromJson( const NlohmannJson& Json, FEffectPathDefinition& Out, TArray<FString>* Warnings = nullptr );
	// 種類・範囲・有限な値を確認し、問題がある項目の説明を返す。
	TArray<FString> Validate() const;
	// 表示方式を含む全項目から、変更を判定する識別値を求める。
	uint64 ComputeHash() const;

private:
	// JSON 読込時に UseTarget が真偽値でなかったことを保持する印。
	bool bInvalidUseTarget = false;
};

// 大文字小文字を区別せず表示方式を読む。知らない名前なら Out を変えず false。
bool TryParse( const FString& Text, FEffectPathDefinition::EKind& Out );
// 表示方式の保存名を返す。未知の値は None、Tree付き不正状態は専用名にする。
const char* ToString( FEffectPathDefinition::EKind Kind );
