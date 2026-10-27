// SPDX-License-Identifier: NOASSERTION
#include "EffectScalarCurve.h"


#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Json/JsonHelpers.h"

namespace
{
	// 保存名から補間方法を読む。未知の名前は既定値を保つ。
	bool ParseInterpolation( const FString& Text, FEffectScalarCurve::EInterpolation& Out )
	{
		if ( Text.Equals( "Linear", ESearchCase::IgnoreCase ) ) Out = FEffectScalarCurve::EInterpolation::Linear;
		else if ( Text.Equals( "Step", ESearchCase::IgnoreCase ) ) Out = FEffectScalarCurve::EInterpolation::Step;
		else if ( Text.Equals( "Smooth", ESearchCase::IgnoreCase ) ) Out = FEffectScalarCurve::EInterpolation::Smooth;
		else return false;
		return true;
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

	// 数値を丸めず float32 に読む。失敗時は Validate が検出する NaN を残す。
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
}

float32 FEffectScalarCurve::Evaluate( float32 Time, float32 DefaultValue ) const
{
	// 返す既定値にも非有限値を通さない。
	const float32 SafeDefault = NMath::IsFinite( DefaultValue ) ? DefaultValue : 0.0f;
	if ( !NMath::IsFinite( Time ) || Keys.IsEmpty() ) return SafeDefault;
	// 壊れた直接入力でも計算量と参照範囲を固定する。
	const int32 Count = NMath::Min( Keys.Num(), MaxKeys );
	int32 FirstIndex = INDEX_NONE; // 有限なキーのうち最小時刻を持つ位置。
	int32 LastIndex = INDEX_NONE; // 有限なキーのうち最大時刻を持つ位置。
	int32 LowerIndex = INDEX_NONE; // 評価時刻以下で最も近いキーの位置。
	int32 UpperIndex = INDEX_NONE; // 評価時刻以上で最も近いキーの位置。
	// 時刻が同じキーは配列の先頭を採用し、順序不正でも範囲外を参照しない。
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		const FKey& Key = Keys[Index]; // 今回調べる有限値候補。
		if ( !NMath::IsFinite( Key.Time ) || !NMath::IsFinite( Key.Value ) ) continue;
		if ( FirstIndex == INDEX_NONE || Key.Time < Keys[FirstIndex].Time || ( Key.Time == Keys[FirstIndex].Time && Index < FirstIndex ) ) FirstIndex = Index;
		if ( LastIndex == INDEX_NONE || Key.Time > Keys[LastIndex].Time || ( Key.Time == Keys[LastIndex].Time && Index < LastIndex ) ) LastIndex = Index;
		if ( Key.Time <= Time && ( LowerIndex == INDEX_NONE || Key.Time > Keys[LowerIndex].Time || ( Key.Time == Keys[LowerIndex].Time && Index < LowerIndex ) ) ) LowerIndex = Index;
		if ( Key.Time >= Time && ( UpperIndex == INDEX_NONE || Key.Time < Keys[UpperIndex].Time || ( Key.Time == Keys[UpperIndex].Time && Index < UpperIndex ) ) ) UpperIndex = Index;
	}
	if ( FirstIndex == INDEX_NONE ) return SafeDefault;
	if ( LowerIndex == INDEX_NONE ) return Keys[FirstIndex].Value;
	if ( UpperIndex == INDEX_NONE ) return Keys[LastIndex].Value;
	const FKey& Lower = Keys[LowerIndex]; // 評価時刻の左にあるキー。
	const FKey& Upper = Keys[UpperIndex]; // 評価時刻の右にあるキー。
	if ( LowerIndex == UpperIndex || Upper.Time <= Lower.Time ) return Lower.Value;
	if ( Interpolation == EInterpolation::Step ) return Lower.Value;
	// double で差を計算し、有限な float32 端点の補間中も桁あふれを避ける。
	double Alpha = ( static_cast<double>( Time ) - Lower.Time ) / ( static_cast<double>( Upper.Time ) - Lower.Time ); // 二つのキーの間での割合。
	Alpha = NMath::Clamp( Alpha, 0.0, 1.0 );
	if ( Interpolation == EInterpolation::Smooth ) Alpha = Alpha * Alpha * ( 3.0 - 2.0 * Alpha );
	const double Value = static_cast<double>( Lower.Value ) + ( static_cast<double>( Upper.Value ) - Lower.Value ) * Alpha; // 補間後の有限な値。
	return NMath::IsFinite( Value ) ? static_cast<float32>( Value ) : SafeDefault;
}

NlohmannJson FEffectScalarCurve::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object(); // 曲線の保存先。
	Json["Interpolation"] = InterpolationName( Interpolation );
	Json["Keys"] = NlohmannJson::array();
	for ( const FKey& Key : Keys )
	{
		NlohmannJson Item = NlohmannJson::object(); // 一つの時刻と値。
		Item["Time"] = Key.Time;
		Item["Value"] = Key.Value;
		Json["Keys"].push_back( Item );
	}
	return Json;
}

bool FEffectScalarCurve::FromJson( const NlohmannJson& Json, FEffectScalarCurve& Out, TArray<FString>* Warnings )
{
	if ( !Json.is_object() ) return false;
	FEffectScalarCurve Curve; // 入力を読んで組み立てる曲線。
	if ( Json.contains( "Interpolation" ) )
	{
		const FString Text = NJson::GetOr<FString>( Json, "Interpolation", FString() ); // JSON の補間名。
		if ( !ParseInterpolation( Text, Curve.Interpolation ) )
		{
			Curve.Interpolation = static_cast<EInterpolation>( 0xFFu );
			if ( Warnings != nullptr ) Warnings->Add( "Interpolation が不明です。Validate で修正してください" );
		}
	}
	if ( Json.contains( "Keys" ) )
	{
		if ( !Json["Keys"].is_array() )
		{
			if ( Warnings != nullptr ) Warnings->Add( "Keys は配列にしてください。Validate で修正してください" );
			FKey InvalidKey; // 不正な配列を Validate で検出させるキー。
			InvalidKey.Time = TNumericLimits<float32>::QuietNaN();
			InvalidKey.Value = TNumericLimits<float32>::QuietNaN();
			Curve.Keys.Add( InvalidKey );
		}
		else
		{
			for ( int32 Index = 0; Index < static_cast<int32>( Json["Keys"].size() ); ++Index )
			{
				const NlohmannJson& Item = Json["Keys"][static_cast<size_t>( Index )]; // 今回読む JSON キー。
				FKey Key; // JSON の時刻と値を保持するキー。
				if ( !Item.is_object() )
				{
					Key.Time = TNumericLimits<float32>::QuietNaN();
					Key.Value = TNumericLimits<float32>::QuietNaN();
				}
				else
				{
					const auto Time = Item.find( "Time" ); // 時刻フィールドの有無と値。
					const auto Value = Item.find( "Value" ); // 値フィールドの有無と値。
					const bool bTimeValid = Time != Item.end() ? ReadFloat( *Time, Key.Time ) : false; // 時刻を正しく読めたか。
					const bool bValueValid = Value != Item.end() ? ReadFloat( *Value, Key.Value ) : false; // 値を正しく読めたか。
					if ( Time == Item.end() ) Key.Time = TNumericLimits<float32>::QuietNaN();
					if ( Value == Item.end() ) Key.Value = TNumericLimits<float32>::QuietNaN();
					if ( ( !bTimeValid || !bValueValid ) && Warnings != nullptr ) Warnings->Add( FString::Printf( "Keys[%d] の Time / Value は数値が必要です", Index ) );
				}
				if ( !Item.is_object() && Warnings != nullptr ) Warnings->Add( FString::Printf( "Keys[%d] はオブジェクトにしてください", Index ) );
				Curve.Keys.Add( Key );
			}
		}
	}
	Out = MoveTemp( Curve );
	return true;
}

TArray<FString> FEffectScalarCurve::Validate() const
{
	TArray<FString> Issues; // 見つかった曲線の問題。
	if ( Interpolation != EInterpolation::Linear && Interpolation != EInterpolation::Step && Interpolation != EInterpolation::Smooth ) Issues.Add( "Interpolation が不正です" );
	if ( Keys.Num() > MaxKeys ) Issues.Add( FString::Printf( "Keys は %d 個以下にしてください", MaxKeys ) );
	float32 PreviousTime = 0.0f; // 直前の有限キーの時刻。
	bool bHasPrevious = false; // 比較できる前のキーがあるか。
	for ( int32 Index = 0; Index < Keys.Num(); ++Index )
	{
		const FKey& Key = Keys[Index]; // 検証するキー。
		if ( !NMath::IsFinite( Key.Time ) || !NMath::IsFinite( Key.Value ) )
		{
			Issues.Add( FString::Printf( "Keys[%d] の Time / Value は有限値にしてください", Index ) );
			continue;
		}
		if ( Key.Time < 0.0f || Key.Time > 1.0f ) Issues.Add( FString::Printf( "Keys[%d].Time は 0 から 1 にしてください", Index ) );
		if ( bHasPrevious && Key.Time <= PreviousTime ) Issues.Add( FString::Printf( "Keys[%d].Time は前のキーより大きくしてください", Index ) );
		PreviousTime = Key.Time;
		bHasPrevious = true;
	}
	return Issues;
}

uint64 FEffectScalarCurve::ComputeHash() const
{
	uint64 Hash = 1469598103934665603ull; // FNV-1a の初期値。
	MixValue( Hash, static_cast<uint8>( Interpolation ) );
	MixValue( Hash, Keys.Num() );
	for ( const FKey& Key : Keys )
	{
		MixValue( Hash, Key.Time );
		MixValue( Hash, Key.Value );
	}
	return Hash;
}
