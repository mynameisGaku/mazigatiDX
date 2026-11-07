#include "PhysicsSample.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Library/Physics/Body/RigidBody.h"
#include "Library/Physics/DebugDraw/PhysicsDebugView.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Common/PhysicsView.h"
#include "Source/Physics/PhysicsBody.h"
#include "Source/Physics/RopeJoint.h"
#include "Source/Physics/SensorZone.h"
#include "Source/Physics/StaticBlock.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CPhysicsSample> Registrar( FSampleInfo { "Physics", "Physics: bodies, joints and sensors", "Rigid bodies, a rope, a sensor zone and a ray pick (Jolt). Click a body to kick it; Space adds one; F4 shows the shapes.", 80 } );

	const FColor32 Palette[] = { FColor32( 230, 90, 80 ), FColor32( 240, 190, 70 ), FColor32( 90, 200, 140 ), FColor32( 90, 150, 230 ), FColor32( 190, 120, 230 ) };
}

void CPhysicsSample::OnEnter()
{
	// 物理のデバッグビュー（F4）の線を、この 2D の画面に合わせる（3D のゲームなら要らない）
	NPhysicsDebugView::SetLineMapper( []( FDrawContext& Draw, const FVector3& A, const FVector3& B, const FColor32& Color )
	{
		Draw.Line( NPhysicsSampleView::ToScreenX( A.x ), NPhysicsSampleView::ToScreenY( A.y ), NPhysicsSampleView::ToScreenX( B.x ), NPhysicsSampleView::ToScreenY( B.y ), Color );
	} );

	// 動かないもの: 床と、左右の壁と、足場（画面の幅 1280 px = 25.6 m）
	SpawnObject<CStaticBlock>( FVector3( 12.8f, 0.5f, 0.0f ), FVector3( 12.8f, 0.5f, 2.0f ) );
	SpawnObject<CStaticBlock>( FVector3( 0.25f, 7.0f, 0.0f ), FVector3( 0.25f, 7.0f, 2.0f ) );
	SpawnObject<CStaticBlock>( FVector3( 25.35f, 7.0f, 0.0f ), FVector3( 0.25f, 7.0f, 2.0f ) );
	SpawnObject<CStaticBlock>( FVector3( 6.0f, 4.0f, 0.0f ), FVector3( 3.0f, 0.25f, 2.0f ) );

	// 足場へ落とす物
	for ( int32 Index = 0; Index < 5; ++Index )
	{
		SpawnAt( FVector3( 4.0f + static_cast<float32>( Index ) * 1.0f, 6.5f + static_cast<float32>( Index ) * 0.8f, 0.0f ) );
	}

	// センサーの真上から落とす物（領域を通り抜けて、床に積もる。通り抜ける間、センサーが反応する）
	SpawnAt( FVector3( 13.2f, 8.0f, 0.0f ) );
	SpawnAt( FVector3( 14.8f, 10.0f, 0.0f ) );

	// ロープ: フックの真下から少し横へ外した物を置くと、振り子として揺れる
	const FVector3 Hook( 19.0f, 12.0f, 0.0f );
	TSharedPtr<CPhysicsBody> Weight = SpawnObject<CPhysicsBody>( CPhysicsBody::EShape::Ball, FVector3( 20.5f, 10.0f, 0.0f ), 0.6f, FColor32( 240, 190, 70 ) );
	SpawnObject<CRopeJoint>( Weight, Hook, 4.0f );

	// センサー: 床の近く。ここへ物が落ちて入ると、枠が明るくなる
	Zone = SpawnObject<CSensorZone>( FVector3( 14.0f, 2.0f, 0.0f ), FVector3( 2.0f, 1.0f, 2.0f ) );
}

void CPhysicsSample::OnExit()
{
	// 次のシーンに、この画面の変換を残さない
	NPhysicsDebugView::SetLineMapper( NPhysicsDebugView::FLineMapper() );
}

void CPhysicsSample::SpawnAt( const FVector3& Position )
{
	const CPhysicsBody::EShape Shape = ( NumSpawned % 2 ) == 0 ? CPhysicsBody::EShape::Box : CPhysicsBody::EShape::Ball;
	const float32 Size = 0.35f + 0.1f * static_cast<float32>( NumSpawned % 3 );
	SpawnObject<CPhysicsBody>( Shape, Position, Size, Palette[NumSpawned % 5] );
	++NumSpawned;
}

void CPhysicsSample::PickWithMouse()
{
	const IInput* Input = GetAppSubsystem<IInput>();
	const CPhysicsSubsystem* Physics = GetSubsystem<CPhysicsSubsystem>();
	if ( Input == nullptr || Physics == nullptr )
	{
		return;
	}
	const FMouseState& Mouse = Input->GetMouseState();
	const FVector3 Point = NPhysicsSampleView::ToWorld( Mouse.X, Mouse.Y );

	// 画面の手前（Z が大きいほう）から、奥へ向かってレイを飛ばす。最初に当たった剛体の UserData が、CPhysicsBody
	FRayHit Hit;
	CPhysicsBody* Picked = nullptr;
	if ( Physics->RayCast( FVector3( Point.x, Point.y, 10.0f ), FVector3( 0.0f, 0.0f, -1.0f ), 20.0f, Hit ) )
	{
		Picked = static_cast<CPhysicsBody*>( Hit.UserData );
	}
	bHasPicked = false;
	for ( const TSharedPtr<CPhysicsBody>& Body : FindObjects<CPhysicsBody>() )
	{
		const bool bIsPicked = ( Body.Get() == Picked );
		Body->SetHighlighted( bIsPicked );
		bHasPicked = bHasPicked || bIsPicked;
	}

	// 左クリック: 指している物を、真上へ蹴る
	if ( Picked != nullptr && Picked->GetBody() != nullptr && Input->IsMouseButtonPressed( EMouseButton::Left ) )
	{
		Picked->GetBody()->AddImpulse( FVector3( 0.0f, 6.0f, 0.0f ) );
	}
}

void CPhysicsSample::OnSampleTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input == nullptr )
	{
		return;
	}
	PickWithMouse();
	if ( Input->IsKeyPressed( EKey::Space ) || Input->IsMouseButtonPressed( EMouseButton::Right ) )
	{
		const FMouseState& Mouse = Input->GetMouseState();
		SpawnAt( NPhysicsSampleView::ToWorld( Mouse.X, Mouse.Y ) );
	}
}

void CPhysicsSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.TextF( 16, 64, FColor32::Gray, "Bodies: %d   Sensor: %d inside, %d entered in total", FindObjects<CPhysicsBody>().Num(), Zone != nullptr ? Zone->GetNumInside() : 0, Zone != nullptr ? Zone->GetNumEntered() : 0 );
	Screen.Text( 16, 80, "Left click: kick the body under the mouse    Space / right click: add a body    F4: physics shapes", FColor32::Gray );
}
