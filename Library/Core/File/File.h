// SPDX-License-Identifier: MIT
#pragma once

#include <sal.h>
#include <cstdarg>
#include <cstdio>   // 中身は FILE*（C の標準入出力）。標準ライブラリに触れるのは、このヘッダーだけに閉じ込める
#include <share.h>  // _SH_DENYNO / _SH_DENYWR

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Types/Utility.h"

//=============================================================================
// FFile / NFile
//   ファイルを読み書きする。<fstream> の std::ofstream / std::ifstream と、<cstdio> の FILE* の代わり。
//   パスは FString（システムのマルチバイト。日本語のパスも通る）。コピーはできず、移動はできる。壊れるときに閉じる。
//
//   FFile File;
//   if ( File.Open( Path, FFile::EMode::Write ) )
//   {
//       File.WriteText( "{\n" );
//       File.Printf( "  \"count\": %d\n", Count );
//       File.WriteText( "}\n" );
//       if ( File.HasError() ) { ... }          // 書き込みの失敗（ディスクが満杯など）
//   }
//
//   TArray<uint8> Bytes;
//   if ( NFile::ReadAllBytes( Path, Bytes ) ) { ... }                  // 全部読む
//   NFile::WriteAllBytes( Path, Bytes.GetData(), Bytes.Num() );        // 新しく書く（あれば上書き）
//
//   バイナリで開くと、中身をそのまま読み書きする。テキストで開くと、改行を変換する
//   （書くときに "\n" は "\r\n" になり、読むときに "\r\n" は "\n" になる。読むときは 0x1A でファイルの終わりとみなす）。
//=============================================================================
class FFile
{
public:
	// 開き方。Write は新しく作り直し（あれば空にする）、Append は末尾に足す。
	enum class EMode : uint8
	{
		Read,
		Write,
		Append
	};

	// 開いている間、ほかのプロセス（や同じプロセスの別のハンドル）に許すこと。
	enum class EShare : uint8
	{
		// 読みも書きも許す。
		AllowAll,
		// 書き込みを許さない（読みは許す）。ログを書き続けるファイルを、ほかから書き換えられないようにする。
		DenyWrite
	};

	// 何も開いていない状態。
	FFile() = default;
	~FFile() { Close(); }

	FFile( const FFile& ) = delete;
	FFile& operator=( const FFile& ) = delete;

	FFile( FFile&& Other ) noexcept : Handle( Other.Handle )
	{
		Other.Handle = nullptr;
	}

	FFile& operator=( FFile&& Other ) noexcept
	{
		if ( this != &Other )
		{
			Close();
			Handle = Other.Handle;
			Other.Handle = nullptr;
		}
		return *this;
	}

	// ファイルを開く。開いていたものは先に閉じる。開けなければ false（Read で無いファイル、Write でフォルダーが無いなど）。
	bool Open( const FString& Path, EMode Mode, bool bText = false, EShare Share = EShare::AllowAll )
	{
		Close();
		const TArray<wchar_t> Wide = Path.ToWide();
		const wchar_t* ModeText = nullptr;
		switch ( Mode )
		{
		case EMode::Read: ModeText = bText ? L"rt" : L"rb"; break;
		case EMode::Write: ModeText = bText ? L"wt" : L"wb"; break;
		case EMode::Append: ModeText = bText ? L"at" : L"ab"; break;
		}
		Handle = ::_wfsopen( Wide.GetData(), ModeText, Share == EShare::DenyWrite ? _SH_DENYWR : _SH_DENYNO );
		return Handle != nullptr;
	}

	// 閉じる（書き込みの残りをファイルへ出す）。閉じて失敗しなければ true。開いていなければ何もせず true。
	bool Close()
	{
		if ( Handle == nullptr ) return true;
		const int Result = std::fclose( Handle );
		Handle = nullptr;
		return Result == 0;
	}

	// 開いているか。
	FORCEINLINE bool IsOpen() const noexcept { return Handle != nullptr; }

	// Buffer へ最大 Size バイト読み、読めたバイト数を返す。ファイルの終わりか失敗で、Size より少なくなる。
	usize Read( void* Buffer, usize Size ) { return Handle != nullptr ? std::fread( Buffer, 1, Size, Handle ) : 0; }

	// Data から Size バイトを書き、書けたバイト数を返す。失敗すると Size より少なくなる。
	usize Write( const void* Data, usize Size ) { return Handle != nullptr && Size > 0 ? std::fwrite( Data, 1, Size, Handle ) : 0; }

	// 文字列（終端の '\0' は含めない）を書く。全部書けたら true。
	bool WriteText( const char* Text )
	{
		const usize Length = NCString::Length( Text );
		return Write( Text, Length ) == Length;
	}

	// 書式（NCString::Format と同じ）で書く。長さに上限はない。書けたら true。
	bool Printf( _Printf_format_string_ const char* InFormat, ... )
	{
		if ( Handle == nullptr ) return false;
		va_list Args;
		va_start( Args, InFormat );
		const int Result = std::vfprintf( Handle, InFormat, Args );
		va_end( Args );
		return Result >= 0;
	}

	// 書き込みの残りを、すぐファイルへ出す。成功したら true。
	bool Flush() { return Handle != nullptr && std::fflush( Handle ) == 0; }

	// これまでに、読み書きの失敗があったか。
	bool HasError() const { return Handle != nullptr && std::ferror( Handle ) != 0; }

private:
	// 開いているファイル（FILE*）。開いていなければ nullptr。
	FILE* Handle = nullptr;
};

namespace NFile
{
	// ファイルを全部読む。開けないか、読む途中で失敗したら false（OutBytes は読めたところまで）。空のファイルは true で、OutBytes は空。
	inline bool ReadAllBytes( const FString& Path, TArray<uint8>& OutBytes, bool bText = false )
	{
		OutBytes.Reset();
		FFile File;
		if ( !File.Open( Path, FFile::EMode::Read, bText ) ) return false;
		uint8 Chunk[8192];
		for ( ;; )
		{
			const usize Read = File.Read( Chunk, sizeof( Chunk ) );
			if ( Read == 0 ) break;
			OutBytes.Append( Chunk, static_cast<int32>( Read ) );
		}
		return !File.HasError();
	}

	// Data の Size バイトを、新しいファイルとして書く（あれば上書きして短くする）。開けないか、書ききれないか、閉じて失敗したら false。
	inline bool WriteAllBytes( const FString& Path, const void* Data, usize Size, bool bText = false )
	{
		FFile File;
		if ( !File.Open( Path, FFile::EMode::Write, bText ) ) return false;
		const usize Written = File.Write( Data, Size );
		const bool bClosed = File.Close();
		return Written == Size && bClosed;
	}
}
