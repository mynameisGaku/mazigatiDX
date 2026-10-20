#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/World/WorldDefinition.h"

//=============================================================================
// ワールドの検証
//   FWorldDefinition を読んで、問題の一覧を作る純粋な関数。エディタの検証パネル、保存・出荷の前の確認、テストが使う。
//   アセットがあるかどうかは、IWorldAssetQuery 越しに確かめる（CAssetManager に直接は頼らない。偽物で確かめられる）。
//
//   Error   : このままでは正しく置けない・結果が間違う（Id の重複・親の循環・無いアセット・使えない値）
//   Warning : 置けるが、意図と違う可能性が高い（形の無い当たり判定の指定・知らない部品・0 の拡大）
//=============================================================================
enum class EWorldIssueSeverity : uint8
{
	Warning,
	Error,
};

enum class EWorldIssueCode : uint8
{
	// ワールド
	NewerVersion,
	InvalidId,
	DuplicateId,
	NextIdTooSmall,
	EmptyRuleTag,
	ShadowedRule,
	// 階層
	SelfParent,
	MissingParent,
	ParentCycle,
	// タグ・プロパティ
	EmptyTag,
	DuplicateTag,
	EmptyPropName,
	DuplicatePropName,
	BadPropValue,
	MissingObjectReference,
	// 値
	NonFiniteValue,
	ZeroScale,
	// アセット・部品
	EmptyAssetPath,
	MissingAsset,
	BadBillboard,
	BadSound,
	BadEffect,
	BadCollider,
	BadModel,
	BadMesh,
	BadPrefab,
	CollisionWithoutCollider,
	UnsupportedComponent,
};

const char* ToString( EWorldIssueSeverity Severity );
const char* ToString( EWorldIssueCode Code );

struct FWorldIssue
{
	EWorldIssueSeverity Severity = EWorldIssueSeverity::Error;
	EWorldIssueCode Code = EWorldIssueCode::InvalidId;
	int32 ObjectId = WorldNoId;   // 問題のあるオブジェクト。ワールド全体の問題なら WorldNoId
	FString Message;

	// "[Error] #3 'Torch': ..." のような一行
	FString ToDisplayString() const;
};

// アセットの有無を調べる窓口（パスは、アセットのルートからの相対）
class IWorldAssetQuery
{
public:
	virtual ~IWorldAssetQuery() = default;
	virtual bool Exists( const FString& Path ) const = 0;
	// 入れ子のワールド（Prefab 部品）の中身。入れ子の循環の検出に使う。読めない・分からないときは nullptr（その先は調べない）
	virtual const FWorldDefinition* GetWorld( const FString& Path ) const { (void)Path; return nullptr; }
};

namespace NWorld
{
	// Assets が nullptr なら、アセットの有無は調べない。
	// WorldPath は、この定義のファイル（入れ子の循環の検出で、自分自身を含めるため。分からなければ空）
	TArray<FWorldIssue> ValidateWorld( const FWorldDefinition& World, const IWorldAssetQuery* Assets, const FString& WorldPath = FString() );

	// Error が 1 つでもあるか
	bool HasErrors( const TArray<FWorldIssue>& Issues );
}
