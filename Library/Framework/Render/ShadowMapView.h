// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/DrawCommand.h"
#include "Library/Framework/Render/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FShadowMapView
//   シャドウマップ（影の絵）を描くときの「光から見た景色」の決め方。純粋な値と判定（DxLib を使わない）。
//   描くときは、FDrawContext::BeginShadowMap / FShadowMapPassScope に渡す（実行のときに DxLib へ反映する）。
//
//   影は、光が進む向き（LightDirection）に沿って、平行に落ちる。影を計算する範囲（ワールドの箱。AreaMin〜AreaMax）の中だけに影ができ、
//   範囲が狭いほど、同じ大きさのシャドウマップでも、影の輪郭が細かくなる。範囲の外には影ができない（影を受ける床も、この箱に入れること）。
//   範囲を決めずに bFitToCamera にすると、始めるときのカメラの見える範囲（視錐台）に自動で合わせる。カメラの Far が遠いと範囲が広くなり、輪郭が粗くなる。
//   MakeForCameraView は、カメラの見える範囲を、手前から ShadowDistance までで切って、それを囲む箱を範囲にする（見えている近くだけに影を出し、後ろや遠くには出さない。
//   範囲が狭くなるので、輪郭が細かくなる）。カメラが動くたびに作り直す。
//
//   AdjustDepth: 影の判定に足す深さの補正（光から見た深度を 0〜1 に正規化した値）。面が自分の影で縞になる（シャドウアクネ）なら大きく、影が面から離れて浮くなら小さくする。
//   実測では、0.001 以上で縞が出なくなり、0 / 0.0001 では縞が出た。1 以上と負の値は、影が出なくなる・すべてが影になる。
//=============================================================================
struct FShadowMapView
{
	// 補正の深さの既定値
	static constexpr float32 DefaultAdjustDepth = 0.005f;

	// 光が進む向き（影ができる向きの反対側から、照らす向き）。長さは問わない。長さ 0 は使えない。真下（0, -1, 0）や真上も使える
	FVector3 LightDirection = FVector3( 0.0f, -1.0f, 0.0f );

	// true なら、影を計算する範囲を、始めるときのカメラの視錐台に自動で合わせる。false なら AreaMin〜AreaMax
	bool bFitToCamera = true;

	// 影を計算する範囲（ワールドの箱）。bFitToCamera が false のときだけ使う。3 つの軸すべてで AreaMin < AreaMax
	FVector3 AreaMin = FVector3( -10.0f, -10.0f, -10.0f );
	FVector3 AreaMax = FVector3( 10.0f, 10.0f, 10.0f );

	// 影の判定に足す深さの補正。0 以上 1 未満
	float32 AdjustDepth = DefaultAdjustDepth;

	// カメラの見える範囲に合わせる（範囲は自動）
	static FShadowMapView MakeFitToCamera( const FVector3& InLightDirection )
	{
		FShadowMapView View;
		View.LightDirection = InLightDirection;
		View.bFitToCamera = true;
		return View;
	}

	// カメラの見える範囲のうち、手前（Near）から ShadowDistance までを囲む箱に影を計算する。ShadowDistance は、Far より遠ければ Far にする。
	// 箱の高さは MinHeight〜MaxHeight（影を受ける地面と、影を落とす物の高さが入るようにする）。横と奥行きは、見える範囲の角を囲み、Margin だけ広げる
	// （見える範囲のすぐ外の物の影も入るように）。カメラが使えない・ShadowDistance が Near 以下・MinHeight >= MaxHeight・値が有限でないときは、MakeFitToCamera と同じ
	static FShadowMapView MakeForCameraView( const FVector3& InLightDirection, const FCamera& Camera, const FCameraViewport& Viewport, float32 ShadowDistance, float32 MinHeight, float32 MaxHeight, float32 Margin = 0.0f );

	// 箱（InAreaMin〜InAreaMax）の中に影を計算する
	static FShadowMapView MakeForArea( const FVector3& InLightDirection, const FVector3& InAreaMin, const FVector3& InAreaMax )
	{
		FShadowMapView View;
		View.LightDirection = InLightDirection;
		View.bFitToCamera = false;
		View.AreaMin = InAreaMin;
		View.AreaMax = InAreaMax;
		return View;
	}

	// 値が使えるか（向きが有限で長さが 0 でない。範囲を使うなら、有限で、3 つの軸すべてで AreaMin < AreaMax。補正が有限で 0 以上 1 未満）
	bool IsValid() const
	{
		if ( !IsFiniteVector( LightDirection ) || LightDirection.Length() <= NMath::SmallNumber )
		{
			return false;
		}
		if ( !NMath::IsFinite( AdjustDepth ) || AdjustDepth < 0.0f || AdjustDepth >= 1.0f )
		{
			return false;
		}
		if ( bFitToCamera )
		{
			return true;
		}
		return IsFiniteVector( AreaMin ) && IsFiniteVector( AreaMax ) && AreaMin.x < AreaMax.x && AreaMin.y < AreaMax.y && AreaMin.z < AreaMax.z;
	}

private:
	static bool IsFiniteVector( const FVector3& Vector )
	{
		return NMath::IsFinite( Vector.x ) && NMath::IsFinite( Vector.y ) && NMath::IsFinite( Vector.z );
	}
};
