// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Platform/ImGui/Render/ImGuiCaptureDrawList.h"
#include "Library/Platform/DX12/Render/DX12CaptureTarget.h"

class FDX12Device;
class FDX12ImGuiRenderer;
class FDX12ImGuiGeometryRenderer;
struct ImGuiContext;
struct ImDrawCmd;

// ImGuiの撮影用描画リストを、独立したDX12描画先へ接続する。OSの窓は作らない。
class FDX12ImGuiCaptureRenderer : public IRenderSurface
{
public:
	// 既存のGPUとImGui描画実装を借り、撮影用の描画先を所有する。
	explicit FDX12ImGuiCaptureRenderer( FDX12Device& InOwner );
	FDX12ImGuiCaptureRenderer( const FDX12ImGuiCaptureRenderer& ) = delete;
	FDX12ImGuiCaptureRenderer& operator=( const FDX12ImGuiCaptureRenderer& ) = delete;
	// 撮影のGPU処理が終わってから、描画リストと3D記録を破棄する。
	~FDX12ImGuiCaptureRenderer() override;
	// 所有する文脈のフレーム内で描き直しを始める。不正設定・二重開始はfalse。
	bool Begin( const FOffscreenCaptureDesc& Desc );
	// 描画リストを再生し、画素を読み戻す。失敗時はOutを保って通常描画へ戻す。
	bool End( TArray<FColor32>& Out );
	// 描き直しの記録中だけtrue。
	bool IsActive() const;
	// 撮影中だけ2Dの記録先を貸す。
	IRenderCanvas2D* GetCanvas2D() override;
	// 撮影中だけ3Dと専用深度の記録先を貸す。
	IRenderGeometry3D* GetGeometry3D() override;
	// 色消去を2D・3Dと同じ順序で記録する。
	void RecordClear( FColor32 Color ) override;
	// 通常描画と撮影が共有するキューの完了を待つ。
	bool WaitForGPU() override;
private:
	// 描画リストのこの位置で、GPU側へ色消去を渡す。
	static void ClearCallback( const ImDrawList*, const ImDrawCmd* Command );
	// 消去色を描画順に固定するための値。
	struct FClearCommand
	{
		FDX12ImGuiCaptureRenderer* Owner = nullptr; // 描画リストの再生先。
		FColor32 Color; // 記録した時点の色。
	};
	FDX12Device& Owner; // 通常描画のフレーム状態を確認する借用先。
	FDX12CaptureTarget Target; // ImGuiを必要としないGPU側の描画先。
	FDX12ImGuiRenderer* Renderer = nullptr; // 同じ文脈の公式の描画接続。
	ID3D12DescriptorHeap* ImageHeap = nullptr; // 所有者が管理する画像のheap。
	FDX12ImGuiGeometryRenderer* Geometry = nullptr; // 撮影用の3D描画記録。
	ID3D12Resource* BoundDepth = nullptr; // GPU描画先の再作成を検出する借用値。
	FImGuiCaptureDrawList Recording; // DX12から独立した撮影用の描画記録。
	bool bActive = false, bHealthy = false; // 記録中の印と、失敗後の再利用の禁止。
};
