// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "VendorModules/ImGui/imgui.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

// 画像と頂点色の3Dパイプライン・深度・フレーム内のアップロード領域を所有する。
class FDX12GeometryRenderer : public IRenderGeometry3D
{
public:
	// 所有するGPU資源とフレーム状態は複写しない。
	FDX12GeometryRenderer() = default;
	FDX12GeometryRenderer( const FDX12GeometryRenderer& ) = delete;
	FDX12GeometryRenderer& operator=( const FDX12GeometryRenderer& ) = delete;
	// 所有GPUと論理解像度を固定する。失敗時はfalse。
	bool Initialize( ID3D12Device* InDevice, int32 InWidth, int32 InHeight );
	// GPU待ち後だけ呼ぶ。前フレームの頂点を解放して深度を消す。
	void BeginFrame( ID3D12GraphicsCommandList* InCommands, D3D12_CPU_DESCRIPTOR_HANDLE InRtv, ID3D12DescriptorHeap* InSrvHeap = nullptr );
	// 記録先を外す。GPU上で使用中の頂点は保持する。
	void EndFrame();
	// 論理幅。
	int32 GetWidth() const override;
	// 論理高さ。
	int32 GetHeight() const override;
	// 値を所有コピーして、ImGuiと同じ命令列へ3D描画を記録する。
	bool DrawGeometry( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State ) override;
	// 既存の画像記述子を借りてUV付きの頂点を描く。
	bool DrawTexturedGeometry( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State, const FTextureDrawView& Texture ) override;
	// 同じ命令列へ深度消去を記録する。
	void ClearDepth() override;
private:
	// 画像の有無だけを分岐し、同じ検査とアップロードを使う。
	bool DrawInternal( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State, const FTextureDrawView* Texture );
	// 描画状態に合うGPUパイプラインを取得または生成する。
	ID3D12PipelineState* GetPipeline( const FGeometryDrawState& State, bool bTextured );
	// 描画順を保ってGPUへ3Dを記録する。
	static void DrawCallback( const ImDrawList*, const ImDrawCmd* Command );
	// 描画順を保って深度を消す。
	static void ClearDepthCallback( const ImDrawList*, const ImDrawCmd* Command );
	// コールバックへ値で渡す描画。GPU資源は所有者が完了まで保持する。
	struct FPendingDraw
	{
		FDX12GeometryRenderer* Owner = nullptr; // 記録先。
		ID3D12PipelineState* Pipeline = nullptr; // 所有者のキャッシュにある状態。
		D3D12_VERTEX_BUFFER_VIEW Vertices = {}; // 所有者が保持する頂点領域。
		D3D_PRIMITIVE_TOPOLOGY Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; // 頂点のつなぎ方。
		UINT64 TextureBinding = 0; // 借用画像のGPU記述子。0なら頂点色だけ。
		UINT SamplerIndex = 0; // 補間と外側の参照設定。
		UINT NumVertices = 0; // 描く数。
	};
	// ComPtrのoperator&をコンテナの要素参照として呼ばないため、所有参照を値へ包む。
	struct FPipelineRecord
	{
		Microsoft::WRL::ComPtr<ID3D12PipelineState> Resource; // 所有するパイプライン。
	};
	// 頂点バッファの所有参照。コンテナ自体はCOMの取得窓口を扱わない。
	struct FUploadRecord
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // GPU完了まで保持する頂点。
	};
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 所有GPUへの参照。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature; // 画像と補間を受け取る設定窓口。
	Microsoft::WRL::ComPtr<ID3DBlob> VertexShader, PixelShader, TexturedPixelShader; // コンパイル済みの頂点色シェーダー。
	Microsoft::WRL::ComPtr<ID3D12Resource> Depth; // D32の深度画像。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DsvHeap; // 深度画像の記述子。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> SamplerHeap; // 補間2種類×参照方法4×4。
	UINT SamplerStride = 0; // sampler記述子の幅。
	ID3D12DescriptorHeap* SrvHeap = nullptr; // 今フレームの画像記述子を借用する。
	TMap<uint64, FPipelineRecord> Pipelines; // 状態ごとのパイプライン。
	TArray<FUploadRecord> Uploads; // GPU完了まで保持する頂点。
	ID3D12GraphicsCommandList* Commands = nullptr; // 今フレームだけ借用する記録先。
	D3D12_CPU_DESCRIPTOR_HANDLE Rtv = {}; // 今フレームだけ借用する画面。
	uint64 UploadBytes = 0; // 今フレームに保持した頂点のバイト数。
	int32 Width = 0, Height = 0; // 固定の論理解像度。
};
