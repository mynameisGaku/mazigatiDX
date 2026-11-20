// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/TypeDefinition.h"

class CModelInstance;
class IStaticModelSource;
struct FDrawCommandView;
struct FDrawSpan;
struct FStaticModelDrawView;

// 評価済みのモデルの姿勢・材質範囲・不透明度を保持する共有値。
class FModelPoseSnapshot
{
public:
	// ゲームスレッドで現在の姿勢を捕捉する。所有付きビューのない実行先では無効値。
	static FModelPoseSnapshot Capture( const CModelInstance& Instance );
	// 明示したモデルの所有先を使う。所有先はこの値より長く生存させる。
	static FModelPoseSnapshot Capture( IStaticModelSource& Source, int32 Handle );
	// 姿勢を固定できたかを返す。非表示の形状も有効。
	bool IsValid() const { return Data.IsValid(); }
	// 命令に付いた形状を借りる。番号の所有先を指定した場合は同一性も確認する。
	static const FStaticModelDrawView* Resolve( const FDrawSpan& Span, const FDrawCommandView& Command, const IStaticModelSource* ExpectedSource = nullptr );

private:
	TSharedPtr<const FStaticModelDrawView> Data; // 頂点・添字・材質範囲の所有を共有するビュー。
	const IStaticModelSource* Owner = nullptr; // 画像の番号を発行したバックエンド。
};
