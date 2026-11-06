// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Json/JsonObject.h"

class CAssetSubsystem;

//=============================================================================
// アセットの参照の検索・改名・削除
//   データアセット（.gxsprite / .gxmodelanim / .gxworld / .gxeffect / .gxblackboard / .gxstate / .gxbt / .gxparts）は、別のアセットを、アセットのルートからの相対パスの
//   文字列で指している（ワールドのモデル、ビヘイビアツリーのブラックボード、エフェクトの画像など）。このファイルの関数は、定義の型には依存せず、JSON の中の
//   「パスと同じ文字列」を探す・置き換える（区切りと大文字小文字の違いは、同じパスとして扱う）。
//
//   - FindReferences : そのパスを指しているファイルと、箇所の数
//   - RenameAsset    : ファイルを移し（フォルダは作る）、それを指しているファイルの文字列を、新しいパスへ書き換える。拡張子は変えられない
//   - DeleteAsset    : ファイルを消す（参照は直さない。呼ぶ前に FindReferences で確かめる）
//
//   読み込み済みのアセットのキャッシュは更新しない。開いているエディタは、開き直す（改名・削除したファイル自体が、エディタで開かれていないことを確かめてから呼ぶ）。
//   ファイルを読み書きする。ゲームスレッドから呼ぶ。
//=============================================================================
struct FAssetReference
{
	FString FilePath;                     // 指しているファイル（アセットのルートからの相対。区切りは /）
	int32 Count = 0;                      // 指している箇所の数
};

struct FAssetRenameResult
{
	bool bSuccess = false;
	FString Error;                        // 失敗の理由（成功なら空）
	TArray<FAssetReference> Updated;      // 書き換えたファイル（文字列を書き換えた数つき）
};

namespace NAssetReferences
{
	// JSON の中の、文字列の値が、NormalizedTarget（NWorld::NormalizeAssetPath した形）と同じパスである箇所の数
	int32 CountPathStrings( const NlohmannJson& Json, const FString& NormalizedTarget );
	// 同じ箇所を NewPath に置き換えて、置き換えた数を返す（NewPath の区切りは / にそろえる）
	int32 ReplacePathStrings( NlohmannJson& Json, const FString& NormalizedTarget, const FString& NewPath );

	// AssetPath を指しているファイル（AssetPath 自身は除く）を、パスの順に返す。パスが空なら、空
	TArray<FAssetReference> FindReferences( CAssetSubsystem& Assets, const FString& AssetPath );

	// OldPath を NewPath に改名して、参照を書き換える。失敗したときは、何も変えない（書き換えに失敗した場合は、書き換え済みのファイルも元のパスへ戻し、ファイルも移し戻す）
	//   失敗する場合: パスが空・ルートの外（絶対パス・..）・拡張子が違う・OldPath が無い・NewPath が既にある・移せない
	FAssetRenameResult RenameAsset( CAssetSubsystem& Assets, const FString& OldPath, const FString& NewPath );

	// ファイルを消す。無い・消せないときは false（理由は OutError）
	bool DeleteAsset( CAssetSubsystem& Assets, const FString& AssetPath, FString* OutError = nullptr );
}
