#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Math/Transform.h"

//=============================================================================
// ワールドの階層（Parent で表した木）の問い合わせ
//   FWorldDefinition を読むだけの純粋な関数。壊れたデータ（存在しない親・循環）でも止まる（無限ループにならない）。
//   戻り値の番号は、FWorldDefinition::Objects の中の位置（Id ではない）。兄弟は配列の順。
//=============================================================================
namespace NWorld
{
	// ParentId の直接の子（ParentId が WorldNoId なら、親のいないオブジェクト）
	TArray<int32> GetChildIndices( const FWorldDefinition& World, int32 ParentId );

	// RootId のオブジェクトと、その子孫（親が子より先の順）。RootId が無ければ空
	TArray<int32> GetSubtreeIndices( const FWorldDefinition& World, int32 RootId );

	// Ids のうち、存在して、祖先が Ids に入っていないもの（根）。重複は 1 つにして、配列（Objects）の順に並べる。
	// 選んだオブジェクトをまとめて動かす・消す・複製するとき、子は親と一緒に扱われるので、根だけを対象にする
	TArray<int32> FilterToRoots( const FWorldDefinition& World, const TArray<int32>& Ids );

	// CandidateId が AncestorId の子孫か（同じ Id は false）
	bool IsDescendantOf( const FWorldDefinition& World, int32 AncestorId, int32 CandidateId );

	// 最上位（親のいない）を 0 とした深さ。存在しない親や循環があれば、そこまでの深さ
	int32 GetDepth( const FWorldDefinition& World, int32 ObjectId );

	// ObjectId のワールドでの位置・回転・拡大（親から順に合成する。Library/Math の FTransform::Combine と同じ近似）
	FTransform ComputeWorldTransform( const FWorldDefinition& World, int32 ObjectId );

	// 親のワールドでの姿勢（親がいない・存在しない親なら単位）
	FTransform ComputeParentWorldTransform( const FWorldDefinition& World, int32 ObjectId );

	// ワールドでの姿勢 ChildWorld を、親（ParentWorld）の中でのローカルの姿勢にする（FTransform::Combine の逆）。
	// 位置と向きは、親が非一様に拡大されていても正確。拡大は成分ごとの割り算（親の拡大が 0 の成分は、そのまま）
	FTransform WorldToLocal( const FTransform& ParentWorld, const FTransform& ChildWorld );

	// すべてのオブジェクトのワールドでの姿勢（Objects と同じ並び）。ComputeWorldTransform を全部に呼ぶのと同じ結果を、全体で一度の走査で求める
	//   （エディタが毎フレーム全部を描くので、1 つずつ親をたどり直さない）。存在しない親は最上位として、循環していても止まる（循環の中では親を単位として扱う）
	TArray<FTransform> ComputeAllWorldTransforms( const FWorldDefinition& World );

	// 設置する順番
	struct FWorldSpawnOrder
	{
		TArray<int32> Order;     // 親が先、兄弟は配列の順。最上位のものと、存在しない親を持つもの（Orphans）から始まる
		TArray<int32> Orphans;   // 親の Id が存在しないオブジェクト（最上位として扱う）
		TArray<int32> Cyclic;    // 親をたどると循環するオブジェクト（設置しない）
	};

	// bSkipDisabled が true なら、bEnabled が false のオブジェクトと、その子孫は含めない（Cyclic にも入れない）
	FWorldSpawnOrder ComputeSpawnOrder( const FWorldDefinition& World, bool bSkipDisabled );
}
