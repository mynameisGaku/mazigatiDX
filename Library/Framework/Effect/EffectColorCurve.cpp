// SPDX-License-Identifier: NOASSERTION
#include "EffectColorCurve.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Json/JsonHelpers.h"

namespace
{
	// JSON の数値を丸めず float32 に読む。失敗時は Validate が検出する NaN を残す。
	bool ReadFloat( const NlohmannJson& Json, float32& Out )
	{
		// 読めない値が検証で見つかるよう、失敗値を NaN にする。
		Out = TNumericLimits<float32>::QuietNaN();
		if ( !Json.is_number() ) return false;
		try
		{
			Out = Json.get<float32>();
			return true;
		}
		catch ( const nlohmann::json::exception& )
		{
			return false;
		}
	}

	// 補間方法を JSON の保存名へ変換する。不正な値は Validate 用に残す。
	const char* InterpolationName( FEffectScalarCurve::EInterpolation Value )
	{
		switch ( Value )
		{
		case FEffectScalarCurve::EInterpolation::Linear: return "Linear";
		case FEffectScalarCurve::EInterpolation::Step: return "Step";
		case FEffectScalarCurve::EInterpolation::Smooth: return "Smooth";
		}
		return "Invalid";
	}

	// 指定成分が有限なら値を 0..1 に収め、壊れた値は 0 にする。
	float32 SafeChannel( float32 Value ) { return NMath::IsFinite( Value ) ? NMath::Clamp( Value, 0.0f, 1.0f ) : 0.0f; }

	// キーをそのまま返す場合も RGBA 各成分を安全な範囲へ収める。
	FColor SafeColor( const FColor& Value ) { return FColor( SafeChannel( Value.r ), SafeChannel( Value.g ), SafeChannel( Value.b ), SafeChannel( Value.a ) ); }

	// FNV-1a へ値のバイトだけを加える。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		// 型の余白を含めない、今回の値のバイト列。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Value );
		for ( usize Index = 0; Index < sizeof( T ); ++Index )
		{
			Hash ^= Bytes[Index];
			Hash *= 1099511628211ull;
		}
	}

	// JSON の四成分配列を読む。形が違う場合は全成分を無効値にする。
	bool ReadColor( const NlohmannJson& Json, FColor& Out )
	{
		// 成分が欠けた色を Validate で拒否できるよう先に NaN を入れる。
		const float32 Invalid = TNumericLimits<float32>::QuietNaN();
		Out = FColor( Invalid, Invalid, Invalid, Invalid );
		if ( !Json.is_array() || Json.size() != 4 ) return false;
		// RGBA の読み取り結果。
		float32 Channels[4] = { Invalid, Invalid, Invalid, Invalid };
		for ( int32 Index = 0; Index < 4; ++Index ) if ( !ReadFloat( Json[static_cast<size_t>( Index )], Channels[Index] ) ) return false;
		Out = FColor( Channels[0], Channels[1], Channels[2], Channels[3] );
		return true;
	}
}

FColor FEffectColorCurve::Evaluate( float32 Time, const FColor& DefaultValue ) const
{
	// 呼び出し元の既定色にも非有限値を通さない。
	const FColor SafeDefault( SafeChannel( DefaultValue.r ), SafeChannel( DefaultValue.g ), SafeChannel( DefaultValue.b ), SafeChannel( DefaultValue.a ) );
	if ( !NMath::IsFinite( Time ) || Keys.IsEmpty() ) return SafeDefault;
	// 壊れた直接入力でも計算量と参照範囲を固定する。
	const int32 Count = NMath::Min( Keys.Num(), MaxKeys );
	// 有効キーの最小時刻。
	int32 FirstIndex = INDEX_NONE;
	// 有効キーの最大時刻。
	int32 LastIndex = INDEX_NONE;
	// 評価時刻以下で最も近いキー。
	int32 LowerIndex = INDEX_NONE;
	// 評価時刻以上で最も近いキー。
	int32 UpperIndex = INDEX_NONE;
	// 重複時刻は配列の先頭を採用し、順序不正でも範囲外を参照しない。
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		// 今回調べるキー。
		const FKey& Key = Keys[Index];
		if ( !NMath::IsFinite( Key.Time ) || !NMath::IsFinite( Key.Value.r ) || !NMath::IsFinite( Key.Value.g ) || !NMath::IsFinite( Key.Value.b ) || !NMath::IsFinite( Key.Value.a ) ) continue;
		if ( FirstIndex == INDEX_NONE || Key.Time < Keys[FirstIndex].Time || ( Key.Time == Keys[FirstIndex].Time && Index < FirstIndex ) ) FirstIndex = Index;
		if ( LastIndex == INDEX_NONE || Key.Time > Keys[LastIndex].Time || ( Key.Time == Keys[LastIndex].Time && Index < LastIndex ) ) LastIndex = Index;
		if ( Key.Time <= Time && ( LowerIndex == INDEX_NONE || Key.Time > Keys[LowerIndex].Time || ( Key.Time == Keys[LowerIndex].Time && Index < LowerIndex ) ) ) LowerIndex = Index;
		if ( Key.Time >= Time && ( UpperIndex == INDEX_NONE || Key.Time < Keys[UpperIndex].Time || ( Key.Time == Keys[UpperIndex].Time && Index < UpperIndex ) ) ) UpperIndex = Index;
	}
	if ( FirstIndex == INDEX_NONE ) return SafeDefault;
	if ( LowerIndex == INDEX_NONE ) return SafeColor( Keys[FirstIndex].Value );
	if ( UpperIndex == INDEX_NONE ) return SafeColor( Keys[LastIndex].Value );
	// 評価時刻の左にあるキー。
	const FKey& Lower = Keys[LowerIndex];
	// 評価時刻の右にあるキー。
	const FKey& Upper = Keys[UpperIndex];
	if ( LowerIndex == UpperIndex || Upper.Time <= Lower.Time ) return SafeColor( Lower.Value );
	if ( Interpolation == FEffectScalarCurve::EInterpolation::Step ) return SafeColor( Lower.Value );
	// float32 の極端な有限値同士でも差があふれないよう double で補間する。
	// キー間の割合。
	double Alpha = NMath::Clamp( ( static_cast<double>( Time ) - Lower.Time ) / ( static_cast<double>( Upper.Time ) - Lower.Time ), 0.0, 1.0 );
	if ( Interpolation == FEffectScalarCurve::EInterpolation::Smooth ) Alpha = Alpha * Alpha * ( 3.0 - 2.0 * Alpha );
	// 赤成分の補間値。
	const double R = static_cast<double>( Lower.Value.r ) + ( static_cast<double>( Upper.Value.r ) - Lower.Value.r ) * Alpha;
	// 緑成分の補間値。
	const double G = static_cast<double>( Lower.Value.g ) + ( static_cast<double>( Upper.Value.g ) - Lower.Value.g ) * Alpha;
	// 青成分の補間値。
	const double B = static_cast<double>( Lower.Value.b ) + ( static_cast<double>( Upper.Value.b ) - Lower.Value.b ) * Alpha;
	// アルファ成分の補間値。
	const double A = static_cast<double>( Lower.Value.a ) + ( static_cast<double>( Upper.Value.a ) - Lower.Value.a ) * Alpha;
	if ( !NMath::IsFinite( R ) || !NMath::IsFinite( G ) || !NMath::IsFinite( B ) || !NMath::IsFinite( A ) ) return SafeDefault;
	return FColor( SafeChannel( static_cast<float32>( R ) ), SafeChannel( static_cast<float32>( G ) ), SafeChannel( static_cast<float32>( B ) ), SafeChannel( static_cast<float32>( A ) ) );
}

NlohmannJson FEffectColorCurve::ToJson() const
{
	// 曲線の保存先。
	NlohmannJson Json = NlohmannJson::object();
	Json["Interpolation"] = InterpolationName( Interpolation );
	Json["Keys"] = NlohmannJson::array();
	for ( const FKey& Key : Keys )
	{
		// 一つの時刻と RGBA 値。
		NlohmannJson Item = NlohmannJson::object();
		Item["Time"] = Key.Time;
		Item["Value"] = NlohmannJson::array( { Key.Value.r, Key.Value.g, Key.Value.b, Key.Value.a } );
		Json["Keys"].push_back( Item );
	}
	return Json;
}

bool FEffectColorCurve::FromJson( const NlohmannJson& Json, FEffectColorCurve& Out, TArray<FString>* Warnings )
{
	if ( !Json.is_object() ) return false;
	// 入力を読んで組み立てる曲線。
	FEffectColorCurve Curve;
	if ( Json.contains( "Interpolation" ) )
	{
		// JSON の補間名。
		const FString Text = NJson::GetOr<FString>( Json, "Interpolation", FString() );
		if ( Text.Equals( "Step", ESearchCase::IgnoreCase ) ) Curve.Interpolation = FEffectScalarCurve::EInterpolation::Step;
		else if ( Text.Equals( "Smooth", ESearchCase::IgnoreCase ) ) Curve.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		else if ( !Text.Equals( "Linear", ESearchCase::IgnoreCase ) )
		{
			Curve.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
			if ( Warnings != nullptr ) Warnings->Add( "Interpolation が不明です。Validate で修正してください" );
		}
	}
	if ( Json.contains( "Keys" ) )
	{
		if ( !Json["Keys"].is_array() )
		{
			if ( Warnings != nullptr ) Warnings->Add( "Keys は配列にしてください。Validate で修正してください" );
			// 不正配列を保存前に検出させるキー。
			FKey InvalidKey;
			InvalidKey.Time = TNumericLimits<float32>::QuietNaN();
			InvalidKey.Value = FColor( InvalidKey.Time, InvalidKey.Time, InvalidKey.Time, InvalidKey.Time );
			Curve.Keys.Add( InvalidKey );
		}
		else for ( int32 Index = 0; Index < static_cast<int32>( Json["Keys"].size() ); ++Index )
		{
			// 今回読む JSON キー。
			const NlohmannJson& Item = Json["Keys"][static_cast<size_t>( Index )];
			// JSON の時刻と RGBA 値。
			FKey Key;
			if ( !Item.is_object() )
			{
				Key.Time = TNumericLimits<float32>::QuietNaN();
				Key.Value = FColor( Key.Time, Key.Time, Key.Time, Key.Time );
				if ( Warnings != nullptr ) Warnings->Add( FString::Printf( "Keys[%d] はオブジェクトにしてください", Index ) );
			}
			else
			{
				// 時刻値の有無と内容。
				const auto Time = Item.find( "Time" );
				// RGBA 値の有無と内容。
				const auto Value = Item.find( "Value" );
				// 時刻を数値で読めたか。
				const bool bTimeValid = Time != Item.end() && ReadFloat( *Time, Key.Time );
				// RGBA を4成分で読めたか。
				const bool bValueValid = Value != Item.end() && ReadColor( *Value, Key.Value );
				if ( !bTimeValid ) Key.Time = TNumericLimits<float32>::QuietNaN();
				if ( !bValueValid ) Key.Value = FColor( TNumericLimits<float32>::QuietNaN(), TNumericLimits<float32>::QuietNaN(), TNumericLimits<float32>::QuietNaN(), TNumericLimits<float32>::QuietNaN() );
				if ( ( !bTimeValid || !bValueValid ) && Warnings != nullptr ) Warnings->Add( FString::Printf( "Keys[%d] の Time / RGBA を数値で読み取れません", Index ) );
			}
			Curve.Keys.Add( Key );
		}
	}
	Out = MoveTemp( Curve );
	return true;
}

TArray<FString> FEffectColorCurve::Validate() const
{
	// 見つかった曲線の問題。
	TArray<FString> Issues;
	if ( Interpolation != FEffectScalarCurve::EInterpolation::Linear && Interpolation != FEffectScalarCurve::EInterpolation::Step && Interpolation != FEffectScalarCurve::EInterpolation::Smooth ) Issues.Add( "Interpolation が不正です" );
	if ( Keys.Num() > MaxKeys ) Issues.Add( FString::Printf( "Keys は %d 個以下にしてください", MaxKeys ) );
	// 直前の有限キーの時刻。
	float32 PreviousTime = 0.0f;
	// 比較対象の前キーがあるか。
	bool bHasPrevious = false;
	for ( int32 Index = 0; Index < Keys.Num(); ++Index )
	{
		// 検証するキー。
		const FKey& Key = Keys[Index];
		if ( !NMath::IsFinite( Key.Time ) || !NMath::IsFinite( Key.Value.r ) || !NMath::IsFinite( Key.Value.g ) || !NMath::IsFinite( Key.Value.b ) || !NMath::IsFinite( Key.Value.a ) )
		{
			Issues.Add( FString::Printf( "Keys[%d] の Time / RGBA は有限値にしてください", Index ) );
			continue;
		}
		if ( Key.Time < 0.0f || Key.Time > 1.0f ) Issues.Add( FString::Printf( "Keys[%d].Time は 0 から 1 にしてください", Index ) );
		if ( bHasPrevious && Key.Time <= PreviousTime ) Issues.Add( FString::Printf( "Keys[%d].Time は前のキーより大きくしてください", Index ) );
		if ( Key.Value.r < 0.0f || Key.Value.r > 1.0f || Key.Value.g < 0.0f || Key.Value.g > 1.0f || Key.Value.b < 0.0f || Key.Value.b > 1.0f || Key.Value.a < 0.0f || Key.Value.a > 1.0f ) Issues.Add( FString::Printf( "Keys[%d].Value の RGBA は 0 から 1 にしてください", Index ) );
		PreviousTime = Key.Time;
		bHasPrevious = true;
	}
	return Issues;
}

uint64 FEffectColorCurve::ComputeHash() const
{
	// FNV-1a の初期値。
	uint64 Hash = 1469598103934665603ull;
	MixValue( Hash, static_cast<uint8>( Interpolation ) );
	MixValue( Hash, Keys.Num() );
	for ( const FKey& Key : Keys )
	{
		MixValue( Hash, Key.Time );
		MixValue( Hash, Key.Value.r );
		MixValue( Hash, Key.Value.g );
		MixValue( Hash, Key.Value.b );
		MixValue( Hash, Key.Value.a );
	}
	return Hash;
}
