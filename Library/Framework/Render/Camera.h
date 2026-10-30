// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"

//=============================================================================
// カメラ（3D の見る位置・向き・レンズ）と、画面の座標の変換
//   純粋な計算（DxLib を使わない）。左手系・Y 上（DxLib と同じ）。画面の座標は、描画先の左上が (0, 0)、右下が (Width, Height)、単位はピクセル。
//   描画に使うときは、FDrawContext::PushCamera( Camera ) / FCameraScope（記録して、実行のときに DxLib のカメラへ反映する）。
//   画面の点 <-> 世界の点 / 光線、視錐台との交差（描かなくてよいものを省く）は、DxLib を呼ばずに計算できる（描画先の大きさを、FCameraViewport で渡す）。
//
//   FCamera Camera = FCamera::MakePerspective( FVector3( 0.0f, 5.0f, -10.0f ), FVector3::Zero, 1.0f );
//   FCameraScope Scope( CommandList, Camera );           // この中の 3D の描画は、このカメラで見える
//   FVector2 Pixel;
//   if ( Camera.WorldToScreen( FCameraViewport { 1280, 720 }, Point, Pixel ) ) { ... }   // 世界の点が、画面のどこに見えるか
//=============================================================================

// 射影の種類
enum class ECameraProjection : uint8
{
	Perspective,    // 透視射影（遠くの物が小さく見える）
	Orthographic,   // 正射影（距離で大きさが変わらない。2.5D・設計図向き）
};

// カメラが映す先（描画先）の大きさ。ピクセル
struct FCameraViewport
{
	int32 Width = 1280;
	int32 Height = 720;

	FORCEINLINE bool IsValid() const { return Width >= 1 && Height >= 1; }
	FORCEINLINE float32 GetAspect() const { return IsValid() ? static_cast<float32>( Width ) / static_cast<float32>( Height ) : 1.0f; }
};

// 光線（Direction は長さ 1）
struct FCameraRay
{
	FVector3 Origin;
	FVector3 Direction = FVector3::Forward;
};

// 視錐台の面 1 枚。内側が正: Dot( Normal, P ) + Distance >= 0 なら、点 P は面の内側
struct FFrustumPlane
{
	FVector3 Normal = FVector3::Forward;
	float32 Distance = 0.0f;

	FORCEINLINE float32 SignedDistance( const FVector3& Point ) const { return FVector3::Dot( Normal, Point ) + Distance; }
};

//=============================================================================
// FFrustum
//   カメラが映す範囲（視錐台）。6 枚の面（左・右・下・上・近い・遠い）で囲まれる。
//   描かなくてよいものを省く（視錐台カリング）のに使う。DxLib の CheckCameraViewClip 系と同じ範囲。
//=============================================================================
struct FFrustum
{
	enum EPlane : int32
	{
		Left,
		Right,
		Bottom,
		Top,
		Near,
		Far,
		NumPlanes,
	};

	FFrustumPlane Planes[NumPlanes];

	// 点が、範囲の中にあるか（面の上は、中）
	bool ContainsPoint( const FVector3& Point ) const;

	// 球が、範囲と重なるか（少しでも触れていれば true。完全に外なら false）
	bool IntersectsSphere( const FVector3& Center, float32 Radius ) const;

	// 軸に平行な箱 (Min, Max) が、範囲と重なるか（少しでも触れていれば true。完全に外なら false。保守的: 外なのに true になることはあるが、中なのに false にはならない）
	bool IntersectsBox( const FVector3& Min, const FVector3& Max ) const;
};

//=============================================================================
// FCamera
//=============================================================================
struct FCamera
{
	// 見る位置・見ている点・上の向き（Up は、見る向きと平行でないこと）
	FVector3 Position = FVector3( 0.0f, 0.0f, -10.0f );
	FVector3 Target = FVector3::Zero;
	FVector3 Up = FVector3::Up;

	ECameraProjection Projection = ECameraProjection::Perspective;

	// 透視射影の縦の視野角（ラジアン。0 より大きく π より小さい）。DxLib の既定は 60 度
	float32 FovY = NMath::PI / 3.0f;

	// 正射影で映す範囲の縦の長さ（ワールドの単位。横は、画面の縦横比に合わせる）
	float32 OrthoHeight = 10.0f;

	// 映す奥行きの範囲（Near より近い物・Far より遠い物は見えない。0 < Near < Far）
	float32 Near = 0.1f;
	float32 Far = 1000.0f;

	// 横方向の拡大率（1 が標準。大きいほど、画面の横へ引き伸ばされる）。画素が縦横に不ぞろいな画面の補正に使う（DxLib の SetCameraDotAspect と同じ値）
	float32 HorizontalStretch = 1.0f;

	// 光軸が通る画面の点を、描画先の中心から、ずらす量（ピクセル。レンズシフト。DxLib の SetCameraScreenCenter）
	FVector2 ScreenOffset;

	// 透視射影のカメラ（Position から Target を見る）
	static FCamera MakePerspective( const FVector3& InPosition, const FVector3& InTarget, float32 InFovY = NMath::PI / 3.0f );

	// 正射影のカメラ（Position から Target を見る。InOrthoHeight は、映す範囲の縦の長さ）
	static FCamera MakeOrthographic( const FVector3& InPosition, const FVector3& InTarget, float32 InOrthoHeight );

	// 値が使えるか（NaN・無限大が無い。0 < Near < Far。視野角・縦の長さ・横の拡大率が正。Position と Target が離れていて、Up が見る向きと平行でない）
	bool IsValid() const;

	// 見る向き・右・上（どれも長さ 1。左手系: 右 = 上 x 前）。IsValid() が false のカメラでは、使わない
	FVector3 GetForward() const;
	FVector3 GetRight() const;
	FVector3 GetUpVector() const;

	// ビュー行列（世界 -> カメラ）と、射影行列（カメラ -> 画面。Z は 0..1）。DxLib の GetCameraViewMatrix / GetCameraProjectionMatrix と同じ
	FMatrix4x4 GetViewMatrix() const;
	FMatrix4x4 GetProjectionMatrix( const FCameraViewport& Viewport ) const;

	// 世界の点が、画面のどこに見えるか。Near と Far のあいだ（カメラの前）にあれば true で、OutPixel に画面の座標（ピクセル）、OutDepth（null でなければ）に奥行き（0 が Near、1 が Far）を返す。
	// カメラの後ろ・奥行きの範囲の外・使えない値のときは false
	bool WorldToScreen( const FCameraViewport& Viewport, const FVector3& World, FVector2& OutPixel, float32* OutDepth = nullptr ) const;

	// 画面の点（ピクセル）と奥行き（0 が Near、1 が Far）から、世界の点を求める。使えない値のときは false
	bool ScreenToWorld( const FCameraViewport& Viewport, const FVector2& Pixel, float32 Depth, FVector3& OutWorld ) const;

	// 画面の点（ピクセル）を通る光線（Origin は、Near の面の上。ピッキングに使う）。使えない値のときは、Origin を Position、Direction を GetForward にして false
	bool ScreenToRay( const FCameraViewport& Viewport, const FVector2& Pixel, FCameraRay& OutRay ) const;

	// 視錐台（IsValid() が false のカメラでは、空の範囲）
	FFrustum GetFrustum( const FCameraViewport& Viewport ) const;
};
