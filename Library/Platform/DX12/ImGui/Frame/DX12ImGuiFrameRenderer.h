// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/DX12/Device/DX12Device.h"
#include "Library/Platform/DX12/ImGui/Render/DX12ImGuiRenderer.h"

// 借用したGPUへImGuiの描画を記録する。GPUの所有・開始・提出は行わない。
class CDX12ImGuiFrameRenderer
{
public:
	// UI接続とGPUへの借用参照を複写しない。
	CDX12ImGuiFrameRenderer() = default;
	CDX12ImGuiFrameRenderer( const CDX12ImGuiFrameRenderer& ) = delete;
	CDX12ImGuiFrameRenderer& operator=( const CDX12ImGuiFrameRenderer& ) = delete;
	// GPU完了か喪失を確認してUIだけを破棄し、GPUの借用を戻す。
	~CDX12ImGuiFrameRenderer();
	// 初期化済みGPUと現在のImGui文脈を借りる。未初期化・二重接続・UI接続済みはfalse。
	bool Initialize( CDX12Device& InDevice );
	// 提出済みGPU処理の完了後にUIだけを外す。記録中・待機失敗では保持し、GPUは解放しない。
	bool TryShutdown( uint32 TimeoutMs = 10000 );
	// 完了か喪失まで待ってUIの借用を戻す。GPUとImGui文脈の破棄より先に呼ぶ。
	void Shutdown();
	// 呼出側のGPU記録中にUIを準備する。GPU未開始・別文脈・未接続はfalse。
	bool BeginFrame();
	// 主画面の倍率を補正してUIをGPUへ記録する。GPU提出は呼出側が行う。
	bool Render( ImDrawData* Data );
	// 撮影接続へ描画実装を貸す。呼出側はこの所有者より先に借用を戻す。
	CDX12ImGuiRenderer& GetImGuiRenderer();
private:
	CDX12Device* Device = nullptr; // GPU所有者からの借用。UIは作成・提出・解放を行わない。
	CDX12ImGuiRenderer ImGuiRenderer; // 外部窓を含むUIの描画実装。
	ImGuiContext* Context = nullptr; // 所有は呼出側。別の文脈からの記録を拒否する。
};
