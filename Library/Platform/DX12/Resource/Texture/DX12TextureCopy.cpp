// SPDX-License-Identifier: MIT
#include "DX12TextureCopy.h"

bool NDX12TextureCopy::RecordRegionCopy( ID3D12GraphicsCommandList* Commands, ID3D12Resource* Source, D3D12_RESOURCE_STATES SourceState, ID3D12Resource* Dest, int32 X0, int32 Y0, int32 X1, int32 Y1 )
{
	if ( !Commands || !Source || !Dest || Source == Dest || X0 < 0 || Y0 < 0 || X1 <= X0 || Y1 <= Y0 )
	{
		return false;
	}
	const D3D12_RESOURCE_DESC SourceDesc = Source->GetDesc();
	const D3D12_RESOURCE_DESC DestDesc = Dest->GetDesc();
	if ( SourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || DestDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || SourceDesc.Format != DestDesc.Format || SourceDesc.SampleDesc.Count != 1 || DestDesc.SampleDesc.Count != 1
		|| static_cast<UINT64>( X1 ) > SourceDesc.Width || static_cast<UINT>( Y1 ) > SourceDesc.Height || DestDesc.Width != static_cast<UINT64>( X1 - X0 ) || DestDesc.Height != static_cast<UINT>( Y1 - Y0 ) )
	{
		return false;
	}
	// 定常のSRV状態（後の描画・computeが読む状態）。
	constexpr D3D12_RESOURCE_STATES ShaderResource = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	D3D12_RESOURCE_BARRIER Enter[2] = {};
	Enter[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Enter[0].Transition.pResource = Source;
	Enter[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Enter[0].Transition.StateBefore = SourceState;
	Enter[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	Enter[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Enter[1].Transition.pResource = Dest;
	Enter[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Enter[1].Transition.StateBefore = ShaderResource;
	Enter[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	Commands->ResourceBarrier( 2, Enter );
	D3D12_TEXTURE_COPY_LOCATION DestLocation = {};
	DestLocation.pResource = Dest;
	DestLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION SourceLocation = {};
	SourceLocation.pResource = Source;
	SourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	const D3D12_BOX Box = { static_cast<UINT>( X0 ), static_cast<UINT>( Y0 ), 0, static_cast<UINT>( X1 ), static_cast<UINT>( Y1 ), 1 };
	Commands->CopyTextureRegion( &DestLocation, 0, 0, 0, &SourceLocation, &Box );
	D3D12_RESOURCE_BARRIER Exit[2] = {};
	Exit[0] = Enter[0];
	Exit[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	Exit[0].Transition.StateAfter = SourceState;
	Exit[1] = Enter[1];
	Exit[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	Exit[1].Transition.StateAfter = ShaderResource;
	Commands->ResourceBarrier( 2, Exit );
	return true;
}
