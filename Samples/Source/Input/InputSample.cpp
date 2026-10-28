#include "InputSample.h"

#include "Source/Catalog/SampleCatalog.h"
#include "Source/Input/GamepadView.h"
#include "Source/Input/KeyboardView.h"
#include "Source/Input/MouseView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CInputSample> Registrar( FSampleInfo { "Input", "Input: keyboard, mouse, gamepad", "Reads IInput (keyboard and mouse) and IGamepadInput, and shows the state of each.", 20 } );
}

void CInputSample::OnEnter()
{
	SpawnObject<CKeyboardView>( FVector2( 60.0f, 110.0f ) );
	SpawnObject<CMouseView>( FVector2( 60.0f, 400.0f ) );
	SpawnObject<CGamepadView>( FVector2( 640.0f, 110.0f ) );
}
