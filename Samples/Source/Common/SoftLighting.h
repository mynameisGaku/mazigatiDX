// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/DrawContext.h"

//=============================================================================
// NSoftLighting / FSoftLightingScope
//   やわらかい光。DxLib の既定のライト 1 つだけだと、光が当たらない裏側は、環境光がないので真っ黒になる。
//   これを、次の 2 つで、なだらかな陰にする:
//     補助光     既定のライトの反対側から、弱い光を足す。裏側は、図形の色のままで、うっすら明るくなる（環境光は、図形の色に関係なく同じ色を足すので、色がにごる）
//     光の回り込み  LightingOptions の bWrapLighting。光の当たる面と当たらない面の境目を、なだらかにする
//   スコープのあいだに記録した 3D の図形が、やわらかい光で照らされる。ライトは 2 つ（既定のライトと補助光）使う（上限は 6 つ）。
//
//   {
//       FSoftLightingScope Soft( CommandList );
//       CommandList.Sphere3D( ... );
//   }
//=============================================================================
namespace NSoftLighting
{
	// 補助光。向き（光が進む向き）は、既定のライト（DxLib の既定: 奥の左上から手前の右下へ向かう (1, -1, 1)）の、横は真逆、縦は少し下から。
	// 地面の照り返しのように、裏側や下の面をうっすら照らす。ハイライトは足さない
	inline FLight MakeFillLight()
	{
		FLight Light = FLight::MakeDirectional( FVector3( -1.0f, 0.4f, -1.0f ), FColor( 0.42f, 0.44f, 0.52f ) );
		Light.Specular = FColor::Black;
		return Light;
	}
}

class FSoftLightingScope
{
public:
	explicit FSoftLightingScope( FDrawContext& InCommandList )
		: CommandList( InCommandList )
		, FillLight( InCommandList, NSoftLighting::MakeFillLight() )
	{
		FLightingOptions Options;
		Options.bWrapLighting = true;
		CommandList.LightingOptions( Options );
	}

	~FSoftLightingScope()
	{
		CommandList.LightingOptions( FLightingOptions() );
	}

	FSoftLightingScope( const FSoftLightingScope& ) = delete;
	FSoftLightingScope& operator=( const FSoftLightingScope& ) = delete;

private:
	FDrawContext& CommandList;
	FLightScope FillLight;
};
