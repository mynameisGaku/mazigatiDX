#include "SpriteAnimator.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Ecs/SystemParams.h"

//-----------------------------------------------------------------------------
// 再生の操作
//-----------------------------------------------------------------------------
bool FSpriteAnimator::Play( const FString& ClipName, bool bRestart )
{
	if ( !Sheet.IsValid() ) return false;
	const int32 Index = Sheet->FindClip( ClipName );
	if ( Index == INDEX_NONE ) return false;
	return PlayIndex( Index, bRestart );
}

bool FSpriteAnimator::PlayIndex( int32 NewClipIndex, bool bRestart )
{
	if ( !Sheet.IsValid() || Sheet->GetClip( NewClipIndex ) == nullptr ) return false;
	if ( !bRestart && ClipIndex == NewClipIndex && bPlaying ) return true;

	ClipIndex = NewClipIndex;
	Rewind( 0.0 );
	return true;
}

bool FSpriteAnimator::PlayFrom( const FString& ClipName, float32 NormalizedStart )
{
	if ( !Sheet.IsValid() ) return false;
	const int32 Index = Sheet->FindClip( ClipName );
	if ( Index == INDEX_NONE ) return false;

	ClipIndex = Index;
	Rewind( NMath::Clamp( static_cast<float64>( NormalizedStart ), 0.0, 1.0 ) );
	return true;
}

void FSpriteAnimator::Rewind( float64 NormalizedStart )
{
	const FAnimTimeline* Timeline = Sheet.IsValid() ? Sheet->GetTimeline( ClipIndex ) : nullptr;
	if ( Timeline == nullptr || Timeline->IsEmpty() )
	{
		ClipIndex = INDEX_NONE;
		return;
	}

	const float64 Total = Timeline->Total;
	// 逆再生は最後から始める（NormalizedStart は逆再生でも「クリップの先頭からの位置」）。
	// 終わりちょうどだと、Loop では次の周回の先頭になってしまうので、ほんの少し手前
	Playhead = Speed < 0.0f && NormalizedStart <= 0.0 ? std::nextafter( Total, 0.0 ) : NormalizedStart * Total;

	const FAnimLocation Location = NAnim::Locate( *Timeline, Playhead );
	Step = Location.Step;
	LastAbsoluteStep = Location.AbsoluteStep - ( Speed < 0.0f ? -1 : 1 );   // 最初の Update で、いまのコマに「入った」ことにする
	bPlaying = true;
	bFinished = false;
	bFinishedThisUpdate = false;
	Events.Empty();
}

void FSpriteAnimator::Stop()
{
	bPlaying = false;
	bFinished = false;
	bFinishedThisUpdate = false;
	Events.Empty();
	if ( ClipIndex != INDEX_NONE ) Rewind( 0.0 );
	bPlaying = false;
}

void FSpriteAnimator::ShowFrame( int32 SheetFrameIndex )
{
	ClipIndex = INDEX_NONE;
	ManualFrame = SheetFrameIndex;
	bPlaying = false;
	bFinished = false;
	bFinishedThisUpdate = false;
	Events.Empty();
}

void FSpriteAnimator::Update( float32 DeltaTime )
{
	Events.Empty();
	bFinishedThisUpdate = false;
	if ( !bPlaying || !Sheet.IsValid() ) return;

	const FAnimTimeline* Timeline = Sheet->GetTimeline( ClipIndex );
	const FSpriteClip* Clip = Sheet->GetClip( ClipIndex );
	if ( Timeline == nullptr || Clip == nullptr || Timeline->IsEmpty() ) return;

	Playhead += static_cast<float64>( DeltaTime ) * static_cast<float64>( Speed );
	const FAnimLocation Location = NAnim::Locate( *Timeline, Playhead );

	// 通り過ぎたコマの出来事を、起きた順に集める
	NAnim::ForEachEnteredStep( LastAbsoluteStep, Location.AbsoluteStep, Timeline->NumSteps(), [&]( int32 EnteredStep )
	{
		// 時間軸のステップ -> クリップの何コマ目（PingPong の復路は、往路のコマと同じ番号）
		const int32 ClipStep = Timeline->StepSource[EnteredStep];
		for ( const FSpriteAnimEvent& Event : Clip->Events )
		{
			if ( Event.Step == ClipStep ) Events.Add( Event.Name );
		}
	} );
	LastAbsoluteStep = Location.AbsoluteStep;
	Step = Location.Step;

	if ( Timeline->Loop == EAnimLoopMode::Once )
	{
		const bool bAtEnd = Speed >= 0.0f ? Playhead >= Timeline->Total : Playhead <= 0.0;
		if ( bAtEnd )
		{
			Playhead = Speed >= 0.0f ? Timeline->Total : 0.0;
			bPlaying = false;
			bFinished = true;
			bFinishedThisUpdate = true;
		}
	}
}

void FSpriteAnimator::Seek( float32 NormalizedTime )
{
	const FAnimTimeline* Timeline = Sheet.IsValid() ? Sheet->GetTimeline( ClipIndex ) : nullptr;
	if ( Timeline == nullptr || Timeline->IsEmpty() ) return;

	Playhead = NMath::Clamp( static_cast<float64>( NormalizedTime ), 0.0, 1.0 ) * static_cast<float64>( Timeline->Total );
	const FAnimLocation Location = NAnim::Locate( *Timeline, Playhead );
	Step = Location.Step;
	LastAbsoluteStep = Location.AbsoluteStep;
	bFinished = false;
	bFinishedThisUpdate = false;
	Events.Empty();
}

//-----------------------------------------------------------------------------
// 状態
//-----------------------------------------------------------------------------
FString FSpriteAnimator::GetClipName() const
{
	const FSpriteClip* Clip = Sheet.IsValid() ? Sheet->GetClip( ClipIndex ) : nullptr;
	return Clip != nullptr ? Clip->Name : FString();
}

int32 FSpriteAnimator::GetFrameIndex() const
{
	if ( ClipIndex == INDEX_NONE ) return ManualFrame;

	const FAnimTimeline* Timeline = Sheet.IsValid() ? Sheet->GetTimeline( ClipIndex ) : nullptr;
	const FSpriteClip* Clip = Sheet.IsValid() ? Sheet->GetClip( ClipIndex ) : nullptr;
	if ( Timeline == nullptr || Clip == nullptr || !Timeline->StepSource.IsValidIndex( Step ) ) return INDEX_NONE;

	const int32 ClipStep = Timeline->StepSource[Step];
	return Clip->Frames.IsValidIndex( ClipStep ) ? Clip->Frames[ClipStep] : INDEX_NONE;
}

float32 FSpriteAnimator::GetNormalizedTime() const
{
	const FAnimTimeline* Timeline = Sheet.IsValid() ? Sheet->GetTimeline( ClipIndex ) : nullptr;
	if ( Timeline == nullptr || Timeline->Total <= 0.0f ) return 0.0f;

	float64 Time = Playhead;
	if ( Timeline->Loop == EAnimLoopMode::Once ) Time = NMath::Clamp( Time, 0.0, static_cast<float64>( Timeline->Total ) );
	else Time -= std::floor( Time / Timeline->Total ) * Timeline->Total;
	return static_cast<float32>( Time / Timeline->Total );
}

bool FSpriteAnimator::ConsumeEvent( const FString& Name )
{
	const int32 Index = Events.Find( Name );
	if ( Index == INDEX_NONE ) return false;
	Events.RemoveAt( Index );
	return true;
}

//-----------------------------------------------------------------------------
// 描画
//-----------------------------------------------------------------------------
void FSpriteAnimator::Record( FDrawContext& Draw, const FVector2& Position, const FVector2& InScale, float32 AngleRadians ) const
{
	if ( !bVisible || !Sheet.IsValid() || Alpha <= 0.0f ) return;

	const int32 TextureHandle = Sheet->GetTextureHandle();
	const FSpriteFrame* Frame = Sheet->GetFrame( GetFrameIndex() );
	if ( TextureHandle < 0 || Frame == nullptr || Frame->Width <= 0 || Frame->Height <= 0 ) return;

	const bool bTinted = Tint.r != 255 || Tint.g != 255 || Tint.b != 255;
	const bool bTranslucent = Alpha < 1.0f;
	if ( bTinted ) Draw.Tint( Tint );
	if ( bTranslucent ) Draw.Blend( EDrawBlend::Alpha, static_cast<int32>( NMath::Clamp( Alpha, 0.0f, 1.0f ) * 255.0f + 0.5f ) );

	Draw.SpriteRect( Position.x + Offset.x, Position.y + Offset.y, TextureHandle, Frame->X, Frame->Y, Frame->Width, Frame->Height, Frame->PivotX, Frame->PivotY,
					 Scale.x * InScale.x, Scale.y * InScale.y, AngleOffset + AngleRadians, bFlipX, bFlipY );
}

void FSpriteAnimator::RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const
{
	const FVector3& Position = Transform.GetWorldPosition();
	const FVector3& WorldScale = Transform.GetWorldScale();
	const FQuaternion& Q = Transform.GetWorldRotation();

	// Z 軸まわりの回転角（ほかの軸の回転は、2D のスプライトでは使わない）
	float32 Angle = std::atan2( 2.0f * ( Q.w * Q.z + Q.x * Q.y ), 1.0f - 2.0f * ( Q.y * Q.y + Q.z * Q.z ) );
	// 縦横の一方だけ反転する変換では、回る向きも逆になる
	if ( Space.PixelsPerUnit.x * Space.PixelsPerUnit.y < 0.0f ) Angle = -Angle;

	const FVector2 ScreenPosition( Space.Origin.x + Position.x * Space.PixelsPerUnit.x, Space.Origin.y + Position.y * Space.PixelsPerUnit.y );
	Record( Draw, ScreenPosition, FVector2( NMath::Abs( WorldScale.x ), NMath::Abs( WorldScale.y ) ), Angle );
}
