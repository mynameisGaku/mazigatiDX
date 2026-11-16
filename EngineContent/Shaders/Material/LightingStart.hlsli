// SPDX-License-Identifier: MIT

SubstrateLighting MatLighting( float3 world, float3 normal )
{
	SubstrateLighting lighting;
	lighting.LightColor = g_LightColor.rgb;
