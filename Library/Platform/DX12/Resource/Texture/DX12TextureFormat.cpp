// SPDX-License-Identifier: MIT
#include "DX12TextureFormat.h"

bool NDX12TextureFormat::GetComputeCapabilities( ID3D12Device *Device, FComputeTextureCapabilities &Out )
{
	if ( !Device || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	D3D12_FEATURE_DATA_FORMAT_SUPPORT Format{ DXGI_FORMAT_R8G8B8A8_UNORM }; // 実際に作成するRGBA8形式の能力。
	D3D12_FEATURE_DATA_FORMAT_SUPPORT R32Float{ DXGI_FORMAT_R32_FLOAT }; // 読み取れる深度のR32Float形式の能力。
	D3D12_FEATURE_DATA_D3D12_OPTIONS Options{}; // RGBA8のtyped loadを含む追加形式群の能力。
	if ( FAILED( Device->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &Format, sizeof( Format ) ) ) || FAILED( Device->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &R32Float, sizeof( R32Float ) ) ) || FAILED( Device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof( Options ) ) ) )
	{
		return false;
	}
	const UINT RequiredRead = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD; // Loadに必要な組合せ。
	const UINT RequiredWrite = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE; // 読み書き両方を保証する。
	FComputeTextureCapabilities Result; // 照会が全て成功した場合だけ返す。
	Result.bRGBA8Read = ( Format.Support1 & RequiredRead ) == RequiredRead;
	Result.bRGBA8ReadWrite = Result.bRGBA8Read && Options.TypedUAVLoadAdditionalFormats && ( Format.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW ) && ( Format.Support2 & RequiredWrite ) == RequiredWrite;
	Result.bR32FloatRead = ( R32Float.Support1 & RequiredRead ) == RequiredRead;
	Out = Result;
	return true;
}

bool NDX12TextureFormat::GetRenderTargetCapabilities( ID3D12Device *Device, FRenderTargetCapabilities &Out )
{
	if ( !Device || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	D3D12_FEATURE_DATA_FORMAT_SUPPORT Color{ DXGI_FORMAT_R8G8B8A8_UNORM }; // RGBA8描画先として必要な能力。
	D3D12_FEATURE_DATA_FORMAT_SUPPORT Depth{ DXGI_FORMAT_D32_FLOAT }; // D32深度書き込みに必要な能力。
	D3D12_FEATURE_DATA_FORMAT_SUPPORT DepthRead{ DXGI_FORMAT_R32_FLOAT }; // D32深度のSRV読み取りに必要な能力。
	if ( FAILED( Device->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &Color, sizeof( Color ) ) ) || FAILED( Device->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &Depth, sizeof( Depth ) ) ) || FAILED( Device->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &DepthRead, sizeof( DepthRead ) ) ) )
	{
		return false;
	}
	const UINT RequiredColor = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_SHADER_LOAD | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_BLENDABLE; // 描画先と2D/3D/compute入力の共用条件。
	const UINT RequiredDepthWrite = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL; // 深度描画先の条件。
	const UINT RequiredDepthRead = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD; // 深度SRV読み取りの条件。
	FRenderTargetCapabilities Result; // 照会が全て成功した場合だけ返す。
	Result.bRGBA8RenderTarget = ( Color.Support1 & RequiredColor ) == RequiredColor;
	Result.bReadableDepthD32 = ( Depth.Support1 & RequiredDepthWrite ) == RequiredDepthWrite && ( DepthRead.Support1 & RequiredDepthRead ) == RequiredDepthRead;
	Result.MaxColorTargets = Result.bRGBA8RenderTarget ? D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT : 0;
	Out = Result;
	return true;
}
