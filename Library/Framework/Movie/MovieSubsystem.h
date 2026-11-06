// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Video/MovieBackend.h"
#include "Library/Core/Log/LogCategory.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Movie/Movie.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

DECLARE_LOG_CATEGORY_EXTERN( LogMovie, Log )

class CAssetSubsystem;

//=============================================================================
// CMovieSubsystem
//   ムービーを開く入口（アプリで 1 つ、自動で生成される）。再生や描画は、開いた CMovie が行う。
//
//   CMovieSubsystem* Movies = GetAppSubsystem<CMovieSubsystem>();
//   TSharedPtr<CMovie> Opening = Movies->Open( "Movies/Opening.mp4" );     // アセットのルートフォルダの下から探す
//
//   - 相対パスは、アセットのルートフォルダ（CAssetSubsystem::ResolvePath）から探す。絶対パスもそのまま開ける
//   - 開いたムービーを覚えておき（弱い参照）、終わるとき（Deinitialize）に、まだ残っているものを使えなくする
//=============================================================================
class CMovieSubsystem : public CAppSubsystem
{
public:
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;

	// 開く。見つからない・開けないときは nullptr（ログに出る）
	TSharedPtr<CMovie> Open( const FString& Path );

	// 確認用: 窓口を差し替える（nullptr で元に戻す）
	void SetBackendOverride( IMovieBackend* InBackend );

private:
	IMovieBackend* GetBackend() const { return BackendOverride != nullptr ? BackendOverride : Backend; }

	IMovieBackend* Backend = nullptr;
	IMovieBackend* BackendOverride = nullptr;
	CAssetSubsystem* Assets = nullptr;
	TArray<TWeakPtr<CMovie>> Movies;
};
