// SPDX-License-Identifier: MIT
cbuffer C:register(b0){float4 color;} float4 VS(uint id:SV_VertexID):SV_POSITION{ return float4(id==2?3:-1,id==1?3:-1,0,1); } float4 PS():SV_TARGET{return color;}