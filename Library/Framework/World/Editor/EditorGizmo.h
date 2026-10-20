#pragma once
#include "Library/Framework/World/Editor/EditorCamera.h"
#include "Library/Framework/World/Editor/EditorGeometry.h"
#include "Library/Math/Transform.h"

//=============================================================================
// ギズモ（移動・回転・拡大の取っ手）の計算
//   取っ手の形（描くのも当たりを調べるのも同じ）、光線がどの取っ手に当たるか、ドラッグでの変化量。
//   純粋な計算（DxLib・ImGui を使わない）。画面への描画は DebugUI 側が、ここの形を投影して行う。
//
//   FEditorGizmoFrame Frame = NWorldEditor::MakeGizmoFrame( Camera, View, Origin, Orientation );
//   const EEditorGizmoHandle Hover = NWorldEditor::HitTestGizmo( Frame, EEditorGizmoMode::Translate, Ray );
//   FEditorGizmoDrag Drag;
//   Drag.Begin( Frame, Mode, Hover, Ray, Camera );            // マウスを押したとき
//   const FEditorGizmoDelta Delta = Drag.Update( Ray, Snap ); // 動かしている間（毎フレーム）。開始からの変化量
//   const FTransform Moved = NWorldEditor::ApplyGizmoDelta( StartTransform, Delta, Mode, Frame );
//=============================================================================
enum class EEditorGizmoMode : uint8
{
	Translate,
	Rotate,
	Scale,
};

enum class EEditorGizmoSpace : uint8
{
	World,   // ワールドの軸
	Local,   // オブジェクトの向きの軸
};

enum class EEditorGizmoHandle : uint8
{
	None,
	AxisX,     // 移動: X 軸に沿って / 回転: X 軸まわり / 拡大: X 方向
	AxisY,
	AxisZ,
	PlaneYZ,   // 移動: X に垂直な面の中で
	PlaneXZ,
	PlaneXY,
	Free,      // 移動: 画面に平行に / 拡大: 3 方向まとめて
};

// 軸の番号（0 = X, 1 = Y, 2 = Z）と取っ手の対応
inline EEditorGizmoHandle GetGizmoAxisHandle( int32 Axis )
{
	return Axis == 0 ? EEditorGizmoHandle::AxisX : Axis == 1 ? EEditorGizmoHandle::AxisY : EEditorGizmoHandle::AxisZ;
}
inline EEditorGizmoHandle GetGizmoPlaneHandle( int32 NormalAxis )
{
	return NormalAxis == 0 ? EEditorGizmoHandle::PlaneYZ : NormalAxis == 1 ? EEditorGizmoHandle::PlaneXZ : EEditorGizmoHandle::PlaneXY;
}

struct FEditorGizmoFrame
{
	FVector3 Origin = FVector3::Zero;
	FVector3 Axes[3] = { FVector3::Right, FVector3::Up, FVector3::Forward };   // 直交、長さ 1
	// 取っ手を出す向き（+1 = 軸の向き、-1 = 逆）。カメラから遠ざかる向きの軸は、手前に反転して表示する（奥向きの軸が、手前の面の取っ手に隠れないように）。
	// 反転しても、動かす向き（Axes）は変わらない。形（GetGizmoAxisSegment など）にだけ掛かる
	float32 Flip[3] = { 1.0f, 1.0f, 1.0f };
	float32 Length = 1.0f;       // 軸の長さ（ワールド）。画面では一定のピクセル数になる
	float32 PixelSize = 0.01f;   // 原点の奥行きでの 1 ピクセルのワールドの大きさ（当たりの太さに使う）
};

struct FEditorGizmoDelta
{
	FVector3 Translation = FVector3::Zero;            // 移動: ワールドでの移動量
	FQuaternion Rotation = FQuaternion::Identity;     // 回転: ワールドの軸まわり（ギズモの原点を中心に）
	FVector3 Scale = FVector3::One;                   // 拡大: ギズモの軸（Axes[0..2]）ごとの倍率
	float32 AngleRadians = 0.0f;                      // 回転: 開始からの回転の合計（符号つき。1 周を超えても続く）
};

namespace NWorldEditor
{
	// ギズモの位置と大きさ。Orientation は軸の向き（ワールドの軸なら単位）。ScreenLength は、軸の画面での長さ（ピクセル）。
	// カメラから遠ざかる向きの軸は、取っ手を手前へ反転する（Flip）
	FEditorGizmoFrame MakeGizmoFrame( const FEditorCamera& Camera, const FEditorViewport& Viewport, const FVector3& Origin, const FQuaternion& Orientation, float32 ScreenLength = 100.0f );

	//-------------------------------------------------------------------------
	// 取っ手の形（描くのも、当たりを調べるのも、これを使う）
	//-------------------------------------------------------------------------
	void GetGizmoAxisSegment( const FEditorGizmoFrame& Frame, int32 Axis, FVector3& OutStart, FVector3& OutEnd );
	// 移動の「面」の取っ手（NormalAxis に垂直な面の中の四角形）。四隅は周回の順
	void GetGizmoPlaneCorners( const FEditorGizmoFrame& Frame, int32 NormalAxis, FVector3 ( &OutCorners )[4] );
	float32 GetGizmoRingRadius( const FEditorGizmoFrame& Frame );
	float32 GetGizmoCenterRadius( const FEditorGizmoFrame& Frame );

	// 光線が当たる取っ手（当たらなければ None）。OutT は光線の上の位置。
	// 重なったときは、中央 > 面と軸のうち手前のほう
	EEditorGizmoHandle HitTestGizmo( const FEditorGizmoFrame& Frame, EEditorGizmoMode Mode, const FEditorRay& Ray, float32* OutT = nullptr );
}

//-----------------------------------------------------------------------------
// FEditorGizmoDrag
//   ドラッグ 1 回の状態。開始したときの光線を覚えて、いまの光線との差から、開始からの変化量を返す（前のフレームからの差を足さないので、ずれが溜まらない）。
//   光線が取っ手とほぼ平行なとき（軸の真正面から見ているなど）は、変化が不安定になるので、前の値のまま。
//-----------------------------------------------------------------------------
class FEditorGizmoDrag
{
public:
	bool IsActive() const { return bActive; }
	EEditorGizmoHandle GetHandle() const { return Handle; }
	EEditorGizmoMode GetMode() const { return Mode; }
	const FEditorGizmoFrame& GetFrame() const { return Frame; }

	// 取っ手をつかむ。None・取っ手の向きが光線と平行・開始の点が求まらないときは false
	bool Begin( const FEditorGizmoFrame& InFrame, EEditorGizmoMode InMode, EEditorGizmoHandle InHandle, const FEditorRay& StartRay, const FEditorCamera& Camera );

	// いまの光線での、開始からの変化量。Snap > 0 なら刻み（移動: ワールドの長さ、回転: ラジアン、拡大: 倍率）に丸める
	FEditorGizmoDelta Update( const FEditorRay& Ray, float32 Snap = 0.0f );

	void End() { bActive = false; }

private:
	bool bActive = false;
	EEditorGizmoMode Mode = EEditorGizmoMode::Translate;
	EEditorGizmoHandle Handle = EEditorGizmoHandle::None;
	FEditorGizmoFrame Frame;

	FVector3 PlaneNormal = FVector3::Up;     // 面の中で動かす / 回すときの面
	FVector3 StartPoint = FVector3::Zero;    // 面の上の、開始の点
	float32 StartAxisParam = 0.0f;           // 軸に沿って動かす / 拡大するときの、開始の位置
	int32 AxisIndex = 0;
	FVector3 DiagonalDirection = FVector3::Right;   // Free の拡大: 画面で右上へ動かすと大きくなる向き
	float32 LastRawAngle = 0.0f;
	float32 AccumulatedAngle = 0.0f;
	FEditorGizmoDelta Last;
};

namespace NWorldEditor
{
	// ワールドの点 Point に、ギズモの変化量を適用した点（メッシュの頂点を動かすのに使う）。
	//   移動: 動かす。回転: Frame.Origin のまわりに回す。拡大: Frame.Origin から見た、Frame の軸ごとの成分に、倍率を掛ける
	FVector3 ApplyGizmoDeltaToPoint( const FVector3& Point, const FEditorGizmoDelta& Delta, EEditorGizmoMode Mode, const FEditorGizmoFrame& Frame );

	// 開始したときの姿勢 Start（ワールド）に、ギズモの変化量を適用した姿勢。
	//   移動: 位置を動かす。回転: 位置を Frame.Origin のまわりに回し、向きをワールドの軸まわりに回す。
	//   拡大: 各オブジェクトの拡大に掛ける（Frame の軸がオブジェクト自身の軸なら、その軸の拡大だけが変わる。ワールドの軸なら、軸が近い方向ほど強く掛かる近似）
	FTransform ApplyGizmoDelta( const FTransform& Start, const FEditorGizmoDelta& Delta, EEditorGizmoMode Mode, const FEditorGizmoFrame& Frame );
}
