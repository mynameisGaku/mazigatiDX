#include "Villager.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Source/Village/VillageArt.h"

namespace
{
	// 話しかけられる距離（ピクセル）
	constexpr float32 TalkDistance = 88.0f;

	// 会話を閉じてから、また話しかけられるまでの間（秒。閉じたキーで、すぐ開き直さないため）
	constexpr float32 TalkCooldown = 0.3f;

	// 下向きのコマ（キャラクターの画像の 0 番と 1 番）
	constexpr int32 DownCell = 0;
}

CVillager::CVillager( const TSharedPtr<FVillageState>& InState, const FVector2& InPosition, const TSharedPtr<CTexture>& InSheet, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Position( InPosition )
	, Sheet( InSheet )
	, Font( InFont )
{
}

void CVillager::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
	if ( !State.IsValid() )
	{
		return;
	}
	const FVector2 Delta = State->PlayerPosition - Position;
	bNear = Delta.x * Delta.x + Delta.y * Delta.y <= TalkDistance * TalkDistance;

	const IInput* Input = GetAppSubsystem<IInput>();
	const bool bPressed = Input != nullptr && ( Input->IsKeyPressed( EKey::Enter ) || Input->IsKeyPressed( EKey::Space ) );
	if ( bNear && bPressed && !State->bDialogueOpen && State->Time - State->DialogueClosedTime > TalkCooldown )
	{
		State->bTalkRequested = true;
	}
}

void CVillager::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !Sheet.IsValid() )
	{
		return;
	}
	FWorldScope World( CommandList, *State );

	// 足元の影
	CommandList.Blend( EDrawBlend::Alpha, 90 );
	CommandList.Oval( NMath::RoundToInt( Position.x ), NMath::RoundToInt( Position.y ) - 1, 10, 4, FColor32::Black );
	CommandList.Blend( EDrawBlend::Opaque );

	// ゆっくり足踏みする姿（下向き）
	const int32 Frame = static_cast<int32>( Time * 1.5f ) % 2;
	const float32 Left = NMath::Floor( Position.x + 0.5f ) - static_cast<float32>( NVillageArt::CharacterWidth / 2 );
	const float32 Top = NMath::Floor( Position.y + 0.5f ) - static_cast<float32>( NVillageArt::CharacterHeight - 1 );
	CommandList.ImagePart( Left, Top, ( DownCell + Frame ) * NVillageArt::CharacterWidth, 0, NVillageArt::CharacterWidth, NVillageArt::CharacterHeight, Sheet );

	// 近づくと、頭の上に「!」（上下に揺れる。影つきで、拡大して描く）
	if ( bNear && Font.IsValid() )
	{
		FTextStyle Style;
		Style.AnchorX = 0.5f;
		Style.AnchorY = 1.0f;
		Style.ScaleX = 1.6f;
		Style.ScaleY = 1.6f;
		Style.bShadow = true;
		Style.ShadowColor = FColor32( 80, 40, 0 );
		Font->RecordStyled( CommandList, Position.x, Top - 4.0f + NMath::Sin( Time * 6.0f ) * 3.0f, "!", FColor32( 255, 226, 80 ), Style );
	}
}
