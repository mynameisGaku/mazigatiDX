#pragma once

// Memory.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// Memory.h の末尾から include される。直接 include しない

template<typename T>
void NMemoryOps::RelocateItems( T* Dest, T* Src, int32 Count )
{
	if constexpr ( bIsBitwiseRelocatable<T> )
	{
		FMemory::Memcpy( Dest, Src, sizeof( T ) * static_cast<usize>( Count ) );
	}
	else
	{
		for ( int32 i = 0; i < Count; ++i )
		{
			new ( Dest + i ) T( std::move( Src[i] ) );
			Src[i].~T();
		}
	}
}