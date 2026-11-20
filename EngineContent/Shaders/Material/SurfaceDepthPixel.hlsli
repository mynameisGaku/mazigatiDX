// SPDX-License-Identifier: MIT

float4 main( VertexOutput i ) : SV_Target
{
#ifdef MATERIAL_MASKED
	MatClipOpacity( EvaluateShadowOpacity( i ), MATERIAL_CUTOFF );
#endif
	return float4( i.Position.z, 0.0, 0.0, 1.0 );
}
