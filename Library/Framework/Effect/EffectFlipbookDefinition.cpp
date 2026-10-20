// SPDX-License-Identifier: NOASSERTION
#include "EffectFlipbookDefinition.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "Library/Json/JsonHelpers.h"

namespace
{
	// 読み込み時に不正だった項目を個別に記録するビット。
	constexpr uint8 InvalidEnabled = 1u << 0;
	// 読み込み時に不正だったシート名を示すビット。
	constexpr uint8 InvalidSheet = 1u << 1;
	// 読み込み時に不正だったクリップ名を示すビット。
	constexpr uint8 InvalidClip = 1u << 2;
	// 読み込み時に不正だった時計種別を示すビット。
	constexpr uint8 InvalidTimeMode = 1u << 3;
	// 読み込み時に不正だった速度を示すビット。
	constexpr uint8 InvalidSpeed = 1u << 4;
	// 全項目の型が不明な JSON から作られた状態。
	constexpr uint8 InvalidAll = InvalidEnabled | InvalidSheet | InvalidClip | InvalidTimeMode | InvalidSpeed;
	// 定義内の未知時計種別を、往復可能な値として表す番号。
	constexpr uint8 InvalidModeValue = 0xFFu;

	// FNV-1a へ値のバイト列を追加する。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		// 値のバイト列を読む位置。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Value );
		for ( usize Index = 0; Index < sizeof( T ); ++Index )
		{
			Hash ^= Bytes[Index];
			Hash *= 1099511628211ull;
		}
	}

	// FString と次の値の境界を区別して hash へ混ぜる。
	void MixString( uint64& Hash, const FString& Text )
	{
		// 文字列中の各文字を固定幅で混ぜる。
		for ( int32 Index = 0; Index < Text.Len(); ++Index ) MixValue( Hash, static_cast<uint32>( Text[Index] ) );
		// 文字列終端の区切り値。
		MixValue( Hash, 0xFFFFFFFFu );
	}

	// float32 を数値変換で丸めず JSON から読む。
	bool ReadFloat( const NlohmannJson& Json, float32& Out )
	{
		// 読めない入力を既定値に見せない番兵値。
		Out = std::numeric_limits<float32>::quiet_NaN();
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

	// 任意の JSON 数値入力について読み込み理由を通知する。
	void Warn( TArray<FString>* Warnings, const FString& Message )
	{
		if ( Warnings != nullptr ) Warnings->Add( Message );
	}
}

bool FEffectFlipbookDefinition::IsDefault() const
{
	return InvalidFieldTypes == 0 && !bEnabled && Sheet.IsEmpty() && Clip.IsEmpty() && TimeMode == ETimeMode::Lifetime && Speed == 1.0f;
}

bool FEffectFlipbookDefinition::IsValidSettings() const
{
	return InvalidFieldTypes == 0 && ( TimeMode == ETimeMode::Lifetime || TimeMode == ETimeMode::Seconds ) && std::isfinite( Speed ) && Speed >= 0.0f && Speed <= 128.0f && ( !bEnabled || !Sheet.IsEmpty() );
}

NlohmannJson FEffectFlipbookDefinition::ToJson() const
{
	// 不正状態も書き戻して保存時の検証へ残す JSON。
	NlohmannJson Json = NlohmannJson::object();
	Json["Enabled"] = ( InvalidFieldTypes & InvalidEnabled ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( bEnabled );
	Json["Sheet"] = ( InvalidFieldTypes & InvalidSheet ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( Sheet );
	Json["Clip"] = ( InvalidFieldTypes & InvalidClip ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( Clip );
	if ( ( InvalidFieldTypes & InvalidTimeMode ) != 0 ) Json["TimeMode"] = nullptr;
	else if ( TimeMode == ETimeMode::Lifetime ) Json["TimeMode"] = "Lifetime";
	else if ( TimeMode == ETimeMode::Seconds ) Json["TimeMode"] = "Seconds";
	else Json["TimeMode"] = "Invalid";
	Json["Speed"] = ( InvalidFieldTypes & InvalidSpeed ) != 0 || !std::isfinite( Speed ) ? NlohmannJson( nullptr ) : NlohmannJson( Speed );
	return Json;
}

bool FEffectFlipbookDefinition::FromJson( const NlohmannJson& Json, FEffectFlipbookDefinition& Out, TArray<FString>* Warnings )
{
	// 非 object も検証可能な不正定義として保持する。
	FEffectFlipbookDefinition Definition;
	if ( !Json.is_object() )
	{
		Definition.InvalidFieldTypes = InvalidAll;
		Warn( Warnings, "Flipbook は object にしてください" );
		Out = std::move( Definition );
		return true;
	}
	if ( Json.contains( "Enabled" ) )
	{
		if ( Json["Enabled"].is_boolean() ) Definition.bEnabled = Json["Enabled"].get<bool>();
		else { Definition.InvalidFieldTypes |= InvalidEnabled; Warn( Warnings, "Enabled は true / false にしてください" ); }
	}
	if ( Json.contains( "Sheet" ) )
	{
		if ( Json["Sheet"].is_string() ) Definition.Sheet = NJson::GetOr<FString>( Json, "Sheet", FString() );
		else { Definition.InvalidFieldTypes |= InvalidSheet; Warn( Warnings, "Sheet は文字列にしてください" ); }
	}
	if ( Json.contains( "Clip" ) )
	{
		if ( Json["Clip"].is_string() ) Definition.Clip = NJson::GetOr<FString>( Json, "Clip", FString() );
		else { Definition.InvalidFieldTypes |= InvalidClip; Warn( Warnings, "Clip は文字列にしてください" ); }
	}
	if ( Json.contains( "TimeMode" ) )
	{
		if ( !Json["TimeMode"].is_string() )
		{
			Definition.InvalidFieldTypes |= InvalidTimeMode;
			Definition.TimeMode = static_cast<ETimeMode>( InvalidModeValue );
			Warn( Warnings, "TimeMode は Lifetime または Seconds にしてください" );
		}
		else
		{
			// 入力文字列で指定された再生時計。
			const FString Mode = NJson::GetOr<FString>( Json, "TimeMode", FString() );
			if ( Mode == "Lifetime" ) Definition.TimeMode = ETimeMode::Lifetime;
			else if ( Mode == "Seconds" ) Definition.TimeMode = ETimeMode::Seconds;
			else { Definition.TimeMode = static_cast<ETimeMode>( InvalidModeValue ); Warn( Warnings, "TimeMode は Lifetime または Seconds にしてください" ); }
		}
	}
	if ( Json.contains( "Speed" ) && !ReadFloat( Json["Speed"], Definition.Speed ) )
	{
		Definition.InvalidFieldTypes |= InvalidSpeed;
		Warn( Warnings, "Speed は有限の数値にしてください" );
	}
	Out = std::move( Definition );
	return true;
}

TArray<FString> FEffectFlipbookDefinition::Validate() const
{
	// 保存できない状態と範囲外値の一覧。
	TArray<FString> Issues;
	if ( ( InvalidFieldTypes & InvalidEnabled ) != 0 ) Issues.Add( "Enabled の値型が不正です" );
	if ( ( InvalidFieldTypes & InvalidSheet ) != 0 ) Issues.Add( "Sheet の値型が不正です" );
	if ( ( InvalidFieldTypes & InvalidClip ) != 0 ) Issues.Add( "Clip の値型が不正です" );
	if ( ( InvalidFieldTypes & InvalidTimeMode ) != 0 || ( TimeMode != ETimeMode::Lifetime && TimeMode != ETimeMode::Seconds ) ) Issues.Add( "TimeMode は Lifetime または Seconds にしてください" );
	if ( ( InvalidFieldTypes & InvalidSpeed ) != 0 || !std::isfinite( Speed ) || Speed < 0.0f || Speed > 128.0f ) Issues.Add( "Speed は有限の 0 から 128 にしてください" );
	if ( bEnabled && Sheet.IsEmpty() && ( InvalidFieldTypes & InvalidSheet ) == 0 ) Issues.Add( "有効な Flipbook には Sheet が必要です" );
	return Issues;
}

uint64 FEffectFlipbookDefinition::ComputeHash() const
{
	// 定義の各値と不正状態を混ぜる識別値。
	uint64 Hash = 1469598103934665603ull;
	MixValue( Hash, bEnabled );
	MixString( Hash, Sheet );
	MixString( Hash, Clip );
	MixValue( Hash, static_cast<uint8>( TimeMode ) );
	MixValue( Hash, Speed );
	MixValue( Hash, InvalidFieldTypes );
	return Hash;
}

bool NEffectFlipbook::IsValidTimeline( const FAnimTimeline& Timeline )
{
	// Timelineは最大256コマのclipからのPingPong展開までを受け付ける。
	// 展開後の表示step数。
	const int32 Count = Timeline.NumSteps();
	if ( Count <= 0 || Count > 510 || Timeline.StepEnd.Num() != Count || !std::isfinite( Timeline.Total ) || Timeline.Total <= 0.0f ) return false;
	if ( Timeline.Loop != EAnimLoopMode::Once && Timeline.Loop != EAnimLoopMode::Loop && Timeline.Loop != EAnimLoopMode::PingPong ) return false;
	// 各累積時刻と元clipのコマ参照が正しい。
	// 直前stepの終わり時刻。
	float32 PreviousEnd = 0.0f;
	// stepごとに時間と元コマ番号を確認する。
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		// 現在stepの累積終端時刻。
		const float32 End = Timeline.StepEnd[Index];
		// timelineが参照する元clipのコマ。
		const int32 Source = Timeline.StepSource[Index];
		if ( !std::isfinite( End ) || End <= PreviousEnd || End > Timeline.Total || Source < 0 || Source >= 256 ) return false;
		PreviousEnd = End;
	}
	return Timeline.StepEnd.Last() == Timeline.Total;
}

int32 NEffectFlipbook::EvaluateStep( const FEffectFlipbookDefinition& Definition, const FAnimTimeline& Timeline, float64 AgeSeconds, float32 NormalizedAge )
{
	// 粒子ごとの呼び出しでは配列全体を再検証せず、必要な境界だけを見る。
	// timelineの表示step数。
	const int32 Count = Timeline.NumSteps();
	if ( !Definition.bEnabled || !Definition.IsValidSettings() || Count <= 0 || Count > 510 || Timeline.StepEnd.Num() != Count || !std::isfinite( Timeline.Total ) || Timeline.Total <= 0.0f ) return INDEX_NONE;
	if ( Timeline.Loop != EAnimLoopMode::Once && Timeline.Loop != EAnimLoopMode::Loop && Timeline.Loop != EAnimLoopMode::PingPong ) return INDEX_NONE;
	if ( Timeline.StepEnd.Last() != Timeline.Total ) return INDEX_NONE;
	if ( Definition.TimeMode == FEffectFlipbookDefinition::ETimeMode::Lifetime )
	{
		if ( !std::isfinite( NormalizedAge ) ) return INDEX_NONE;
		// 寿命割合を0から1へ収めた値。
		const float64 T = std::clamp( static_cast<float64>( NormalizedAge ), 0.0, 1.0 );
		if ( T >= 1.0 ) return Timeline.StepSource.Last() >= 0 && Timeline.StepSource.Last() < 256 ? Count - 1 : INDEX_NONE;
		// 寿命割合をclipの累積秒へ写した時刻。
		const float64 Time = T * static_cast<float64>( Timeline.Total );
		// 累積秒に対応するtimeline step。
		const int32 Step = NAnim::Locate( Timeline, Time ).Step;
		return Step >= 0 && Step < Count && Timeline.StepSource[Step] >= 0 && Timeline.StepSource[Step] < 256 ? Step : INDEX_NONE;
	}
	if ( Definition.TimeMode != FEffectFlipbookDefinition::ETimeMode::Seconds || !std::isfinite( AgeSeconds ) || AgeSeconds < 0.0 ) return INDEX_NONE;
	// double積のoverflowを検査してからOnce/Loop/PingPongの時間範囲へ収める。
	// 粒子の経過秒に再生速度を掛けた時刻。
	const float64 Playhead = AgeSeconds * static_cast<float64>( Definition.Speed );
	if ( !std::isfinite( Playhead ) ) return INDEX_NONE;
	// loop方式で範囲調整する再生時刻。
	float64 Time = Playhead;
	if ( Timeline.Loop == EAnimLoopMode::Once ) Time = std::clamp( Playhead, 0.0, static_cast<float64>( Timeline.Total ) );
	else
	{
		Time = std::fmod( Playhead, static_cast<float64>( Timeline.Total ) );
		if ( !std::isfinite( Time ) ) return INDEX_NONE;
		if ( Time < 0.0 ) Time += static_cast<float64>( Timeline.Total );
	}
	// 調整時刻が指すtimeline step。
	const int32 Step = NAnim::Locate( Timeline, Time ).Step;
	return Step >= 0 && Step < Count && Timeline.StepSource[Step] >= 0 && Timeline.StepSource[Step] < 256 ? Step : INDEX_NONE;
}
