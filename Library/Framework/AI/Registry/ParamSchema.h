// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/AI/AIIssue.h"
#include "Library/Json/JsonObject.h"

//=============================================================================
// パラメータのスキーマ
//   登録する部品（Task / Condition / State の処理など）が持つ、パラメータの定義（名前・型・既定値・範囲・説明）。
//   JSON の読み込み・エディタの Inspector・検証が、この 1 つから決まる（部品ごとの画面や読み込みのコードを書かない）。
//
//   FParamSchema Schema = FParamSchema()
//       .Float( "SightRange", 7.0f ).Range( 0.0f, 50.0f ).Hint( "視界の長さ" )
//       .String( "Clip" ).Required()
//       .Enum( "Mode", "Walk|Run", "Walk" );
//
//   読み込み: FParamSet Params = NAI::ResolveParams( Schema, JsonObject, "States/Run/OnEnter[0]", &Issues );
//     型の検査・足りない項目への既定値・範囲への丸め・未知の名前の警告を行い、問題は Issues に足す。
//
//   初版の型は、スカラー・文字列・列挙・ブラックボードのキー・クリップ名・アセットのパスに限る。
//=============================================================================
enum class EParamType : uint8
{
	Bool,
	Int,
	Float,
	String,
	Enum,            // Options のどれか（文字列で持つ）
	BlackboardKey,   // ブラックボードのキーの名前
	Clip,            // アニメーションのクリップの名前
	AssetPath,       // アセットのパス
	Scalar,          // 真偽値・数・文字列のどれか（JSON のまま持つ。SetBlackboard の Value など、型が実行時に決まるもの）
};

const char* ToString( EParamType Type );

// パラメータの値 1 つ（型の欄だけを使う: Bool = bValue、Int = IntValue、Float = FloatValue、それ以外 = TextValue）
struct FParamValue
{
	bool bValue = false;
	int32 IntValue = 0;
	float32 FloatValue = 0.0f;
	FString TextValue;
};

struct FParamDefinition
{
	FString Name;
	EParamType Type = EParamType::Bool;
	FParamValue Default;
	// 範囲（Int / Float。bHasRange のときだけ）
	float32 Min = 0.0f;
	float32 Max = 0.0f;
	bool bHasRange = false;
	// 足りないとき、既定値ではなくエラーにするか
	bool bRequired = false;
	// エディタに出す説明
	FString Hint;
	// Enum の選択肢
	TArray<FString> Options;
};

class FParamSchema
{
public:
	// 追加（どれも最後に足したパラメータを指す。続けて Range / Hint / Required を付けられる）
	FParamSchema& Bool( const char* Name, bool bDefault = false );
	FParamSchema& Int( const char* Name, int32 Default = 0 );
	FParamSchema& Float( const char* Name, float32 Default = 0.0f );
	FParamSchema& String( const char* Name, const char* Default = "" );
	// Options は '|' 区切り（"Walk|Run"）。Default は Options のどれか
	FParamSchema& Enum( const char* Name, const char* Options, const char* Default );
	FParamSchema& BlackboardKey( const char* Name );
	FParamSchema& Clip( const char* Name );
	FParamSchema& AssetPath( const char* Name );
	FParamSchema& Scalar( const char* Name );

	// 最後に足したパラメータに付ける
	FParamSchema& Range( float32 Min, float32 Max );
	FParamSchema& Hint( const char* Text );
	FParamSchema& Required();

	const TArray<FParamDefinition>& GetParams() const { return Params; }
	int32 GetNumParams() const { return Params.Num(); }
	// 名前のパラメータの番号。無ければ -1
	int32 FindIndex( const FString& Name ) const;

private:
	FParamDefinition& Add( const char* Name, EParamType Type );

	TArray<FParamDefinition> Params;
};

// 読み込んだパラメータの値（スキーマと同じ順に並ぶ）。部品は、作られるときに 1 回だけ読んで、メンバーに持つ
class FParamSet
{
public:
	FParamSet() = default;
	explicit FParamSet( const FParamSchema& InSchema );

	bool GetBool( const char* Name ) const;
	int32 GetInt( const char* Name ) const;
	float32 GetFloat( const char* Name ) const;
	// String / Enum / BlackboardKey / Clip / AssetPath（Scalar は JSON の文字列表現）
	const FString& GetText( const char* Name ) const;
	// Scalar を JSON の値として読む（未指定は null）
	NlohmannJson GetJson( const char* Name ) const;

	const TArray<FParamValue>& GetValues() const { return Values; }
	TArray<FParamValue>& GetValues() { return Values; }

private:
	const FParamValue* Find( const char* Name, EParamType Expected1, EParamType Expected2 ) const;

	FParamSchema Schema;
	TArray<FParamValue> Values;
};

namespace NAI
{
	// JSON のパラメータ（オブジェクト。無くてもよい）を、スキーマに従って読む。
	// 型違い・範囲外・未知の名前・足りない必須は Issues に足す（Where は問題の場所の表示用）
	FParamSet ResolveParams( const FParamSchema& Schema, const NlohmannJson* ParamsJson, const FString& Where, TArray<FAIIssue>* Issues );

	// 値を JSON にする（既定値と同じ項目は書かない）
	NlohmannJson ParamsToJson( const FParamSchema& Schema, const FParamSet& Params );
}
