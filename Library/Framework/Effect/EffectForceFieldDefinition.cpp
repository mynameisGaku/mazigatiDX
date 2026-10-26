// SPDX-License-Identifier: NOASSERTION
#include "EffectForceFieldDefinition.h"

#include <cmath>
#include <limits>
#include <utility>

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Json/JsonHelpers.h"

namespace
{
	// EnabledのJSON型違いを保持するbit。
	constexpr uint16 InvalidEnabled = 1u << 0;
	// KindのJSON型違いを保持するbit。
	constexpr uint16 InvalidKind = 1u << 1;
	// FollowEmitterのJSON型違いを保持するbit。
	constexpr uint16 InvalidFollowEmitter = 1u << 2;
	// AccelerationのJSON型違いを保持するbit。
	constexpr uint16 InvalidAcceleration = 1u << 3;
	// PositionのJSON型違いを保持するbit。
	constexpr uint16 InvalidPosition = 1u << 4;
	// AxisのJSON型違いを保持するbit。
	constexpr uint16 InvalidAxis = 1u << 5;
	// RadiusのJSON型違いを保持するbit。
	constexpr uint16 InvalidRadius = 1u << 6;
	// StrengthのJSON型違いを保持するbit。
	constexpr uint16 InvalidStrength = 1u << 7;
	// FrequencyのJSON型違いを保持するbit。
	constexpr uint16 InvalidFrequency = 1u << 8;
	// ScrollSpeedのJSON型違いを保持するbit。
	constexpr uint16 InvalidScrollSpeed = 1u << 9;
	// field object自体が不正な場合に立てるbit一式。
	constexpr uint16 InvalidAll = InvalidEnabled | InvalidKind | InvalidFollowEmitter | InvalidAcceleration | InvalidPosition | InvalidAxis | InvalidRadius | InvalidStrength | InvalidFrequency | InvalidScrollSpeed;
	// 未知の種類をUniformとして動かさないための値。
	constexpr uint8 InvalidKindValue = 0xFFu;

	// 定義済みの力場種類かを判定する。
	bool IsKnownKind( FEffectForceFieldDefinition::EKind Kind )
	{
		return Kind == FEffectForceFieldDefinition::EKind::Uniform || Kind == FEffectForceFieldDefinition::EKind::Radial || Kind == FEffectForceFieldDefinition::EKind::Vortex || Kind == FEffectForceFieldDefinition::EKind::Turbulence;
	}

	// 二乗和のoverflowを避けてdoubleでベクトルの長さ二乗を返す。
	double VectorLengthSquared( const FVector3& Value )
	{
		// X成分をdoubleへ広げた値。
		const double X = Value.x;
		// Y成分をdoubleへ広げた値。
		const double Y = Value.y;
		// Z成分をdoubleへ広げた値。
		const double Z = Value.z;
		return X * X + Y * Y + Z * Z;
	}

	// 読み込み時の問題を呼び出し元へ伝える。
	void Warn( TArray<FString>* Warnings, const char* Message )
	{
		if ( Warnings != nullptr ) Warnings->Add( Message );
	}

	// 三成分のJSON配列を読み、不正時はNaNを保持する。
	bool ReadVector( const NlohmannJson& Json, FVector3& Out )
	{
		// Validateで読み取り失敗を検出する値。
		const float32 Invalid = TNumericLimits<float32>::QuietNaN();
		Out = FVector3( Invalid, Invalid, Invalid );
		if ( !Json.is_array() || Json.size() != 3 || !Json[0].is_number() || !Json[1].is_number() || !Json[2].is_number() ) return false;
		try { Out = FVector3( Json[0].get<float32>(), Json[1].get<float32>(), Json[2].get<float32>() ); return true; }
		catch ( const nlohmann::json::exception& ) { return false; }
	}

	// JSON numberを読み、不正時はNaNを保持する。
	bool ReadFloat( const NlohmannJson& Json, float32& Out )
	{
		// Validateで読み取り失敗を検出する値。
		Out = TNumericLimits<float32>::QuietNaN();
		if ( !Json.is_number() ) return false;
		try { Out = Json.get<float32>(); return true; }
		catch ( const nlohmann::json::exception& ) { return false; }
	}

	// FNV-1a hashへbyte列を追加する。
	void MixValue( uint64& Hash, const void* Data, usize Size )
	{
		// 保存値のbyte列。
		const uint8* Bytes = static_cast<const uint8*>( Data );
		for ( usize Index = 0; Index < Size; ++Index ) { Hash ^= Bytes[Index]; Hash *= 1099511628211ull; }
	}

	// 値型の全byteをhashへ追加する。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		MixValue( Hash, &Value, sizeof( T ) );
	}

	// FVector3の実成分だけをhashへ追加する。
	void MixVector( uint64& Hash, const FVector3& Value )
	{
		MixValue( Hash, Value.x );
		MixValue( Hash, Value.y );
		MixValue( Hash, Value.z );
	}
}

const char* ToString( FEffectForceFieldDefinition::EKind Kind )
{
	switch ( Kind )
	{
	case FEffectForceFieldDefinition::EKind::Uniform: return "Uniform";
	case FEffectForceFieldDefinition::EKind::Radial: return "Radial";
	case FEffectForceFieldDefinition::EKind::Vortex: return "Vortex";
	case FEffectForceFieldDefinition::EKind::Turbulence: return "Turbulence";
	}
	return "Invalid";
}

bool FEffectForceFieldDefinition::IsValidSettings() const
{
	// ベクトル三成分の有限性と上限を調べるラムダ。
	const auto IsValidVector = []( const FVector3& Value )
	{
		// 各成分が許容範囲にあるか調べる。
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z ) && NMath::Abs( Value.x ) <= MaxMagnitude && NMath::Abs( Value.y ) <= MaxMagnitude && NMath::Abs( Value.z ) <= MaxMagnitude;
	};
	// 回転軸ベクトルの長さ二乗。
	const double AxisLengthSquared = VectorLengthSquared( Axis );
	return InvalidFieldTypes == 0 && IsKnownKind( Kind ) && IsValidVector( Acceleration ) && IsValidVector( Position ) && IsValidVector( Axis ) && NMath::IsFinite( AxisLengthSquared ) && AxisLengthSquared >= 1.0e-12 && NMath::IsFinite( Radius ) && Radius > 0.0f && Radius <= MaxMagnitude && NMath::IsFinite( Strength ) && NMath::Abs( Strength ) <= MaxMagnitude && NMath::IsFinite( Frequency ) && Frequency > 0.0f && Frequency <= MaxMagnitude && NMath::IsFinite( ScrollSpeed ) && NMath::Abs( ScrollSpeed ) <= MaxMagnitude;
}

FVector3 FEffectForceFieldDefinition::EvaluateAcceleration( const FVector3& WorldPosition ) const
{
	return EvaluateAcceleration( WorldPosition, FTransform::Identity, 0.0f );
}

FVector3 FEffectForceFieldDefinition::EvaluateAcceleration( const FVector3& WorldPosition, const FTransform& EffectTransform, float32 Time ) const
{
	if ( !bEnabled || !IsValidSettings() || !NMath::IsFinite( WorldPosition.x ) || !NMath::IsFinite( WorldPosition.y ) || !NMath::IsFinite( WorldPosition.z ) || !NMath::IsFinite( Time ) ) return FVector3::Zero;
	// 追従時に局所座標と出力加速度へ適用する正規化済み回転。
	FQuaternion FollowRotation = FQuaternion::Identity;
	// 力場を評価する座標（追従時はエミッター局所、非追従時はワールド）。
	FVector3 EvaluationPosition = WorldPosition;
	if ( bFollowEmitter )
	{
		// 四元数X成分をdoubleへ広げた値。
		const double QuatX = EffectTransform.Rotation.x;
		// 四元数Y成分をdoubleへ広げた値。
		const double QuatY = EffectTransform.Rotation.y;
		// 四元数Z成分をdoubleへ広げた値。
		const double QuatZ = EffectTransform.Rotation.z;
		// 四元数W成分をdoubleへ広げた値。
		const double QuatW = EffectTransform.Rotation.w;
		// 四元数の長さ二乗。
		const double QuatLenSq = QuatX * QuatX + QuatY * QuatY + QuatZ * QuatZ + QuatW * QuatW;
		if ( !NMath::IsFinite( EffectTransform.Position.x ) || !NMath::IsFinite( EffectTransform.Position.y ) || !NMath::IsFinite( EffectTransform.Position.z ) || !NMath::IsFinite( QuatX ) || !NMath::IsFinite( QuatY ) || !NMath::IsFinite( QuatZ ) || !NMath::IsFinite( QuatW ) || !NMath::IsFinite( QuatLenSq ) || QuatLenSq <= 1.0e-12 ) return FVector3::Zero;
		// 四元数を正規化する長さ。
		const double QuatLen = NMath::Sqrt( QuatLenSq );
		if ( !NMath::IsFinite( QuatLen ) || QuatLen <= 0.0 ) return FVector3::Zero;
		FollowRotation = FQuaternion( static_cast<float32>( QuatX / QuatLen ), static_cast<float32>( QuatY / QuatLen ), static_cast<float32>( QuatZ / QuatLen ), static_cast<float32>( QuatW / QuatLen ) );
		// エミッター位置からWorld位置へのX差。
		const double OffsetX = static_cast<double>( WorldPosition.x ) - EffectTransform.Position.x;
		// エミッター位置からWorld位置へのY差。
		const double OffsetY = static_cast<double>( WorldPosition.y ) - EffectTransform.Position.y;
		// エミッター位置からWorld位置へのZ差。
		const double OffsetZ = static_cast<double>( WorldPosition.z ) - EffectTransform.Position.z;
		// float32で表せる絶対値の上限。
		const double FloatLimit = TNumericLimits<float32>::Max();
		if ( !NMath::IsFinite( OffsetX ) || !NMath::IsFinite( OffsetY ) || !NMath::IsFinite( OffsetZ ) || NMath::Abs( OffsetX ) > FloatLimit || NMath::Abs( OffsetY ) > FloatLimit || NMath::Abs( OffsetZ ) > FloatLimit ) return FVector3::Zero;
		// 逆回転へ渡す有限な相対位置。
		const FVector3 Offset( static_cast<float32>( OffsetX ), static_cast<float32>( OffsetY ), static_cast<float32>( OffsetZ ) );
		EvaluationPosition = FollowRotation.UnrotateVector( Offset );
		if ( !NMath::IsFinite( EvaluationPosition.x ) || !NMath::IsFinite( EvaluationPosition.y ) || !NMath::IsFinite( EvaluationPosition.z ) ) return FVector3::Zero;
	}
	// 場の座標系で求めた加速度。
	FVector3 LocalResult = FVector3::Zero;
	if ( Kind == EKind::Uniform ) LocalResult = Acceleration;
	else
	{
		// 評価位置からfield中心までのX差。
		const double DeltaX = static_cast<double>( EvaluationPosition.x ) - Position.x;
		// 評価位置からfield中心までのY差。
		const double DeltaY = static_cast<double>( EvaluationPosition.y ) - Position.y;
		// 評価位置からfield中心までのZ差。
		const double DeltaZ = static_cast<double>( EvaluationPosition.z ) - Position.z;
		// 二乗和のoverflowを避けるdouble距離。
		const double Distance = NMath::Sqrt( DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ );
		if ( !NMath::IsFinite( Distance ) || Distance >= static_cast<double>( Radius ) ) return FVector3::Zero;
		// 中心で1、半径境界で0になる線形減衰係数。
		const double Falloff = 1.0 - Distance / static_cast<double>( Radius );
		// 半径に比例して弱まる加速度の大きさ。
		const double Magnitude = static_cast<double>( Strength ) * Falloff;
		if ( Kind == EKind::Radial )
		{
			if ( Distance == 0.0 ) return FVector3::Zero;
			LocalResult = FVector3( static_cast<float32>( DeltaX / Distance * Magnitude ), static_cast<float32>( DeltaY / Distance * Magnitude ), static_cast<float32>( DeltaZ / Distance * Magnitude ) );
		}
		else if ( Kind == EKind::Vortex )
		{
			if ( Distance == 0.0 ) return FVector3::Zero;
			// 回転軸の長さを求めるdouble値。
			const double AxisLength = NMath::Sqrt( VectorLengthSquared( Axis ) );
			if ( !NMath::IsFinite( AxisLength ) || AxisLength <= 0.0 ) return FVector3::Zero;
			// 正規化した回転軸のX成分。
			const double AxisX = static_cast<double>( Axis.x ) / AxisLength;
			// 正規化した回転軸のY成分。
			const double AxisY = static_cast<double>( Axis.y ) / AxisLength;
			// 正規化した回転軸のZ成分。
			const double AxisZ = static_cast<double>( Axis.z ) / AxisLength;
			// UnitAxis × Delta のX成分。
			const double CrossX = AxisY * DeltaZ - AxisZ * DeltaY;
			// UnitAxis × Delta のY成分。
			const double CrossY = AxisZ * DeltaX - AxisX * DeltaZ;
			// UnitAxis × Delta のZ成分。
			const double CrossZ = AxisX * DeltaY - AxisY * DeltaX;
			// 外積ベクトルの長さ。
			const double CrossLen = NMath::Sqrt( CrossX * CrossX + CrossY * CrossY + CrossZ * CrossZ );
			if ( !NMath::IsFinite( CrossLen ) || CrossLen <= 1.0e-12 ) return FVector3::Zero;
			LocalResult = FVector3( static_cast<float32>( CrossX / CrossLen * Magnitude ), static_cast<float32>( CrossY / CrossLen * Magnitude ), static_cast<float32>( CrossZ / CrossLen * Magnitude ) );
		}
		else if ( Kind == EKind::Turbulence )
		{
			// 空間周波数のdouble値。
			const double Omega = static_cast<double>( Frequency );
			// 時間スクロールによる共通位相。
			const double Phase = static_cast<double>( Time ) * static_cast<double>( ScrollSpeed );
			// X座標に対応する位相。
			const double PhaseX = DeltaX * Omega + Phase;
			// Y座標に対応する位相。
			const double PhaseY = DeltaY * Omega + Phase;
			// Z座標に対応する位相。
			const double PhaseZ = DeltaZ * Omega + Phase;
			if ( !NMath::IsFinite( PhaseX ) || !NMath::IsFinite( PhaseY ) || !NMath::IsFinite( PhaseZ ) ) return FVector3::Zero;
			// 3軸が独立に変化する決定論的Beltrami波のX成分。
			const double WaveX = NMath::Sin( PhaseY ) + NMath::Cos( PhaseZ );
			// 3軸が独立に変化する決定論的Beltrami波のY成分。
			const double WaveY = NMath::Sin( PhaseZ ) + NMath::Cos( PhaseX );
			// 3軸が独立に変化する決定論的Beltrami波のZ成分。
			const double WaveZ = NMath::Sin( PhaseX ) + NMath::Cos( PhaseY );
			LocalResult = FVector3( static_cast<float32>( WaveX * Magnitude ), static_cast<float32>( WaveY * Magnitude ), static_cast<float32>( WaveZ * Magnitude ) );
		}
		else return FVector3::Zero;
	}
	// 追従時はエミッターの回転でワールド向きへ戻した最終加速度。
	const FVector3 Result = bFollowEmitter ? FollowRotation.Rotate( LocalResult ) : LocalResult;
	if ( !NMath::IsFinite( Result.x ) || !NMath::IsFinite( Result.y ) || !NMath::IsFinite( Result.z ) ) return FVector3::Zero;
	return Result;
}

NlohmannJson FEffectForceFieldDefinition::ToJson() const
{
	// 保存するfield object。
	NlohmannJson Json = NlohmannJson::object();
	Json["Enabled"] = ( InvalidFieldTypes & InvalidEnabled ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( bEnabled );
	Json["Kind"] = ( InvalidFieldTypes & InvalidKind ) != 0 || !IsKnownKind( Kind ) ? NlohmannJson( nullptr ) : NlohmannJson( ToString( Kind ) );
	Json["FollowEmitter"] = ( InvalidFieldTypes & InvalidFollowEmitter ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( bFollowEmitter );
	Json["Acceleration"] = ( InvalidFieldTypes & InvalidAcceleration ) != 0 || !NMath::IsFinite( Acceleration.x ) || !NMath::IsFinite( Acceleration.y ) || !NMath::IsFinite( Acceleration.z ) ? NlohmannJson( nullptr ) : NlohmannJson::array( { Acceleration.x, Acceleration.y, Acceleration.z } );
	Json["Position"] = ( InvalidFieldTypes & InvalidPosition ) != 0 || !NMath::IsFinite( Position.x ) || !NMath::IsFinite( Position.y ) || !NMath::IsFinite( Position.z ) ? NlohmannJson( nullptr ) : NlohmannJson::array( { Position.x, Position.y, Position.z } );
	Json["Axis"] = ( InvalidFieldTypes & InvalidAxis ) != 0 || !NMath::IsFinite( Axis.x ) || !NMath::IsFinite( Axis.y ) || !NMath::IsFinite( Axis.z ) ? NlohmannJson( nullptr ) : NlohmannJson::array( { Axis.x, Axis.y, Axis.z } );
	Json["Radius"] = ( InvalidFieldTypes & InvalidRadius ) != 0 || !NMath::IsFinite( Radius ) ? NlohmannJson( nullptr ) : NlohmannJson( Radius );
	Json["Strength"] = ( InvalidFieldTypes & InvalidStrength ) != 0 || !NMath::IsFinite( Strength ) ? NlohmannJson( nullptr ) : NlohmannJson( Strength );
	Json["Frequency"] = ( InvalidFieldTypes & InvalidFrequency ) != 0 || !NMath::IsFinite( Frequency ) ? NlohmannJson( nullptr ) : NlohmannJson( Frequency );
	Json["ScrollSpeed"] = ( InvalidFieldTypes & InvalidScrollSpeed ) != 0 || !NMath::IsFinite( ScrollSpeed ) ? NlohmannJson( nullptr ) : NlohmannJson( ScrollSpeed );
	return Json;
}

bool FEffectForceFieldDefinition::FromJson( const NlohmannJson& Json, FEffectForceFieldDefinition& Out, TArray<FString>* Warnings )
{
	// 入力の不正状態も保持する読み込み先。
	FEffectForceFieldDefinition Definition;
	if ( !Json.is_object() )
	{
		Definition.InvalidFieldTypes = InvalidAll;
		Warn( Warnings, "力場はオブジェクトにしてください" );
		Out = MoveTemp( Definition );
		return true;
	}
	if ( Json.contains( "Enabled" ) )
	{
		if ( Json["Enabled"].is_boolean() ) Definition.bEnabled = Json["Enabled"].get<bool>();
		else { Definition.InvalidFieldTypes |= InvalidEnabled; Warn( Warnings, "Enabledはtrueまたはfalseにしてください" ); }
	}
	if ( Json.contains( "Kind" ) )
	{
		if ( !Json["Kind"].is_string() ) { Definition.InvalidFieldTypes |= InvalidKind; Definition.Kind = static_cast<EKind>( InvalidKindValue ); Warn( Warnings, "KindはUniform、Radial、Vortex、Turbulenceのいずれかにしてください" ); }
		else
		{
			// JSONに書かれたfieldの種類。
			const FString Kind = NJson::GetOr<FString>( Json, "Kind", FString() );
			if ( Kind == "Uniform" ) Definition.Kind = EKind::Uniform;
			else if ( Kind == "Radial" ) Definition.Kind = EKind::Radial;
			else if ( Kind == "Vortex" ) Definition.Kind = EKind::Vortex;
			else if ( Kind == "Turbulence" ) Definition.Kind = EKind::Turbulence;
			else { Definition.InvalidFieldTypes |= InvalidKind; Definition.Kind = static_cast<EKind>( InvalidKindValue ); Warn( Warnings, "KindはUniform、Radial、Vortex、Turbulenceのいずれかにしてください" ); }
		}
	}
	if ( Json.contains( "FollowEmitter" ) )
	{
		if ( Json["FollowEmitter"].is_boolean() ) Definition.bFollowEmitter = Json["FollowEmitter"].get<bool>();
		else { Definition.InvalidFieldTypes |= InvalidFollowEmitter; Warn( Warnings, "FollowEmitterはtrueまたはfalseにしてください" ); }
	}
	if ( Json.contains( "Acceleration" ) && !ReadVector( Json["Acceleration"], Definition.Acceleration ) ) { Definition.InvalidFieldTypes |= InvalidAcceleration; Warn( Warnings, "Accelerationは三つの数値配列にしてください" ); }
	if ( Json.contains( "Position" ) && !ReadVector( Json["Position"], Definition.Position ) ) { Definition.InvalidFieldTypes |= InvalidPosition; Warn( Warnings, "Positionは三つの数値配列にしてください" ); }
	if ( Json.contains( "Axis" ) && !ReadVector( Json["Axis"], Definition.Axis ) ) { Definition.InvalidFieldTypes |= InvalidAxis; Warn( Warnings, "Axisは三つの数値配列にしてください" ); }
	if ( Json.contains( "Radius" ) && !ReadFloat( Json["Radius"], Definition.Radius ) ) { Definition.InvalidFieldTypes |= InvalidRadius; Warn( Warnings, "Radiusは数値にしてください" ); }
	if ( Json.contains( "Strength" ) && !ReadFloat( Json["Strength"], Definition.Strength ) ) { Definition.InvalidFieldTypes |= InvalidStrength; Warn( Warnings, "Strengthは数値にしてください" ); }
	if ( Json.contains( "Frequency" ) && !ReadFloat( Json["Frequency"], Definition.Frequency ) ) { Definition.InvalidFieldTypes |= InvalidFrequency; Warn( Warnings, "Frequencyは数値にしてください" ); }
	if ( Json.contains( "ScrollSpeed" ) && !ReadFloat( Json["ScrollSpeed"], Definition.ScrollSpeed ) ) { Definition.InvalidFieldTypes |= InvalidScrollSpeed; Warn( Warnings, "ScrollSpeedは数値にしてください" ); }
	Out = MoveTemp( Definition );
	return true;
}

TArray<FString> FEffectForceFieldDefinition::Validate() const
{
	// 保存前に修正が必要な値。
	TArray<FString> Issues;
	if ( ( InvalidFieldTypes & InvalidEnabled ) != 0 ) Issues.Add( "ForceField.Enabledはtrueまたはfalseにしてください" );
	if ( ( InvalidFieldTypes & InvalidKind ) != 0 || !IsKnownKind( Kind ) ) Issues.Add( "ForceField.KindはUniform、Radial、Vortex、Turbulenceのいずれかにしてください" );
	if ( ( InvalidFieldTypes & InvalidFollowEmitter ) != 0 ) Issues.Add( "ForceField.FollowEmitterはtrueまたはfalseにしてください" );
	// ベクトル三成分の保存状態を検査するラムダ。
	const auto ValidateVector = [&]( const FVector3& Value, uint16 InvalidBit, const char* Name )
	{
		// 未使用fieldを含む三成分の保存状態。
		if ( ( InvalidFieldTypes & InvalidBit ) != 0 || !NMath::IsFinite( Value.x ) || !NMath::IsFinite( Value.y ) || !NMath::IsFinite( Value.z ) || NMath::Abs( Value.x ) > MaxMagnitude || NMath::Abs( Value.y ) > MaxMagnitude || NMath::Abs( Value.z ) > MaxMagnitude ) Issues.Add( FString( "ForceField." ) + Name + "は有限で各成分の絶対値を1000000以下にしてください" );
	};
	ValidateVector( Acceleration, InvalidAcceleration, "Acceleration" );
	ValidateVector( Position, InvalidPosition, "Position" );
	// 回転軸ベクトルの長さ二乗。
	const double AxisLengthSquared = VectorLengthSquared( Axis );
	if ( ( InvalidFieldTypes & InvalidAxis ) != 0 || !NMath::IsFinite( Axis.x ) || !NMath::IsFinite( Axis.y ) || !NMath::IsFinite( Axis.z ) || NMath::Abs( Axis.x ) > MaxMagnitude || NMath::Abs( Axis.y ) > MaxMagnitude || NMath::Abs( Axis.z ) > MaxMagnitude || !NMath::IsFinite( AxisLengthSquared ) || AxisLengthSquared < 1.0e-12 ) Issues.Add( "ForceField.Axisはゼロでない有限ベクトルで各成分の絶対値を1000000以下にしてください" );
	if ( ( InvalidFieldTypes & InvalidRadius ) != 0 || !NMath::IsFinite( Radius ) || Radius <= 0.0f || Radius > MaxMagnitude ) Issues.Add( "ForceField.Radiusは0より大きく1000000以下にしてください" );
	if ( ( InvalidFieldTypes & InvalidStrength ) != 0 || !NMath::IsFinite( Strength ) || NMath::Abs( Strength ) > MaxMagnitude ) Issues.Add( "ForceField.Strengthは有限で絶対値を1000000以下にしてください" );
	if ( ( InvalidFieldTypes & InvalidFrequency ) != 0 || !NMath::IsFinite( Frequency ) || Frequency <= 0.0f || Frequency > MaxMagnitude ) Issues.Add( "ForceField.Frequencyは0より大きく1000000以下にしてください" );
	if ( ( InvalidFieldTypes & InvalidScrollSpeed ) != 0 || !NMath::IsFinite( ScrollSpeed ) || NMath::Abs( ScrollSpeed ) > MaxMagnitude ) Issues.Add( "ForceField.ScrollSpeedは有限で絶対値を1000000以下にしてください" );
	return Issues;
}

uint64 FEffectForceFieldDefinition::ComputeHash() const
{
	// field内容から計算するFNV-1a hash。
	uint64 Hash = 1469598103934665603ull;
	// hash対象のboolを固定幅へ変換した値。
	const uint8 Enabled = bEnabled ? 1u : 0u;
	// 未知値も識別できるkindのbyte値。
	const uint8 KindValue = static_cast<uint8>( Kind );
	// 追従フラグを固定幅へ変換した値。
	const uint8 FollowEmitter = bFollowEmitter ? 1u : 0u;
	MixValue( Hash, Enabled );
	MixValue( Hash, KindValue );
	MixValue( Hash, FollowEmitter );
	MixVector( Hash, Acceleration );
	MixVector( Hash, Position );
	MixVector( Hash, Axis );
	MixValue( Hash, Radius );
	MixValue( Hash, Strength );
	MixValue( Hash, Frequency );
	MixValue( Hash, ScrollSpeed );
	MixValue( Hash, InvalidFieldTypes );
	return Hash;
}
