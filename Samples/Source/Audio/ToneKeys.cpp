#include "ToneKeys.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Audio/ToneMaker.h"

namespace
{
	constexpr int32 KeyWidth = 96;
	constexpr int32 KeyHeight = 220;
	constexpr int32 KeyGap = 6;

	// ドから 1 オクターブ上のドまでの、半音の数（長音階）
	constexpr int32 Semitones[CToneKeys::NumKeys] = { 0, 2, 4, 5, 7, 9, 11, 12 };
	const char* const Labels[CToneKeys::NumKeys] = { "Do", "Re", "Mi", "Fa", "So", "La", "Ti", "Do" };
	const EKey Keys[CToneKeys::NumKeys] = { EKey::Num1, EKey::Num2, EKey::Num3, EKey::Num4, EKey::Num5, EKey::Num6, EKey::Num7, EKey::Num8 };

	// 元の音（ド）の周波数（Hz）。ほかの音は、半音ごとに 2 の 12 乗根倍
	constexpr float32 BaseFrequency = 261.63f;

	float32 GetPitch( int32 Index )
	{
		return NMath::Pow( 2.0f, static_cast<float32>( Semitones[Index] ) / 12.0f );
	}

	// 左端の鍵盤が -0.8（左）、右端が 0.8（右）
	float32 GetPan( int32 Index )
	{
		return -0.8f + 1.6f * static_cast<float32>( Index ) / static_cast<float32>( CToneKeys::NumKeys - 1 );
	}
}

CToneKeys::CToneKeys( const FVector2& InOrigin )
	: Origin( InOrigin )
{
	for ( int32 Index = 0; Index < NumKeys; ++Index )
	{
		SinceTouched[Index] = 100.0f;
	}
}

void CToneKeys::OnSpawn()
{
	// 小さめの音量で作る（耳に優しいように）
	Note = NToneMaker::MakeSound( "ToneKeysNote", NToneMaker::MakeNote( BaseFrequency, 0.8f, 0.4f ) );
}

void CToneKeys::Press( int32 Index )
{
	SinceTouched[Index] = 0.0f;
	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	if ( Audio == nullptr || Note == nullptr )
	{
		return;
	}
	FSoundPlayParams Params;
	Params.Pitch = GetPitch( Index );
	Params.Pan = GetPan( Index );
	Audio->Play( Note, Params );
}

void CToneKeys::Tick( float32 DeltaTime )
{
	for ( int32 Index = 0; Index < NumKeys; ++Index )
	{
		SinceTouched[Index] += DeltaTime;
	}
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	for ( int32 Index = 0; Index < NumKeys; ++Index )
	{
		if ( Input->IsKeyPressed( Keys[Index] ) )
		{
			Press( Index );
		}
	}
	if ( Input->IsMouseButtonPressed( EMouseButton::Left ) )
	{
		const FMouseState& Mouse = Input->GetMouseState();
		const int32 LocalX = Mouse.X - static_cast<int32>( Origin.x );
		const int32 LocalY = Mouse.Y - static_cast<int32>( Origin.y );
		const int32 Index = LocalX / ( KeyWidth + KeyGap );
		if ( LocalX >= 0 && LocalY >= 0 && LocalY < KeyHeight && Index < NumKeys && LocalX % ( KeyWidth + KeyGap ) < KeyWidth )
		{
			Press( Index );
		}
	}
}

void CToneKeys::Draw( FDrawContext& CommandList )
{
	for ( int32 Index = 0; Index < NumKeys; ++Index )
	{
		const int32 Left = static_cast<int32>( Origin.x ) + Index * ( KeyWidth + KeyGap );
		const int32 Top = static_cast<int32>( Origin.y );
		// 押した直後は明るく、0.4 秒かけて、元の色へ戻る
		const float32 Glow = NMath::Clamp( 1.0f - SinceTouched[Index] / 0.4f, 0.0f, 1.0f );
		const FColor32 Fill( static_cast<uint8>( 200 + 55.0f * Glow ), static_cast<uint8>( 200 + 55.0f * Glow ), static_cast<uint8>( 210 - 110.0f * Glow ) );
		CommandList.Box( Left, Top, Left + KeyWidth, Top + KeyHeight, Fill, true );
		CommandList.Box( Left, Top, Left + KeyWidth, Top + KeyHeight, FColor32( 40, 44, 56 ), false, 2 );
		CommandList.TextF( Left + 34, Top + KeyHeight - 52, FColor32( 40, 44, 56 ), "%s", Labels[Index] );
		CommandList.TextF( Left + 40, Top + KeyHeight - 30, FColor32( 90, 96, 110 ), "[%d]", Index + 1 );
	}
}
