#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NFontFile
//   フォントファイル（.ttf / .otf / .ttc）から、フォント名（ファミリー名）を読み取る。
//   ファイルを「アプリの間だけ使えるフォント」として登録しても、使うにはフォント名を指定する必要があるため。
//   DxLib や Windows には依存しない。
//
//   FString Face;
//   if ( NFontFile::ReadFaceName( "Fonts/NotoSansJP.ttf", Face ) ) ...   // "Noto Sans JP"
//=============================================================================
namespace NFontFile
{
	// ファイルから読む。読めなければ false
	bool ReadFaceName( const FString& Path, FString& OutFaceName );

	// ファイルの中身から読む（.ttc は最初のフォント）。英語の名前があればそれを、無ければ見つかった名前を返す
	bool ReadFaceNameFromMemory( const uint8* Data, usize Size, FString& OutFaceName );
}
