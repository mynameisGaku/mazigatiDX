// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeTraits.h"
#include "Library/Framework/Scene/SceneManager.h"
#include "Source/Catalog/SampleInfo.h"

//=============================================================================
// CSampleCatalog
//   サンプルの一覧。各サンプルが、自分の .cpp の中で、静的な初期化として登録する（TSampleRegistrar）。
//   サンプルを足すときに、アプリケーションや一覧の画面のコードを書き換えなくてよいようにするため。
//
//   // Hello/HelloSample.cpp
//   namespace
//   {
//       const TSampleRegistrar<CHelloSample> Registrar( FSampleInfo { "Hello", "Hello", "描画の基本", 10 } );
//   }
//
//   // アプリケーション（起動時）
//   CSampleCatalog::Get().RegisterAll( GetSceneManager() );
//
//   登録できない名前（空・使えない文字・"Browser"・重なり）は、登録されずに、理由が GetRegistrationErrors() に残る。
//   （静的な初期化の中では、ログがまだ使えないことがあるので、理由は起動のときにアプリケーションがログへ出す）
//=============================================================================
class CSampleCatalog
{
public:
	// 一覧の画面のシーンの名前（サンプルの名前には使えない）
	static const char* GetBrowserSceneName() { return "Browser"; }

	// サンプルの名前として使えるか（英字で始まり、英数字と _ だけ）
	static bool IsValidName( const FString& Name );

	// 1 つだけある一覧
	static CSampleCatalog& Get();

	// TScene のシーンを、Info の Name で登録する。登録できたら true
	template<typename TScene>
	bool Add( const FSampleInfo& Info )
	{
		static_assert( bIsBaseOf<CScene, TScene>, "Add<T>: T must derive from CScene" );
		FSampleInfo Entry = Info;
		Entry.RegisterScene = []( CSceneManager& Scenes, const FString& Name ) { Scenes.RegisterScene<TScene>( Name ); };
		return AddEntry( Entry );
	}

	// 登録されているサンプルの数
	FORCEINLINE int32 Num() const { return Samples.Num(); }

	// 一覧に出す順（Order の小さい順、同じなら Name の順）に並べたもの
	TArray<FSampleInfo> GetSorted() const;

	// Name のサンプル。無ければ nullptr
	const FSampleInfo* Find( const FString& Name ) const;

	// すべてのサンプルのシーンを、シーンマネージャーへ登録する
	void RegisterAll( CSceneManager& Scenes ) const;

	// 登録できなかった理由（1 件ずつ）
	FORCEINLINE const TArray<FString>& GetRegistrationErrors() const { return Errors; }

	// 一覧の画面で最後に選んでいたサンプルの名前（サンプルから戻ったときに、同じところを選ぶため）
	FORCEINLINE const FString& GetLastSelected() const { return LastSelected; }
	FORCEINLINE void SetLastSelected( const FString& Name ) { LastSelected = Name; }

private:
	bool AddEntry( const FSampleInfo& Entry );

	TArray<FSampleInfo> Samples;
	TArray<FString> Errors;
	FString LastSelected;
};

//=============================================================================
// TSampleRegistrar
//   静的な変数として置くと、起動前に、TScene を一覧へ登録する。
//=============================================================================
template<typename TScene>
class TSampleRegistrar
{
public:
	explicit TSampleRegistrar( const FSampleInfo& Info ) { CSampleCatalog::Get().Add<TScene>( Info ); }
};
