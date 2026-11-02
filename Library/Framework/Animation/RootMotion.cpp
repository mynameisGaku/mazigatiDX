// SPDX-License-Identifier: MIT
#include "Library/Framework/Animation/RootMotion.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// motion分解でproper rigidの軸長・直交性を判定する許容誤差。
	constexpr float32 RigidMotionTolerance = 1.0e-4f;

	// 外部入力はprojective成分を厳密に拒否し、丸め誤差のm33だけをaffine値へ戻す。
	bool TryCanonicalizeAffineInput( const FMatrix4x4& Matrix, FMatrix4x4& OutAffine )
	{
		for ( int32 Row = 0; Row < 4; ++Row ) // 入力値のfiniteを調べる行番号。
		{
			for ( int32 Column = 0; Column < 4; ++Column ) // 入力値のfiniteを調べる列番号。
			{
				if ( !NMath::IsFinite( Matrix.m[Row][Column] ) )
				{
					return false;
				}
			}
		}
		if ( Matrix.m[0][3] != 0.0f || Matrix.m[1][3] != 0.0f || Matrix.m[2][3] != 0.0f || NMath::Abs( Matrix.m[3][3] - 1.0f ) > RigidMotionTolerance )
		{
			return false;
		}
		FMatrix4x4 Affine = Matrix; // 入力を変更せずcanonical形へ直す一時値。
		Affine.m[0][3] = 0.0f;
		Affine.m[1][3] = 0.0f;
		Affine.m[2][3] = 0.0f;
		Affine.m[3][3] = 1.0f;
		OutAffine = Affine;
		return true;
	}

	// 外部入力のprojective成分を許さず、有限なaffine行列だけを受け付ける。
	bool IsFiniteStrictAffine( const FMatrix4x4& Matrix )
	{
		for ( int32 Row = 0; Row < 4; ++Row ) // 全要素をfinite検査する行番号。
		{
			for ( int32 Column = 0; Column < 4; ++Column ) // 全要素をfinite検査する列番号。
			{
				if ( !NMath::IsFinite( Matrix.m[Row][Column] ) )
				{
					return false;
				}
			}
		}
		return Matrix.m[0][3] == 0.0f && Matrix.m[1][3] == 0.0f && Matrix.m[2][3] == 0.0f && Matrix.m[3][3] == 1.0f;
	}

	// 既存逆行列のdet下限を避けるため、basisの3x3成分だけを共通倍率で正規化して逆行列を求める。
	bool TryInverseStrictAffineBasis( const FMatrix4x4& Basis, FMatrix4x4& OutInverse )
	{
		if ( !IsFiniteStrictAffine( Basis ) )
		{
			return false;
		}
		float32 MaxAxisComponent = 0.0f; // 3x3基底成分の共通正規化倍率。
		for ( int32 Row = 0; Row < 3; ++Row ) // 基底行を調べる番号。
		{
			for ( int32 Column = 0; Column < 3; ++Column ) // 基底列を調べる番号。
			{
				MaxAxisComponent = NMath::Max( MaxAxisComponent, NMath::Abs( Basis.m[Row][Column] ) );
			}
		}
		if ( !NMath::IsFinite( MaxAxisComponent ) || MaxAxisComponent <= 0.0f )
		{
			return false;
		}
		const float32 InverseScale = 1.0f / MaxAxisComponent; // normalized basisを作る逆倍率。
		if ( !NMath::IsFinite( InverseScale ) )
		{
			return false;
		}
		FMatrix4x4 NormalizedBasis = FMatrix4x4::Scale( InverseScale ) * Basis; // 左から倍率を掛けて3x3だけを正規化する。
		if ( !IsFiniteStrictAffine( NormalizedBasis ) )
		{
			return false;
		}
		const FVector3 AxisX = NormalizedBasis.GetAxisX(); // 正規化後のX基底。
		const FVector3 AxisY = NormalizedBasis.GetAxisY(); // 正規化後のY基底。
		const FVector3 AxisZ = NormalizedBasis.GetAxisZ(); // 正規化後のZ基底。
		const float32 Determinant = FVector3::Dot( FVector3::Cross( AxisX, AxisY ), AxisZ ); // 正向きを判定する3x3行列式。
		if ( !NMath::IsFinite( Determinant ) || Determinant <= 0.0f )
		{
			return false;
		}
		FMatrix4x4 InverseNormalized; // 既存TryInverseへ渡す、倍率正規化済みbasisの逆行列。
		if ( !NormalizedBasis.TryInverse( InverseNormalized ) )
		{
			return false;
		}
		FMatrix4x4 Inverse = InverseNormalized * FMatrix4x4::Scale( InverseScale ); // B=S(k)*NからB^-1=N^-1*S(1/k)を復元する。
		if ( !NMath::IsFinite( Inverse.m[0][3] ) || !NMath::IsFinite( Inverse.m[1][3] ) || !NMath::IsFinite( Inverse.m[2][3] ) || !NMath::IsFinite( Inverse.m[3][3] ) || NMath::Abs( Inverse.m[0][3] ) > RigidMotionTolerance || NMath::Abs( Inverse.m[1][3] ) > RigidMotionTolerance || NMath::Abs( Inverse.m[2][3] ) > RigidMotionTolerance || NMath::Abs( Inverse.m[3][3] - 1.0f ) > RigidMotionTolerance )
		{
			return false;
		}
		Inverse.m[0][3] = 0.0f;
		Inverse.m[1][3] = 0.0f;
		Inverse.m[2][3] = 0.0f;
		Inverse.m[3][3] = 1.0f;
		if ( !IsFiniteStrictAffine( Inverse ) )
		{
			return false;
		}
		OutInverse = Inverse;
		return true;
	}

	// float行列逆で失われる精度を避けるため、正規化済み3x3 basisの逆をdoubleで求める。
	bool TryInverseLinearDouble( const FMatrix4x4& Basis, float64 OutInverse[3][3] )
	{
		float64 MaxComponent = 0.0; // 正規化に使う3x3最大成分。
		for ( int32 Row = 0; Row < 3; ++Row ) // 3x3の行番号。
		{
			for ( int32 Column = 0; Column < 3; ++Column ) // 3x3の列番号。
			{
				MaxComponent = NMath::Max( MaxComponent, static_cast<float64>( NMath::Abs( Basis.m[Row][Column] ) ) );
			}
		}
		if ( !NMath::IsFinite( MaxComponent ) || MaxComponent <= 0.0 )
		{
			return false;
		}
		float64 Normalized[3][3] = {}; // 値域を抑えたbasisの3x3成分。
		for ( int32 Row = 0; Row < 3; ++Row ) // 正規化する行番号。
		{
			for ( int32 Column = 0; Column < 3; ++Column ) // 正規化する列番号。
			{
				Normalized[Row][Column] = static_cast<float64>( Basis.m[Row][Column] ) / MaxComponent;
			}
		}
		const float64 Determinant = Normalized[0][0] * ( Normalized[1][1] * Normalized[2][2] - Normalized[1][2] * Normalized[2][1] ) - Normalized[0][1] * ( Normalized[1][0] * Normalized[2][2] - Normalized[1][2] * Normalized[2][0] ) + Normalized[0][2] * ( Normalized[1][0] * Normalized[2][1] - Normalized[1][1] * Normalized[2][0] ); // 正規化行列の行列式。
		if ( !NMath::IsFinite( Determinant ) || Determinant <= static_cast<float64>( NMath::SmallNumber ) )
		{
			return false;
		}
		const float64 InverseDeterminant = 1.0 / Determinant; // 余因子を逆行列へ変換する係数。
		const float64 InverseMaxComponent = 1.0 / MaxComponent; // 元basisの共通倍率を戻す係数。
		float64 InverseNormalized[3][3] = {}; // 正規化basisの余因子行列。
		InverseNormalized[0][0] = ( Normalized[1][1] * Normalized[2][2] - Normalized[1][2] * Normalized[2][1] ) * InverseDeterminant;
		InverseNormalized[0][1] = ( Normalized[0][2] * Normalized[2][1] - Normalized[0][1] * Normalized[2][2] ) * InverseDeterminant;
		InverseNormalized[0][2] = ( Normalized[0][1] * Normalized[1][2] - Normalized[0][2] * Normalized[1][1] ) * InverseDeterminant;
		InverseNormalized[1][0] = ( Normalized[1][2] * Normalized[2][0] - Normalized[1][0] * Normalized[2][2] ) * InverseDeterminant;
		InverseNormalized[1][1] = ( Normalized[0][0] * Normalized[2][2] - Normalized[0][2] * Normalized[2][0] ) * InverseDeterminant;
		InverseNormalized[1][2] = ( Normalized[0][2] * Normalized[1][0] - Normalized[0][0] * Normalized[1][2] ) * InverseDeterminant;
		InverseNormalized[2][0] = ( Normalized[1][0] * Normalized[2][1] - Normalized[1][1] * Normalized[2][0] ) * InverseDeterminant;
		InverseNormalized[2][1] = ( Normalized[0][1] * Normalized[2][0] - Normalized[0][0] * Normalized[2][1] ) * InverseDeterminant;
		InverseNormalized[2][2] = ( Normalized[0][0] * Normalized[1][1] - Normalized[0][1] * Normalized[1][0] ) * InverseDeterminant;
		for ( int32 Row = 0; Row < 3; ++Row ) // 元basis逆行列の行番号。
		{
			for ( int32 Column = 0; Column < 3; ++Column ) // 元basis逆行列の列番号。
			{
				OutInverse[Row][Column] = InverseNormalized[Row][Column] * InverseMaxComponent;
				if ( !NMath::IsFinite( OutInverse[Row][Column] ) )
				{
					return false;
				}
			}
		}
		return true;
	}

	// finite affine行列のlinear部が、右手系の単位直交基底かを調べる。
	bool IsProperRigidAffine( const FMatrix4x4& Matrix )
	{
		if ( !IsFiniteStrictAffine( Matrix ) )
		{
			return false;
		}
		const FVector3 AxisX = Matrix.GetAxisX(); // 判定対象のX基底。
		const FVector3 AxisY = Matrix.GetAxisY(); // 判定対象のY基底。
		const FVector3 AxisZ = Matrix.GetAxisZ(); // 判定対象のZ基底。
		const float32 LengthXSquared = AxisX.LengthSquared(); // X軸長の二乗。
		const float32 LengthYSquared = AxisY.LengthSquared(); // Y軸長の二乗。
		const float32 LengthZSquared = AxisZ.LengthSquared(); // Z軸長の二乗。
		const float32 DotXY = FVector3::Dot( AxisX, AxisY ); // XとYの直交性。
		const float32 DotXZ = FVector3::Dot( AxisX, AxisZ ); // XとZの直交性。
		const float32 DotYZ = FVector3::Dot( AxisY, AxisZ ); // YとZの直交性。
		const float32 Determinant = FVector3::Dot( FVector3::Cross( AxisX, AxisY ), AxisZ ); // 反転の有無と正規直交性。
		return NMath::IsFinite( LengthXSquared ) && NMath::IsFinite( LengthYSquared ) && NMath::IsFinite( LengthZSquared ) && NMath::IsFinite( DotXY ) && NMath::IsFinite( DotXZ ) && NMath::IsFinite( DotYZ ) && NMath::IsFinite( Determinant ) && NMath::Abs( LengthXSquared - 1.0f ) <= RigidMotionTolerance && NMath::Abs( LengthYSquared - 1.0f ) <= RigidMotionTolerance && NMath::Abs( LengthZSquared - 1.0f ) <= RigidMotionTolerance && NMath::Abs( DotXY ) <= RigidMotionTolerance && NMath::Abs( DotXZ ) <= RigidMotionTolerance && NMath::Abs( DotYZ ) <= RigidMotionTolerance && Determinant > 0.0f && NMath::Abs( Determinant - 1.0f ) <= 4.0f * RigidMotionTolerance;
	}

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

bool NRootMotion::TryExtractRigidMotion( const FMatrix4x4& Delta, FVector3& OutTranslation, FQuaternion& OutRotation )
{
	FMatrix4x4 AffineDelta; // m33の積丸めだけを正規化した入力差分。
	if ( !TryCanonicalizeAffineInput( Delta, AffineDelta ) || !IsProperRigidAffine( AffineDelta ) )
	{
		return false;
	}
	const FVector3 Translation = AffineDelta.GetTranslation(); // 成功後に返す平行移動。
	const FQuaternion Rotation = AffineDelta.GetRotation().Normalized(); // 検査済み正規直交軸から得る単位回転。
	if ( !NMath::IsFinite( Translation.x ) || !NMath::IsFinite( Translation.y ) || !NMath::IsFinite( Translation.z ) || !NMath::IsFinite( Rotation.x ) || !NMath::IsFinite( Rotation.y ) || !NMath::IsFinite( Rotation.z ) || !NMath::IsFinite( Rotation.w ) )
	{
		return false;
	}
	OutTranslation = Translation;
	OutRotation = Rotation;
	return true;
}

bool NRootMotion::TryConvertToActorMotion( const FMatrix4x4& SourceDelta, const FMatrix4x4& ReferenceToActorBasis, FVector3& OutTranslation, FQuaternion& OutRotation )
{
	FMatrix4x4 AffineDelta; // m33の積丸めだけを正規化したsource差分。
	FMatrix4x4 AffineBasis; // m33の積丸めだけを正規化した完全な基準行列。
	FMatrix4x4 InverseBasisValidation; // 既存TryInverseで相対行列式下限を確認する一時値。
	if ( !TryCanonicalizeAffineInput( SourceDelta, AffineDelta ) || !TryCanonicalizeAffineInput( ReferenceToActorBasis, AffineBasis ) || !TryInverseStrictAffineBasis( AffineBasis, InverseBasisValidation ) )
	{
		return false;
	}
	float64 InverseLinear[3][3] = {}; // basisの線形部分をdoubleで逆にした値。
	if ( !TryInverseLinearDouble( AffineBasis, InverseLinear ) )
	{
		return false;
	}
	float64 BasisLinear[3][3] = {}; // basisの線形部分をdoubleへ移した値。
	float64 SourceLinear[3][3] = {}; // source差分の線形部分をdoubleへ移した値。
	for ( int32 Row = 0; Row < 3; ++Row ) // 3x3入力を変換する行番号。
	{
		for ( int32 Column = 0; Column < 3; ++Column ) // 3x3入力を変換する列番号。
		{
			BasisLinear[Row][Column] = static_cast<float64>( AffineBasis.m[Row][Column] );
			SourceLinear[Row][Column] = static_cast<float64>( AffineDelta.m[Row][Column] );
		}
	}
	const bool bPureTranslation = SourceLinear[0][0] == 1.0 && SourceLinear[0][1] == 0.0 && SourceLinear[0][2] == 0.0 && SourceLinear[1][0] == 0.0 && SourceLinear[1][1] == 1.0 && SourceLinear[1][2] == 0.0 && SourceLinear[2][0] == 0.0 && SourceLinear[2][1] == 0.0 && SourceLinear[2][2] == 1.0; // source差分の線形部が厳密なidentityか。
	float64 ConvertedLinear[3][3] = {}; // actor空間差分の線形部分。
	if ( bPureTranslation )
	{
		ConvertedLinear[0][0] = 1.0;
		ConvertedLinear[1][1] = 1.0;
		ConvertedLinear[2][2] = 1.0;
	}
	else
	{
		float64 InverseTimesSource[3][3] = {}; // A逆*Rの中間積。
		for ( int32 Row = 0; Row < 3; ++Row ) // 最初の積の行番号。
		{
			for ( int32 Column = 0; Column < 3; ++Column ) // 最初の積の列番号。
			{
				for ( int32 Inner = 0; Inner < 3; ++Inner ) // 最初の積の内側添字。
				{
					InverseTimesSource[Row][Column] += InverseLinear[Row][Inner] * SourceLinear[Inner][Column];
				}
			}
		}
		for ( int32 Row = 0; Row < 3; ++Row ) // Clinearの行番号。
		{
			for ( int32 Column = 0; Column < 3; ++Column ) // Clinearの列番号。
			{
				for ( int32 Inner = 0; Inner < 3; ++Inner ) // Clinearの内側添字。
				{
					ConvertedLinear[Row][Column] += InverseTimesSource[Row][Inner] * BasisLinear[Inner][Column];
				}
			}
		}
	}
	float64 ConvertedTranslation[3] = {}; // actor空間差分の平行移動。
	for ( int32 Column = 0; Column < 3; ++Column ) // translationの出力軸。
	{
		for ( int32 Inner = 0; Inner < 3; ++Inner ) // source移動をbasisへ写す内側添字。
		{
			ConvertedTranslation[Column] += static_cast<float64>( AffineDelta.m[3][Inner] ) * BasisLinear[Inner][Column];
		}
		if ( !bPureTranslation )
		{
			for ( int32 Inner = 0; Inner < 3; ++Inner ) // basis offsetに線形差分を適用する内側添字。
			{
				const float64 IdentityMinusLinear = ( Inner == Column ? 1.0 : 0.0 ) - ConvertedLinear[Inner][Column]; // I-Clinearの要素。
				ConvertedTranslation[Column] += static_cast<float64>( AffineBasis.m[3][Inner] ) * IdentityMinusLinear;
			}
		}
	}
	FMatrix4x4 Converted; // double計算結果を格納する最終affine差分。
	for ( int32 Row = 0; Row < 3; ++Row ) // 最終線形部の行番号。
	{
		for ( int32 Column = 0; Column < 3; ++Column ) // 最終線形部の列番号。
		{
			if ( !NMath::IsFinite( ConvertedLinear[Row][Column] ) || NMath::Abs( ConvertedLinear[Row][Column] ) > static_cast<float64>( TNumericLimits<float32>::Max() ) )
			{
				return false;
			}
			Converted.m[Row][Column] = static_cast<float32>( ConvertedLinear[Row][Column] );
		}
	}
	for ( int32 Axis = 0; Axis < 3; ++Axis ) // 最終translationの軸番号。
	{
		if ( !NMath::IsFinite( ConvertedTranslation[Axis] ) || NMath::Abs( ConvertedTranslation[Axis] ) > static_cast<float64>( TNumericLimits<float32>::Max() ) )
		{
			return false;
		}
		Converted.m[3][Axis] = static_cast<float32>( ConvertedTranslation[Axis] );
	}
	if ( !IsProperRigidAffine( Converted ) )
	{
		return false;
	}
	const FVector3 Translation = Converted.GetTranslation(); // 成功後に返すactor空間の移動。
	const FQuaternion Rotation = Converted.GetRotation().Normalized(); // 成功後に返すactor空間の単位回転。
	if ( !NMath::IsFinite( Translation.x ) || !NMath::IsFinite( Translation.y ) || !NMath::IsFinite( Translation.z ) || !NMath::IsFinite( Rotation.x ) || !NMath::IsFinite( Rotation.y ) || !NMath::IsFinite( Rotation.z ) || !NMath::IsFinite( Rotation.w ) )
	{
		return false;
	}
	OutTranslation = Translation;
	OutRotation = Rotation;
	return true;
}
