// SPDX-License-Identifier: MIT

float4 main( VertexOutput i ) : SV_Target
{
	return float4( i.Position.z, 0.0, 0.0, 1.0 );
}
