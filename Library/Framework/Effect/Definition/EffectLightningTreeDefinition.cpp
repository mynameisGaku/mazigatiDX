// SPDX-License-Identifier: MIT
#include "EffectLightningTreeDefinition.h"


#include "Library/Core/Types/Numeric.h"

namespace
{
	// 読み取れない整数を、後段の範囲検査で拒否できる値へ置き換える。
	int32 ReadInteger( const NlohmannJson& Json, const char* Name, int32 DefaultValue )
	{
		if ( !Json.contains( Name ) ) return DefaultValue;
		const NlohmannJson& Value = Json[Name];
		if ( !Value.is_number_integer() && !Value.is_number_unsigned() ) return TNumericLimits<int32>::Min();
		if ( Value.is_number_unsigned() )
		{
			const uint64 Number = Value.get<uint64>();
			return Number <= static_cast<uint64>( TNumericLimits<int32>::Max() ) ? static_cast<int32>( Number ) : TNumericLimits<int32>::Min();
		}
		const int64 Number = Value.get<int64>();
		return Number >= TNumericLimits<int32>::Min() && Number <= TNumericLimits<int32>::Max() ? static_cast<int32>( Number ) : TNumericLimits<int32>::Min();
	}

	// 読み取れない実数を、Validateで必ず検出される非有限値にする。
	float32 ReadFloat( const NlohmannJson& Json, const char* Name, float32 DefaultValue )
	{
		if ( !Json.contains( Name ) ) return DefaultValue;
		const NlohmannJson& Value = Json[Name];
		if ( !Value.is_number() ) return TNumericLimits<float32>::QuietNaN();
		return Value.get<float32>();
	}

	// FNV-1aで値のバイトを順に混ぜる。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		// 今回混ぜる値のバイト列。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Value );
		// 値を構成する各byte。
		for ( usize Index = 0; Index < sizeof( Value ); ++Index )
		{
			Hash ^= Bytes[Index];
			Hash *= 1099511628211ull;
		}
	}
}

bool FEffectLightningTreeDefinition::IsDefault() const
{
	return Depth == 2 && ChildBranches == 2 && BranchAngleDegrees == 35.0f && GrowthSeconds == 0.08f && BranchDelaySeconds == 0.005f && StrokeCount == 3 && StrokeInterval == 0.06f && StrokeSeconds == 0.018f;
}

NlohmannJson FEffectLightningTreeDefinition::ToJson() const
{
	// ツリー雷の保存設定。
	NlohmannJson Json = NlohmannJson::object();
	Json["Depth"] = Depth;
	Json["ChildBranches"] = ChildBranches;
	Json["BranchAngleDegrees"] = BranchAngleDegrees;
	Json["GrowthSeconds"] = GrowthSeconds;
	Json["BranchDelaySeconds"] = BranchDelaySeconds;
	Json["StrokeCount"] = StrokeCount;
	Json["StrokeInterval"] = StrokeInterval;
	Json["StrokeSeconds"] = StrokeSeconds;
	return Json;
}

bool FEffectLightningTreeDefinition::FromJson( const NlohmannJson& Json, FEffectLightningTreeDefinition& Out, TArray<FString>* Warnings )
{
	// JSON object以外も不正値として保持し、Validateで保存を止める。
	FEffectLightningTreeDefinition Definition;
	if ( !Json.is_object() )
	{
		Definition.Depth = TNumericLimits<int32>::Min();
		Definition.ChildBranches = TNumericLimits<int32>::Min();
		Definition.BranchAngleDegrees = TNumericLimits<float32>::QuietNaN();
		Definition.GrowthSeconds = TNumericLimits<float32>::QuietNaN();
		Definition.BranchDelaySeconds = TNumericLimits<float32>::QuietNaN();
		Definition.StrokeCount = TNumericLimits<int32>::Min();
		Definition.StrokeInterval = TNumericLimits<float32>::QuietNaN();
		Definition.StrokeSeconds = TNumericLimits<float32>::QuietNaN();
		if ( Warnings != nullptr ) Warnings->Add( "Path.Tree は object である必要があります" );
		Out = Definition;
		return true;
	}
	Definition.Depth = ReadInteger( Json, "Depth", Definition.Depth );
	Definition.ChildBranches = ReadInteger( Json, "ChildBranches", Definition.ChildBranches );
	Definition.BranchAngleDegrees = ReadFloat( Json, "BranchAngleDegrees", Definition.BranchAngleDegrees );
	Definition.GrowthSeconds = ReadFloat( Json, "GrowthSeconds", Definition.GrowthSeconds );
	Definition.BranchDelaySeconds = ReadFloat( Json, "BranchDelaySeconds", Definition.BranchDelaySeconds );
	Definition.StrokeCount = ReadInteger( Json, "StrokeCount", Definition.StrokeCount );
	Definition.StrokeInterval = ReadFloat( Json, "StrokeInterval", Definition.StrokeInterval );
	Definition.StrokeSeconds = ReadFloat( Json, "StrokeSeconds", Definition.StrokeSeconds );
	Out = Definition;
	if ( Warnings != nullptr ) for ( const FString& Issue : Out.Validate() ) Warnings->Add( Issue );
	return true;
}

TArray<FString> FEffectLightningTreeDefinition::Validate() const
{
	// 保存前に修正が必要な項目。
	TArray<FString> Issues;
	if ( Depth < 0 || Depth > 2 ) Issues.Add( "Path.Tree.Depth は 0 以上 2 以下にしてください" );
	if ( ChildBranches < 0 || ChildBranches > 4 ) Issues.Add( "Path.Tree.ChildBranches は 0 以上 4 以下にしてください" );
	if ( !NMath::IsFinite( BranchAngleDegrees ) || BranchAngleDegrees < 5.0f || BranchAngleDegrees > 85.0f ) Issues.Add( "Path.Tree.BranchAngleDegrees は 5 以上 85 以下にしてください" );
	if ( !NMath::IsFinite( GrowthSeconds ) || GrowthSeconds < 0.0f || GrowthSeconds > 10.0f ) Issues.Add( "Path.Tree.GrowthSeconds は 0 以上 10 以下にしてください" );
	if ( !NMath::IsFinite( BranchDelaySeconds ) || BranchDelaySeconds < 0.0f || BranchDelaySeconds > 1.0f ) Issues.Add( "Path.Tree.BranchDelaySeconds は 0 以上 1 以下にしてください" );
	if ( StrokeCount < 1 || StrokeCount > 8 ) Issues.Add( "Path.Tree.StrokeCount は 1 以上 8 以下にしてください" );
	if ( !NMath::IsFinite( StrokeInterval ) || StrokeInterval < 0.001f || StrokeInterval > 2.0f ) Issues.Add( "Path.Tree.StrokeInterval は 0.001 以上 2 以下にしてください" );
	if ( !NMath::IsFinite( StrokeSeconds ) || StrokeSeconds < 0.0001f || !NMath::IsFinite( StrokeInterval ) || StrokeSeconds > StrokeInterval ) Issues.Add( "Path.Tree.StrokeSeconds は 0.0001 以上 StrokeInterval 以下にしてください" );
	return Issues;
}

uint64 FEffectLightningTreeDefinition::ComputeHash() const
{
	// 全fieldを独立して識別するハッシュ。
	uint64 Hash = 1469598103934665603ull;
	MixValue( Hash, Depth );
	MixValue( Hash, ChildBranches );
	MixValue( Hash, BranchAngleDegrees );
	MixValue( Hash, GrowthSeconds );
	MixValue( Hash, BranchDelaySeconds );
	MixValue( Hash, StrokeCount );
	MixValue( Hash, StrokeInterval );
	MixValue( Hash, StrokeSeconds );
	return Hash;
}
