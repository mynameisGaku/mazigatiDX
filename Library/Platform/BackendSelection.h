// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/BackendMacros.h"

// 起動時に選ぶ実装。指定なし・未知の値は従来どおりDxLib。
enum class EPlatformBackend : uint8
{
	DxLib, // 従来の実装。
	Null,  // 画面を持たない検査用。
	DX12,  // WindowsのDirect3D 12。
};

namespace NBackendSelection
{
	// 実行中のコマンドラインから実装を選ぶ。ビルドで実装を固定している（MAZIGATI_BACKEND。Library/Core/Types/BackendMacros.h）ときは、固定した実装
	EPlatformBackend GetRequested();
	// 渡したコマンドラインを解析する。大文字小文字は区別しない。
	EPlatformBackend GetRequested( const FString& CommandLine );
	// Null実装を選んだ場合だけtrue。
	bool IsNullRequested();
	// 渡したコマンドラインでNull実装を調べる。
	bool IsNullRequested( const FString& CommandLine );
}
