// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NBackendSupport
//   バックエンドが「この機能には対応していない」ことを、ログに出す。
//   他のバックエンドではできるのに、このバックエンド（DxLib など）ではできない機能を、黙って無視せずに、利用者が気づけるようにするための窓口。
//   状態を持たない小さな共有の道具なので、名前空間（NMath や NFile と同じ扱い）。
//
//   // 実装の中で、できない入力を受けたとき:
//   NBackendSupport::ReportUnsupported( "DxLib", "Sound with more than 2 channels", "DxLib soft sounds have only 1 or 2 channels" );
//
//   - 同じ (バックエンド名, 機能) の組は、最初の 1 回だけ警告に出す（毎フレーム呼ばれても、ログを埋めない）
//   - できないことは、実装の関数の近くに、簡潔なコメント（「<バックエンド名> 未対応: 理由」）でも書いておく
//   - メインスレッドから呼ぶ
//=============================================================================
namespace NBackendSupport
{
	// BackendName のバックエンドが Feature に対応していないことを、警告のログに 1 回だけ出す。Reason は理由（省略できる）。
	// 出したら true、すでに出していたら false
	bool ReportUnsupported( const char* BackendName, const char* Feature, const char* Reason = nullptr );

	// 出したことがある組の数（確認用）
	int32 GetNumReported();

	// 「出したことがある」記録を消す（確認用）
	void ResetReported();
}
