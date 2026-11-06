// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/AI/Registry/AIParts.h"

//=============================================================================
// CAIRegistry
//   AI の部品を、名前で登録する場所。ゲーム側が起動時に 1 回、登録の関数を呼ぶ（NPhysicsWorld::RegisterSpawners と同じ形）。
//   JSON のアセットは名前で部品を指し、実行時にこのレジストリから作る。エディタは、一覧とスキーマから Palette と Inspector を作る。
//
//   // ゲーム側（起動時）
//   NAI::RegisterStateAction<FAction_StopMoving>( "StopMoving" );
//   NAI::RegisterStateAction<FAction_SetCapsuleHeight>( "SetCapsuleHeight",
//       FParamSchema().Float( "Height", 1.4f ).Range( 0.2f, 4.0f ).Hint( "カプセルの高さ" ), "Movement" );
//   NAI::RegisterCondition<FCondition_IsGrounded>( "IsGrounded" );
//   NAI::GetRegistry().AddAlias( "Stop", "StopMoving" );          // 古い名前の JSON も読める
//
//   - 名前の重複は、後から登録したものに置き換えて警告する
//   - 別名（AddAlias）は、検索のときに本名へ引き直す
//   - 単体の確認用に、グローバルとは別の CAIRegistry を作って使ってもよい
//=============================================================================
class CAIRegistry
{
public:
	// 登録された部品の情報（スキーマと、作り方）
	template<typename TBase>
	struct TEntry
	{
		FString Name;
		FString Category;
		FString Description;
		FParamSchema Schema;
		TUniquePtr<TBase> ( *Create )() = nullptr;
	};
	using FStateActionEntry = TEntry<FAIStateAction>;
	using FConditionEntry = TEntry<FAICondition>;

	template<typename T>
	void RegisterStateAction( const char* Name, const FParamSchema& Schema = FParamSchema(), const char* Category = "", const char* Description = "" )
	{
		AddEntry( StateActions, StateActionIndex, Name, Schema, Category, Description, &CreatePart<FAIStateAction, T> );
	}

	template<typename T>
	void RegisterCondition( const char* Name, const FParamSchema& Schema = FParamSchema(), const char* Category = "", const char* Description = "" )
	{
		AddEntry( Conditions, ConditionIndex, Name, Schema, Category, Description, &CreatePart<FAICondition, T> );
	}

	// 古い名前 OldName を、本名 Name へ引き直す。本名が未登録でも登録できる（検索のときに引き直す）
	void AddAlias( const char* OldName, const char* Name );

	// 名前（別名でもよい）の部品。無ければ nullptr
	const FStateActionEntry* FindStateAction( const FString& Name ) const;
	const FConditionEntry* FindCondition( const FString& Name ) const;

	// 登録順の一覧（エディタの Palette 用）
	const TArray<FStateActionEntry>& GetStateActions() const { return StateActions; }
	const TArray<FConditionEntry>& GetConditions() const { return Conditions; }

	void Clear();

private:
	template<typename TBase, typename T>
	static TUniquePtr<TBase> CreatePart()
	{
		return TUniquePtr<TBase>( new T() );
	}

	template<typename TBase>
	void AddEntry( TArray<TEntry<TBase>>& Entries, TMap<FString, int32>& Index, const char* Name, const FParamSchema& Schema, const char* Category, const char* Description, TUniquePtr<TBase> ( *Create )() )
	{
		TEntry<TBase> Entry;
		Entry.Name = Name;
		Entry.Category = Category;
		Entry.Description = Description;
		Entry.Schema = Schema;
		Entry.Create = Create;
		if ( const int32* Existing = Index.Find( Entry.Name ) )
		{
			NotifyDuplicate( Entry.Name );
			Entries[*Existing] = Entry;
			return;
		}
		Index.Add( Entry.Name, Entries.Num() );
		Entries.Add( Entry );
	}

	// 別名を本名へ引き直す
	FString ResolveName( const FString& Name ) const;
	static void NotifyDuplicate( const FString& Name );

	TArray<FStateActionEntry> StateActions;
	TMap<FString, int32> StateActionIndex;
	TArray<FConditionEntry> Conditions;
	TMap<FString, int32> ConditionIndex;
	TMap<FString, FString> Aliases;
};

namespace NAI
{
	// アプリで 1 つのレジストリ（ゲーム側が起動時に登録する）
	CAIRegistry& GetRegistry();

	template<typename T>
	void RegisterStateAction( const char* Name, const FParamSchema& Schema = FParamSchema(), const char* Category = "", const char* Description = "" )
	{
		GetRegistry().RegisterStateAction<T>( Name, Schema, Category, Description );
	}

	template<typename T>
	void RegisterCondition( const char* Name, const FParamSchema& Schema = FParamSchema(), const char* Category = "", const char* Description = "" )
	{
		GetRegistry().RegisterCondition<T>( Name, Schema, Category, Description );
	}
}
