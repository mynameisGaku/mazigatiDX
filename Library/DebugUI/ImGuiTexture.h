#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// ImGui で DxLib の画像を表示する
//   DxLib のグラフィックハンドル（LoadGraph / MakeScreen で作ったもの）を、ImGui::Image / AddImage に渡せる ID にする。
//   Direct3D 11 の ShaderResourceView を作ってキャッシュする（ImGui の描画は DxLib と同じデバイスで行われるので、そのまま使える）。
//
//   ImGui::Image( NDebugUI::GetImGuiTexture( Texture->GetHandle() ), ImVec2( 256, 256 ) );
//
//   - 画像のハンドルが削除されたり、別の画像に使い回されたりしても、中身を見て作り直すので古い絵は出ない
//   - 一定フレーム使われなかったものは TickImGuiTextureCache で解放する
//   - メインスレッドだけで使う。Direct3D 11 でなければ（または無効なハンドルなら）0
//=============================================================================
namespace NDebugUI
{
	ImTextureID GetImGuiTexture( int32 GraphHandle );

	// 毎フレーム 1 回呼ぶ。しばらく使われなかった ShaderResourceView を解放する
	void TickImGuiTextureCache();
	void ClearImGuiTextureCache();
	// いまキャッシュしている数（確認用）
	int32 GetImGuiTextureCacheSize();

	//-------------------------------------------------------------------------
	// 拡大しても、ぼかさずにドットを見せる（ピクセルアートの確認用）
	//   BeginNearest と EndNearest の間に AddImage した画像は、最近傍で補間される
	//-------------------------------------------------------------------------
	void BeginNearest( ImDrawList* DrawList );
	void EndNearest( ImDrawList* DrawList );
}
