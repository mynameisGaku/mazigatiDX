#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// コンソールの出力（行の種類・出力先・行の溜め場所）
//   コマンドの出力は IConsoleOutput に書く。画面の窓は FConsoleBuffer を持って表示する。確認では FConsoleBuffer を渡して中身を調べる。
//   メインスレッドから使う（スレッドセーフではない）。
//=============================================================================
enum class EConsoleLine : uint8
{
	Info,       // 通常の出力
	Command,    // 入力したコマンドの表示（"> fps"）
	Warning,    // 注意
	Error,      // 失敗・間違い
};

class IConsoleOutput
{
public:
	virtual ~IConsoleOutput() = default;
	// Text に改行（'\n'）が入っていれば、行ごとに分けて扱ってよい
	virtual void Write( EConsoleLine Kind, const FString& Text ) = 0;
	// 溜めている出力を消す（clear コマンド用。消せない出力先は、何もしなくてよい）
	virtual void Clear() {}
};

struct FConsoleLine
{
	EConsoleLine Kind = EConsoleLine::Info;
	FString Text;
};

//-----------------------------------------------------------------------------
// FConsoleBuffer
//   出力を、行ごとに最大 MaxLines 行まで溜める（古いものから捨てる）。Write で改行ごとに行を分ける。
//-----------------------------------------------------------------------------
class FConsoleBuffer final : public IConsoleOutput
{
public:
	explicit FConsoleBuffer( int32 InMaxLines = 2000 );

	void Write( EConsoleLine Kind, const FString& Text ) override;
	void Clear() override;

	FORCEINLINE int32 Num() const { return NumLines; }
	// 古い順に数えた Index 番目
	const FConsoleLine& Get( int32 Index ) const;
	// 変更のたびに増える（画面が、再描画や自動スクロールの要否を調べる用）
	FORCEINLINE uint64 GetRevision() const { return Revision; }
	// すべての行を、改行でつないだ 1 つの文字列にする（コピー用）
	FString ToText() const;

private:
	TArray<FConsoleLine> Lines;   // リングバッファ
	int32 MaxLines;
	int32 Head = 0;               // 最も古い行の位置
	int32 NumLines = 0;
	uint64 Revision = 0;
};
