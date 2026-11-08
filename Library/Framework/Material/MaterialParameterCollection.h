// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Vector4.h"

//=============================================================================
// CMaterialParameterCollection（マテリアルパラメーターコレクション。.gxmaterialparametercollection）
//   名前つきの値（数・ベクトル・色）の集まり。複数のマテリアルが、同じ名前で読む、共有の値。1 か所を変えると、読んでいるすべてのマテリアルに反映される
//   （時間帯の色・天候・ダメージの赤み・UI 全体のテーマ色など）。マテリアルは、グラフの ParameterCollectionScalar / ParameterCollectionVector ノード（コレクションの名前と値の名前）で読む。
//
//   TSharedPtr<CMaterialParameterCollection> Weather = MakeShared<CMaterialParameterCollection>( "Weather" );
//   Weather->SetScalar( "Rain", 0.8f );
//   Weather->SetVector( "Tint", FVector4( 0.6f, 0.7f, 0.9f, 1.0f ) );
//   Material->BindParameterCollection( Weather );                  // CMaterialSubsystem::GetParameterCollection( "Weather" ) を使えば、名前で、自動で結ばれる
//
//   - 値は、すべて 4 つ組で持つ（数は x）。MVVM の TObservable の OnChanged から SetScalar を呼べば、画面の見た目を、ビューモデルで動かせる。
//   - GetVersion は、値が変わるたびに増える（変わったかを調べる用）。
//   - コレクションに無い名前は、マテリアルのノードの既定の値になる。
//=============================================================================
class CMaterialParameterCollection
{
public:
	explicit CMaterialParameterCollection( const FString& InName = FString() )
		: Name( InName )
	{
	}

	const FString& GetName() const { return Name; }

	void SetScalar( const FString& ParameterName, float32 Value );
	void SetVector( const FString& ParameterName, const FVector4& Value );
	// 値を読む（無ければ false）
	bool TryGet( const FString& ParameterName, FVector4& OutValue ) const;
	float32 GetScalar( const FString& ParameterName, float32 Default = 0.0f ) const;
	bool Has( const FString& ParameterName ) const;
	bool Remove( const FString& ParameterName );
	void Clear();

	int32 Num() const { return Entries.Num(); }
	const FString& GetNameAt( int32 Index ) const { return Entries[Index].Name; }
	FVector4 GetValueAt( int32 Index ) const { return Entries[Index].Value; }
	bool IsScalarAt( int32 Index ) const { return Entries[Index].bScalar; }

	uint32 GetVersion() const { return Version; }

	// JSON（.gxmaterialparametercollection）: { "Format": "gxmaterialparametercollection", "Version": 1, "Name": "Weather", "Parameters": { "Rain": 0.8, "Tint": [0.6, 0.7, 0.9, 1.0] } }
	FString ToJson() const;
	static bool FromJson( const FString& Text, CMaterialParameterCollection& OutParameterCollection, FString* OutError = nullptr );

private:
	struct FEntry
	{
		FString Name;
		FVector4 Value = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
		bool bScalar = false;
	};

	int32 IndexOf( const FString& ParameterName ) const;
	void Set( const FString& ParameterName, const FVector4& Value, bool bScalar );

	FString Name;
	TArray<FEntry> Entries;
	uint32 Version = 1;
};
