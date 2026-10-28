#include "MouseView.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"

namespace
{
	// ボタン 1 つの絵: ボタン・ラベル
	struct FButtonCell
	{
		EMouseButton Button;
		const char* Label;
	};

	const FButtonCell Buttons[] = {
		{ EMouseButton::Left, "Left" },
		{ EMouseButton::Middle, "Middle" },
		{ EMouseButton::Right, "Right" },
	};

	// ボタンの絵の大きさと間隔（ピクセル）
	constexpr int32 ButtonWidth = 72;
	constexpr int32 ButtonHeight = 40;
	constexpr int32 ButtonGap = 8;
}

CMouseView::CMouseView( const FVector2& InOrigin )
	: Origin( InOrigin )
{}

void CMouseView::Tick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr )
	{
		WheelTotal += Input->GetMouseState().WheelY;
	}
}

void CMouseView::Draw( FDrawContext& CommandList )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}

	const int32 X = static_cast<int32>( Origin.x );
	const int32 Y = static_cast<int32>( Origin.y );
	const FMouseState& State = Input->GetMouseState();

	CommandList.Text( X, Y - 24, "Mouse (GetMouseState / IsMouseButtonDown)", FColor32::White );
	CommandList.TextF( X, Y, FColor32::White, "Position: (%d, %d)   Delta: (%d, %d)   Wheel: %.1f", State.X, State.Y, State.DeltaX, State.DeltaY, WheelTotal );

	// ボタン（押している間は塗りつぶす）
	for ( int32 i = 0; i < 3; ++i )
	{
		const bool bDown = Input->IsMouseButtonDown( Buttons[i].Button );
		const int32 Left = X + i * ( ButtonWidth + ButtonGap );
		const int32 Top = Y + 32;
		if ( bDown )
		{
			CommandList.Box( Left, Top, Left + ButtonWidth, Top + ButtonHeight, FColor32::Green );
		}
		else
		{
			CommandList.Box( Left, Top, Left + ButtonWidth, Top + ButtonHeight, FColor32::Gray, false, 2 );
		}
		CommandList.Text( Left + 8, Top + 12, Buttons[i].Label, bDown ? FColor32::Black : FColor32::White );
	}

	// カーソルの位置を示す十字（ウィンドウの外・フォーカスがないときは出さない）
	if ( State.bPositionValid && State.bFocused )
	{
		CommandList.Line( State.X - 12, State.Y, State.X + 12, State.Y, FColor32::Yellow );
		CommandList.Line( State.X, State.Y - 12, State.X, State.Y + 12, FColor32::Yellow );
	}
}
