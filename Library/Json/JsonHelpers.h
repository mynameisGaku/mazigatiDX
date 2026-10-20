#pragma once
#include <cmath>

#include "Library/Json/JsonObject.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// JSON を読み書きするときの小さな補助（データアセットの定義ファイル用）
//
//   const int32 Columns = NJson::GetOr<int32>( Json, "Columns", 1 );          // 無い / 型が違えば既定値
//   Json["Fps"] = NJson::Round4( Fps );                                       // 0.1f が 0.10000000149... と保存されないように
//   Json["Offset"] = NJson::ToJson( Offset );  Offset = NJson::ReadVector3( Json, "Offset", FVector3::Zero );
//=============================================================================
namespace NJson
{
	// Object[Key] を T として読む。無い・null・型が違うときは Default
	template<typename T>
	T GetOr( const NlohmannJson& Object, const char* Key, const T& Default )
	{
		if ( !Object.is_object() ) return Default;
		const auto It = Object.find( Key );
		if ( It == Object.end() || It->is_null() ) return Default;
		try
		{
			return It->get<T>();
		}
		catch ( const nlohmann::json::exception& )
		{
			return Default;
		}
	}

	// 4 桁で丸めた double（float の誤差が保存したファイルに出ないように）
	inline double Round4( float32 Value )
	{
		return std::round( static_cast<double>( Value ) * 10000.0 ) / 10000.0;
	}

	inline NlohmannJson ToJson( const FVector3& V )
	{
		return NlohmannJson::array( { Round4( V.x ), Round4( V.y ), Round4( V.z ) } );
	}

	// [x, y, z] を読む。無い・形が違うときは Default
	inline FVector3 ReadVector3( const NlohmannJson& Object, const char* Key, const FVector3& Default )
	{
		if ( !Object.is_object() ) return Default;
		const auto It = Object.find( Key );
		if ( It == Object.end() || !It->is_array() || It->size() < 3 ) return Default;
		try
		{
			return FVector3( ( *It )[0].get<float32>(), ( *It )[1].get<float32>(), ( *It )[2].get<float32>() );
		}
		catch ( const nlohmann::json::exception& )
		{
			return Default;
		}
	}
}
