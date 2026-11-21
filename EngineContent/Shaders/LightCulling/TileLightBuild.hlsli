// SPDX-License-Identifier: MIT
// GPU で、クラスタ（タイル x 奥行きの層）ごとの光の一覧を作る（Library/Framework/Render/LightCulling/TileLightGrid.cpp の CTileLightGrid::Build の HLSL 版。判定は同じ式）。
// 出力は、CTileLightGrid::WriteImage が詰める画像と同じ並びの 8 ビット RGBA の画像（TileLightList.hlsli が読む）。画像の画素 1 つが、1 つの呼び出し（BuildTileLightTexel）。
// 使う側が、この部品の前に、定数バッファの場所を決める（Library/Framework/Render/LightCulling/TileLightBuilder.h の FTileLightBuildData と同じ並び）:
//   #define TILE_LIGHT_BUILD_VIEW_REGISTER b4        // カメラ・クラスタの分け方（FTileLightBuildView）
//   #define TILE_LIGHT_BUILD_POSITION_REGISTER b5    // 光の位置と半径（FTileLightBuildData::Positions）
//   #define TILE_LIGHT_BUILD_AXIS_REGISTER b6        // スポットライトの軸と円錐の半角の cos（FTileLightBuildData::Axes）
// ピクセルシェーダーとして使うときは、さらに #define TILE_LIGHT_BUILD_PIXEL を決める（入り口 main は、画面全体の四角形を、一覧の画像と同じ大きさの描画先へ描くこと）。
// コンピュートシェーダーなど、別の入り口から使うときは、BuildTileLightTexel を呼ぶ。
//
// CPU の一覧との違い: GPU の浮動小数点の丸めで、境目の光を落とさないよう、各比べに、ごく小さな余裕（slack）を足している。つまり、GPU の一覧は、CPU の一覧と同じか、
// 境目で、ほんの少し多く光を含む（足りなくはならない）。光の寄与は、範囲の縁で 0 なので、多く含んでも結果は変わらない。溢れの判定（上限を超える光があるか）も、同じ数え方。
#define TILE_LIGHT_BUILD_MAX_LIGHTS 254

cbuffer cbTileLightBuildView : register( TILE_LIGHT_BUILD_VIEW_REGISTER )
{
	float4 g_BuildCameraPosition;   // xyz: カメラの位置、w: Far
	float4 g_BuildRayOrigin;        // xyz: 画面の左上（uv = 0, 0）の、カメラから Far の面の点までのベクトル
	float4 g_BuildRayU;             // xyz: uv.x が 0 から 1 へ変わるときの、ベクトルの増分
	float4 g_BuildRayV;             // xyz: uv.y が 0 から 1 へ変わるときの、ベクトルの増分
	float4 g_BuildForward;          // xyz: カメラの前向き（単位ベクトル）、w: Near
	float4 g_BuildGrid;             // x: タイル 1 枚の大きさ（ピクセル）、y: 層の数、z: クラスタ 1 つの横の画素の数、w: 光の数
	float4 g_BuildScreen;           // x: 映す先の幅、y: 高さ、z: 1 つの一覧に入る光の数の上限、w: 層の範囲を広げる割合（CTileLightGrid::SliceMargin）
};

cbuffer cbTileLightBuildPositions : register( TILE_LIGHT_BUILD_POSITION_REGISTER )
{
	float4 g_BuildLightPosition[TILE_LIGHT_BUILD_MAX_LIGHTS];   // xyz: 位置、w: 光の届く半径（0 以下は、どのクラスタにも入れない光）
};

cbuffer cbTileLightBuildAxes : register( TILE_LIGHT_BUILD_AXIS_REGISTER )
{
	float4 g_BuildLightAxis[TILE_LIGHT_BUILD_MAX_LIGHTS];       // xyz: スポットライトの軸（単位ベクトル）、w: 円錐の半角の cos（-2 は、全方向に光る点の光）
};

// 光が届く範囲が、単位ベクトル direction の向きへ、光の位置からどこまで広がるか（CPU の GetExtent と同じ式。0 以上）。
// 点の光は球なので、いつも半径。スポットライトは、direction が軸から半角以内なら半径、外なら、円錐の縁の向きへの広がり（半径 x cos( 軸との角 - 半角 )）。それが負なら、頂点（0）
float BuildExtent( float radius, float4 axisAndCos, float3 direction )
{
	if ( axisAndCos.w < -1.5 )
	{
		return radius;
	}
	float cosHalf = axisAndCos.w;
	float cosToAxis = dot( direction, axisAndCos.xyz );
	if ( cosToAxis >= cosHalf )
	{
		return radius;
	}
	float sinHalf = sqrt( max( 0.0, 1.0 - cosHalf * cosHalf ) );
	float sinToAxis = sqrt( max( 0.0, 1.0 - cosToAxis * cosToAxis ) );
	return radius * max( 0.0, cosToAxis * cosHalf + sinToAxis * sinHalf );
}

// 向き a・b（カメラから Far の面の点へのベクトル）が張る面の法線を、タイルの中心（center）の側へ向けて返す
float3 BuildInwardNormal( float3 a, float3 b, float3 center )
{
	float3 normal = normalize( cross( a, b ) );
	return dot( normal, center ) < 0.0 ? -normal : normal;
}

float3 BuildRayToFarPlane( float2 uv )
{
	return g_BuildRayOrigin.xyz + g_BuildRayU.xyz * uv.x + g_BuildRayV.xyz * uv.y;
}

// 層の境目の奥行き（boundary は 0〜層の数）。Near x ( Far / Near )^( boundary / 層の数 )
float BuildSliceDepth( int boundary, int numSlices )
{
	float nearDepth = g_BuildForward.w;
	float farDepth = g_BuildCameraPosition.w;
	return nearDepth * pow( farDepth / nearDepth, (float)boundary / (float)numSlices );
}

// 一覧の画像の画素 pixel（x: クラスタの横の画素、y: 画像の行 = 層 x タイルの行の数 + タイルの行）の、光の番号 + 1 の 3 つ（0 は、一覧の終わり）。
// クラスタが溢れていたら（届く光が上限より多い）、先頭の画素は (255, 255, 255)、ほかの画素は 0（CTileLightGrid::WriteImage と同じ）。値は 0〜255
int3 BuildTileLightTexel( int2 pixel )
{
	int tileSize = (int)g_BuildGrid.x;
	int numSlices = (int)g_BuildGrid.y;
	int texelsPerTile = (int)g_BuildGrid.z;
	int numLights = min( (int)g_BuildGrid.w, TILE_LIGHT_BUILD_MAX_LIGHTS );
	int width = (int)g_BuildScreen.x;
	int height = (int)g_BuildScreen.y;
	int capacity = (int)g_BuildScreen.z;
	float margin = g_BuildScreen.w;
	float nearDepth = g_BuildForward.w;
	float farDepth = g_BuildCameraPosition.w;

	// 画素に対応する、クラスタと、一覧の中の画素の番号
	int tilesY = ( height + tileSize - 1 ) / tileSize;
	int tileX = pixel.x / texelsPerTile;
	int texel = pixel.x - tileX * texelsPerTile;
	int slice = pixel.y / tilesY;
	int tileY = pixel.y - slice * tilesY;

	// タイルの 4 つの側面（カメラを通る面）の、内側を向いた法線。右端・下端のタイルは、画面の端まで
	float u0 = (float)( tileX * tileSize ) / (float)width;
	float v0 = (float)( tileY * tileSize ) / (float)height;
	float u1 = (float)min( ( tileX + 1 ) * tileSize, width ) / (float)width;
	float v1 = (float)min( ( tileY + 1 ) * tileSize, height ) / (float)height;
	float3 c00 = BuildRayToFarPlane( float2( u0, v0 ) );
	float3 c10 = BuildRayToFarPlane( float2( u1, v0 ) );
	float3 c01 = BuildRayToFarPlane( float2( u0, v1 ) );
	float3 c11 = BuildRayToFarPlane( float2( u1, v1 ) );
	float3 center = c00 + c10 + c01 + c11;
	float3 normal0 = BuildInwardNormal( c00, c10, center );
	float3 normal1 = BuildInwardNormal( c10, c11, center );
	float3 normal2 = BuildInwardNormal( c11, c01, center );
	float3 normal3 = BuildInwardNormal( c01, c00, center );

	// 層の奥行きの範囲（前後に少し広げる。最初の層は手前へ、最後の層は奥へ、どこまでも）
	float sliceLow = slice == 0 ? -3.0e38 : BuildSliceDepth( slice, numSlices ) * ( 1.0 - margin );
	float sliceHigh = slice == numSlices - 1 ? 3.0e38 : BuildSliceDepth( slice + 1, numSlices ) * ( 1.0 + margin );

	float3 forward = g_BuildForward.xyz;
	float3 backward = -forward;
	int count = 0;
	int3 packed = int3( 0, 0, 0 );
	[loop]
	for ( int k = 0; k < numLights; ++k )
	{
		float4 positionAndRadius = g_BuildLightPosition[k];
		float radius = positionAndRadius.w;
		if ( radius <= 0.0 )
		{
			continue;
		}
		float4 axisAndCos = g_BuildLightAxis[k];
		float3 offset = positionAndRadius.xyz - g_BuildCameraPosition.xyz;
		float slack = 1.0e-5 * ( length( offset ) + radius ) + 1.0e-5; // 浮動小数点の丸めで、境目の光を落とさない余裕
		float depth = dot( offset, forward );
		float depthMax = depth + BuildExtent( radius, axisAndCos, forward );
		float depthMin = depth - BuildExtent( radius, axisAndCos, backward );
		// Near・Far の範囲の外の光は、どのクラスタにも入れない
		if ( depthMax < nearDepth - slack || depthMin > farDepth + slack )
		{
			continue;
		}
		// 光が届く範囲が、4 つの側面の内側に、少しでもかかっているか
		if ( dot( normal0, offset ) < -BuildExtent( radius, axisAndCos, normal0 ) - slack ||
			dot( normal1, offset ) < -BuildExtent( radius, axisAndCos, normal1 ) - slack ||
			dot( normal2, offset ) < -BuildExtent( radius, axisAndCos, normal2 ) - slack ||
			dot( normal3, offset ) < -BuildExtent( radius, axisAndCos, normal3 ) - slack )
		{
			continue;
		}
		// 光が届く範囲の奥行きの範囲が、層の範囲と重なるか
		if ( depthMax < sliceLow - slack || depthMin > sliceHigh + slack )
		{
			continue;
		}
		// 届く光を、光の番号の昇順に数える。この画素が受け持つ 3 つ（texel x 3 番目から）だけを覚える
		int local = count - texel * 3;
		if ( local == 0 )
		{
			packed.x = k + 1;
		}
		else if ( local == 1 )
		{
			packed.y = k + 1;
		}
		else if ( local == 2 )
		{
			packed.z = k + 1;
		}
		++count;
	}
	if ( count > capacity )
	{
		return texel == 0 ? int3( 255, 255, 255 ) : int3( 0, 0, 0 );
	}
	return packed;
}

#ifdef TILE_LIGHT_BUILD_PIXEL
float4 main( float4 position : SV_POSITION, float4 color : COLOR0, float2 uv : TEXCOORD0 ) : SV_Target
{
	int3 texel = BuildTileLightTexel( int2( floor( position.xy ) ) );
	return float4( (float3)texel / 255.0, 1.0 );
}
#endif
