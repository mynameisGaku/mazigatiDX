// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"

class CAssetPathResolver;
class CAssetLoadRecorder;
class IAssetBackend;
class ITextureBackend;
class IModelBackend;
class IFontBackend;
class ISoundBackend;

//=============================================================================
// CAssetLoadContext
//   各ローダーが読み込みに使う、共通の持ち物（バックエンド・パスを解決する人・記録する人）をまとめて渡す人。
//   読み込む前の下ごしらえ（バックエンドがあるか・ファイルがあるか）も、ここで行う。
//   バックエンドは CAssetSubsystem が差し替える（検査では偽のバックエンド）ので、ローダーは作るときに文脈への参照だけを持つ。
//=============================================================================
class CAssetLoadContext
{
public:
	CAssetLoadContext( CAssetPathResolver& InPaths, CAssetLoadRecorder& InRecorder );

	void SetBackend( IAssetBackend* InBackend )
	{
		Backend = InBackend;
	}

	IAssetBackend* GetBackend() const
	{
		return Backend;
	}

	// 役割ごとの窓口（バックエンドが無ければ nullptr）。ローダーは、自分に必要な窓口だけを使う
	ITextureBackend* GetTextureBackend() const;
	IModelBackend* GetModelBackend() const;
	IFontBackend* GetFontBackend() const;
	ISoundBackend* GetSoundBackend() const;

	CAssetPathResolver& GetPaths() const
	{
		return Paths;
	}

	CAssetLoadRecorder& GetRecorder() const
	{
		return Recorder;
	}

	// 読み込む前の共通の確認（バックエンドがあるか・ファイルがあるか）。通れば実在する絶対パスを返す。
	// 通らなければ、ログ（Error）に出して、失敗を記録する。Kind はログに出す種類の名前（"world" など）
	TOptional<FString> PrepareLoad( const char* Kind, const FString& Path ) const;

private:
	IAssetBackend* Backend = nullptr;
	CAssetPathResolver& Paths;
	CAssetLoadRecorder& Recorder;
};
