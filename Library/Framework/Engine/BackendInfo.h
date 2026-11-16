// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/BackendMacros.h"
#include "Library/Platform/BackendSelection.h"

//=============================================================================
// NBackendInfo
//   いま動いている実装（バックエンド）を、ゲームやフレームワークのコードから調べる窓口。
//   起動の引数（--backend=dx12 / null。指定なしは DxLib）で決まり、実行中は変わらない。いつでも（起動の直後・サブシステムの初期化の前でも）呼べる。
//   ビルドで実装を固定したとき（/p:MazigatiBackend=dx12 など）は、その実装（--backend は無視）。
//   プリプロセッサ（#if）で切り分けたいときは、Library/Core/Types/BackendMacros.h のマクロ（MAZIGATI_BACKEND_IS_DX12 など）を使う。
//
//   基本は、バックエンドを意識しないで書く（IPlatform などの窓口を使う）。どうしてもバックエンドごとの処理が要るときだけ、これで分ける:
//
//     if ( NBackendInfo::Is( EPlatformBackend::DX12 ) ) { ... }       // 特定の実装
//     if ( NBackendInfo::IsDxLib() ) { ... }                          // 短い書き方（IsDX12 / IsNull もある）
//     LOG( LogGame, Log, "backend: %s", *NBackendInfo::GetName() );   // 名前（"DxLib" / "DX12" / "Null"）
//
//   分けるのは、窓口で埋められない違いに限る。分けた理由は、コメントに残す（台帳 Docs/BackendCompatibility.csv の項目と対にすると探しやすい）。
//   機能が使えるかの確認は、バックエンドの名前ではなく、能力の窓口（IDrawExecutor::Supports... など）を先に使う。
//=============================================================================
namespace NBackendInfo
{
	// 動いているバックエンド
	EPlatformBackend Get();
	// 名前（"DxLib" / "Null" / "DX12"）
	FString GetName();
	// 名前から（大文字小文字は区別しない）。知らない名前は false
	bool TryParse( const FString& Name, EPlatformBackend& Out );

	inline bool Is( EPlatformBackend Backend ) { return Get() == Backend; }
	inline bool IsDxLib() { return Get() == EPlatformBackend::DxLib; }
	inline bool IsDX12() { return Get() == EPlatformBackend::DX12; }
	inline bool IsNull() { return Get() == EPlatformBackend::Null; }
}
