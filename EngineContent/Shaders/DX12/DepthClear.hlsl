// SPDX-License-Identifier: MIT
cbuffer C:register(b0){float depthValue;} float4 VS(uint id:SV_VertexID):SV_POSITION{ return float4(id==2?3:-1,id==1?3:-1,depthValue,1); }