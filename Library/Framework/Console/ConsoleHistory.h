#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"

//=============================================================================
// FConsoleHistory
//   入力した行の履歴（純粋）。上下キーでたどる・先頭が合う履歴の候補（予測変換）・保存用の読み書き。
//
//   History.Add( "fps" );                       // 実行したときに足す。空と、同じ行（古いほうが消える。最後が一番新しい）は重複しない
//   Input = History.Previous( Input );          // ↑: 古いほうへ。最初の 1 回目に、入力中の文字を退避する
//   Input = History.Next( Input );              // ↓: 新しいほうへ。一番新しいところを過ぎたら、退避した文字に戻る
//   History.FindMatches( "tel", 5 );            // 行頭が "tel" の履歴を、新しい順に
//=============================================================================
class FConsoleHistory
{
public:
	explicit FConsoleHistory( int32 InMaxEntries = 200 );

	void Add( const FString& Line );
	void Clear();

	FORCEINLINE int32 Num() const { return Lines.Num(); }
	// 古い順に数えた Index 番目
	FORCEINLINE const FString& Get( int32 Index ) const { return Lines[Index]; }
	FORCEINLINE const TArray<FString>& GetAll() const { return Lines; }
	// 保存したものを読み戻す（上限を超えていたら、古いものから捨てる。重複は、あとのものが残る）
	void SetAll( const TArray<FString>& InLines );

	// たどる
	FString Previous( const FString& CurrentInput );
	FString Next( const FString& CurrentInput );
	FORCEINLINE bool IsNavigating() const { return Cursor != INDEX_NONE; }
	void ResetNavigation();

	// 行頭が Prefix（大文字小文字は区別しない）の履歴を、新しい順に最大 MaxCount 個。Prefix と同じ行は除く。Prefix が空なら何も返さない
	TArray<FString> FindMatches( const FString& Prefix, int32 MaxCount ) const;

private:
	TArray<FString> Lines;     // 古い順
	int32 MaxEntries;
	int32 Cursor = INDEX_NONE; // たどっている位置（INDEX_NONE ならたどっていない）
	FString Saved;             // たどり始めたときの入力
};
