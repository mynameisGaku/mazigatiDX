// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"

//=============================================================================
// CAssetPathResolver
//   アセットのパスを解決する人。ルートフォルダ（既定は "Assets"）を覚え、アセットのパスを実在するファイルの絶対パスにし、
//   保存先のパス（フォルダも作る）を用意する。読み込みや保存そのものはしない（それは各ローダーの仕事）。
//
//   CAssetPathResolver Paths;
//   Paths.SetRootDirectory( "Assets" );
//   TOptional<FString> Full = Paths.ResolvePath( "Textures/Hero.png" );
//
//   - ルートの相対パスは、作業ディレクトリ・exe のフォルダ・その親から探す（見つからなければ、指定の名前のまま）
//   - ルートに無ければ、作業ディレクトリ・exe のフォルダ（と、その親）も探す。絶対パスはそのまま
//
//   エンジン組み込みの素材（EngineContent フォルダ。UI の基本画像・既定のマテリアルなど。読み取り専用）:
//     "Engine:/UI/Window.png" のように、接頭辞 "Engine:/" を付けたパスは、ユーザーのルート（Assets）ではなく、
//     EngineContent フォルダの下から探す。保存はできない（PrepareSavePath は空を返す）
//=============================================================================
class CAssetPathResolver
{
public:
	// アセットのルートフォルダ
	void SetRootDirectory( const FString& Directory );
	FString GetRootDirectory() const;

	// エンジン組み込みの素材のフォルダ（既定は "EngineContent"）。読み取り専用。
	// 作業フォルダ・実行ファイルの近く（親のフォルダも）から探す。無ければ、場所を書いたファイル（"EngineContent.location"。フォルダへのパス 1 行。
	// 絶対パスか、そのファイルのあるフォルダからの相対パス）を、同じ探し方で探して、そこを使う（フレームワークとは別の場所に作ったゲームのプロジェクト用）
	void SetEngineContentDirectory( const FString& Directory );
	FString GetEngineContentDirectory() const;

	// "Engine:/" で始まるパスか。始まるなら、その後ろの相対パスを OutRelative に返す（先頭の '/' は除く）
	static bool IsEnginePath( const FString& Path, FString* OutRelative = nullptr );

	// アセットのパスを、実在するファイルの絶対パスにする。見つからなければ空
	TOptional<FString> ResolvePath( const FString& Path ) const;

	// 保存先の絶対パス（相対ならルートフォルダの下）。途中のフォルダも作る。作れなければ空
	TOptional<FString> PrepareSavePath( const FString& Path ) const;

	// キャッシュのキー。Windows のパスは大文字小文字を区別せず、区切りも '\' と '/' の両方があるので、同じファイルは同じキーにする
	static FString MakeKey( const FString& FullPath );

private:
	FString RootDirectoryName = "Assets";
	mutable TOptional<FString> ResolvedRoot;
	FString EngineContentDirectoryName = "EngineContent";
	mutable TOptional<FString> ResolvedEngineContent;
};
