#include "KeyboardView.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Library/Framework/Scene/Scene.h"

namespace
{
	// キー 1 つの絵: 押すキー・ラベル・左上の位置（並びの左上から）・幅（ピクセル）
	struct FKeyCell
	{
		EKey Key;
		const char* Label;
		int32 X;
		int32 Y;
		int32 Width;
	};

	// キーの大きさ（高さ。正方形のキーは幅も同じ）と、キー同士の間隔（ピクセル）
	constexpr int32 KeySize = 56;
	constexpr int32 KeyGap = 8;
	constexpr int32 Step = KeySize + KeyGap;

	const FKeyCell Cells[] = {
		// WASD
		{ EKey::W, "W", Step, 0, KeySize },
		{ EKey::A, "A", 0, Step, KeySize },
		{ EKey::S, "S", Step, Step, KeySize },
		{ EKey::D, "D", Step * 2, Step, KeySize },
		// 矢印
		{ EKey::Up, "Up", Step * 5, 0, KeySize },
		{ EKey::Left, "Left", Step * 4, Step, KeySize },
		{ EKey::Down, "Down", Step * 5, Step, KeySize },
		{ EKey::Right, "Right", Step * 6, Step, KeySize },
		// Space・Enter・Shift
		{ EKey::Space, "Space", 0, Step * 2 + 8, Step * 3 - KeyGap },
		{ EKey::Enter, "Enter", Step * 4, Step * 2 + 8, Step * 2 - KeyGap },
		{ EKey::LeftShift, "Shift", Step * 6 + 16, Step * 2 + 8, Step * 2 - KeyGap },
	};
}

CKeyboardView::CKeyboardView( const FVector2& InOrigin )
	: Origin( InOrigin )
{}

void CKeyboardView::Draw()
{
	FImmediateDraw Screen( GetScene()->GetDrawExecutor() );
	const int32 X = static_cast<int32>( Origin.x );
	const int32 Y = static_cast<int32>( Origin.y );
	const IInput* Input = GetAppSubsystem<IInput>();

	Screen.Text( X, Y - 24, "Keyboard (IsKeyDown)", FColor32::White );
	for ( const FKeyCell& Cell : Cells )
	{
		const bool bDown = Input != nullptr && Input->IsKeyDown( Cell.Key );
		const int32 Left = X + Cell.X;
		const int32 Top = Y + Cell.Y;

		// 押している間は塗りつぶし、離していれば枠だけ
		if ( bDown ) Screen.Box( Left, Top, Left + Cell.Width, Top + KeySize, FColor32::Green );
		else Screen.Box( Left, Top, Left + Cell.Width, Top + KeySize, FColor32::Gray, false, 2 );
		Screen.Text( Left + 8, Top + KeySize / 2 - 8, Cell.Label, bDown ? FColor32::Black : FColor32::White );
	}
}
