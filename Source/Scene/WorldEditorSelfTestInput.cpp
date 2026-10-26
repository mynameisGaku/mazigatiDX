#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/WorldEditor.h"
#include "Library/Framework/World/Editor/EditorGizmo.h"
#include "Library/Framework/World/WorldHierarchy.h"
#include "WorldEditorSelfTestSupport.h"

using namespace NAssetTest;
using namespace NWorldEditorTest;

//=============================================================================
// ワールドエディタの視界への入力（マウスとキーで、どう操作したらどうなるか）の確認
//   ImGui を使わずに、入力の写し（FWorldEditorInput）を 1 フレームずつ流し込む。
//   ImGui の入力から写しを作る部分は、画面で確かめる（スクリーンショット）。
//=============================================================================
namespace
{
	using EMouseMode = FWorldEditorState::EMouseMode;

	// 入力を 1 フレームずつ流す台本
	class FInputScript
	{
	public:
		explicit FInputScript( FWorldEditorState& InState ) : S( InState ) {}

		bool bCtrl = false;
		bool bShift = false;
		bool bAlt = false;
		bool bHovered = true;
		FVector3 Fly = FVector3::Zero;
		float32 DeltaTime = 0.1f;

		float32 GetX() const { return X; }
		float32 GetY() const { return Y; }

		// マウスを動かして、1 フレーム
		void MoveTo( float32 NewX, float32 NewY )
		{
			DX = NewX - X;
			DY = NewY - Y;
			X = NewX;
			Y = NewY;
			Step( false, false, false, false, 0.0f );
		}
		void MoveToPixel( const FVector2& Pixel ) { MoveTo( Pixel.x, Pixel.y ); }
		void Idle() { Step( false, false, false, false, 0.0f ); }
		void PressLeft() { bLeft = true; Step( true, false, false, false, 0.0f ); }
		void ReleaseLeft() { bLeft = false; Step( false, false, false, false, 0.0f ); }
		void PressMiddle() { bMiddle = true; Step( false, true, false, false, 0.0f ); }
		void ReleaseMiddle() { bMiddle = false; Step( false, false, false, false, 0.0f ); }
		void PressRight() { bRight = true; Step( false, false, true, false, 0.0f ); }
		void ReleaseRight() { bRight = false; Step( false, false, false, false, 0.0f ); }
		void PressEscape() { Step( false, false, false, true, 0.0f ); }
		void Wheel( float32 Amount ) { Step( false, false, false, false, Amount ); }

		// 押して離す（動かさない）
		void Click() { PressLeft(); ReleaseLeft(); }

	private:
		void Step( bool bLeftClicked, bool bMiddleClicked, bool bRightClicked, bool bEscape, float32 WheelAmount )
		{
			FWorldEditorInput Input;
			Input.MouseX = X;
			Input.MouseY = Y;
			Input.MouseDeltaX = DX;
			Input.MouseDeltaY = DY;
			Input.Wheel = WheelAmount;
			Input.bLeftDown = bLeft;
			Input.bMiddleDown = bMiddle;
			Input.bRightDown = bRight;
			Input.bLeftClicked = bLeftClicked;
			Input.bMiddleClicked = bMiddleClicked;
			Input.bRightClicked = bRightClicked;
			Input.bCtrl = bCtrl;
			Input.bShift = bShift;
			Input.bAlt = bAlt;
			Input.bEscapePressed = bEscape;
			Input.Fly = Fly;
			Input.DeltaTime = DeltaTime;
			Input.bHovered = bHovered;
			NDebugUI::ProcessWorldEditorInput( S, Input );
			DX = 0.0f;
			DY = 0.0f;
		}

		FWorldEditorState& S;
		float32 X = 0.0f, Y = 0.0f, DX = 0.0f, DY = 0.0f;
		bool bLeft = false, bMiddle = false, bRight = false;
	};

	struct FUiFixture
	{
		FWorldEditorState State;
		int32 Near = 0, Far = 0, Side = 0;

		FUiFixture()
		{
			State.View = MakeViewport();
			State.Session.Reset( FWorldDefinition(), "" );
			State.Session.Camera = MakeCamera( 0.0f, 0.0f, 10.0f );
			// 重なった 2 つ（Near の真後ろに Far）と、横の Side
			Near = State.Session.CreateCollider( EWorldColliderShape::Box, false, FVector3( 0, 0, 0 ) );
			Far = State.Session.CreateCollider( EWorldColliderShape::Box, false, FVector3( 0, 0, 5 ) );
			Side = State.Session.CreateCollider( EWorldColliderShape::Box, false, FVector3( 3, 0, 0 ) );
			State.Session.ClearSelection();
		}

		FVector2 Pixel( const FVector3& Point ) const
		{
			FVector2 Result;
			State.Session.Camera.WorldToScreen( State.View, Point, Result );
			return Result;
		}
		const FWorldObject& Get( int32 Id ) const { return ObjectOrEmpty( State.Session.Document.Get(), Id ); }
	};

	void TestClickAndMarquee( NAssetTest::FChecker& C )
	{
		C.Group = "editor-input-select";
		FUiFixture F;
		FWorldEditorState& S = F.State;
		FInputScript Mouse( S );
		const auto Ids = []( std::initializer_list<int32> List ) { TArray<int32> Result; for ( const int32 Id : List ) Result.Add( Id ); return Result; };
		S.bShowGizmo = false;   // ギズモの取っ手は、物より先に押される（ギズモのテストで確かめる）。ここでは、選ぶ操作だけを見る

		// ホバー
		Mouse.MoveToPixel( F.Pixel( FVector3( 3, 0, 0 ) ) );
		C.Check( S.HoverObjectId == F.Side && S.HoverHandle == EEditorGizmoHandle::None, "hovering over an object reports it" );
		Mouse.MoveTo( 20, 20 );
		C.Check( S.HoverObjectId == WorldNoId, "and nothing over empty space" );

		// クリック
		Mouse.MoveToPixel( F.Pixel( FVector3( 3, 0, 0 ) ) );
		Mouse.PressLeft();
		C.Check( S.MouseMode == EMouseMode::Pending && S.Session.GetSelection().IsEmpty(), "pressing the left button waits: it may be a click or a rectangle" );
		Mouse.ReleaseLeft();
		C.Check( S.MouseMode == EMouseMode::None && S.Session.GetSelection() == Ids( { F.Side } ), "releasing without moving is a click: the object is selected" );

		// 少しの揺れはクリックのまま
		Mouse.MoveTo( 200, 100 );
		Mouse.PressLeft();
		Mouse.MoveTo( 203, 102 );
		C.Check( S.MouseMode == EMouseMode::Pending, "a 3 pixel wobble is still a click" );
		Mouse.ReleaseLeft();
		C.Check( S.Session.GetSelection().IsEmpty(), "and a click on empty space clears the selection" );

		// Ctrl / Shift
		Mouse.MoveToPixel( F.Pixel( FVector3( 3, 0, 0 ) ) );
		Mouse.Click();
		Mouse.MoveToPixel( F.Pixel( FVector3( 0, 0, 0 ) ) );
		Mouse.bCtrl = true;
		Mouse.Click();
		C.Check( S.Session.GetSelection() == Ids( { F.Side, F.Near } ), "Ctrl+click toggles an object into the selection" );
		Mouse.Click();
		C.Check( S.Session.GetSelection() == Ids( { F.Side } ), "and out of it again" );
		Mouse.bCtrl = false;
		Mouse.bShift = true;
		Mouse.Click();
		C.Check( S.Session.GetSelection() == Ids( { F.Side, F.Near } ), "Shift+click adds" );
		Mouse.Click();
		C.Check( S.Session.GetSelection() == Ids( { F.Side, F.Near } ), "and adding one that is already selected changes nothing" );
		Mouse.bShift = false;
		Mouse.MoveTo( 200, 100 );
		Mouse.bShift = true;
		Mouse.Click();
		Mouse.bShift = false;
		C.Check( S.Session.GetSelection() == Ids( { F.Side, F.Near } ), "a Shift+click on empty space keeps the selection" );

		// 重なったもの
		S.Session.ClearSelection();
		Mouse.MoveTo( 400, 300 );
		Mouse.Click();
		C.Check( S.Session.GetPrimary() == F.Near, "a click on overlapped objects picks the nearest" );
		Mouse.Click();
		C.Check( S.Session.GetPrimary() == F.Far, "clicking the same place again picks the one behind" );
		Mouse.Click();
		C.Check( S.Session.GetPrimary() == F.Near, "and again, back to the first" );

		// 矩形
		S.Session.ClearSelection();
		Mouse.MoveTo( 10, 10 );
		Mouse.PressLeft();
		Mouse.MoveTo( 300, 200 );
		C.Check( S.MouseMode == EMouseMode::Marquee, "dragging more than a few pixels starts a rectangle" );
		Mouse.MoveTo( 790, 590 );
		Mouse.ReleaseLeft();
		C.Check( S.MouseMode == EMouseMode::None && S.Session.GetSelection().Num() == 3, "releasing selects what is inside (here: all three)" );
		S.Session.ClearSelection();
		const FVector2 SidePixel = F.Pixel( FVector3( 3, 0, 0 ) );
		Mouse.MoveTo( SidePixel.x - 30, SidePixel.y - 30 );
		Mouse.PressLeft();
		Mouse.MoveTo( SidePixel.x + 30, SidePixel.y + 30 );
		Mouse.ReleaseLeft();
		C.Check( S.Session.GetSelection() == Ids( { F.Side } ), "a small rectangle selects only the object inside it" );
		Mouse.MoveTo( 390, 290 );
		Mouse.bShift = true;
		Mouse.PressLeft();
		Mouse.MoveTo( 410, 310 );
		Mouse.ReleaseLeft();
		Mouse.bShift = false;
		C.Check( S.Session.GetSelection().Num() == 3, "a rectangle with Shift adds to the selection" );
		Mouse.MoveTo( 10, 10 );
		Mouse.PressLeft();
		Mouse.MoveTo( 50, 50 );
		Mouse.ReleaseLeft();
		C.Check( S.Session.GetSelection().IsEmpty(), "a rectangle around nothing clears the selection" );

		// ビューポートの外
		Mouse.bHovered = false;
		S.HoverObjectId = F.Side;
		Mouse.MoveToPixel( F.Pixel( FVector3( 3, 0, 0 ) ) );
		Mouse.Click();
		C.Check( S.Session.GetSelection().IsEmpty() && S.HoverObjectId == WorldNoId && S.MouseMode == EMouseMode::None, "when the mouse is not over the view (another window on top) nothing happens" );
	}

	void TestGizmoByMouse( NAssetTest::FChecker& C )
	{
		C.Group = "editor-input-gizmo";
		FUiFixture F;
		FWorldEditorState& S = F.State;
		S.Session.Camera = MakeCamera( 0.6f, 0.45f, 10.0f );
		FInputScript Mouse( S );
		S.Session.Select( F.Side );

		FEditorGizmoFrame Frame;
		S.Session.GetGizmoFrame( S.View, Frame );
		FVector3 AxisStart, AxisEnd;
		NWorldEditor::GetGizmoAxisSegment( Frame, 0, AxisStart, AxisEnd );
		const FVector3 Grab = AxisStart + ( AxisEnd - AxisStart ) * 0.7f;
		const FVector3 StartPosition = F.Get( F.Side ).Transform.Position;

		Mouse.MoveToPixel( F.Pixel( Grab ) );
		C.Check( S.HoverHandle == EEditorGizmoHandle::AxisX, "hovering over the X axis of the gizmo is reported (%d)", static_cast<int32>( S.HoverHandle ) );
		const int32 Base = S.Session.Document.GetNumUndo();
		Mouse.PressLeft();
		C.Check( S.MouseMode == EMouseMode::Gizmo && S.Session.IsGizmoDragging() && S.Session.GetSelection().Num() == 1, "pressing on a handle starts a drag (and does not change the selection)" );
		Mouse.MoveToPixel( F.Pixel( Grab + FVector3( 2, 0, 0 ) ) );
		Mouse.MoveToPixel( F.Pixel( Grab + FVector3( 2, 0, 0 ) ) );
		C.Check( Near3( F.Get( F.Side ).Transform.Position, StartPosition + FVector3( 2, 0, 0 ), 3e-3f ), "moving the mouse 2 units along the axis moves the object by (2,0,0) (%s)", *F.Get( F.Side ).Transform.Position.ToString() );
		Mouse.ReleaseLeft();
		C.Check( S.MouseMode == EMouseMode::None && !S.Session.IsGizmoDragging() && S.Session.Document.GetNumUndo() == Base + 1, "releasing ends the drag as one history entry" );

		// 取り消し: Esc
		S.Session.GetGizmoFrame( S.View, Frame );
		NWorldEditor::GetGizmoAxisSegment( Frame, 0, AxisStart, AxisEnd );
		const FVector3 Grab2 = AxisStart + ( AxisEnd - AxisStart ) * 0.7f;
		const FVector3 Before = F.Get( F.Side ).Transform.Position;
		Mouse.MoveToPixel( F.Pixel( Grab2 ) );
		Mouse.PressLeft();
		Mouse.MoveToPixel( F.Pixel( Grab2 + FVector3( 3, 0, 0 ) ) );
		C.Check( !Near3( F.Get( F.Side ).Transform.Position, Before, 0.5f ), "(the object moves during the drag)" );
		Mouse.PressEscape();
		C.Check( S.MouseMode == EMouseMode::None && !S.Session.IsGizmoDragging() && Near3( F.Get( F.Side ).Transform.Position, Before, 0.0f ) && S.Session.Document.GetNumUndo() == Base + 1, "Esc cancels: the position is back exactly, and no history is left" );
		Mouse.ReleaseLeft();
		C.Check( Near3( F.Get( F.Side ).Transform.Position, Before, 0.0f ), "releasing the button afterwards does not re-apply anything" );

		// 取り消し: 右ボタン
		Mouse.MoveToPixel( F.Pixel( Grab2 ) );
		Mouse.PressLeft();
		Mouse.MoveToPixel( F.Pixel( Grab2 + FVector3( 3, 0, 0 ) ) );
		Mouse.PressRight();
		C.Check( S.MouseMode == EMouseMode::None && Near3( F.Get( F.Side ).Transform.Position, Before, 0.0f ), "pressing the right button also cancels" );
		Mouse.ReleaseRight();
		Mouse.ReleaseLeft();

		// スナップを、Ctrl で逆にする
		S.Session.bSnap = true;
		S.Session.MoveSnap = 0.5f;
		S.Session.GetGizmoFrame( S.View, Frame );
		NWorldEditor::GetGizmoAxisSegment( Frame, 0, AxisStart, AxisEnd );
		const FVector3 Grab3 = AxisStart + ( AxisEnd - AxisStart ) * 0.7f;
		const FVector3 Start3 = F.Get( F.Side ).Transform.Position;
		Mouse.MoveToPixel( F.Pixel( Grab3 ) );
		Mouse.PressLeft();
		Mouse.MoveToPixel( F.Pixel( Grab3 + FVector3( 1.3f, 0, 0 ) ) );
		C.Check( Near3( F.Get( F.Side ).Transform.Position, Start3 + FVector3( 1.5f, 0, 0 ), 3e-3f ), "with snap on, 1.3 becomes 1.5" );
		Mouse.bCtrl = true;
		Mouse.MoveToPixel( F.Pixel( Grab3 + FVector3( 1.3f, 0, 0 ) ) );
		C.Check( Near3( F.Get( F.Side ).Transform.Position, Start3 + FVector3( 1.3f, 0, 0 ), 3e-3f ), "holding Ctrl turns the snap off while it is held" );
		Mouse.bCtrl = false;
		Mouse.ReleaseLeft();
		S.Session.bSnap = false;

		// ギズモの取っ手は、その下にある物より先に押される: 選択した物の中心（自由に動かす取っ手）を押しても、選び直さない
		S.Session.ClearSelection();
		S.Session.Select( F.Side );
		Mouse.MoveToPixel( F.Pixel( F.Get( F.Side ).Transform.Position ) );
		C.Check( S.HoverHandle == EEditorGizmoHandle::Free, "the centre of the gizmo is the free handle" );
		Mouse.PressLeft();
		C.Check( S.MouseMode == EMouseMode::Gizmo, "and takes the press before the object under it" );
		Mouse.ReleaseLeft();
		C.Check( S.Session.GetSelection().Num() == 1 && S.Session.GetPrimary() == F.Side, "so the selection does not change" );

		// 取っ手が無い所を押すと、選択のクリック（取っ手は優先されるが、外れていれば選択）
		S.Session.ClearSelection();
		S.Session.Select( F.Side );
		Mouse.MoveTo( 30, 30 );
		Mouse.Click();
		C.Check( S.Session.GetSelection().IsEmpty(), "pressing away from the gizmo is an ordinary click" );

		// 回転: 輪をつかむ
		S.Session.Select( F.Side );
		S.Session.GizmoMode = EEditorGizmoMode::Rotate;
		S.Session.GetGizmoFrame( S.View, Frame );
		const float32 Radius = NWorldEditor::GetGizmoRingRadius( Frame );
		FVector3 OnRing = Frame.Origin;
		float32 Best = -1.0e9f;
		for ( int32 i = 0; i < 90; ++i )
		{
			const float32 Angle = NMath::TWO_PI * static_cast<float32>( i ) / 90.0f;
			const FVector3 Point = Frame.Origin + ( Frame.Axes[2] * std::cos( Angle ) + Frame.Axes[0] * std::sin( Angle ) ) * Radius;
			const float32 Facing = FVector3::Dot( Point - Frame.Origin, S.Session.Camera.GetEye() - Frame.Origin );
			if ( Facing > Best )
			{
				Best = Facing;
				OnRing = Point;
			}
		}
		Mouse.MoveToPixel( F.Pixel( OnRing ) );
		C.Check( S.HoverHandle == EEditorGizmoHandle::AxisY, "in rotate mode the Y ring is hovered (%d)", static_cast<int32>( S.HoverHandle ) );
		Mouse.PressLeft();
		const FVector3 Perp = FVector3::Cross( FVector3::Up, OnRing - Frame.Origin ).Normalized();
		const FVector3 Turned = Frame.Origin + ( OnRing - Frame.Origin ) * std::cos( NMath::HALF_PI ) + Perp * ( Radius * std::sin( NMath::HALF_PI ) );
		S.Session.RotateSnapDegrees = 15.0f;
		Mouse.MoveToPixel( F.Pixel( Turned ) );
		const float32 Yaw = F.Get( F.Side ).Transform.RotationDegrees.y;
		Mouse.ReleaseLeft();
		C.Check( NMath::Abs( NMath::Abs( Yaw ) - 90.0f ) < 1.0f, "dragging a quarter of the ring turns the object about 90 degrees (%.1f)", Yaw );
	}

	void TestCameraByMouse( NAssetTest::FChecker& C )
	{
		C.Group = "editor-input-camera";
		FUiFixture F;
		FWorldEditorState& S = F.State;
		FInputScript Mouse( S );
		FEditorCamera& Camera = S.Session.Camera;
		Camera = MakeCamera( 0.6f, 0.45f, 10.0f, FVector3( 1, 2, 3 ) );

		// 回る: Alt + 左ドラッグ
		Mouse.MoveTo( 400, 300 );
		Mouse.bAlt = true;
		Mouse.PressLeft();
		C.Check( S.MouseMode == EMouseMode::Orbit, "Alt + left button orbits" );
		Mouse.MoveTo( 430, 310 );
		C.Check( NMath::Abs( Camera.Yaw - 0.3f ) < 1e-4f && NMath::Abs( Camera.Pitch - 0.55f ) < 1e-4f && Near3( Camera.Target, FVector3( 1, 2, 3 ) ), "a (30, 10) pixel drag changes the yaw by -0.3 and the pitch by +0.1 and keeps the target" );
		Mouse.bAlt = false;
		Mouse.ReleaseLeft();
		C.Check( S.MouseMode == EMouseMode::None && S.Session.GetSelection().IsEmpty(), "releasing ends it (and selects nothing)" );

		// 平行に動かす: 中ボタン
		const FVector3 Target = Camera.Target;
		FVector2 Before, After;
		Camera.WorldToScreen( S.View, Target, Before );
		Mouse.MoveTo( 400, 300 );
		Mouse.PressMiddle();
		C.Check( S.MouseMode == EMouseMode::Pan, "the middle button pans" );
		Mouse.MoveTo( 450, 280 );
		Mouse.ReleaseMiddle();
		Camera.WorldToScreen( S.View, Target, After );
		C.Check( NMath::Abs( After.x - Before.x - 50.0f ) < 0.1f && NMath::Abs( After.y - Before.y + 20.0f ) < 0.1f, "the previously centred point follows the mouse by (50, -20) pixels (%.2f, %.2f)", After.x - Before.x, After.y - Before.y );

		// 見回す・飛ぶ: 右ボタン
		const FVector3 Eye = Camera.GetEye();
		Mouse.MoveTo( 400, 300 );
		Mouse.PressRight();
		C.Check( S.MouseMode == EMouseMode::Look, "the right button looks around" );
		Mouse.MoveTo( 420, 300 );
		C.Check( Near3( Camera.GetEye(), Eye, 1e-3f ), "looking around keeps the eye" );
		C.Check( NMath::Abs( Camera.Yaw - 0.1f ) < 1e-4f, "and turns the view: a 20 pixel drag right lowers the yaw by 0.2 (%.4f)", Camera.Yaw );
		const FVector3 EyeBeforeFly = Camera.GetEye();
		const FVector3 Forward = Camera.GetForward();
		Mouse.Fly = FVector3( 0, 0, 1 );
		Mouse.DeltaTime = 0.5f;
		Mouse.Idle();
		const float32 Speed = Camera.Distance * 0.8f;
		C.Check( Near3( Camera.GetEye() - EyeBeforeFly, Forward * ( Speed * 0.5f ), 1e-3f ), "W while looking flies forward by distance * 0.8 per second (moved %.3f)", ( Camera.GetEye() - EyeBeforeFly ).Length() );
		const FVector3 EyeAfterFly = Camera.GetEye();
		Mouse.bShift = true;
		Mouse.Idle();
		C.Check( NMath::Abs( ( Camera.GetEye() - EyeAfterFly ).Length() - Speed * 0.5f * 3.0f ) < 1e-3f, "Shift triples the speed" );
		Mouse.bShift = false;
		Mouse.Fly = FVector3::Zero;
		Mouse.ReleaseRight();
		const FVector3 EyeStopped = Camera.GetEye();
		Mouse.Fly = FVector3( 0, 0, 1 );
		Mouse.Idle();
		C.Check( S.MouseMode == EMouseMode::None && Near3( Camera.GetEye(), EyeStopped, 0.0f ), "without the right button the movement keys do nothing" );
		Mouse.Fly = FVector3::Zero;

		// ホイール
		const float32 Distance = Camera.Distance;
		Mouse.Wheel( 2.0f );
		C.Check( NMath::Abs( Camera.Distance - Distance * 0.81f ) < 1e-3f, "two wheel steps close in by 19%% (%.3f -> %.3f)", Distance, Camera.Distance );
		Mouse.PressRight();
		const float32 Distance2 = Camera.Distance;
		Mouse.Wheel( 3.0f );
		C.Check( Camera.Distance == Distance2, "the wheel is ignored in the middle of another drag" );
		Mouse.ReleaseRight();
		Mouse.bHovered = false;
		Mouse.Wheel( 3.0f );
		C.Check( Camera.Distance == Distance2, "and when another window is over the view" );
		Mouse.bHovered = true;

		// ドラッグの途中でボタンを離した状態からの復帰
		Mouse.PressLeft();
		Mouse.ReleaseLeft();
		Mouse.PressMiddle();
		Mouse.ReleaseMiddle();
		C.Check( S.MouseMode == EMouseMode::None, "every drag ends when its button is up (no stuck mode)" );
	}
}

void NWorldTest::RunEditorInputTests( NAssetTest::FChecker& C )
{
	TestClickAndMarquee( C );
	TestGizmoByMouse( C );
	TestCameraByMouse( C );
}
