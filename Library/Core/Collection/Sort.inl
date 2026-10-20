#pragma once

// Sort.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// Sort.h の末尾から include される。直接 include しない

template<typename T, typename P>
void NAlgo::NSortDetail::InsertionSort( T* First, int32 Num, P& Pred )
{
	for ( int32 i = 1; i < Num; ++i )
	{
		if ( !Pred( First[i], First[i - 1] ) ) continue;

		T Temp = std::move( First[i] );
		int32 j = i;
		do
		{
			First[j] = std::move( First[j - 1] );
			--j;
		} while ( j > 0 && Pred( Temp, First[j - 1] ) );
		First[j] = std::move( Temp );
	}
}

template<typename T, typename P>
void NAlgo::NSortDetail::SiftDown( T* First, int32 Root, int32 Num, P& Pred )
{
	while ( true )
	{
		int32 Child = Root * 2 + 1;
		if ( Child >= Num ) return;
		if ( Child + 1 < Num && Pred( First[Child], First[Child + 1] ) ) ++Child;
		if ( !Pred( First[Root], First[Child] ) ) return;
		Swap( First[Root], First[Child] );
		Root = Child;
	}
}

template<typename T, typename P>
void NAlgo::NSortDetail::IntroSort( T* First, int32 Num, int32 DepthLimit, P& Pred )
{
	while ( Num > InsertionSortThreshold )
	{
		if ( DepthLimit-- == 0 )
		{
			HeapSort( First, Num, Pred );
			return;
		}

		MedianToFront( First, Num, Pred );

		// First[0] をピボットとして分割（等しい要素は両側に分散させる）
		int32 i = 1;
		int32 j = Num - 1;
		while ( true )
		{
			while ( i <= j && Pred( First[i], First[0] ) ) ++i;
			while ( i <= j && Pred( First[0], First[j] ) ) --j;
			if ( i >= j ) break;
			Swap( First[i], First[j] );
			++i;
			--j;
		}
		Swap( First[0], First[j] );

		// 小さい側を再帰、大きい側はループで処理（スタック深さを抑える）
		const int32 LeftNum  = j;
		const int32 RightNum = Num - j - 1;
		if ( LeftNum < RightNum )
		{
			IntroSort( First, LeftNum, DepthLimit, Pred );
			First += j + 1;
			Num    = RightNum;
		}
		else
		{
			IntroSort( First + j + 1, RightNum, DepthLimit, Pred );
			Num = LeftNum;
		}
	}
	InsertionSort( First, Num, Pred );
}

template<typename T, typename P>
void NAlgo::NSortDetail::MergeSort( T* First, int32 Num, T* Buffer, P& Pred )
{
	if ( Num <= InsertionSortThreshold )
	{
		InsertionSort( First, Num, Pred );
		return;
	}

	const int32 Half = Num / 2;
	MergeSort( First, Half, Buffer, Pred );
	MergeSort( First + Half, Num - Half, Buffer, Pred );

	// 既に整列済みなら何もしない
	if ( !Pred( First[Half], First[Half - 1] ) ) return;

	// 左半分を退避してから先頭へ書き戻しながらマージ
	for ( int32 k = 0; k < Half; ++k ) new ( Buffer + k ) T( std::move( First[k] ) );

	int32 L = 0, R = Half, Out = 0;
	while ( L < Half && R < Num )
	{
		// 等しければ左を優先（安定）
		if ( Pred( First[R], Buffer[L] ) ) First[Out++] = std::move( First[R++] );
		else First[Out++] = std::move( Buffer[L++] );
	}
	while ( L < Half ) First[Out++] = std::move( Buffer[L++] );

	NMemoryOps::DestructItems( Buffer, Half );
}