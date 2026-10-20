#pragma once
#include <memory>
#include <vector>

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// TChunkedPool
//   コンポーネントのデータを置く入れ物（ECS の「コンポーネントの配列」）。
//   - 要素は番号 (slot) で指す。Allocate / Free で出し入れし、Free した番号は後で再利用される（順番は決まっている）
//   - 領域は固定サイズの塊 (chunk) ごとに確保するので、要素を増やしても既存の要素のアドレスは動かない
//     （参照を持ったまま新しく Allocate してもよい。TArray のように再確保で無効にならない）
//   - 番号は 0 .. GetNumSlots() - 1 の連続した範囲で、データ並列に回しやすい（Free 済みは IsAlive で除く）
//
//   スレッド: 構造の変更 (Allocate / Free) は 1 つのスレッドから。要素の読み書きは、同じ番号を同時に触らなければ複数スレッドから可能
//=============================================================================
template<typename T, int32 ChunkShift = 10>
class TChunkedPool
{
public:
	static constexpr int32 ChunkSize = 1 << ChunkShift;
	static constexpr int32 ChunkMask = ChunkSize - 1;

	// 新しい要素の番号を返す（T() で初期化される）
	int32 Allocate()
	{
		int32 Slot;
		if ( !FreeSlots.empty() )
		{
			Slot = FreeSlots.back();
			FreeSlots.pop_back();
		}
		else
		{
			Slot = NumSlots++;
			if ( ( Slot >> ChunkShift ) >= static_cast<int32>( Chunks.size() ) ) Chunks.push_back( std::make_unique<T[]>( ChunkSize ) );
			Alive.push_back( 0 );
		}
		( *this )[Slot] = T();
		Alive[Slot] = 1;
		++NumAlive;
		return Slot;
	}

	void Free( int32 Slot )
	{
		if ( !IsAlive( Slot ) ) return;
		( *this )[Slot] = T();   // 持っていたものを手放す
		Alive[Slot] = 0;
		--NumAlive;
		FreeSlots.push_back( Slot );
	}

	FORCEINLINE T& operator[]( int32 Slot ) { return Chunks[Slot >> ChunkShift][Slot & ChunkMask]; }
	FORCEINLINE const T& operator[]( int32 Slot ) const { return Chunks[Slot >> ChunkShift][Slot & ChunkMask]; }

	FORCEINLINE bool IsAlive( int32 Slot ) const { return Slot >= 0 && Slot < NumSlots && Alive[Slot] != 0; }

	// 使われたことのある番号の上限（0 .. この値 - 1 を走査する）
	FORCEINLINE int32 GetNumSlots() const { return NumSlots; }
	FORCEINLINE int32 GetNumAlive() const { return NumAlive; }

private:
	std::vector<std::unique_ptr<T[]>> Chunks;
	std::vector<uint8> Alive;
	std::vector<int32> FreeSlots;
	int32 NumSlots = 0;
	int32 NumAlive = 0;
};
