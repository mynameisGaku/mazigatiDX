// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"

class CShader;
class IImage;

// Surface記録に必要な完成済みの設定と資源。確定後は元の材質を参照しない。
struct FMaterialSurfaceSnapshotData
{
	TArray<uint32> BeginWords; // 場面・パラメーター・画像・シェーダーの開始設定。
	TArray<uint32> EndWords; // シェーダーと画像の解除設定。
	TArray<TSharedPtr<CShader>> Shaders; // 記録した番号を持つ完成済みのシェーダー。
	TArray<TSharedPtr<IImage>> Images; // 記録した画像・環境・影の所有参照。
};
