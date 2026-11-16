// SPDX-License-Identifier: MIT

float MatLinearDepth( float d )
{
	return g_Time.z * g_Time.w / ( g_Time.w - d * ( g_Time.w - g_Time.z ) );
}
