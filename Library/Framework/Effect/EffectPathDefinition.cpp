// SPDX-License-Identifier: MIT
#include "EffectPathDefinition.h"


#include "Library/Core/Types/Numeric.h"
#include "Library/Json/JsonHelpers.h"

namespace
{
	// TreeLightningで誤型の整数をValidateが拒否する範囲外値へする。
	int32 ReadTreeInteger( const NlohmannJson& Json, const char* Name, int32 DefaultValue )
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

	// TreeLightningで誤型の数値をValidateが検出できる非有限値へする。
	float32 ReadTreeFloat( const NlohmannJson& Json, const char* Name, float32 DefaultValue )
	{
		if ( !Json.contains( Name ) ) return DefaultValue;
		const NlohmannJson& Value = Json[Name];
		return Value.is_number() ? Value.get<float32>() : TNumericLimits<float32>::QuietNaN();
	}

	// TreeLightningの終点は3個の有限な数値だけを受け付ける。
	FVector3 ReadTreeEnd( const NlohmannJson& Json, const FVector3& DefaultValue )
	{
		if ( !Json.contains( "End" ) ) return DefaultValue;
		const NlohmannJson& Value = Json["End"];
		if ( !Value.is_array() || Value.size() != 3 ) return FVector3( TNumericLimits<float32>::QuietNaN() );
		for ( usize Index = 0; Index < 3; ++Index ) if ( !Value[Index].is_number() ) return FVector3( TNumericLimits<float32>::QuietNaN() );
		return FVector3( Value[0].get<float32>(), Value[1].get<float32>(), Value[2].get<float32>() );
	}
}

const char* ToString( FEffectPathDefinition::EKind Kind )
{
	switch ( Kind )
	{
	case FEffectPathDefinition::EKind::None: return "None";
	case FEffectPathDefinition::EKind::Ribbon: return "Ribbon";
	case FEffectPathDefinition::EKind::Lightning: return "Lightning";
	case FEffectPathDefinition::EKind::TreeLightning: return "TreeLightning";
	case FEffectPathDefinition::EKind::InvalidTreeLightning: return "InvalidTreeLightning";
	}
	return "None";
}

bool TryParse( const FString& Text, FEffectPathDefinition::EKind& Out )
{
	// 読み込める表示方式の候補。
	for ( const FEffectPathDefinition::EKind Kind : { FEffectPathDefinition::EKind::None, FEffectPathDefinition::EKind::Ribbon, FEffectPathDefinition::EKind::Lightning, FEffectPathDefinition::EKind::TreeLightning, FEffectPathDefinition::EKind::InvalidTreeLightning } )
	{
		if ( Text.Equals( ToString( Kind ), ESearchCase::IgnoreCase ) )
		{
			Out = Kind;
			return true;
		}
	}
	return false;
}

NlohmannJson FEffectPathDefinition::ToJson() const
{
	// 定義の全項目を書き込む、帯専用のオブジェクト。
	NlohmannJson Json = NlohmannJson::object();
	Json["Kind"] = ToString( Kind );
	Json["End"] = NlohmannJson::array( { End.x, End.y, End.z } );
	Json["Segments"] = Segments;
	Json["Branches"] = Branches;
	Json["Jitter"] = Jitter;
	Json["BranchLength"] = BranchLength;
	Json["RefreshHz"] = RefreshHz;
	Json["UVRepeat"] = UVRepeat;
	Json["BreakDistance"] = BreakDistance;
	if ( !Tree.IsDefault() ) Json["Tree"] = Tree.ToJson();
	if ( bInvalidUseTarget ) Json["UseTarget"] = NlohmannJson( nullptr );
	else if ( bUseTarget ) Json["UseTarget"] = true;
	return Json;
}

bool FEffectPathDefinition::FromJson( const NlohmannJson& Json, FEffectPathDefinition& Out, TArray<FString>* Warnings )
{
	if ( !Json.is_object() ) return false;
	// 読めない項目があっても、ほかの項目と既定値を保つ定義。
	FEffectPathDefinition Definition;
	// Tree設定がある未知kindはNoneへ化けず、Validateで保存を止める。
	const bool bHasTree = Json.contains( "Tree" );
	if ( Json.contains( "Kind" ) && !TryParse( NJson::GetOr<FString>( Json, "Kind", FString() ), Definition.Kind ) )
	{
		if ( Warnings != nullptr ) Warnings->Add( "Path.Kind の表示方式を読めません" );
		if ( bHasTree ) Definition.Kind = EKind::InvalidTreeLightning;
	}
	const bool bStrictTreeKind = Definition.Kind == EKind::TreeLightning;
	Definition.End = bStrictTreeKind ? ReadTreeEnd( Json, Definition.End ) : NJson::ReadVector3( Json, "End", Definition.End );
	Definition.Segments = bStrictTreeKind ? ReadTreeInteger( Json, "Segments", Definition.Segments ) : NJson::GetOr<int32>( Json, "Segments", Definition.Segments );
	Definition.Branches = bStrictTreeKind ? ReadTreeInteger( Json, "Branches", Definition.Branches ) : NJson::GetOr<int32>( Json, "Branches", Definition.Branches );
	Definition.Jitter = bStrictTreeKind ? ReadTreeFloat( Json, "Jitter", Definition.Jitter ) : NJson::GetOr<float32>( Json, "Jitter", Definition.Jitter );
	Definition.BranchLength = bStrictTreeKind ? ReadTreeFloat( Json, "BranchLength", Definition.BranchLength ) : NJson::GetOr<float32>( Json, "BranchLength", Definition.BranchLength );
	Definition.RefreshHz = bStrictTreeKind ? ReadTreeFloat( Json, "RefreshHz", Definition.RefreshHz ) : NJson::GetOr<float32>( Json, "RefreshHz", Definition.RefreshHz );
	Definition.UVRepeat = bStrictTreeKind ? ReadTreeFloat( Json, "UVRepeat", Definition.UVRepeat ) : NJson::GetOr<float32>( Json, "UVRepeat", Definition.UVRepeat );
	Definition.BreakDistance = bStrictTreeKind ? ReadTreeFloat( Json, "BreakDistance", Definition.BreakDistance ) : NJson::GetOr<float32>( Json, "BreakDistance", Definition.BreakDistance );
	if ( Json.contains( "Tree" ) ) FEffectLightningTreeDefinition::FromJson( Json["Tree"], Definition.Tree, Warnings );
	if ( Json.contains( "UseTarget" ) )
	{
		// 動的終点の有効指定として渡された JSON 値。
		const NlohmannJson& UseTargetValue = Json["UseTarget"];
		if ( UseTargetValue.is_boolean() ) Definition.bUseTarget = UseTargetValue.get<bool>();
		else
		{
			Definition.bInvalidUseTarget = true;
			if ( Warnings != nullptr ) Warnings->Add( "Path.UseTarget は真偽値にしてください" );
		}
	}
	Out = Definition;
	return true;
}

TArray<FString> FEffectPathDefinition::Validate() const
{
	// 各項目の不正な値を、編集画面と保存の検証へ返す一覧。
	TArray<FString> Issues;
	if ( Kind != EKind::None && Kind != EKind::Ribbon && Kind != EKind::Lightning && Kind != EKind::TreeLightning ) Issues.Add( "Path.Kind の表示方式が不正です" );
	if ( !NMath::IsFinite( End.x ) || !NMath::IsFinite( End.y ) || !NMath::IsFinite( End.z ) ) Issues.Add( "Path.End は有限の座標にしてください" );
	else if ( ( Kind == EKind::Lightning || Kind == EKind::TreeLightning ) && End.x == 0.0f && End.y == 0.0f && End.z == 0.0f ) Issues.Add( "Path.End は雷の始点と異なる位置にしてください" );
	if ( Segments < 2 || Segments > MaxSegments ) Issues.Add( "Path.Segments は 2 以上 128 以下にしてください" );
	if ( Branches < 0 || Branches > MaxBranches ) Issues.Add( "Path.Branches は 0 以上 16 以下にしてください" );
	if ( !NMath::IsFinite( Jitter ) || Jitter < 0.0f || ( Kind == EKind::TreeLightning && Jitter > 1.0f ) ) Issues.Add( Kind == EKind::TreeLightning ? "TreeLightning の Path.Jitter は有限の 0 以上 1 以下にしてください" : "Path.Jitter は有限の 0 以上の値にしてください" );
	if ( !NMath::IsFinite( BranchLength ) || BranchLength < 0.0f || ( Kind == EKind::TreeLightning && BranchLength > 2.0f ) ) Issues.Add( Kind == EKind::TreeLightning ? "TreeLightning の Path.BranchLength は有限の 0 以上 2 以下にしてください" : "Path.BranchLength は有限の 0 以上の値にしてください" );
	if ( !NMath::IsFinite( RefreshHz ) || RefreshHz < 0.0f ) Issues.Add( "Path.RefreshHz は有限の 0 以上の値にしてください" );
	if ( !NMath::IsFinite( UVRepeat ) || UVRepeat <= 0.0f ) Issues.Add( "Path.UVRepeat は有限の正の繰り返し回数にしてください" );
	if ( !NMath::IsFinite( BreakDistance ) || BreakDistance <= 0.0f ) Issues.Add( "Path.BreakDistance は有限の正の距離にしてください" );
	for ( const FString& Issue : Tree.Validate() ) Issues.Add( Issue );
	if ( bInvalidUseTarget ) Issues.Add( "Path.UseTarget は真偽値にしてください" );
	return Issues;
}

uint64 FEffectPathDefinition::ComputeHash() const
{
	// 全項目のバイトを順に混ぜる識別値。
	uint64 Hash = 1469598103934665603ull;
	// 型の余白を含めず、一つの値の表現だけを混ぜる処理。
	const auto Mix = [&]( const auto& Value )
	{
		// 今回混ぜる値のバイト列。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Value );
		// 値の中のバイト位置。
		for ( usize Index = 0; Index < sizeof( Value ); ++Index )
		{
			Hash ^= Bytes[Index];
			Hash *= 1099511628211ull;
		}
	};
	Mix( static_cast<uint8>( Kind ) );
	Mix( End.x );
	Mix( End.y );
	Mix( End.z );
	Mix( Segments );
	Mix( Branches );
	Mix( Jitter );
	Mix( BranchLength );
	Mix( RefreshHz );
	Mix( UVRepeat );
	Mix( BreakDistance );
	if ( !Tree.IsDefault() )
	{
		// 旧hashとの互換を守り、Tree設定だけを識別するtag。
		const uint32 TreeTag = 0x54524545u;
		// Treeの設定全体を固定幅で混ぜる値。
		const uint64 TreeHash = Tree.ComputeHash();
		Mix( TreeTag );
		Mix( TreeHash );
	}
	if ( bUseTarget || bInvalidUseTarget )
	{
		// 旧hashとの互換を守り、動的終点設定だけを識別するtag。
		const uint32 TargetTag = 0x54524754u;
		// 有効と型不正を区別する状態値。
		const uint8 TargetState = bInvalidUseTarget ? 2u : 1u;
		Mix( TargetTag );
		Mix( TargetState );
	}
	return Hash;
}
