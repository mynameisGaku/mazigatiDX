#include "GamepadView.h"

#include "Library/Core/Interfaces/Input/IGamepadInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Library/Framework/Scene/Scene.h"

namespace
{
	// 表示する台数の上限と、1 台ぶんの高さ（ピクセル）
	constexpr int32 MaxDevices = 2;
	constexpr int32 DeviceHeight = 150;

	// スティックの絵の半径（ピクセル）
	constexpr int32 StickRadius = 36;

	// フェイスボタン 1 つの絵: ボタン・ラベル・位置（スティックの絵の右から）
	struct FFaceButton
	{
		EGamepadButton Button;
		const char* Label;
		int32 X;
		int32 Y;
	};

	const FFaceButton FaceButtons[] = {
		{ EGamepadButton::North, "N", 0, -28 },
		{ EGamepadButton::West, "W", -28, 0 },
		{ EGamepadButton::East, "E", 28, 0 },
		{ EGamepadButton::South, "S", 0, 28 },
	};

	// スティックの絵を描く。外側の円がスティックの動ける範囲で、中の点が、倒している向き（軸の値 -1..1）
	void DrawStick( FImmediateDraw& Screen, int32 CenterX, int32 CenterY, float32 AxisX, float32 AxisY )
	{
		Screen.Circle( CenterX, CenterY, StickRadius, FColor32::Gray, false, 2 );
		Screen.Circle( CenterX + static_cast<int32>( AxisX * StickRadius ), CenterY + static_cast<int32>( AxisY * StickRadius ), 6, FColor32::Green );
	}
}

CGamepadView::CGamepadView( const FVector2& InOrigin )
	: Origin( InOrigin )
{}

void CGamepadView::Draw()
{
	FImmediateDraw Screen( GetScene()->GetDrawExecutor() );
	const int32 X = static_cast<int32>( Origin.x );
	const int32 Y = static_cast<int32>( Origin.y );
	const IGamepadInput* Gamepads = GetAppSubsystem<IGamepadInput>();

	Screen.Text( X, Y - 24, "Gamepad (GetDevices / GetState)", FColor32::White );
	if ( Gamepads == nullptr || Gamepads->GetDevices().IsEmpty() )
	{
		Screen.Text( X, Y, "No gamepad is connected. Plug one in.", FColor32::Orange );
		return;
	}

	const TArray<FGamepadInfo>& Devices = Gamepads->GetDevices();
	for ( int32 i = 0; i < Devices.Num() && i < MaxDevices; ++i )
	{
		const FGamepadState* State = Gamepads->GetState( Devices[i].Id );
		if ( State == nullptr ) continue;

		const int32 Top = Y + i * DeviceHeight;
		Screen.TextF( X, Top, FColor32::White, "#%u %s", Devices[i].Id, *Devices[i].Name );

		// 左右のスティック
		const int32 LeftX = X + StickRadius + 8;
		const int32 RightX = LeftX + StickRadius * 2 + 100;
		const int32 StickY = Top + 34 + StickRadius;
		DrawStick( Screen, LeftX, StickY, State->GetAxis( EGamepadAxis::LeftX ), State->GetAxis( EGamepadAxis::LeftY ) );
		DrawStick( Screen, RightX, StickY, State->GetAxis( EGamepadAxis::RightX ), State->GetAxis( EGamepadAxis::RightY ) );

		// フェイスボタン（押している間は塗りつぶす）
		const int32 FaceX = RightX + StickRadius + 80;
		for ( const FFaceButton& Face : FaceButtons )
		{
			const bool bDown = State->IsDown( Face.Button );
			const int32 ButtonX = FaceX + Face.X;
			const int32 ButtonY = StickY + Face.Y;
			if ( bDown ) Screen.Circle( ButtonX, ButtonY, 12, FColor32::Green );
			else Screen.Circle( ButtonX, ButtonY, 12, FColor32::Gray, false, 2 );
			Screen.Text( ButtonX - 4, ButtonY - 8, Face.Label, bDown ? FColor32::Black : FColor32::White );
		}

		// トリガー（0..1 の押し込み）
		Screen.TextF( LeftX - StickRadius, StickY + StickRadius + 8, FColor32::Gray, "Triggers: L=%.2f  R=%.2f", State->GetAxis( EGamepadAxis::LeftTrigger ), State->GetAxis( EGamepadAxis::RightTrigger ) );
	}
}
