// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// バックエンドを、プリプロセッサ（#if）で切り分けるためのマクロ。
//
//   バックエンド（DxLib / DX12 / Null）は、ふつう実行時に --backend=... で選ぶ（NBackendInfo で調べる）。
//   ビルドの設定で「このビルドは、この実装だけ」と決めると、#if で、バックエンドごとのコードを切り分けられる。
//
//   ■ ビルドで決める（プロジェクトのプロパティ。Build/mazigatiDX.props が、コンパイラーの /D へ変えて、全プロジェクトに渡す）
//        /p:MazigatiBackend=dx12            このビルドは DX12 に固定（dxlib / dx12 / null）。指定なしは、実行時に選ぶ
//        /p:MazigatiWithoutDxLib=true       DxLib を組み込まない（Library/Platform/DxLib をコンパイルしない）
//        /p:MazigatiWithoutDX12=true        DX12 を組み込まない
//      フレームワークとゲームは、同じ設定でビルドする（props を共有していれば、そろう）。
//      固定したビルドでは、--backend の指定は無視する（NBackendSelection::GetRequested が、固定した実装を返す）。
//
//   ■ 使う側（#if で切り分ける）
//        #if MAZIGATI_BACKEND_IS_DX12            // DX12 に固定したビルドだけ
//        #if MAZIGATI_HAS_BACKEND_DX12           // DX12 の実装が組み込まれている（実行時に選べる、または固定）
//        #if MAZIGATI_BACKEND == MAZIGATI_BACKEND_DXLIB
//        #if MAZIGATI_BACKEND_IS_FIXED           // どれかに固定したビルド
//      実行時に選ぶビルド（既定）では、MAZIGATI_BACKEND_IS_* は、すべて 0（どれが動くかは、実行するまで分からない）。
//      実行時に分けるなら NBackendInfo（Library/Framework/Engine/BackendInfo.h）。
//
//   分けるのは、窓口（IPlatform・IDrawExecutor など）で埋められない違いに限る。理由は、コメントに残す。
//=============================================================================

// バックエンドの番号（MAZIGATI_BACKEND の値）
#define MAZIGATI_BACKEND_RUNTIME 0 // 固定しない（実行時に選ぶ）
#define MAZIGATI_BACKEND_DXLIB 1
#define MAZIGATI_BACKEND_DX12 2
#define MAZIGATI_BACKEND_NULL 3

#ifndef MAZIGATI_BACKEND
#define MAZIGATI_BACKEND MAZIGATI_BACKEND_RUNTIME
#endif

#if MAZIGATI_BACKEND < 0 || MAZIGATI_BACKEND > 3
#error "MAZIGATI_BACKEND must be 0 (runtime), 1 (DxLib), 2 (DX12) or 3 (Null). Use /p:MazigatiBackend=dxlib|dx12|null."
#endif

// どれかに固定したビルドか
#if MAZIGATI_BACKEND == MAZIGATI_BACKEND_RUNTIME
#define MAZIGATI_BACKEND_IS_FIXED 0
#else
#define MAZIGATI_BACKEND_IS_FIXED 1
#endif

// 固定したビルドで、その実装か（固定しないビルドは、すべて 0）
#define MAZIGATI_BACKEND_IS_DXLIB ( MAZIGATI_BACKEND == MAZIGATI_BACKEND_DXLIB )
#define MAZIGATI_BACKEND_IS_DX12 ( MAZIGATI_BACKEND == MAZIGATI_BACKEND_DX12 )
#define MAZIGATI_BACKEND_IS_NULL ( MAZIGATI_BACKEND == MAZIGATI_BACKEND_NULL )

// 実装が組み込まれているか（固定しないビルドは、組み込みから外していなければ 1。固定したビルドは、その実装だけ 1）
#if MAZIGATI_BACKEND == MAZIGATI_BACKEND_RUNTIME
#ifdef MAZIGATI_WITHOUT_DXLIB
#define MAZIGATI_HAS_BACKEND_DXLIB 0
#else
#define MAZIGATI_HAS_BACKEND_DXLIB 1
#endif
#ifdef MAZIGATI_WITHOUT_DX12
#define MAZIGATI_HAS_BACKEND_DX12 0
#else
#define MAZIGATI_HAS_BACKEND_DX12 1
#endif
#define MAZIGATI_HAS_BACKEND_NULL 1
#else
#define MAZIGATI_HAS_BACKEND_DXLIB MAZIGATI_BACKEND_IS_DXLIB
#define MAZIGATI_HAS_BACKEND_DX12 MAZIGATI_BACKEND_IS_DX12
#define MAZIGATI_HAS_BACKEND_NULL MAZIGATI_BACKEND_IS_NULL
#endif
