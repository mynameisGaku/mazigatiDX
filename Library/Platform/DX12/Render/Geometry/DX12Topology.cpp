// SPDX-License-Identifier: MIT
#include "DX12Topology.h"

D3D_PRIMITIVE_TOPOLOGY NDX12Topology::ToD3D( EGeometryTopology Topology )
{
	return Topology == EGeometryTopology::Points ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST : Topology == EGeometryTopology::Lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}
