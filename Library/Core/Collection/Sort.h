#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <utility>

#include "Library/Core/Memory/Memory.h"

//=============================================================================
// Algo
//   連続領域に対するソートなどのアルゴリズム。
//   Pred( A, B ) は「A を B より前に置くなら true」（operator< 相当）。
//=============================================================================
namespace NAlgo
{
	template<typename T = void>
	struct TLess
	{
		FORCEINLINE constexpr bool operator()( const T& A, const T& B ) const { return A < B; }
	};

	template<>
	struct TLess<void>
	{
		template<typename A, typename B>
		FORCEINLINE constexpr bool operator()( const A& Lhs, const B& Rhs ) const { return Lhs < Rhs; }
	};

	template<typename T>
	FORCEINLINE void Swap( T& A, T& B )
	{
		if ( &A == &B ) return;
		T Temp = std::move( A );
		A = std::move( B );
		B = std::move( Temp );
	}

	template<typename T>
	FORCEINLINE void Reverse( T* First, int32 Num )
	{
		for ( int32 i = 0, j = Num - 1; i < j; ++i, --j ) Swap( First[i], First[j] );
	}

	namespace NSortDetail
	{
		inline constexpr int32 InsertionSortThreshold = 16;

		// 安定な挿入ソート
		template<typename T, typename P>
		void InsertionSort( T* First, int32 Num, P& Pred );

		template<typename T, typename P>
		void SiftDown( T* First, int32 Root, int32 Num, P& Pred );

		template<typename T, typename P>
		FORCEINLINE void HeapSort( T* First, int32 Num, P& Pred )
		{
			for ( int32 i = Num / 2 - 1; i >= 0; --i ) SiftDown( First, i, Num, Pred );
			for ( int32 End = Num - 1; End > 0; --End )
			{
				Swap( First[0], First[End] );
				SiftDown( First, 0, End, Pred );
			}
		}

		// 3 つの中央値を First[0] に置く
		template<typename T, typename P>
		FORCEINLINE void MedianToFront( T* First, int32 Num, P& Pred )
		{
			T* A = First + 1;
			T* B = First + Num / 2;
			T* C = First + Num - 1;
			if ( Pred( *B, *A ) ) Swap( *A, *B );
			if ( Pred( *C, *B ) ) Swap( *B, *C );
			if ( Pred( *B, *A ) ) Swap( *A, *B );
			Swap( First[0], *B );
		}

		// イントロソート（クイックソート + 再帰が深すぎればヒープソート）
		template<typename T, typename P>
		void IntroSort( T* First, int32 Num, int32 DepthLimit, P& Pred );

		// マージソート。Buffer は Num / 2 + 1 要素分の未初期化メモリ
		template<typename T, typename P>
		void MergeSort( T* First, int32 Num, T* Buffer, P& Pred );

		FORCEINLINE int32 Log2Floor( int32 Value )
		{
			int32 Result = 0;
			while ( Value > 1 ) { Value >>= 1; ++Result; }
			return Result;
		}
	}

	// 不安定ソート O(n log n)
	template<typename T, typename P>
	FORCEINLINE void Sort( T* First, int32 Num, P Pred )
	{
		if ( Num < 2 ) return;
		NSortDetail::IntroSort( First, Num, NSortDetail::Log2Floor( Num ) * 2, Pred );
	}

	template<typename T>
	FORCEINLINE void Sort( T* First, int32 Num ) { Sort( First, Num, TLess<T>() ); }

	// 安定ソート O(n log n)（等しい要素の順序を保つ）
	template<typename T, typename P>
	FORCEINLINE void StableSort( T* First, int32 Num, P Pred )
	{
		if ( Num < 2 ) return;
		T* Buffer = FMemory::AllocateArray<T>( Num / 2 + 1 );
		NSortDetail::MergeSort( First, Num, Buffer, Pred );
		FMemory::Free( Buffer );
	}

	template<typename T>
	FORCEINLINE void StableSort( T* First, int32 Num ) { StableSort( First, Num, TLess<T>() ); }
}

#include "Sort.inl"
