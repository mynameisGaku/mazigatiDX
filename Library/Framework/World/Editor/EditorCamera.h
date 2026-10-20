#pragma once
#include "Library/Framework/World/Editor/EditorGeometry.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Math/Vector2.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// エディタのカメラと、画面の座標の変換
//   純粋な計算（DxLib・ImGui を使わない）。マウスの動き（ピクセル）を、カメラの動きにする。
//   左手系・Y 上。画面の座標は、ビューポートの左上が (0, 0)、右下が (Width, Height)、単位はピクセル。
//
//   - 注視点（Target）のまわりを回る（Orbit）、視点を動かさず見回す（LookAround）、画面に平行に動かす（Pan）、近づく / 離れる（Zoom）、
//     向いている方向に進む（MoveLocal。WASD の移動）、物に寄る（Focus）。
//   - 画面の点 <-> ワールドの点 / 光線（WorldToScreen / ScreenToRay）。ピッキングとギズモが使う。
//
//   FEditorCamera Camera;
//   const FEditorViewport View { 1280, 720 };
//   FEditorRay Ray = Camera.ScreenToRay( View, MouseX, MouseY );
//=============================================================================
struct FEditorViewport
{
	int32 Width = 1280;
	int32 Height = 720;
	float32 FovY = 0.8f;      // 縦の視野角（ラジアン）
	float32 Near = 0.05f;
	float32 Far = 2000.0f;

	float32 GetAspect() const { return Height > 0 ? static_cast<float32>( Width ) / static_cast<float32>( Height ) : 1.0f; }
	bool IsValid() const { return Width >= 2 && Height >= 2; }
};

struct FEditorCamera
{
	FVector3 Target = FVector3( 0.0f, 1.0f, 0.0f );   // 見ている点（Orbit の中心）
	float32 Yaw = 0.6f;                               // Y 軸まわり（ラジアン）。0 のとき、視点は注視点の手前（-Z）から奥（+Z）を見る
	float32 Pitch = 0.45f;                            // 正で上から見下ろす（ラジアン）
	float32 Distance = 12.0f;                         // 視点と注視点の距離

	static constexpr float32 MaxPitch = 1.5f;         // 真上・真下（1.5708）の手前で止める（向きが定まらなくなるので）
	static constexpr float32 MinDistance = 0.02f;
	static constexpr float32 MaxDistance = 20000.0f;

	FVector3 GetEye() const;
	FVector3 GetForward() const;    // 視点 -> 注視点（長さ 1）
	FVector3 GetRight() const;      // 画面の右（長さ 1）
	FVector3 GetUp() const;         // 画面の上（長さ 1）

	// 注視点のまわりを回る（マウスの動き DeltaX / DeltaY（ピクセル）。右へ動かすと、物が右へ回ってくる）
	void Orbit( float32 DeltaX, float32 DeltaY, float32 RadiansPerPixel = 0.01f );
	// 視点を動かさず、見回す（右へ動かすと、右を向く）
	void LookAround( float32 DeltaX, float32 DeltaY, float32 RadiansPerPixel = 0.01f );
	// 画面に平行に動かす（つかんだ点が、マウスについてくる）
	void Pan( const FEditorViewport& Viewport, float32 DeltaX, float32 DeltaY );
	// ホイール 1 目盛りで 10% ずつ。正で近づく
	void Zoom( float32 WheelSteps, float32 Rate = 0.1f );
	// 視点と注視点を一緒に、画面の右・上・奥の向きに動かす（長さはワールドの単位）
	void MoveLocal( const FVector3& Move );
	// 球（Center, Radius）が画面に収まる位置へ寄る（向きは変えない）
	void Focus( const FVector3& Center, float32 Radius, const FEditorViewport& Viewport );

	FMatrix4x4 GetViewMatrix() const;
	FMatrix4x4 GetProjectionMatrix( const FEditorViewport& Viewport ) const;

	// ワールドの点 -> 画面のピクセル。視点の後ろ（近い面より手前）なら false。OutDepth は視線方向の距離
	bool WorldToScreen( const FEditorViewport& Viewport, const FVector3& Point, FVector2& OutPixel, float32* OutDepth = nullptr ) const;
	// 線分 A-B を、近い面で切って（視点の後ろの部分を捨てて）画面のピクセルにする。画面に出る部分が無ければ false。
	// 輪郭線やギズモを、ImGui の線として描くとき、視点をまたぐ線が壊れないようにするため
	bool ProjectSegment( const FEditorViewport& Viewport, const FVector3& A, const FVector3& B, FVector2& OutPixelA, FVector2& OutPixelB ) const;
	// 画面のピクセル -> 視点から出る光線
	FEditorRay ScreenToRay( const FEditorViewport& Viewport, float32 PixelX, float32 PixelY ) const;
	// その点の奥行きで、1 ピクセルがワールドの何単位か（ギズモや目印を、画面で一定の大きさにするため）
	float32 GetPixelSize( const FEditorViewport& Viewport, const FVector3& Point ) const;
};
