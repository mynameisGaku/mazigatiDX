#include "Darkness.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 真夜中のときの、夜の層の濃さ（アルファ。0〜255）と色
	constexpr int32 NightAlpha = 215;
	const FColor32 NightColor( 6, 8, 36 );

	// 暗さが変わる速さ（1 秒あたり。0 が昼、1 が真夜中）
	constexpr float32 FadeSpeed = 1.5f;

	// 主人公のランタンと、街灯の光の半径（ピクセル）
	constexpr float32 LanternRadius = 190.0f;
	constexpr float32 LampRadius = 130.0f;

	// 街灯の灯りの高さ（足元から。ピクセル）
	constexpr float32 LampHeight = 34.0f;

	// 光の色（暖かいオレンジ）と、加算で重ねる強さ（0〜255）
	const FColor32 WarmColor( 255, 190, 110 );
	constexpr int32 WarmStrength = 80;
}

CDarkness::CDarkness( const TSharedPtr<FVillageState>& InState, const TArray<FVector2>& InLampPositions, const TSharedPtr<CRenderTarget>& InLayer, const TSharedPtr<CTexture>& InGlow )
	: State( InState )
	, LampPositions( InLampPositions )
	, Layer( InLayer )
	, Glow( InGlow )
{
}

void CDarkness::Tick( float32 DeltaTime )
{
	if ( !State.IsValid() )
	{
		return;
	}
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && Input->IsKeyPressed( EKey::N ) )
	{
		State->bNight = !State->bNight;
	}
	const float32 Target = State->bNight ? 1.0f : 0.0f;
	const float32 Step = FadeSpeed * DeltaTime;
	State->Darkness = NMath::Clamp( Target, State->Darkness - Step, State->Darkness + Step );
}

void CDarkness::CollectLights( TArray<FLight>& OutLights ) const
{
	// 主人公のランタン（胸のあたり）
	FLight Lantern;
	Lantern.Center = State->WorldToScreen( FVector2( State->PlayerPosition.x, State->PlayerPosition.y - 14.0f ) );
	Lantern.Radius = LanternRadius;
	OutLights.Add( Lantern );

	// 街灯（見えている範囲の近くのものだけ）
	for ( const FVector2& Lamp : LampPositions )
	{
		FLight Light;
		Light.Center = State->WorldToScreen( FVector2( Lamp.x, Lamp.y - LampHeight ) );
		Light.Radius = LampRadius;
		const bool bVisible = Light.Center.x > static_cast<float32>( FVillageState::ViewLeft ) - Light.Radius && Light.Center.x < static_cast<float32>( FVillageState::ViewLeft + FVillageState::ViewWidth ) + Light.Radius
							  && Light.Center.y > static_cast<float32>( FVillageState::ViewTop ) - Light.Radius && Light.Center.y < static_cast<float32>( FVillageState::ViewTop + FVillageState::ViewHeight ) + Light.Radius;
		if ( bVisible )
		{
			OutLights.Add( Light );
		}
	}
}

void CDarkness::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Layer.IsValid() || !Glow.IsValid() || State->Darkness <= 0.01f )
	{
		return;
	}
	TArray<FLight> Lights;
	CollectLights( Lights );

	// 1. 夜の層: 暗い青で塗る（アルファが、夜の濃さ）。その上に、光の画像で、光の位置のアルファを減らす
	//    （画像のアルファ a のぶんだけ、いまのアルファが (1 - a) 倍になる。色は、そのまま残す）
	{
		FTargetScope Scope( CommandList, Layer );
		const int32 BaseAlpha = NMath::RoundToInt( static_cast<float32>( NightAlpha ) * State->Darkness );
		CommandList.Clear( FColor32( NightColor.r, NightColor.g, NightColor.b, static_cast<uint8>( NMath::Clamp( BaseAlpha, 0, 255 ) ) ) );
		FCustomBlend ReduceAlpha;
		ReduceAlpha.SrcColor = EDrawBlendFactor::Zero;
		ReduceAlpha.DestColor = EDrawBlendFactor::One;
		ReduceAlpha.ColorOp = EDrawBlendOp::Add;
		ReduceAlpha.SrcAlpha = EDrawBlendFactor::Zero;
		ReduceAlpha.DestAlpha = EDrawBlendFactor::InvSrcAlpha;
		ReduceAlpha.AlphaOp = EDrawBlendOp::Add;
		CommandList.CustomBlend( ReduceAlpha );
		for ( const FLight& Light : Lights )
		{
			CommandList.ImageStretch( Light.Center.x - Light.Radius, Light.Center.y - Light.Radius, Light.Center.x + Light.Radius, Light.Center.y + Light.Radius, Glow );
		}
		CommandList.Blend( EDrawBlend::Opaque );
	}

	// 2. 夜の層を、ふつうの画像として、見せる範囲に重ねる（アルファブレンド）
	CommandList.Blend( EDrawBlend::Alpha );
	CommandList.ImagePart( static_cast<float32>( FVillageState::ViewLeft ), static_cast<float32>( FVillageState::ViewTop ), FVillageState::ViewLeft, FVillageState::ViewTop, FVillageState::ViewWidth, FVillageState::ViewHeight, Layer );
	CommandList.Blend( EDrawBlend::Opaque );

	// 3. 光を、暖かい色で、加算ブレンドで重ねる
	CommandList.Tint( WarmColor );
	CommandList.Blend( EDrawBlend::Add, NMath::RoundToInt( static_cast<float32>( WarmStrength ) * State->Darkness ) );
	{
		FClipScope Clip( CommandList, FVillageState::ViewLeft, FVillageState::ViewTop, FVillageState::ViewLeft + FVillageState::ViewWidth, FVillageState::ViewTop + FVillageState::ViewHeight );
		for ( const FLight& Light : Lights )
		{
			CommandList.ImageStretch( Light.Center.x - Light.Radius * 0.6f, Light.Center.y - Light.Radius * 0.6f, Light.Center.x + Light.Radius * 0.6f, Light.Center.y + Light.Radius * 0.6f, Glow );
		}
	}
	CommandList.Blend( EDrawBlend::Opaque );
	CommandList.Tint( FColor32::White );
}
