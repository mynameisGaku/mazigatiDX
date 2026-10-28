#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/DebugUI/ImGuiTexture.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// アセットのエディタ（スプライトシート・モデルのアニメーション）が共通で使う、ImGui の小さな部品
//   FString も ImGui も UTF-8 なので、そのまま渡せる。
//=============================================================================
#define UI_TEXT( Text ) ( *( Text ) )   // 表示用: ImGui::TextUnformatted( UI_TEXT( Name ) )。FString を ImGui に渡す場所の目印

namespace NDebugUI
{
	// FString を編集する文字入力（変更されたら書き戻して true）
	bool InputString( const char* Label, FString& Value, ImGuiInputTextFlags Flags = 0 );
	bool InputStringWithHint( const char* Label, const char* Hint, FString& Value, ImGuiInputTextFlags Flags = 0 );

	// ファイルを選ぶ（一覧の中から。絞り込みつき）。選ばれたら true になり、Selected に入る
	bool FileCombo( const char* Label, FString& Selected, const TArray<FString>& Files, const char* NoneText = "(none)" );

	// 画像 Texture（TextureWidth x TextureHeight）の矩形 (X, Y, W, H) を Size の大きさで表示する（Nearest なら拡大してもドットのまま）
	void ImageRect( ImTextureID Texture, int32 TextureWidth, int32 TextureHeight, int32 X, int32 Y, int32 Width, int32 Height, const ImVec2& Size, bool bNearest = true );

	// 市松模様（透明な部分の背景）
	void DrawChecker( ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, float CellSize = 8.0f );

	// ボタンの色を、未保存などの強調に変える
	void PushWarningColor();
	void PopWarningColor();

	// 秒を "0.250 s" のように
	FString FormatSeconds( float32 Seconds );
	FString FormatBytes( int64 Bytes );
}
