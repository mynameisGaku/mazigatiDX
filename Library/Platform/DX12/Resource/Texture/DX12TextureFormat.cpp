// SPDX-License-Identifier: MIT
#include "DX12TextureFormat.h"

bool NDX12TextureFormat::GetComputeCapabilities( ID3D12Device *Device, FComputeTextureCapabilities &Out )
{
	if ( !Device || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	D3D12_FEATURE_DATA_FORMAT_SUPPORT Format{ DXGI_FORMAT_R8G8B8A8_UNORM }; // 実際に作成する形式の能力。
	D3D12_FEATURE_DATA_D3D12_OPTIONS Options{}; // RGBA8のtyped loadを含む追加形式群の能力。
	if ( FAILED( Device->CheckFeatureSupport( D3D12_FEATURE_FORMAT_SUPPORT, &Format, sizeof( Format ) ) ) || FAILED( Device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof( Options ) ) ) )
	{
		return false;
	}
	const UINT RequiredRead = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD; // Loadに必要な組合せ。
	const UINT RequiredWrite = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE; // 読み書き両方を保証する。
	FComputeTextureCapabilities Result; // 照会が全て成功した場合だけ返す。
	Result.bRGBA8Read = ( Format.Support1 & RequiredRead ) == RequiredRead;
	Result.bRGBA8ReadWrite = Result.bRGBA8Read && Options.TypedUAVLoadAdditionalFormats && ( Format.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW ) && ( Format.Support2 & RequiredWrite ) == RequiredWrite;
	Out = Result;
	return true;
}
