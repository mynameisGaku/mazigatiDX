// SPDX-License-Identifier: NOASSERTION
#include "AssetSelfTestCore.h"

#include <algorithm>   // 標準ライブラリは、Min / Max / TNumericLimits を突き合わせる独立した答えとしてだけ使う
#include <cmath>
#include <cstring>
#include <limits>

#include "AssetSelfTestSupport.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Math/MathConstants.h"

namespace NAssetTest
{
	namespace
	{
		// 二つの型が同じかを調べる、標準ライブラリを使わない道具。
		template<typename A, typename B>
		struct TSameType
		{
			// 同じ型なら true。
			static constexpr bool Value = false;
		};

		// 同じ型どうしの特殊化。
		template<typename A>
		struct TSameType<A, A>
		{
			// 同じ型なら true。
			static constexpr bool Value = true;
		};

		// 値の種類（左辺値 / const 左辺値 / 右辺値）を、オーバーロードの選ばれ方で見分ける型。
		struct FTracker
		{
			// 呼び分けの確認だけに使う整数。
			int32 Value = 0;
		};

		// const の左辺値参照を受ける版（戻り値 1）。
		int32 Pick( const FTracker& ) { return 1; }
		// 右辺値参照を受ける版（戻り値 2）。
		int32 Pick( FTracker&& ) { return 2; }
		// 左辺値参照（const でない）を受ける版（戻り値 3）。
		int32 Pick( FTracker& ) { return 3; }

		// 受けた値を Forward で渡す、転送参照の関数。
		template<typename T>
		int32 ForwardPick( T&& Object )
		{
			return Pick( Forward<T>( Object ) );
		}

		// コンパイル時に決まる性質を、実行時の確認にも数える。
		static_assert( NMath::Max( 3, 7 ) == 7 && NMath::Min( 3, 7 ) == 3 && NMath::Max( 7, 3 ) == 7 && NMath::Min( 7, 3 ) == 3, "Min / Max は定数式で使える" );
		static_assert( NMath::Max( 1, 9, 5 ) == 9 && NMath::Min( 4, 2, 8 ) == 2 && NMath::Max( 9, 1, 5 ) == 9 && NMath::Min( 8, 4, 2 ) == 2, "3 引数の Min / Max は定数式で使える" );
		static_assert( NMath::Clamp( 5, 0, 3 ) == 3 && NMath::Clamp( -5, 0, 3 ) == 0 && NMath::Clamp( 2, 0, 3 ) == 2, "Clamp は定数式で使える" );
		static_assert( NMath::IsFinite( 1.5f ) && !NMath::IsFinite( TNumericLimits<float32>::Infinity() ) && !NMath::IsFinite( TNumericLimits<float32>::QuietNaN() ), "IsFinite は定数式で使える" );
		static_assert( TSameType<TRemoveReferenceT<int32&>, int32>::Value && TSameType<TRemoveReferenceT<const int32&&>, const int32>::Value && TSameType<TRemoveReferenceT<int32>, int32>::Value, "TRemoveReferenceT は参照だけを外す" );
		static_assert( TNumericLimits<uint32>::Max() == 4294967295u && TNumericLimits<int32>::Min() == -2147483647 - 1, "TNumericLimits は定数式で使える" );

		// 整数の限界を、標準ライブラリの値と突き合わせる（型ごとに同じ確認を繰り返すためのテンプレート）。
		template<typename T>
		bool IntegerLimitsMatch()
		{
			return TNumericLimits<T>::Min() == ( std::numeric_limits<T>::min )() && TNumericLimits<T>::Max() == ( std::numeric_limits<T>::max )() && TNumericLimits<T>::Lowest() == std::numeric_limits<T>::lowest();
		}

		// float32 の値のビットを取り出す。
		uint32 BitsOf( float32 Value )
		{
			uint32 Bits = 0;
			std::memcpy( &Bits, &Value, sizeof( Bits ) );
			return Bits;
		}

		// float64 の値のビットを取り出す。
		uint64 BitsOf64( float64 Value )
		{
			uint64 Bits = 0;
			std::memcpy( &Bits, &Value, sizeof( Bits ) );
			return Bits;
		}

		// 与えたビットの float32 を作る。
		float32 FloatFromBits( uint32 Bits )
		{
			float32 Value = 0.0f;
			std::memcpy( &Value, &Bits, sizeof( Value ) );
			return Value;
		}

		// float64 の値を、与えたビットから作る。
		float64 DoubleFromBits( uint64 Bits )
		{
			float64 Value = 0.0;
			std::memcpy( &Value, &Bits, sizeof( Value ) );
			return Value;
		}

		// 基本の比較・範囲。型ごとに同じ式を試す。
		void TestBasics( FChecker& C )
		{
			C.Group = "core-numeric-basics";
			C.Check( NMath::Abs( -3 ) == 3 && NMath::Abs( 3 ) == 3 && NMath::Abs( 0 ) == 0 && NMath::Abs( -2.5f ) == 2.5f && NMath::Abs( -2.5 ) == 2.5 && NMath::Abs( -0.0f ) == 0.0f, "Abs: 負の数を正にし、正の数と 0 はそのまま（int / float / double）" );
			// 浮動小数点の Abs は符号ビットを落とす（-0.0 は +0.0 になる。std::fabs と同じ）。
			C.Check( BitsOf( NMath::Abs( -0.0f ) ) == 0u && BitsOf( NMath::Abs( 0.0f ) ) == 0u && BitsOf( NMath::Abs( -0.0f ) ) == BitsOf( std::fabs( -0.0f ) ) && BitsOf( NMath::Abs( -1.5f ) ) == BitsOf( 1.5f ) && NMath::Abs( TNumericLimits<float32>::Lowest() ) == TNumericLimits<float32>::Max(), "Abs(float32): -0.0 は +0.0 になる（符号ビットを落とす）。通常の値と最小値も正しい" );
			C.Check( BitsOf64( NMath::Abs( -0.0 ) ) == 0ull && BitsOf64( NMath::Abs( -0.0 ) ) == BitsOf64( std::fabs( -0.0 ) ) && BitsOf64( NMath::Abs( -2.5 ) ) == BitsOf64( 2.5 ), "Abs(float64): -0.0 は +0.0 になる" );
			C.Check( NMath::IsInfinite( NMath::Abs( -TNumericLimits<float32>::Infinity() ) ) && NMath::Abs( -TNumericLimits<float32>::Infinity() ) > 0.0f && NMath::IsNaN( NMath::Abs( -TNumericLimits<float32>::QuietNaN() ) ), "Abs: 負の無限大は正の無限大に、NaN は NaN のまま" );
			C.Check( NMath::Abs( TNumericLimits<int64>::Max() ) == TNumericLimits<int64>::Max() && NMath::Abs( -TNumericLimits<int64>::Max() ) == TNumericLimits<int64>::Max(), "Abs: 64bit の大きな値でも壊れない" );
			C.Check( NMath::Min( 2, 5 ) == 2 && NMath::Min( 5, 2 ) == 2 && NMath::Max( 2, 5 ) == 5 && NMath::Max( 5, 2 ) == 5 && NMath::Min( -2, -5 ) == -5 && NMath::Max( -2, -5 ) == -2 && NMath::Min( 4, 4 ) == 4, "Min / Max: 順番に依らず小さい方・大きい方を返す（負の数・同じ値も）" );
			C.Check( NMath::Min( 1.5f, 2.5f ) == 1.5f && NMath::Max( 1.5f, 2.5f ) == 2.5f && NMath::Min( 1.5, 2.5 ) == 1.5 && NMath::Max( 1.5, 2.5 ) == 2.5, "Min / Max: float / double でも同じ" );
			C.Check( NMath::Min( 5, 2, 9 ) == 2 && NMath::Min( 9, 5, 2 ) == 2 && NMath::Min( 2, 9, 5 ) == 2 && NMath::Max( 5, 9, 2 ) == 9 && NMath::Max( 9, 2, 5 ) == 9 && NMath::Max( 2, 5, 9 ) == 9, "3 引数の Min / Max: どの位置に最小・最大があっても返す" );
			C.Check( NMath::Clamp( 5, 1, 3 ) == 3 && NMath::Clamp( 0, 1, 3 ) == 1 && NMath::Clamp( 2, 1, 3 ) == 2 && NMath::Clamp( 1, 1, 3 ) == 1 && NMath::Clamp( 3, 1, 3 ) == 3 && NMath::Clamp( 0.5f, 0.0f, 1.0f ) == 0.5f && NMath::Clamp( 1.5f, 0.0f, 1.0f ) == 1.0f, "Clamp: 範囲より上は上限、下は下限、境界と中はそのまま" );
			C.Check( NMath::Sign( -4 ) == -1 && NMath::Sign( 4 ) == 1 && NMath::Sign( 0 ) == 0 && NMath::Sign( -0.5f ) == -1.0f && NMath::Square( -3 ) == 9 && NMath::Square( 1.5f ) == 2.25f, "Sign / Square" );
			// NaN を渡したときの向き。比べた結果が偽なら決まった側を返す（std::min / std::max と同じ）。
			const float32 NaN = TNumericLimits<float32>::QuietNaN();
			const float32 One = 1.0f;
			C.Check( NMath::IsNaN( NMath::Max( NaN, One ) ) == NMath::IsNaN( ( std::max )( NaN, One ) ) && NMath::IsNaN( NMath::Max( One, NaN ) ) == NMath::IsNaN( ( std::max )( One, NaN ) ) && NMath::IsNaN( NMath::Min( NaN, One ) ) == NMath::IsNaN( ( std::min )( NaN, One ) ) && NMath::IsNaN( NMath::Min( One, NaN ) ) == NMath::IsNaN( ( std::min )( One, NaN ) ),
					 "Min / Max に NaN を渡したときの向きが、std::min / std::max と同じ" );
		}

		// NaN・無限大の判定。
		void TestFiniteness( FChecker& C )
		{
			C.Group = "core-numeric-finite";
			// 有限になる値。
			const float32 Finite32[] = { 0.0f, -0.0f, 1.0f, -1.0f, 123456.789f, 1.0e-30f, FloatFromBits( 0x00000001u ), FloatFromBits( 0x007FFFFFu ), FloatFromBits( 0x00800000u ), TNumericLimits<float32>::Max(), TNumericLimits<float32>::Lowest(), TNumericLimits<float32>::Min(), TNumericLimits<float32>::Epsilon() };
			bool bFinite32 = true;
			for ( const float32 Value : Finite32 ) bFinite32 = bFinite32 && NMath::IsFinite( Value ) && !NMath::IsNaN( Value ) && !NMath::IsInfinite( Value );
			C.Check( bFinite32, "float32: 0・負の 0・通常の値・最小の非正規化数・最大・最小・Epsilon は、有限で、NaN でも無限大でもない" );
			// 有限でない float32 の値と、そのビット。
			const float32 PositiveInfinity = FloatFromBits( 0x7F800000u );
			const float32 NegativeInfinity = FloatFromBits( 0xFF800000u );
			const float32 QuietNaN = FloatFromBits( 0x7FC00000u );
			const float32 NegativeNaN = FloatFromBits( 0xFFC00000u );
			const float32 SignalingNaN = FloatFromBits( 0x7F800001u );
			const float32 PayloadNaN = FloatFromBits( 0x7FFFFFFFu );
			C.Check( !NMath::IsFinite( PositiveInfinity ) && !NMath::IsFinite( NegativeInfinity ) && NMath::IsInfinite( PositiveInfinity ) && NMath::IsInfinite( NegativeInfinity ) && !NMath::IsNaN( PositiveInfinity ) && !NMath::IsNaN( NegativeInfinity ), "float32: 正と負の無限大は、有限でなく、無限大で、NaN ではない" );
			C.Check( !NMath::IsFinite( QuietNaN ) && !NMath::IsFinite( NegativeNaN ) && !NMath::IsFinite( SignalingNaN ) && !NMath::IsFinite( PayloadNaN ) && NMath::IsNaN( QuietNaN ) && NMath::IsNaN( NegativeNaN ) && NMath::IsNaN( SignalingNaN ) && NMath::IsNaN( PayloadNaN ) && !NMath::IsInfinite( QuietNaN ) && !NMath::IsInfinite( SignalingNaN ),
					 "float32: 静かな NaN・負の NaN・シグナリング NaN・仮数が全部 1 の NaN は、有限でなく、NaN で、無限大ではない" );
			C.Check( BitsOf( TNumericLimits<float32>::Infinity() ) == 0x7F800000u && NMath::IsNaN( TNumericLimits<float32>::QuietNaN() ) && ( BitsOf( TNumericLimits<float32>::QuietNaN() ) & 0x7F800000u ) == 0x7F800000u && NMath::Infinity == TNumericLimits<float32>::Infinity() && NMath::MaxFloat == TNumericLimits<float32>::Max(), "float32: Infinity と QuietNaN の定数が、指数部が全部 1 の正しい値で、NMath の定数とも一致する" );
			// 有限になる double の値。
			const float64 Finite64[] = { 0.0, -0.0, 1.0, -1.0, 1.0e-300, DoubleFromBits( 1ull ), DoubleFromBits( 0x000FFFFFFFFFFFFFull ), TNumericLimits<float64>::Max(), TNumericLimits<float64>::Lowest(), TNumericLimits<float64>::Min(), TNumericLimits<float64>::Epsilon() };
			bool bFinite64 = true;
			for ( const float64 Value : Finite64 ) bFinite64 = bFinite64 && NMath::IsFinite( Value ) && !NMath::IsNaN( Value ) && !NMath::IsInfinite( Value );
			C.Check( bFinite64, "float64: 有限の値（最小の非正規化数・最大・最小を含む）は、有限で、NaN でも無限大でもない" );
			C.Check( !NMath::IsFinite( DoubleFromBits( 0x7FF0000000000000ull ) ) && NMath::IsInfinite( DoubleFromBits( 0x7FF0000000000000ull ) ) && NMath::IsInfinite( DoubleFromBits( 0xFFF0000000000000ull ) ) && !NMath::IsNaN( DoubleFromBits( 0x7FF0000000000000ull ) ), "float64: 正と負の無限大" );
			C.Check( NMath::IsNaN( DoubleFromBits( 0x7FF8000000000000ull ) ) && NMath::IsNaN( DoubleFromBits( 0xFFF8000000000000ull ) ) && NMath::IsNaN( DoubleFromBits( 0x7FF0000000000001ull ) ) && NMath::IsNaN( DoubleFromBits( 0x7FFFFFFFFFFFFFFFull ) ) && !NMath::IsFinite( DoubleFromBits( 0x7FF8000000000000ull ) ) && !NMath::IsInfinite( DoubleFromBits( 0x7FF8000000000000ull ) ),
					 "float64: 静かな NaN・負の NaN・シグナリング NaN・仮数が全部 1 の NaN は NaN で、有限でも無限大でもない" );
			C.Check( NMath::IsNaN( TNumericLimits<float64>::QuietNaN() ) && NMath::IsInfinite( TNumericLimits<float64>::Infinity() ) && !NMath::IsFinite( TNumericLimits<float64>::Infinity() ), "float64: Infinity と QuietNaN の定数" );
			// 計算で NaN・無限大になる場合（実行時に作る）。
			volatile float32 Zero = 0.0f;
			volatile float32 Largest = TNumericLimits<float32>::Max();
			C.Check( NMath::IsNaN( Zero / Zero ) && NMath::IsInfinite( 1.0f / Zero ) && NMath::IsInfinite( Largest * 2.0f ) && NMath::IsFinite( Largest * 0.5f ), "0 / 0 は NaN、1 / 0 と最大値の 2 倍は無限大、最大値の半分は有限" );
		}

		// 型ごとの限界。標準ライブラリの値と突き合わせる。
		void TestLimits( FChecker& C )
		{
			C.Group = "core-numeric-limits";
			C.Check( IntegerLimitsMatch<int8>() && IntegerLimitsMatch<int16>() && IntegerLimitsMatch<int32>() && IntegerLimitsMatch<int64>() && IntegerLimitsMatch<uint8>() && IntegerLimitsMatch<uint16>() && IntegerLimitsMatch<uint32>() && IntegerLimitsMatch<uint64>(), "整数 8 種類の Min / Max / Lowest が、標準ライブラリと一致する" );
			C.Check( TNumericLimits<int32>::Max() == 2147483647 && TNumericLimits<int32>::Min() == -2147483647 - 1 && TNumericLimits<int64>::Max() == 9223372036854775807ll && TNumericLimits<uint8>::Max() == 255 && TNumericLimits<uint16>::Max() == 65535 && TNumericLimits<uint64>::Max() == 18446744073709551615ull && TNumericLimits<int8>::Min() == -128 && TNumericLimits<int16>::Min() == -32768,
					 "整数の限界の手書きの値（2 の累乗から求めた）とも一致する" );
			C.Check( TNumericLimits<float32>::Max() == ( std::numeric_limits<float32>::max )() && TNumericLimits<float32>::Min() == ( std::numeric_limits<float32>::min )() && TNumericLimits<float32>::Lowest() == std::numeric_limits<float32>::lowest() && TNumericLimits<float32>::Epsilon() == std::numeric_limits<float32>::epsilon(), "float32 の Max / Min / Lowest / Epsilon が、標準ライブラリと一致する" );
			C.Check( TNumericLimits<float64>::Max() == ( std::numeric_limits<float64>::max )() && TNumericLimits<float64>::Min() == ( std::numeric_limits<float64>::min )() && TNumericLimits<float64>::Lowest() == std::numeric_limits<float64>::lowest() && TNumericLimits<float64>::Epsilon() == std::numeric_limits<float64>::epsilon(), "float64 の Max / Min / Lowest / Epsilon が、標準ライブラリと一致する" );
			C.Check( BitsOf( TNumericLimits<float32>::Infinity() ) == BitsOf( std::numeric_limits<float32>::infinity() ) && BitsOf( TNumericLimits<float32>::QuietNaN() ) == BitsOf( std::numeric_limits<float32>::quiet_NaN() ), "float32 の Infinity / QuietNaN が、標準ライブラリとビットまで一致する" );
			// 型の境の性質（実行時に調べる）。
			volatile float32 One = 1.0f;
			volatile float32 Epsilon = TNumericLimits<float32>::Epsilon();
			volatile float32 HalfEpsilon = TNumericLimits<float32>::Epsilon() * 0.5f;
			C.Check( One + Epsilon != 1.0f && One + HalfEpsilon == 1.0f && TNumericLimits<float32>::Lowest() == -TNumericLimits<float32>::Max() && TNumericLimits<float32>::Min() > 0.0f && TNumericLimits<float32>::Max() > 1.0e38f, "Epsilon は 1 に足すと変わる最小の大きさ、Lowest は -Max、Min は 0 より大きい最小の正規化数" );
			// windows.h の min / max マクロと名前がぶつからないこと（大文字の関数名なので、括弧で囲まなくても呼べる）。
			const int32 Largest = TNumericLimits<int32>::Max();
			const int32 Smallest = TNumericLimits<int32>::Min();
			C.Check( Largest > 0 && Smallest < 0 && Largest + Smallest == -1, "Max() / Min() は、括弧で囲まずに呼べる（windows.h のマクロとぶつからない）" );
		}

		// 右辺値の受け渡し。
		void TestMoveAndForward( FChecker& C )
		{
			C.Group = "core-utility-move-forward";
			// 値の種類を変えながら渡す対象。
			FTracker Lvalue;
			const FTracker ConstLvalue;
			C.Check( Pick( Lvalue ) == 3 && Pick( ConstLvalue ) == 1 && Pick( FTracker() ) == 2, "確認用の Pick: 左辺値は 3、const 左辺値は 1、右辺値は 2 を選ぶ" );
			C.Check( Pick( MoveTemp( Lvalue ) ) == 2, "MoveTemp は左辺値を右辺値として渡す" );
			C.Check( Pick( MoveTemp( ConstLvalue ) ) == 1, "MoveTemp は const の値を、const 右辺値にする（右辺値参照の版は選ばれず、const 左辺値参照の版になる）" );
			C.Check( ForwardPick( Lvalue ) == 3 && ForwardPick( ConstLvalue ) == 1 && ForwardPick( MoveTemp( Lvalue ) ) == 2 && ForwardPick( FTracker() ) == 2, "Forward は、左辺値は左辺値のまま、右辺値は右辺値のまま渡す" );
			// 中身を移す実際の使い方。
			FTracker Source;
			Source.Value = 42;
			FTracker Moved = MoveTemp( Source );
			C.Check( Moved.Value == 42, "MoveTemp で移した先に値が入る" );
			// 参照の外し方。
			int32 Number = 5;
			int32& Reference = Number;
			C.Check( TSameType<TRemoveReferenceT<decltype( Reference )>, int32>::Value && TSameType<TRemoveReferenceT<decltype( MoveTemp( Number ) )>, int32>::Value && TSameType<decltype( MoveTemp( Number ) ), int32&&>::Value, "TRemoveReferenceT は参照を外し、MoveTemp の戻り値は右辺値参照" );
		}
	}

	// Core の数値の基本と右辺値の受け渡しを確かめる。
	void RunCoreNumericTests( FChecker& C )
	{
		TestBasics( C );
		TestFiniteness( C );
		TestLimits( C );
		TestMoveAndForward( C );
	}
}
