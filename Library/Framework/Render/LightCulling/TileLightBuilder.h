// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Gpu/Shader.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Deferred/GBufferView.h"
#include "Library/Framework/Render/LightCulling/TileLightGrid.h"
#include "Library/Math/Math.h"

class CAssetSubsystem;
class FDrawContext;

//=============================================================================
// FTileLightBuildData
//   GPU で光の一覧を作るシェーダー（EngineContent/Shaders/LightCulling/TileLightBuild.hlsli）の定数。HLSL の 3 つの cbuffer と同じ並び
//   （View: cbTileLightBuildView、Positions: cbTileLightBuildPositions、Axes: cbTileLightBuildAxes）。1 つの定数バッファは 4096 バイトまでなので、3 つに分けて置く。
//=============================================================================
struct FTileLightBuildView
{
	FGBufferView View;      // xyz: カメラの位置、w: Far。続けて、画面の左上へのベクトルと、uv の増分（FGBufferView と同じ）
	FVector4 Forward;       // xyz: カメラの前向き（単位ベクトル）、w: Near
	FVector4 Grid;          // x: タイル 1 枚の大きさ（ピクセル）、y: 層の数、z: クラスタ 1 つの横の画素の数、w: 光の数
	FVector4 Screen;        // x: 映す先の幅、y: 高さ、z: 1 つの一覧に入る光の数の上限、w: 層の範囲を広げる割合（CTileLightGrid::SliceMargin）
};

struct FTileLightBuildData
{
	FTileLightBuildView View;
	FVector4 Positions[CTileLightGrid::MaxLights];   // xyz: 位置、w: 光の届く半径（0 以下は、どのクラスタにも入れない光）
	FVector4 Axes[CTileLightGrid::MaxLights];        // xyz: スポットライトの軸（単位ベクトル）、w: 円錐の半角の cos（-2 は、全方向に光る点の光）
};

static_assert( sizeof( FTileLightBuildView ) == 112, "FTileLightBuildView is placed into a shader constant buffer as it is" );
static_assert( sizeof( FVector4 ) * CTileLightGrid::MaxLights <= 4096, "the light arrays fit in one constant buffer (4096 bytes)" );

//=============================================================================
// CTileLightBuilder
//   GPU で、クラスタ（タイル x 奥行きの層）ごとの光の一覧を作る（Forward+ / Deferred+ のライトカリングの GPU 版）。CTileLightGrid（CPU の基準実装）と同じ判定を、
//   ピクセルシェーダーで行う: 一覧の画像（8 ビット RGBA）の画素 1 つが、1 つのクラスタの 3 つの光の番号を、全部の光との交差判定で数えて作る。
//   新しい compute の窓口は要らず、公開のシェーダー窓口（ShaderRect・ShaderConstants・描画先）だけで動く。DxLib でも DX12 でも、同じコードで動く。
//   できた画像は、CTileLightGrid::WriteImage が詰める画像と同じ形式・並びなので、TileLightList.hlsli の部品で読める（段は、呼び出し側が決める）。
//   GPU の一覧は、CPU の一覧と同じか、境目で、ほんの少し多く光を含む（足りなくはならない。TileLightBuild.hlsli の説明）。
//
//   CTileLightBuilder Builder;
//   Builder.Create( *Assets, "lights", FCameraViewport { 1280, 640 }, Desc );   // 作る（大きさ・設定が変わったら、もう一度）
//   Builder.Record( CommandList, Camera, Lights, NumLights );                    // 毎フレーム、場面を描く前に記録する
//   CommandList.ShaderTexture( 4, Builder.GetTexture() );                        // 読むシェーダーの段へ置く
//=============================================================================
class CTileLightBuilder
{
public:
	// 定数バッファを置く最初のスロット（View、続けて Positions、Axes の 3 つ。HLSL の TILE_LIGHT_BUILD_*_REGISTER を b4・b5・b6 にする）
	static constexpr int32 FirstConstantSlot = 4;

	// 定数を作る（CPU。検査もこれを使う）。CTileLightGrid::Build と同じ入力の検査（使えなければ false で、Out は変えない）。
	// 半径が 0 以下の光・半角が 0 以下のスポットライトは、半径 0（どこにも入らない）にする
	static bool MakeData( const FCamera& Camera, const FCameraViewport& Viewport, const FCullLight* Lights, int32 NumLights, const FTileLightGridDesc& Desc, FTileLightBuildData& Out );

	// 一覧の画像（描画先）の大きさ（作れない設定なら 0）
	static int32 GetImageWidth( const FCameraViewport& Viewport, const FTileLightGridDesc& Desc );
	static int32 GetImageHeight( const FCameraViewport& Viewport, const FTileLightGridDesc& Desc );

	// 作る: ピクセルシェーダーのコンパイルと、一覧の画像（描画先）。メインスレッドだけ。作り直すときは、前のものを手放す。使えない設定・コンパイルの失敗では、何も持たず false（理由はログ）
	bool Create( CAssetSubsystem& Assets, const FString& Name, const FCameraViewport& Viewport, const FTileLightGridDesc& Desc );

	// 手放す
	void Release();

	// 作れているか
	bool IsValid() const { return Texture.IsValid() && Pixel.IsValid(); }

	// 一覧の画像（読むシェーダーの段に置く）。作れていなければ、空の参照
	const TSharedPtr<CRenderTarget>& GetTexture() const { return Texture; }

	// 一覧を作る描画を、FDrawContext に記録する（描画先は、一覧の画像。ほかの描画の前に呼ぶ）。描画の状態は、変えない。カメラ・光が使えない（MakeData が false）・作れていないときは、何も記録せず false
	bool Record( FDrawContext& CommandList, const FCamera& Camera, const FCullLight* Lights, int32 NumLights ) const;

private:
	TSharedPtr<CShader> Pixel;
	TSharedPtr<CRenderTarget> Texture;
	FCameraViewport Viewport;
	FTileLightGridDesc Desc;
};
