// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>

// 共通の頂点のつなぎ方（点・線・三角形）を、D3D12 の列挙へ変換する。
namespace NDX12Topology
{
	// 点・線は、そのまま。それ以外は、三角形のリスト。
	inline D3D_PRIMITIVE_TOPOLOGY ToD3D( EGeometryTopology Topology )
	{
		return Topology == EGeometryTopology::Points ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST : Topology == EGeometryTopology::Lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}
}
