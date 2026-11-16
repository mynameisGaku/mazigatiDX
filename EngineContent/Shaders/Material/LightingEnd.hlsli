// SPDX-License-Identifier: MIT
	lighting.SkyColor = g_SkyColor.rgb;
	lighting.GroundColor = g_GroundColor.rgb;
	lighting.AmbientColor = g_AmbientColor.rgb;
	lighting.NumPointLights = ( int )g_LightColor.w;
	for ( int index = 0; index < 4; ++index )
	{
		lighting.PointLightPositions[index] = g_PointLightPositions[index];
		lighting.PointLightColors[index] = g_PointLightColors[index];
	}
	lighting.Environment = g_Environment;
	return lighting;
}
