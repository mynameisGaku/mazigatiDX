// SPDX-License-Identifier: MIT
#pragma once
#include "ImGuiCanvas2D.h"
#include "Library/Core/Render/OffscreenCaptureDesc.h"
#include "VendorModules/ImGui/imgui.h"

// 撮影用のImGui描画記録を所有する。GPU資源・提出・Windowsを知らない。
class FImGuiCaptureDrawList
{
public:
	// 現在の文脈を借りる。文脈はこの記録を破棄するまで生存させる。
	FImGuiCaptureDrawList();
	FImGuiCaptureDrawList( const FImGuiCaptureDrawList& ) = delete;
	FImGuiCaptureDrawList& operator=( const FImGuiCaptureDrawList& ) = delete;
	// 撮影の再生が終わってから描画記録を破棄する。
	~FImGuiCaptureDrawList();
	// 通常の記録と分離して、原点0・指定倍率の撮影記録を始める。不正設定・別文脈はfalse。
	bool Begin( const FOffscreenCaptureDesc& Desc );
	// 記録したリストを再生用データへまとめる。失敗時はnullptr。GPUへの提出は行わない。
	ImDrawData* BuildDrawData();
	// 再生を終え、記録中の印を戻す。GPU再生中に呼ばない。
	void Finish();
	// 所有文脈で撮影の記録中ならtrue。
	bool IsActive() const;
	// 記録中だけ2Dの描画先を貸す。
	IRenderCanvas2D* GetCanvas2D();
	// API固有の接続がコールバックを積むために、記録中だけリストを貸す。
	ImDrawList* GetDrawList();
private:
	ImGuiContext* Context = nullptr; // この記録を使う文脈。
	ImDrawList* DrawList = nullptr; // 通常の背景リストと分離した撮影記録。
	FImGuiCanvas2D Canvas; // 論理座標へ倍率を掛けて記録する2D描画先。
	ImDrawData Data; // GPU実装へ渡す描画データ。
	ImVector<ImTextureData*> Textures; // 通常のEndFrame前に必要な文字画像の更新。
	int32 Width = 0, Height = 0; // 撮影用の表示寸法。
	bool bActive = false; // BeginとFinishの対応を保持する。
};
