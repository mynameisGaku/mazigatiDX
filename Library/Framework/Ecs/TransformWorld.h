#pragma once
#include <atomic>
#include <vector>

#include "Library/Core/Collection/CollectionCommon.h"
#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Framework/Ecs/ChunkedPool.h"
#include "Library/Math/Transform.h"

class CGameObject;
class IJobSystem;

//=============================================================================
// FTransformNode
//   1 つの CTransformComponent の実体（ECS のコンポーネントのデータ）。CTransformComponent は、これを指すだけの薄い窓口。
//=============================================================================
struct FTransformNode
{
	FTransform Local;                   // 親からの相対（書き込むのは CTransformComponent 経由だけ）
	FTransform World;                   // ワールド値の控え。bDirty の間は古い
	CGameObject* Owner = nullptr;

	int32 Parent = INDEX_NONE;          // 最も近い祖先の CTransformComponent の番号（親オブジェクトが持っていなければ、その上を探す）
	int32 FirstChild = INDEX_NONE;      // 子の一覧（階層を組み直すたびに作り直す）
	int32 NextSibling = INDEX_NONE;
	int32 Depth = 0;                    // 根が 0

	bool bDirty = true;                 // World が古い。立っている間は、子孫もすべて立っている
	bool bPendingDestroy = false;       // コンポーネントの取り外しが予約された（親としては数えない）
};

//=============================================================================
// FTransformWorld
//   シーンごとに 1 つ。シーンの全 CTransformComponent のデータを持ち、ワールド値をまとめて更新する（ECS の「システム」）。
//
//   - Update: 古くなったワールド値を、階層の浅い順（親が先）に、複数のスレッドで一斉に計算する。
//     同じ深さの要素は互いに独立なので、ワーカーの数や実行の順番によらず結果は完全に同じ（逐次実行と同じ計算式）。
//   - ワールド値は「呼んだ時点で常に最新」: Update を待たずに GetWorld を呼んでも、古い分をその場で（呼び出したスレッドが）計算する。
//     つまり Update は純粋な高速化で、呼ばなくても結果は変わらない（シーンは描画の直前に呼ぶ）
//   - 構造の変更（コンポーネントの追加・取り外し、親子関係の変更）は、次に必要になった時点で 1 回だけ組み直す
//
//   スレッド: すべてメインスレッドから呼ぶ。Update の最中は、メインスレッドが待っているので、ほかから触られない
//=============================================================================
class FTransformWorld
{
public:
	//-------------------------------------------------------------------------
	// 出し入れ（CTransformComponent が呼ぶ）
	//-------------------------------------------------------------------------
	int32 Allocate( CGameObject* Owner, const FTransform& Local );
	void Free( int32 Slot );

	FORCEINLINE FTransformNode& GetNode( int32 Slot ) { return Nodes[Slot]; }
	FORCEINLINE const FTransformNode& GetNode( int32 Slot ) const { return Nodes[Slot]; }

	//-------------------------------------------------------------------------
	// 書き込み
	//-------------------------------------------------------------------------
	// ローカルを変えたあとに呼ぶ（このノードと子孫のワールド値が古くなる）
	FORCEINLINE void MarkDirty( int32 Slot )
	{
		if ( !Nodes[Slot].bDirty ) MarkDirtySlow( Slot );   // 立っているなら、子孫もすべて立っている（不変条件）
	}
	FORCEINLINE void SetLocal( int32 Slot, const FTransform& Local )
	{
		Nodes[Slot].Local = Local;
		MarkDirty( Slot );
	}
	// 親子関係・コンポーネントの追加と取り外しがあったとき
	FORCEINLINE void MarkHierarchyDirty() { bHierarchyDirty = true; }

	// システムが FTransformAccess で書き込む前にメインスレッドで 1 回呼ぶ（ワーカーごとに atomic を触らないため）
	FORCEINLINE void NotifySystemWrite() { bAnyDirty.store( true, std::memory_order_relaxed ); }

	// 並列に動くシステムから、ローカルを書き換えたあとに呼ぶ。子孫のワールド値を古くする処理は、システムの実行が終わったあとに
	// メインスレッドでまとめて行う（スレッドごとの一覧に溜めるだけなので、ほかのスレッドとぶつからない）
	//   子のないノードは、自分のノードのフラグを立てるだけでよい（ほかのスレッドは、このノードを触らない）ので、一覧を通さない
	FORCEINLINE void MarkDirtyDeferred( FTransformNode& Node, int32 Slot )
	{
		if ( Node.FirstChild == INDEX_NONE )
		{
			Node.bDirty = true;
			return;
		}
		if ( !Node.bDirty )
		{
			DeferredDirty[static_cast<size_t>( IJobSystem::GetCurrentThreadIndex() )].push_back( Slot );
		}
	}
	FORCEINLINE void MarkDirtyDeferred( int32 Slot )
	{
		bAnyDirty.store( true, std::memory_order_relaxed );
		MarkDirtyDeferred( Nodes[Slot], Slot );
	}
	void FlushDeferredDirty();

	//-------------------------------------------------------------------------
	// 読み取り
	//-------------------------------------------------------------------------
	// 常に最新のワールド値（古ければ、その場で計算する）
	FORCEINLINE const FTransform& GetWorld( int32 Slot )
	{
		const FTransformNode& Node = Nodes[Slot];
		if ( !bHierarchyDirty && !Node.bDirty ) return Node.World;   // 最新（ほとんどの呼び出しはここ）
		return GetWorldSlow( Slot );
	}

	//-------------------------------------------------------------------------
	// システム
	//-------------------------------------------------------------------------
	// 古いワールド値をすべて計算する。Jobs が null なら（またはワーカーがいなければ）呼び出したスレッドだけで計算する
	void Update( IJobSystem* Jobs );

	FORCEINLINE int32 GetNumNodes() const { return Nodes.GetNumAlive(); }
	FORCEINLINE int32 GetMaxDepth() const { return LevelStart.empty() ? 0 : static_cast<int32>( LevelStart.size() ) - 2; }

private:
	// 階層順の並びの 1 要素。プールの要素はアドレスが動かないので、Update で毎回チャンクを引かずに直接指しておく
	struct FLevelEntry
	{
		FTransformNode* Node = nullptr;
		const FTransformNode* Parent = nullptr;
	};

	void MarkDirtySlow( int32 Slot );
	const FTransform& GetWorldSlow( int32 Slot );
	// 親の番号を探す（祖先オブジェクトをたどる）
	int32 FindParentSlot( const CGameObject* Owner ) const;
	void RebuildHierarchy();
	// Slot と、古い祖先を、親から順に計算する
	void EnsureClean( int32 Slot );
	void Compute( FTransformNode& Node );
	static void ComputeWithParent( FTransformNode& Node, const FTransform& Parent );

private:
	TChunkedPool<FTransformNode> Nodes;

	bool bHierarchyDirty = true;
	std::atomic<bool> bAnyDirty{ false };   // 並列に動くシステムも立てる

	// 階層の浅い順に並べた生きている要素。同じ親の子は隣り合う。LevelStart[d] .. LevelStart[d + 1] が深さ d（最後に終端が 1 つ）
	std::vector<FLevelEntry> Entries;
	std::vector<int32> LevelStart;

	// システムが溜めた、ローカルを書き換えたノード（スレッドごと）
	std::vector<int32> DeferredDirty[32];
};
