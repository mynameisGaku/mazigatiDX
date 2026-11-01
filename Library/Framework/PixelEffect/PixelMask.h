// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/ArrayView.h"
#include "Library/Framework/PixelEffect/PixelEffectTypes.h"

//=============================================================================
// NPixelMask
//   文字の行の並び（'.' と ' ' は空、それ以外は塗り）から、シルエット（FPixelMask）を作る。
//   行の長さは、1 行目に合わせる（長い行は切り、短い行の残りは空）。Scale 倍に拡大する（1 未満は 1）。
//
//   const char* const Rows[] = { ".#.", "###", ".#." };
//   const FPixelMask Plus = NPixelMask::FromRows( Rows );
//=============================================================================
namespace NPixelMask
{
	FPixelMask FromRows( TArrayView<const char* const> Rows, int32 Scale = 1 );
}
