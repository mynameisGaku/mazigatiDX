#include "EffectsGallery.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/System/CommandLine.h"
#include "Source/Effects/EffectsState.h"

namespace
{
	// 台座どうしの間隔（メートル）
	constexpr float32 Spacing = 4.5f;

	// 帯と雷が、マウスで動ける範囲（台座の中心から、横・上の最大）
	constexpr float32 ReachX = 3.0f;
	constexpr float32 ReachY = 3.5f;

	FEffectsEntry MakeEntry( const char* Title, const char* Path, const char* Description )
	{
		FEffectsEntry Entry;
		Entry.Title = Title;
		Entry.Path = Path;
		Entry.Description = Description;
		return Entry;
	}
}

CEffectsGallery::CEffectsGallery( const TSharedPtr<FEffectsState>& InState )
	: State( InState )
{}

void CEffectsGallery::OnSpawn()
{
	State->Entries.Add( MakeEntry( "Aura", "Effects/aura.gxeffect", "Particles that rise and fade around a point." ) );
	State->Entries.Add( MakeEntry( "Campfire", "Effects/campfire.gxeffect", "Flame and smoke: many particles with colour and size over their life." ) );
	State->Entries.Add( MakeEntry( "Sparks", "Effects/sparks.gxeffect", "Short-lived sparks that fly out and fall." ) );
	State->Entries.Add( MakeEntry( "Ribbon trail", "Effects/ribbon_trail.gxeffect", "A ribbon that follows the moving point. Move the mouse." ) );
	State->Entries.Add( MakeEntry( "Branch lightning", "Effects/branch_lightning.gxeffect", "Lightning with branches, redrawn with a new shape over time." ) );
	State->Entries.Add( MakeEntry( "Target lightning", "Effects/target_lightning.gxeffect", "Lightning to a dynamic end point. Move the mouse." ) );

	for ( int32 Index = 0; Index < State->Entries.Num(); ++Index )
	{
		const float32 X = ( static_cast<float32>( Index ) - static_cast<float32>( State->Entries.Num() - 1 ) * 0.5f ) * Spacing;
		State->Entries[Index].Pedestal = FVector3( X, 0.0f, 0.0f );
		Restart( Index );
	}
	// --select=番号 で、最初に選ぶエフェクトを決められる（画面の確認のため）
	const TOptional<FString> Requested = NCommandLine::GetValue( "select" );
	if ( Requested.IsSet() )
	{
		State->Selected = NMath::Clamp( Requested.GetValue().ToInt(), 0, State->Entries.Num() - 1 );
	}
	CameraX = State->Entries[State->Selected].Pedestal.x;
}

void CEffectsGallery::Restart( int32 Index )
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	FEffectsEntry& Entry = State->Entries[Index];
	FEffectPlayParams Params;
	Params.Scale = Entry.Scale;
	FTransform Transform = FTransform::Identity;
	Transform.Position = Entry.Pedestal + FVector3( 0.0f, 0.6f, 0.0f );
	Entry.Playback = FEffectBackendRegistry::Global().Play( Entry.Path, Assets, Params, Transform );
}

void CEffectsGallery::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	State->Time += DeltaTime;
	const IInput* Input = GetAppSubsystem<IInput>();
	const int32 Count = State->Entries.Num();
	if ( Input != nullptr )
	{
		if ( Input->IsKeyPressed( EKey::Left ) || Input->IsKeyPressed( EKey::A ) )
		{
			State->Selected = ( State->Selected + Count - 1 ) % Count;
		}
		if ( Input->IsKeyPressed( EKey::Right ) || Input->IsKeyPressed( EKey::D ) )
		{
			State->Selected = ( State->Selected + 1 ) % Count;
		}
		FEffectsEntry& Selected = State->Entries[State->Selected];
		if ( Input->IsKeyPressed( EKey::Space ) )
		{
			Restart( State->Selected );
		}
		if ( Input->IsKeyPressed( EKey::Up ) || Input->IsKeyPressed( EKey::Down ) )
		{
			Selected.Scale = NMath::Clamp( Selected.Scale * ( Input->IsKeyPressed( EKey::Up ) ? 1.25f : 0.8f ), 0.25f, 4.0f );
			// 大きさは、再生を始めるときに決まるので、始め直す
			Restart( State->Selected );
		}
		// マウスの指す点: カメラから出る線と、舞台（Z = 0 の面）が交わる点
		FCameraRay Ray;
		const FMouseState& Mouse = Input->GetMouseState();
		if ( State->Camera.ScreenToRay( State->GetViewport(), FVector2( static_cast<float32>( Mouse.X ), static_cast<float32>( Mouse.Y ) ), Ray ) && NMath::Abs( Ray.Direction.z ) > 0.0001f )
		{
			const float32 T = -Ray.Origin.z / Ray.Direction.z;
			if ( T > 0.0f )
			{
				State->MouseWorld = Ray.Origin + Ray.Direction * T;
			}
		}
	}

	// 選んでいる台座の、マウスで動かせる範囲に収めた点
	const FEffectsEntry& Selected = State->Entries[State->Selected];
	const FVector3 Reach(
		NMath::Clamp( State->MouseWorld.x, Selected.Pedestal.x - ReachX, Selected.Pedestal.x + ReachX ),
		NMath::Clamp( State->MouseWorld.y, 0.6f, ReachY ),
		0.0f );

	for ( int32 Index = 0; Index < Count; ++Index )
	{
		FEffectsEntry& Entry = State->Entries[Index];
		if ( !Entry.Playback.IsValid() )
		{
			continue;
		}
		// 一度きりのエフェクト（火花など）は、終わったら、見せ続けるために、始め直す
		if ( Entry.Playback->IsFinished() )
		{
			Restart( Index );
		}
		const bool bIsSelected = Index == State->Selected;
		FTransform Transform = FTransform::Identity;
		Transform.Position = Entry.Pedestal + FVector3( 0.0f, 0.6f, 0.0f );
		// 帯は、選んでいるときだけ、マウスの点を追う（選んでいないものは、台座の上でゆっくり円を描く）
		if ( Entry.Path.Contains( "ribbon_trail" ) )
		{
			Transform.Position = bIsSelected ? Reach : Entry.Pedestal + FVector3( NMath::Cos( State->Time * 2.0f ) * 1.2f, 1.6f + 0.4f * NMath::Sin( State->Time * 4.0f ), 0.0f );
		}
		Entry.Playback->SetTransform( Transform );
		// 雷の終点: 選んでいるものはマウスの点、それ以外は、台座の少し上の横
		Entry.Playback->SetTargetPosition( bIsSelected ? Reach : Entry.Pedestal + FVector3( 2.0f, 2.2f, 0.0f ) );
		Entry.Playback->Update( DeltaTime );
	}

	// カメラ: 選んでいる台座へ、なめらかに寄る
	CameraX += ( Selected.Pedestal.x - CameraX ) * NMath::Min( 1.0f, 5.0f * DeltaTime );
	FCamera Camera = FCamera::MakePerspective( FVector3( CameraX, 2.2f, -6.5f ), FVector3( CameraX, 1.5f, 0.0f ), NMath::PI / 3.2f );
	Camera.Near = 0.3f;
	Camera.Far = 200.0f;
	State->Camera = Camera;
}
