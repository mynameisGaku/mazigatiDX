#pragma once
#include <tuple>
#include <type_traits>
#include <utility>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/Pair.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Result.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Console/ConsoleArgs.h"
#include "Library/Framework/Console/ConsoleConfig.h"
#include "Library/Framework/Console/ConsoleTypes.h"

//=============================================================================
// コマンドの登録と実行（純粋。画面・エンジンに依存しない。メインスレッドから使う）
//
//   // 引数の型は、ラムダの引数から自動で決まる。FString を返せば、その文章が出力される。
//   FConsoleRegistry& Console = FConsoleRegistry::Global();
//   auto Registration = Console.Add( "teleport", "Move the player", []( float32 X, float32 Y, float32 Z ) { Player->SetPosition( { X, Y, Z } ); },
//                                    { "x", "y", "z" } );       // { 引数の名前 }（省略できる。使い方・補完の表示用）
//   // 引数の説明を付けるなら { 名前, 説明 }。説明のある引数だけが、入力中の「引数の説明」に出る（説明が無い引数は、使い方の 1 行に名前と型が出るだけ）
//   Console.Add( "give", "Give an item", []( const FString& Item, TOptional<int32> Count ) { ... },
//                { { "item", "Item id (for example sword)" }, "count" } );
//
//   // 出力に書きたいときは、最初の引数に FConsoleContext& を付ける。省略できる引数は TOptional<T>。
//   Console.Add( "spawn", "Spawn enemies", []( FConsoleContext& Context, const FString& Name, TOptional<int32> Count )
//   {
//       Context.Print( FString::Printf( "spawned %d x %s", Count.Get( 1 ), *Name ) );
//   }, { "name", "count" } );
//
//   // 戻り値: void（成功）/ bool（false で失敗）/ FString（出力する）/ TResult<void>（Err で失敗。理由を出力する）
//
//   // 変数（名前だけで今の値、名前 + 値で変更）
//   Console.AddVariable( "r.showfps", &bShowFps, "Show the FPS counter" );
//
//   // 起動時に 1 度だけ登録（Debug ビルドだけ。Release では何も残らない）
//   CONSOLE_COMMAND( "godmode", "Toggle invincibility", []( TOptional<bool> On ) { ... } );
//   CONSOLE_VARIABLE( "g.gravity", &GGravity, "World gravity" );
//
//   登録の戻り値（FConsoleRegistration）は、捨てると登録が解除される。ラムダが this などを使うなら、メンバーに持っておき、
//   持ち主が消えるときに一緒に解除されるようにする。ずっと登録したままなら Keep() を呼ぶ。
//
//   実行:  Console.Execute( "teleport 1 2 3; fps", Output );   // ';' で複数。結果は、最初の失敗（なければ Ok）
//=============================================================================
class FConsoleRegistry;
class FConsoleContext;

enum class EConsoleResult : uint8
{
	Ok,
	Empty,              // 空の行・注釈（何もしなかった）
	UnknownCommand,
	BadArguments,       // 引数の数・型が合わない
	Failed,             // コマンドが失敗を返した
};

// 登録のときに渡す、引数 1 つの名前と説明。"name" だけでも、{ "name", "説明" } でもよい
struct FConsoleArgSpec
{
	FString Name;
	FString Description;       // 空なら、入力中の「引数の説明」には出さない

	FConsoleArgSpec() = default;
	FConsoleArgSpec( const char* InName ) : Name( InName ) {}
	FConsoleArgSpec( const FString& InName ) : Name( InName ) {}
	FConsoleArgSpec( const FString& InName, const FString& InDescription ) : Name( InName ), Description( InDescription ) {}
};

// コマンドの引数 1 つの説明（使い方の表示・補完に使う）
struct FConsoleArgDesc
{
	FString Name;
	FString Description;                               // 空なら、引数の詳しい説明の一覧には出さない
	FString TypeName;
	int32 NumTokens = 1;                               // -1: 残りすべて
	bool bOptional = false;
	TArray<FString> Choices;                           // 補完の候補（固定）
	TCallable<TArray<FString>()> Completer;        // 補完の候補（その場で作る。シーンの名前など）
};

struct FConsoleCommandInfo
{
	FString Name;
	FString Description;
	FString Category;
	TArray<FConsoleArgDesc> Args;
	bool bVariable = false;
	bool bHidden = false;      // 一覧（help）に出さない

	// "teleport <x:float> <y:float> <z:float>  [count:int]"
	FString GetUsage() const;
};

// コマンドの中身が出力に書くための窓口
class FConsoleContext
{
public:
	FConsoleContext( FConsoleRegistry& InRegistry, IConsoleOutput& InOutput, const FString& InCommandName, const TArray<FString>& InArgs )
		: Registry( InRegistry ), Output( InOutput ), CommandName( InCommandName ), Args( InArgs )
	{
	}

	void Print( const FString& Text ) { Output.Write( EConsoleLine::Info, Text ); }
	void Warning( const FString& Text ) { Output.Write( EConsoleLine::Warning, Text ); }
	void Error( const FString& Text ) { Output.Write( EConsoleLine::Error, Text ); }
	// 出力先（別のコマンドを実行して、その出力をそのまま流したいとき）
	FORCEINLINE IConsoleOutput& GetOutput() const { return Output; }
	void ClearOutput() { Output.Clear(); }

	FORCEINLINE FConsoleRegistry& GetRegistry() const { return Registry; }
	FORCEINLINE const FString& GetCommandName() const { return CommandName; }
	// 引数（コマンド名を除く。引用符は外してある）。AddRaw のコマンドが、自分で読む用
	FORCEINLINE const TArray<FString>& GetArgs() const { return Args; }

private:
	FConsoleRegistry& Registry;
	IConsoleOutput& Output;
	FString CommandName;
	const TArray<FString>& Args;
};

//-----------------------------------------------------------------------------
// 補完の結果
//-----------------------------------------------------------------------------
struct FConsoleCompletion
{
	int32 ReplaceFrom = 0;           // 入力欄の、この位置から最後までを置き換える
	TArray<FString> Candidates;      // 候補（置き換える文字列そのもの。空白などを含むなら、引用符で囲んである）
	TArray<FString> Descriptions;    // 候補ごとの説明（Candidates と同じ並び。コマンドの説明・別名の中身。引数の候補は空）
	FString CommonPrefix;            // 候補すべての共通の先頭（Tab で、まずこれを入れる）
	FString Usage;                   // いま入力しているコマンドの使い方（無ければ空）
	int32 ActiveArg = INDEX_NONE;    // いま入力している引数の番号（Usage の中で強調する用。コマンド名の途中なら INDEX_NONE）
	// いま入力しているコマンド（コマンド名を打ち終えているときだけ。引数の説明の表示用。レジストリを変更するまで有効）
	const FConsoleCommandInfo* Command = nullptr;
};

//-----------------------------------------------------------------------------
// 登録の解除（デストラクタで解除する。Keep() で、解除しないことにする）
//-----------------------------------------------------------------------------
class [[nodiscard]] FConsoleRegistration
{
public:
	FConsoleRegistration() = default;
	FConsoleRegistration( const FConsoleRegistration& ) = delete;
	FConsoleRegistration& operator=( const FConsoleRegistration& ) = delete;
	FConsoleRegistration( FConsoleRegistration&& Other ) noexcept { *this = MoveTemp( Other ); }
	FConsoleRegistration& operator=( FConsoleRegistration&& Other ) noexcept
	{
		if ( this != &Other )
		{
			Unregister();
			Registry = Other.Registry;
			Id = Other.Id;
			Other.Registry = nullptr;
		}
		return *this;
	}
	~FConsoleRegistration() { Unregister(); }

	void Unregister();
	// 解除しない（プログラムが終わるまで登録したまま）
	void Keep() { Registry = nullptr; }
	FORCEINLINE bool IsRegistered() const { return Registry != nullptr; }

private:
	friend class FConsoleRegistry;
	FConsoleRegistry* Registry = nullptr;
	uint32 Id = 0;
};

namespace NConsoleDetail
{
	template<typename T> struct TArgInfo
	{
		using Arg = TConsoleArg<T>;
		static constexpr bool bOptional = false;
	};
	template<typename T> struct TArgInfo<TOptional<T>>
	{
		using Arg = TConsoleArg<T>;
		static constexpr bool bOptional = true;
	};

	// 最初の引数が FConsoleContext& なら、それは出力用（コマンドの引数ではない）
	template<typename... A> struct TSplit
	{
		static constexpr bool bHasContext = false;
		using Values = std::tuple<TDecayT<A>...>;
	};
	template<typename... A> struct TSplit<FConsoleContext&, A...>
	{
		static constexpr bool bHasContext = true;
		using Values = std::tuple<TDecayT<A>...>;
	};

	template<typename F> struct TFunctionTraits : TFunctionTraits<decltype( &F::operator() )> {};
	template<typename R, typename... A> struct TFunctionTraits<R ( * )( A... )>
	{
		using Return = R;
		using Split = TSplit<A...>;
	};
	template<typename C, typename R, typename... A> struct TFunctionTraits<R ( C::* )( A... ) const>
	{
		using Return = R;
		using Split = TSplit<A...>;
	};
	template<typename C, typename R, typename... A> struct TFunctionTraits<R ( C::* )( A... )>
	{
		using Return = R;
		using Split = TSplit<A...>;
	};

	template<typename T>
	void DescribeOne( TArray<FConsoleArgDesc>& Out, const TArray<FConsoleArgSpec>& Specs, int32 Index )
	{
		using Info = TArgInfo<T>;
		FConsoleArgDesc Desc;
		const bool bHasSpec = Specs.IsValidIndex( Index );
		Desc.Name = bHasSpec && !Specs[Index].Name.IsEmpty() ? Specs[Index].Name : FString::Printf( "arg%d", Index + 1 );
		if ( bHasSpec ) Desc.Description = Specs[Index].Description;
		Desc.TypeName = Info::Arg::TypeName();
		Desc.NumTokens = Info::Arg::NumTokens;
		Desc.bOptional = Info::bOptional;
		Info::Arg::GetChoices( Desc.Choices );
		Out.Add( Desc );
	}

	template<typename Tuple, size_t... I>
	void DescribeAll( TArray<FConsoleArgDesc>& Out, const TArray<FConsoleArgSpec>& Specs, TIndexSequence<I...> )
	{
		( DescribeOne<std::tuple_element_t<I, Tuple>>( Out, Specs, static_cast<int32>( I ) ), ... );
	}

	// 1 つの引数を読む
	template<typename T>
	bool ParseOne( T& Out, const TArray<FString>& Tokens, int32& Cursor, const FConsoleArgDesc& Desc, FString& Error )
	{
		using Arg = TConsoleArg<T>;
		const int32 Remaining = Tokens.Num() - Cursor;
		const int32 Need = Arg::NumTokens < 0 ? 0 : Arg::NumTokens;
		if ( Remaining < Need )
		{
			Error = Remaining == 0 ? "missing argument '" + Desc.Name + "'" : "argument '" + Desc.Name + "' needs " + FString::FromInt( Need ) + " values";
			return false;
		}
		FString Why;
		if ( !Arg::Parse( Tokens.GetData() + Cursor, Remaining, Out, Why ) )
		{
			Error = "argument '" + Desc.Name + "': " + Why;
			return false;
		}
		Cursor += Arg::NumTokens < 0 ? Remaining : Arg::NumTokens;
		return true;
	}
	template<typename T>
	bool ParseOne( TOptional<T>& Out, const TArray<FString>& Tokens, int32& Cursor, const FConsoleArgDesc& Desc, FString& Error )
	{
		if ( Cursor >= Tokens.Num() ) return true;   // 省略された
		T Value{};
		if ( !ParseOne( Value, Tokens, Cursor, Desc, Error ) ) return false;
		Out = Value;
		return true;
	}

	template<typename Tuple, size_t... I>
	bool ParseAll( Tuple& Values, const TArray<FString>& Tokens, int32& Cursor, const TArray<FConsoleArgDesc>& Descs, FString& Error, TIndexSequence<I...> )
	{
		return ( ParseOne( std::get<I>( Values ), Tokens, Cursor, Descs[static_cast<int32>( I )], Error ) && ... );
	}

	// 戻り値の扱い
	inline EConsoleResult HandleReturn( FConsoleContext& ) { return EConsoleResult::Ok; }
	inline EConsoleResult HandleReturn( FConsoleContext&, bool bOk ) { return bOk ? EConsoleResult::Ok : EConsoleResult::Failed; }
	inline EConsoleResult HandleReturn( FConsoleContext& Context, const FString& Text )
	{
		Context.Print( Text );
		return EConsoleResult::Ok;
	}
	inline EConsoleResult HandleReturn( FConsoleContext& Context, const TResult<void>& Result )
	{
		if ( Result.IsOk() ) return EConsoleResult::Ok;
		Context.Error( Result.UnwrapErr() );
		return EConsoleResult::Failed;
	}

	template<typename TFunction, typename Traits, typename Tuple>
	EConsoleResult Call( TFunction& Function, FConsoleContext& Context, Tuple& Values )
	{
		using Split = typename Traits::Split;
		return std::apply(
			[&]( auto&... V ) -> EConsoleResult
			{
				if constexpr ( bIsVoid<typename Traits::Return> )
				{
					if constexpr ( Split::bHasContext ) Function( Context, V... );
					else Function( V... );
					return HandleReturn( Context );
				}
				else if constexpr ( Split::bHasContext ) return HandleReturn( Context, Function( Context, V... ) );
				else return HandleReturn( Context, Function( V... ) );
			},
			Values );
	}
}

//-----------------------------------------------------------------------------
// 変数の指定
//-----------------------------------------------------------------------------
template<typename T>
struct TConsoleVariableOptions
{
	TOptional<T> Min;                                  // 数のときの、下限・上限（超えた値は、範囲に収める）
	TOptional<T> Max;
	TCallable<void( const T& )> OnChanged;         // 値を変えたあとに呼ぶ
};

//=============================================================================
// FConsoleRegistry
//=============================================================================
class FConsoleRegistry
{
public:
	using FHandler = TCallable<EConsoleResult( FConsoleContext&, const TArray<FString>&, FString& )>;   // 最後は、引数の間違いの理由

	// アプリ全体の登録先（CONSOLE_COMMAND の登録先でもある）
	static FConsoleRegistry& Global();

	//-------------------------------------------------------------------------
	// 登録（同じ名前（大文字小文字は区別しない）を登録し直すと、置き換わる）
	//-------------------------------------------------------------------------
	// ラムダの引数から、引数の型・数が決まる。Args は、引数の名前（と説明）。使い方の表示・補完用で、足りない分は arg1, arg2 ... になる。
	// 説明（Description）は、コマンドにはかならず付けること（help・補完に出る）。引数の説明は、付けたものだけが出る
	template<typename TFunction>
	FConsoleRegistration Add( const FString& Name, const FString& Description, TFunction&& Function, const TArray<FConsoleArgSpec>& Args = TArray<FConsoleArgSpec>(), const FString& Category = FString() )
	{
		using Traits = NConsoleDetail::TFunctionTraits<TDecayT<TFunction>>;
		using Values = typename Traits::Split::Values;

		FEntry Entry;
		Entry.Info.Name = Name;
		Entry.Info.Description = Description;
		Entry.Info.Category = Category;
		NConsoleDetail::DescribeAll<Values>( Entry.Info.Args, Args, TMakeIndexSequence<std::tuple_size_v<Values>>() );
		const TArray<FConsoleArgDesc> Descs = Entry.Info.Args;

		Entry.Handler = MakeShared<FHandler>( [Function = TDecayT<TFunction>( Forward<TFunction>( Function ) ), Descs]( FConsoleContext& Context, const TArray<FString>& Tokens, FString& OutError ) mutable -> EConsoleResult
		{
			Values Parsed{};
			int32 Cursor = 0;
			if ( !NConsoleDetail::ParseAll( Parsed, Tokens, Cursor, Descs, OutError, TMakeIndexSequence<std::tuple_size_v<Values>>() ) ) return EConsoleResult::BadArguments;
			if ( Cursor < Tokens.Num() )
			{
				OutError = Descs.IsEmpty() ? FString( "this command takes no arguments" ) : "too many arguments (unexpected '" + Tokens[Cursor] + "')";
				return EConsoleResult::BadArguments;
			}
			return NConsoleDetail::Call<TDecayT<decltype( Function )>, Traits>( Function, Context, Parsed );
		} );
		return Register( MoveTemp( Entry ) );
	}

	// 自分で引数を読むコマンド（Context.GetArgs()）。Args は、使い方の表示・補完用（空なら自由な形）
	FConsoleRegistration AddRaw( const FString& Name, const FString& Description, TCallable<EConsoleResult( FConsoleContext& )> Function, const TArray<FConsoleArgDesc>& Args = TArray<FConsoleArgDesc>(),
								 const FString& Category = FString() );

	// 変数。名前だけで今の値を出し、名前 + 値で変える。*Variable は、登録が解除されるまで有効であること
	template<typename T>
	FConsoleRegistration AddVariable( const FString& Name, T* Variable, const FString& Description, const TConsoleVariableOptions<T>& Options = TConsoleVariableOptions<T>(), const FString& Category = FString( "Variables" ) )
	{
		return AddVariable<T>( Name, [Variable]() { return *Variable; }, [Variable]( const T& Value ) { *Variable = Value; }, Description, Options, Category );
	}

	// 値の取り出し・設定を関数で行う変数（ウィンドウの大きさなど、変数として持っていないもの）
	template<typename T>
	FConsoleRegistration AddVariable( const FString& Name, TCallable<T()> Getter, TCallable<void( const T& )> Setter, const FString& Description,
									  const TConsoleVariableOptions<T>& Options = TConsoleVariableOptions<T>(), const FString& Category = FString( "Variables" ) )
	{
		const T DefaultValue = Getter();
		FConsoleRegistration Registration = Add(
			Name, Description,
			[Name, Getter, Setter, Options]( FConsoleContext& Context, TOptional<T> NewValue )
			{
				if ( !NewValue.IsSet() )
				{
					Context.Print( Name + " = " + TConsoleArg<T>::ToText( Getter() ) );
					return;
				}
				T Value = NewValue.GetValue();
				if constexpr ( bIsArithmetic<T> )
				{
					if ( Options.Min.IsSet() && Value < Options.Min.GetValue() ) Value = Options.Min.GetValue();
					if ( Options.Max.IsSet() && Value > Options.Max.GetValue() ) Value = Options.Max.GetValue();
				}
				Setter( Value );
				Context.Print( Name + " = " + TConsoleArg<T>::ToText( Getter() ) );
				if ( Options.OnChanged ) Options.OnChanged( Value );
			},
			TArray<FConsoleArgSpec> { "value" }, Category );
		MarkVariable( Name, [Getter]() { return TConsoleArg<T>::ToText( Getter() ); }, [Setter, DefaultValue]() { Setter( DefaultValue ); } );
		return Registration;
	}

	// 登録の解除。無ければ false
	bool Remove( const FString& Name );

	//-------------------------------------------------------------------------
	// 別名（alias）: 別名 -> コマンドの行。別名の後ろに書いた引数は、行の後ろに足される。コマンドと同じ名前の別名は作れない
	//-------------------------------------------------------------------------
	bool SetAlias( const FString& Alias, const FString& Line, FString& OutError );
	bool RemoveAlias( const FString& Alias );
	// Alias の行（無ければ nullptr）
	const FString* FindAlias( const FString& Alias ) const;
	TArray<TPair<FString, FString>> GetAliases() const;

	//-------------------------------------------------------------------------
	// 実行
	//-------------------------------------------------------------------------
	// 1 行（';' で複数のコマンド）を実行する。入力した行そのものの表示（"> ..."）は、呼ぶ側が行う。
	// 間違いは、Output に Error の行として出る。結果は、最初に失敗したコマンドのもの（全部成功なら Ok、何も実行しなければ Empty）
	EConsoleResult Execute( const FString& Line, IConsoleOutput& Output );

	//-------------------------------------------------------------------------
	// 問い合わせ
	//-------------------------------------------------------------------------
	const FConsoleCommandInfo* Find( const FString& Name ) const;
	bool Exists( const FString& Name ) const { return Find( Name ) != nullptr; }
	// 一覧（カテゴリ・名前の順。bIncludeHidden が false なら、隠したものは除く）
	TArray<const FConsoleCommandInfo*> GetCommands( bool bIncludeHidden = false ) const;
	FORCEINLINE int32 GetNumCommands() const { return Entries.Num(); }
	// 変数の今の値の表示 / 登録したときの値に戻す。変数でなければ false
	bool GetVariableText( const FString& Name, FString& OutText ) const;
	bool ResetVariable( const FString& Name );

	// 引数の補完の候補を、あとから指定する（ArgIndex は、コマンド名を除いた引数の番号）
	bool SetArgChoices( const FString& Name, int32 ArgIndex, const TArray<FString>& Choices );
	bool SetArgCompleter( const FString& Name, int32 ArgIndex, TCallable<TArray<FString>()> Completer );
	// コマンドを一覧から隠す / 説明の分類を変える
	bool SetHidden( const FString& Name, bool bHidden );

	// 入力中の行の補完
	FConsoleCompletion Complete( const FString& Line ) const;

private:
	friend class FConsoleRegistration;

	struct FEntry
	{
		FConsoleCommandInfo Info;
		TSharedPtr<FHandler> Handler;    // 実行中にコマンドが増減しても、実行中の関数が消えないように、共有で持つ
		uint32 Id = 0;
		TCallable<FString()> GetText;     // 変数の今の値
		TCallable<void()> Reset;          // 変数を登録時の値に戻す
	};

	FConsoleRegistration Register( FEntry&& Entry );
	void MarkVariable( const FString& Name, TCallable<FString()> GetText, TCallable<void()> Reset );
	void UnregisterById( uint32 Id );
	EConsoleResult ExecuteSegment( const FString& Segment, IConsoleOutput& Output, int32 Depth );
	static FString MakeKey( const FString& Name ) { return Name.ToLower(); }
	FString SuggestNames( const FString& Name ) const;

	TMap<FString, FEntry> Entries;                 // キーは小文字の名前
	TMap<FString, TPair<FString, FString>> Aliases;   // 小文字の名前 -> { 元の名前, 行 }
	uint32 NextId = 1;
};

//-----------------------------------------------------------------------------
// 起動時の登録（CONSOLE_COMMAND / CONSOLE_VARIABLE の下請け）
//-----------------------------------------------------------------------------
namespace NConsole
{
	struct FStaticRegistrar
	{
		explicit FStaticRegistrar( const TCallable<void()>& Action ) { Action(); }
	};
}

#define COG_CONSOLE_CONCAT_INNER( A, B ) A##B
#define COG_CONSOLE_CONCAT( A, B ) COG_CONSOLE_CONCAT_INNER( A, B )

#if COG_DEBUG_CONSOLE
	// CONSOLE_COMMAND( "名前", "説明", ラムダ [, { 引数の名前 } [, "分類"]] )
	#define CONSOLE_COMMAND( Name, Description, ... ) \
		static const NConsole::FStaticRegistrar COG_CONSOLE_CONCAT( GConsoleCommand_, __COUNTER__ )( [] { FConsoleRegistry::Global().Add( Name, Description, __VA_ARGS__ ).Keep(); } )
	// CONSOLE_VARIABLE( "名前", &変数, "説明" )   変数は、プログラムが終わるまで有効なもの（グローバル・static）。範囲などの指定は、AddVariable を直接呼ぶ
	#define CONSOLE_VARIABLE( Name, VariablePointer, Description ) \
		static const NConsole::FStaticRegistrar COG_CONSOLE_CONCAT( GConsoleVariable_, __COUNTER__ )( [] { FConsoleRegistry::Global().AddVariable( Name, VariablePointer, Description ).Keep(); } )
#else
	#define CONSOLE_COMMAND( ... ) static_assert( true, "" )
	#define CONSOLE_VARIABLE( ... ) static_assert( true, "" )
#endif
