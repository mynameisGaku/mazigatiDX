// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// ImGui へプラットフォームの画像を表示する窓口
//   画像のハンドル（IAssetBackend が返したもの）を、ImGui::Image / AddImage に渡せる ImTextureID にする。
//   DxLib を読まない。実装は、プラットフォーム側（Library/Platform/DxLib/DxLibImGuiTexture.h など）が
//   SetImGuiTextureCallbacks で差し込む。差し込まれていない間（--backend=null や、初期化の前・終了のあと）は、
//   GetImGuiTexture は 0 を返し、ほかは何もしない。
//=============================================================================
namespace NDebugUI
{
	// プラットフォームの実装が差し込む関数の一覧
	struct FImGuiTextureCallbacks
	{
		ImTextureID ( *GetTexture )( int32 GraphHandle ) = nullptr;
		void ( *TickCache )() = nullptr;
		void ( *ClearCache )() = nullptr;
		int32 ( *GetCacheSize )() = nullptr;
		void ( *BeginNearest )( ImDrawList* DrawList ) = nullptr;
		void ( *EndNearest )( ImDrawList* DrawList ) = nullptr;
	};

	// プラットフォームの初期化・終了で、関数の一覧を差し込む／外す
	void SetImGuiTextureCallbacks( const FImGuiTextureCallbacks& Callbacks );

	ImTextureID GetImGuiTexture( int32 GraphHandle );

	// 毎フレーム 1 回呼ぶ。しばらく使われていないキャッシュを解放する
	void TickImGuiTextureCache();
	void ClearImGuiTextureCache();
	// いまキャッシュされている数（確認用）
	int32 GetImGuiTextureCacheSize();

	//-------------------------------------------------------------------------
	// 拡大しても、ぼかさずにドットを見せる（ピクセルアートの確認用）
	//   BeginNearest と EndNearest の間で AddImage した画像は、最近傍で補間される
	//-------------------------------------------------------------------------
	void BeginNearest( ImDrawList* DrawList );
	void EndNearest( ImDrawList* DrawList );
}
