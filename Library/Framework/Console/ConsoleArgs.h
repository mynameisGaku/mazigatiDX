#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// コンソールのコマンドの引数の型（文字列 -> 値、値 -> 文字列）
//
//   コマンドのラムダの引数に使える型:
//     bool        true / false / on / off / yes / no / 1 / 0（大文字小文字は区別しない）
//     int32 / int64 / uint32    10 進、または 0x の 16 進。符号つき（uint32 は負の値を受け付けない）
//     float32 / float64         1.5  -2  .5  1e3（全体が数であること。nan / inf は受け付けない）
//     FString                   1 つの引数（引用符で空白を含められる）
//     FVector3                  3 つの引数（x y z）
//     FConsoleRest              残りの引数すべて（空白 1 つでつなぐ）。最後の引数にだけ使える
//     TOptional<T>              省略できる引数（最後のほうにだけ置く）
//
//   新しい型を使いたいときは、TConsoleArg<T> を特殊化する（下の bool の例を参照）。
//=============================================================================

// 残りの引数すべて（echo のように、自由な文章を受け取る）
struct FConsoleRest
{
	FString Text;
};

namespace NConsole
{
	// 全体が整数 / 数 / 真偽の表記であること。Out は、成功したときだけ書き換える
	bool ParseInt64( const FString& Text, int64& Out );
	bool ParseFloat64( const FString& Text, float64& Out );
	bool ParseBool( const FString& Text, bool& Out );

	// 表示用（浮動小数は、読み戻せる程度の桁数で、末尾の 0 は付けない）
	FString FormatFloat( float64 Value );
}

//-----------------------------------------------------------------------------
// TConsoleArg<T>
//   NumTokens: 使う引数の数（-1 なら、残りすべて）
//   TypeName(): 使い方の表示用の名前
//   Parse( Tokens, NumAvailable, Out, Error ): Tokens[0..] から値を作る（NumAvailable は残っている引数の数。NumTokens 以上が保証される）
//   GetChoices( Out ): 補完の候補（無ければ何もしない）
//   ToText( Value ): 値の表示
//-----------------------------------------------------------------------------
template<typename T, typename = void>
struct TConsoleArg;

template<>
struct TConsoleArg<bool>
{
	static constexpr int32 NumTokens = 1;
	static const char* TypeName() { return "bool"; }
	static bool Parse( const FString* Tokens, int32, bool& Out, FString& Error )
	{
		if ( NConsole::ParseBool( Tokens[0], Out ) ) return true;
		Error = "expected true / false (or on / off), got '" + Tokens[0] + "'";
		return false;
	}
	static void GetChoices( TArray<FString>& Out ) { Out = { "true", "false", "on", "off" }; }
	static FString ToText( bool Value ) { return Value ? "true" : "false"; }
};

template<>
struct TConsoleArg<int64>
{
	static constexpr int32 NumTokens = 1;
	static const char* TypeName() { return "int"; }
	static bool Parse( const FString* Tokens, int32, int64& Out, FString& Error )
	{
		if ( NConsole::ParseInt64( Tokens[0], Out ) ) return true;
		Error = "expected an integer, got '" + Tokens[0] + "'";
		return false;
	}
	static void GetChoices( TArray<FString>& ) {}
	static FString ToText( int64 Value ) { return FString::FromInt( Value ); }
};

template<>
struct TConsoleArg<int32>
{
	static constexpr int32 NumTokens = 1;
	static const char* TypeName() { return "int"; }
	static bool Parse( const FString* Tokens, int32, int32& Out, FString& Error )
	{
		int64 Wide = 0;
		if ( !NConsole::ParseInt64( Tokens[0], Wide ) )
		{
			Error = "expected an integer, got '" + Tokens[0] + "'";
			return false;
		}
		if ( Wide < INT32_MIN || Wide > INT32_MAX )
		{
			Error = "'" + Tokens[0] + "' is out of range for a 32-bit integer";
			return false;
		}
		Out = static_cast<int32>( Wide );
		return true;
	}
	static void GetChoices( TArray<FString>& ) {}
	static FString ToText( int32 Value ) { return FString::FromInt( Value ); }
};

template<>
struct TConsoleArg<uint32>
{
	static constexpr int32 NumTokens = 1;
	static const char* TypeName() { return "uint"; }
	static bool Parse( const FString* Tokens, int32, uint32& Out, FString& Error )
	{
		int64 Wide = 0;
		if ( !NConsole::ParseInt64( Tokens[0], Wide ) )
		{
			Error = "expected a non-negative integer, got '" + Tokens[0] + "'";
			return false;
		}
		if ( Wide < 0 || Wide > static_cast<int64>( UINT32_MAX ) )
		{
			Error = "'" + Tokens[0] + "' is out of range (0 .. 4294967295)";
			return false;
		}
		Out = static_cast<uint32>( Wide );
		return true;
	}
	static void GetChoices( TArray<FString>& ) {}
	static FString ToText( uint32 Value ) { return FString::FromInt( Value ); }
};

template<>
struct TConsoleArg<float64>
{
	static constexpr int32 NumTokens = 1;
	static const char* TypeName() { return "float"; }
	static bool Parse( const FString* Tokens, int32, float64& Out, FString& Error )
	{
		if ( NConsole::ParseFloat64( Tokens[0], Out ) ) return true;
		Error = "expected a number, got '" + Tokens[0] + "'";
		return false;
	}
	static void GetChoices( TArray<FString>& ) {}
	static FString ToText( float64 Value ) { return NConsole::FormatFloat( Value ); }
};

template<>
struct TConsoleArg<float32>
{
	static constexpr int32 NumTokens = 1;
	static const char* TypeName() { return "float"; }
	static bool Parse( const FString* Tokens, int32, float32& Out, FString& Error )
	{
		float64 Wide = 0.0;
		if ( !NConsole::ParseFloat64( Tokens[0], Wide ) )
		{
			Error = "expected a number, got '" + Tokens[0] + "'";
			return false;
		}
		Out = static_cast<float32>( Wide );
		return true;
	}
	static void GetChoices( TArray<FString>& ) {}
	static FString ToText( float32 Value ) { return NConsole::FormatFloat( static_cast<float64>( Value ) ); }
};

template<>
struct TConsoleArg<FString>
{
	static constexpr int32 NumTokens = 1;
	static const char* TypeName() { return "text"; }
	static bool Parse( const FString* Tokens, int32, FString& Out, FString& )
	{
		Out = Tokens[0];
		return true;
	}
	static void GetChoices( TArray<FString>& ) {}
	static FString ToText( const FString& Value ) { return Value; }
};

template<>
struct TConsoleArg<FConsoleRest>
{
	static constexpr int32 NumTokens = -1;
	static const char* TypeName() { return "text..."; }
	static bool Parse( const FString* Tokens, int32 NumAvailable, FConsoleRest& Out, FString& )
	{
		Out.Text.Empty();
		for ( int32 i = 0; i < NumAvailable; ++i )
		{
			if ( i > 0 ) Out.Text += " ";
			Out.Text += Tokens[i];
		}
		return true;
	}
	static void GetChoices( TArray<FString>& ) {}
	static FString ToText( const FConsoleRest& Value ) { return Value.Text; }
};

template<>
struct TConsoleArg<FVector3>
{
	static constexpr int32 NumTokens = 3;
	static const char* TypeName() { return "vec3"; }
	static bool Parse( const FString* Tokens, int32, FVector3& Out, FString& Error )
	{
		float64 Values[3] = {};
		for ( int32 i = 0; i < 3; ++i )
		{
			if ( !NConsole::ParseFloat64( Tokens[i], Values[i] ) )
			{
				Error = "expected three numbers (x y z), got '" + Tokens[i] + "'";
				return false;
			}
		}
		Out = FVector3( static_cast<float32>( Values[0] ), static_cast<float32>( Values[1] ), static_cast<float32>( Values[2] ) );
		return true;
	}
	static void GetChoices( TArray<FString>& ) {}
	static FString ToText( const FVector3& Value )
	{
		return NConsole::FormatFloat( Value.x ) + " " + NConsole::FormatFloat( Value.y ) + " " + NConsole::FormatFloat( Value.z );
	}
};

// 省略できる引数（引数の数・型の表示などは、中の型と同じ）
template<typename T>
struct TConsoleArg<TOptional<T>> : TConsoleArg<T>
{
	static constexpr bool bOptional = true;
};
