// SPDX-License-Identifier: MIT
#include "Library/Framework/Animation/RootMotion.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 行列の全要素が有限で、遠近変形を含まないaffineの形かを調べる。
	bool IsFiniteAffine( const FMatrix4x4& Matrix )
	{
		for ( int32 Row = 0; Row < 4; ++Row ) // 検査する行番号。
		{
			for ( int32 Column = 0; Column < 4; ++Column ) // 検査する列番号。
			{
				if ( !NMath::IsFinite( Matrix.m[Row][Column] ) )
				{
					return false;
				}
			}
		}
		return NMath::Abs( Matrix.m[0][3] ) <= 1.0e-4f && NMath::Abs( Matrix.m[1][3] ) <= 1.0e-4f && NMath::Abs( Matrix.m[2][3] ) <= 1.0e-4f && NMath::Abs( Matrix.m[3][3] - 1.0f ) <= 1.0e-4f;
	}

	// 逆行列が存在し、有限な affine 行列として使えることを確かめる。
	bool TryInverseAffine( const FMatrix4x4& Matrix, FMatrix4x4& OutInverse )
	{
		FMatrix4x4 Inverse; // 検査済みの逆行列。
		if ( !IsFiniteAffine( Matrix ) || !Matrix.TryInverse( Inverse ) || !IsFiniteAffine( Inverse ) )
		{
			return false;
		}
		OutInverse = Inverse;
		return true;
	}

	// 行列が有限なaffine逆行列を持つことだけを調べる。
	bool IsInvertibleAffine( const FMatrix4x4& Matrix )
	{
		FMatrix4x4 Inverse; // 可逆性の確認だけに使う一時行列。
		return TryInverseAffine( Matrix, Inverse );
	}

	// 行列積の結果が有限な affine 行列であることを確認する。
	bool TryMultiplyAffine( const FMatrix4x4& Left, const FMatrix4x4& Right, FMatrix4x4& OutProduct )
	{
		if ( !IsFiniteAffine( Left ) || !IsFiniteAffine( Right ) )
		{
			return false;
		}
		const FMatrix4x4 Product = Left * Right; // 行列積を一時保存し、出力引数とのaliasも安全にする。
		if ( !IsFiniteAffine( Product ) )
		{
			return false;
		}
		OutProduct = Product;
		return true;
	}

	// callbackの結果を一時行列へ受け、成功した姿勢だけを返す。
	bool TrySamplePose( const TCallable<bool( float32, FMatrix4x4& )>& Sample, float64 Time, FMatrix4x4& OutPose )
	{
		if ( !NMath::IsFinite( Time ) || Time < 0.0 || Time > static_cast<float64>( TNumericLimits<float32>::Max() ) )
		{
			return false;
		}
		FMatrix4x4 Pose; // callbackからの姿勢を検査前に隔離する。
		if ( !Sample( static_cast<float32>( Time ), Pose ) || !IsFiniteAffine( Pose ) )
		{
			return false;
		}
		OutPose = Pose;
		return true;
	}

	// loop時間を floor(t / length) 周と 0..length の位相へ分解する。
	bool TryDecomposeLoopTime( float64 Time, float64 Length, int64& OutCycles, float32& OutPhase )
	{
		const float64 RawRemainder = NMath::Fmod( Time, Length ); // trunc基準の剰余は巨大な積の差し引きを避ける。
		if ( !NMath::IsFinite( RawRemainder ) )
		{
			return false;
		}
		const float64 TruncatedCycles = ( Time - RawRemainder ) / Length; // 剰余と同じ丸め経路から整数周回を復元する。
		const float64 RoundedCycles = NMath::Round( TruncatedCycles ); // 誤差確認と安全な整数変換に使う周回数。
		constexpr float64 MaxExactCycles = 4503599627370496.0; // 位相を区別できる範囲に2^52周で制限する。
		if ( !NMath::IsFinite( TruncatedCycles ) || !NMath::IsFinite( RoundedCycles ) || NMath::Abs( TruncatedCycles - RoundedCycles ) > 0.25 || NMath::Abs( RoundedCycles ) > MaxExactCycles )
		{
			return false;
		}
		int64 Cycles = static_cast<int64>( RoundedCycles ); // doubleで整数精度を保てる周回数。
		float64 Phase = RawRemainder; // 負時間だけ剰余を正範囲へ移す。
		if ( Phase < 0.0 )
		{
			Phase += Length;
			--Cycles;
		}
		if ( Phase >= Length )
		{
			Phase = 0.0;
			++Cycles;
		}
		float32 StoredPhase = static_cast<float32>( Phase ); // callbackへ渡せる位相。
		if ( !NMath::IsFinite( StoredPhase ) || StoredPhase < 0.0f )
		{
			return false;
		}
		if ( static_cast<float64>( StoredPhase ) >= Length )
		{
			StoredPhase = 0.0f;
			++Cycles;
		}
		if ( NMath::Abs( static_cast<float64>( Cycles ) ) > MaxExactCycles )
		{
			return false;
		}
		OutCycles = Cycles;
		OutPhase = StoredPhase;
		return true;
	}

	// 周回数の絶対値を安全に取り、負周回では逆cycleを二分累乗する。
	bool TryCyclePower( const FMatrix4x4& Cycle, int64 Exponent, FMatrix4x4& OutPower )
	{
		FMatrix4x4 Base = Cycle; // 2分累乗の底。
		uint64 Remaining = 0; // int64最小値でも安全な指数絶対値。
		if ( Exponent < 0 )
		{
			if ( !TryInverseAffine( Cycle, Base ) )
			{
				return false;
			}
			Remaining = static_cast<uint64>( -( Exponent + 1 ) ) + 1;
		}
		else
		{
			Remaining = static_cast<uint64>( Exponent );
		}
		FMatrix4x4 Power = FMatrix4x4::Identity; // これまで掛け合わせた累乗。
		while ( Remaining > 0 )
		{
			if ( ( Remaining & 1u ) != 0u )
			{
				FMatrix4x4 Product; // 累積済みの累乗へ追加する積。
				if ( !TryMultiplyAffine( Power, Base, Product ) )
				{
					return false;
				}
				Power = Product;
			}
			Remaining >>= 1;
			if ( Remaining > 0 )
			{
				FMatrix4x4 Square; // 次のbit位置に備える底の二乗。
				if ( !TryMultiplyAffine( Base, Base, Square ) )
				{
					return false;
				}
				Base = Square;
			}
		}
		if ( !IsInvertibleAffine( Power ) )
		{
			return false;
		}
		OutPower = Power;
		return true;
	}

	// Loop時刻の位相から基準姿勢との差を求める。
	bool TryLoopRelativePoseAt( const TCallable<bool( float32, FMatrix4x4& )>& Sample, float32 Phase, const FMatrix4x4& Start, const FMatrix4x4& InverseStart, FMatrix4x4& OutPose )
	{
		FMatrix4x4 Pose = Start; // 位相0では基準姿勢をそのまま再利用する。
		if ( Phase != 0.0f && !TrySamplePose( Sample, Phase, Pose ) )
		{
			return false;
		}
		return TryMultiplyAffine( Pose, InverseStart, OutPose );
	}
}

bool NRootMotion::ExtractDelta( float64 FromPlayhead, float64 ToPlayhead, float32 ClipLength, EAnimLoopMode Mode, const TCallable<bool( float32, FMatrix4x4& )>& Sample, FMatrix4x4& OutDelta )
{
	if ( !NMath::IsFinite( FromPlayhead ) || !NMath::IsFinite( ToPlayhead ) || !NMath::IsFinite( ClipLength ) || ClipLength <= 0.0f || !Sample || ( Mode != EAnimLoopMode::Once && Mode != EAnimLoopMode::Loop && Mode != EAnimLoopMode::PingPong ) )
	{
		return false;
	}
	if ( FromPlayhead == ToPlayhead )
	{
		OutDelta = FMatrix4x4::Identity;
		return true;
	}
	const float64 Length = static_cast<float64>( ClipLength ); // 比率計算用に精度を上げたclip長。
	FMatrix4x4 Result; // 成功するまで呼び出し側の出力を触らない。
	if ( Mode == EAnimLoopMode::Once )
	{
		const float64 FromTime = NMath::Max( 0.0, NMath::Min( FromPlayhead, Length ) ); // Once範囲へ収めた開始時刻。
		const float64 ToTime = NMath::Max( 0.0, NMath::Min( ToPlayhead, Length ) ); // Once範囲へ収めた終了時刻。
		if ( FromTime == ToTime )
		{
			OutDelta = FMatrix4x4::Identity;
			return true;
		}
		FMatrix4x4 FromPose; // clamp後にsampleした開始姿勢。
		FMatrix4x4 ToPose; // clamp後にsampleした終了姿勢。
		FMatrix4x4 InverseFrom; // 開始姿勢の逆行列。
		if ( !TrySamplePose( Sample, FromTime, FromPose ) || !TrySamplePose( Sample, ToTime, ToPose ) || !TryInverseAffine( FromPose, InverseFrom ) || !IsInvertibleAffine( ToPose ) || !TryMultiplyAffine( ToPose, InverseFrom, Result ) )
		{
			return false;
		}
	}
	else if ( Mode == EAnimLoopMode::Loop )
	{
		int64 FromCycles = 0; // 開始位置の整数周回。
		int64 ToCycles = 0; // 終了位置の整数周回。
		float32 FromPhase = 0.0f; // 開始位置のclip内時刻。
		float32 ToPhase = 0.0f; // 終了位置のclip内時刻。
		if ( !TryDecomposeLoopTime( FromPlayhead, Length, FromCycles, FromPhase ) || !TryDecomposeLoopTime( ToPlayhead, Length, ToCycles, ToPhase ) )
		{
			return false;
		}
		FMatrix4x4 Start; // 0時刻の基準姿勢。
		FMatrix4x4 End; // ClipLength時刻の終端姿勢。
		FMatrix4x4 InverseStart; // 基準姿勢の逆行列。
		FMatrix4x4 Cycle; // 1周分の連続化行列。
		FMatrix4x4 FromRelative; // 開始位相を基準姿勢から表す行列。
		FMatrix4x4 ToRelative; // 終了位相を基準姿勢から表す行列。
		FMatrix4x4 InverseFrom; // 開始相対姿勢の逆行列。
		FMatrix4x4 CycleDifference; // 共通周回を除いた累積行列。
		if ( !TrySamplePose( Sample, 0.0, Start ) || !TrySamplePose( Sample, Length, End ) || !TryInverseAffine( Start, InverseStart ) || !IsInvertibleAffine( End ) || !TryMultiplyAffine( End, InverseStart, Cycle ) || !IsInvertibleAffine( Cycle ) || !TryLoopRelativePoseAt( Sample, FromPhase, Start, InverseStart, FromRelative ) || !TryLoopRelativePoseAt( Sample, ToPhase, Start, InverseStart, ToRelative ) || !TryInverseAffine( FromRelative, InverseFrom ) || !IsInvertibleAffine( ToRelative ) )
		{
			return false;
		}
		int64 CycleDifferenceExponent = 0; // 共通周回を相殺した後に必要な周回数。
		if ( ToCycles > FromCycles )
		{
			if ( FromCycles < 0 && ToCycles > TNumericLimits<int64>::Max() + FromCycles )
			{
				return false;
			}
			CycleDifferenceExponent = ToCycles - FromCycles;
		}
		else
		{
			if ( FromCycles > 0 && ToCycles < TNumericLimits<int64>::Min() + FromCycles )
			{
				return false;
			}
			CycleDifferenceExponent = ToCycles - FromCycles;
		}
		if ( !TryCyclePower( Cycle, CycleDifferenceExponent, CycleDifference ) || !TryMultiplyAffine( ToRelative, CycleDifference, Result ) || !TryMultiplyAffine( Result, InverseFrom, Result ) )
		{
			return false;
		}
	}
	else
	{
		const float64 Period = Length * 2.0; // 往復一周期の長さ。
		float64 FromPhase = NMath::Fmod( FromPlayhead, Period ); // 開始時刻を往復周期内へ折り返す。
		float64 ToPhase = NMath::Fmod( ToPlayhead, Period ); // 終了時刻を往復周期内へ折り返す。
		if ( !NMath::IsFinite( FromPhase ) || !NMath::IsFinite( ToPhase ) )
		{
			return false;
		}
		if ( FromPhase < 0.0 )
		{
			FromPhase += Period;
		}
		if ( ToPhase < 0.0 )
		{
			ToPhase += Period;
		}
		FromPhase = FromPhase <= Length ? FromPhase : Period - FromPhase;
		ToPhase = ToPhase <= Length ? ToPhase : Period - ToPhase;
		FMatrix4x4 FromPose; // 折り返した開始姿勢。
		FMatrix4x4 ToPose; // 折り返した終了姿勢。
		FMatrix4x4 InverseFrom; // 開始姿勢の逆行列。
		if ( !TrySamplePose( Sample, FromPhase, FromPose ) || !TrySamplePose( Sample, ToPhase, ToPose ) || !TryInverseAffine( FromPose, InverseFrom ) || !IsInvertibleAffine( ToPose ) || !TryMultiplyAffine( ToPose, InverseFrom, Result ) )
		{
			return false;
		}
	}
	if ( !IsInvertibleAffine( Result ) )
	{
		return false;
	}
	OutDelta = Result;
	return true;
}
