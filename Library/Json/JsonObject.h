#pragma once

// json.hpp は BOM なし UTF-8 で、コメント中の罫線文字が C4819 を出すので抑制する
// （ビルドは /utf-8 ではなく /execution-charset:utf-8。BOM の無いファイルを UTF-8 として読むと、BOM なしの Shift-JIS の DxLib ヘッダーが壊れる）
#pragma warning( push )
#pragma warning( disable : 4819 )
#include "VendorModules/nlohmann/json.hpp"
#pragma warning( pop )

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

using NlohmannJson = nlohmann::json;

namespace NJson
{
	// JSON ライブラリ（nlohmann）の文字列の型。キーや dump() の結果など、ライブラリとやりとりする文字列の型（UTF-8）。
	// 自分の文字列は FString を使い、ライブラリに渡す・受け取る境目だけでこの型を使う
	using FJsonString = NlohmannJson::string_t;

	// FString -> nlohmann の文字列（キー・値）。どちらも UTF-8 なので中身はそのまま。
	// FString に不正な UTF-8 が入っていると dump() が例外を投げるので、そのときだけ U+FFFD に直す
	FORCEINLINE FJsonString ToJsonString( const FString& Text )
	{
		return Text.IsValidUtf8() ? FJsonString( Text.GetData(), static_cast<usize>( Text.Len() ) ) : FJsonString( *Text.ToValidUtf8() );
	}

	// nlohmann の文字列 -> FString
	FORCEINLINE FString FromJsonString( const FJsonString& Text )
	{
		return FString( Text.c_str(), static_cast<int32>( Text.size() ) );
	}
}

//-----------------------------------------------------------------------------
// FString <-> JSON 文字列
//   JSON も FString も UTF-8 なので、そのまま入れ替える（ToJsonString / FromJsonString）
//-----------------------------------------------------------------------------
namespace nlohmann
{
	template<>
	struct adl_serializer<FString>
	{
		FORCEINLINE static void to_json( json& Json, const FString& Value ) { Json = NJson::ToJsonString( Value ); }
		FORCEINLINE static void from_json( const json& Json, FString& Value ) { Value = NJson::FromJsonString( Json.get_ref<const NJson::FJsonString&>() ); }
	};
}

namespace NJson
{
	//=========================================================================
	// IJsonObject
	//   キーは '.' 区切りで入れ子のオブジェクトを辿れる
	//     { "Viewport": { "Size": { "Width": 1280 } } }  ->  "Viewport.Size.Width"
	//=========================================================================
	class IJsonObject
	{
	public:
		virtual ~IJsonObject() = default;

		virtual bool LoadFromFile( const FString& Filename ) = 0;
		virtual bool SaveToFile( const FString& Filename, int32 Indent = 2 ) const = 0;

		virtual void Set( const FString& Key, const NlohmannJson& Value ) = 0;
		// 見つからなければ null
		virtual NlohmannJson Get( const FString& Key ) const = 0;
		virtual bool HasKey( const FString& Key ) const = 0;
		virtual bool Remove( const FString& Key ) = 0;
		virtual void Clear() = 0;

		// ルート直下の要素数 / キー一覧
		virtual usize Num() const = 0;
		virtual TArray<FString> GetKeys() const = 0;
	};

	//=========================================================================
	// CJsonObject
	//   CJsonObject Config;
	//   Config.LoadFromFile( "Config/ScreenConfig.json" );
	//   int32 Width = Config.GetValue<int32>( "Viewport.Size.Width", 1280 );
	//=========================================================================
	class CJsonObject : public IJsonObject
	{
	public:
		CJsonObject() = default;
		virtual ~CJsonObject() = default;

		bool LoadFromFile( const FString& Filename ) override;
		bool SaveToFile( const FString& Filename, int32 Indent = 2 ) const override;
		FORCEINLINE bool IsLoaded() const { return bLoaded; }

		void Set( const FString& Key, const NlohmannJson& Value ) override;
		NlohmannJson Get( const FString& Key ) const override;
		bool HasKey( const FString& Key ) const override;
		bool Remove( const FString& Key ) override;
		void Clear() override;

		usize Num() const override;
		TArray<FString> GetKeys() const override;

		// キーが無い・型が合わないときは Default を返す
		template<typename T>
		T GetValue( const FString& Key, const T& Default = T() ) const;

		FORCEINLINE const NlohmannJson& GetRoot() const { return Root; }
		FORCEINLINE NlohmannJson& GetRoot() { return Root; }

	private:
		const NlohmannJson* FindNode( const FString& Key ) const;
		// GetValue がキーを見つけられなかった / 型が合わなかったときのログ（テンプレートからログを出せないので .cpp に置く）
		void NotifyMissingKey( const FString& Key ) const;
		void NotifyInvalidValue( const FString& Key, const char* Reason ) const;

	private:
		NlohmannJson Root = NlohmannJson::object();
		bool bLoaded = false;
	};
}

#include "JsonObject.inl"
