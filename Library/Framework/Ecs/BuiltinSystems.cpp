#include "BuiltinSystems.h"

#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Animation/SpriteAnimator.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/Render/Billboard.h"
#include "Library/Framework/Render/MeshRender.h"
#include "Library/Framework/Ecs/System.h"

void NEcs::RegisterBuiltinSystems( FSystemScheduler& Scheduler )
{
	// 書き込む型が同じ（FTransformAccess）なので、2 つは登録順に 1 つずつ動く。それぞれの中は並列
	Scheduler.Add( EPhase::PreUpdate, "Motion.Angular", []( float32 DeltaTime, FTransformAccess& Transform, const FAngularMotion& Motion )
	{
		const FQuaternion Spin = FQuaternion::FromAxisAngle( Motion.Axis, Motion.RadiansPerSecond * DeltaTime );
		Transform.SetLocalRotation( ( Spin * Transform.GetLocalRotation() ).Normalized() );
	} );

	Scheduler.Add( EPhase::PreUpdate, "Motion.Linear", []( float32 DeltaTime, FTransformAccess& Transform, const FLinearMotion& Motion )
	{
		Transform.SetLocalPosition( Transform.GetLocalPosition() + Motion.Velocity * DeltaTime );
	} );

	// アニメーション（FSpriteAnimator / FModelAnimator をオブジェクトに付けると、更新と描画が自動で動く）
	Scheduler.Add( EPhase::PreUpdate, "Animation.Sprite", []( float32 DeltaTime, FSpriteAnimator& Animator )
	{
		Animator.Update( DeltaTime );
	} );
	Scheduler.Add( EPhase::PreUpdate, "Animation.Model", []( float32 DeltaTime, FModelAnimator& Animator )
	{
		Animator.Update( DeltaTime );
	} );
	// mesh effectの個体時計だけを更新し、記録関数を純粋な読み取りに保つ。
	Scheduler.Add( EPhase::PreUpdate, "Render.MeshEffect.Update", []( float32 DeltaTime, FMeshRenderer& Mesh )
	{
		Mesh.Update( DeltaTime );
	} );

	Scheduler.AddDraw( "Animation.Sprite.Draw", []( FDrawContext& Draw, const FTransformView& Transform, const FSpriteAnimator& Animator )
	{
		if ( Animator.bAutoDraw ) Animator.RecordAt( Draw, Transform );
	} );
	Scheduler.AddDraw( "Animation.Model.Draw", []( FDrawContext& Draw, const FTransformView& Transform, const FModelAnimator& Animator )
	{
		if ( Animator.bAutoDraw ) Animator.RecordAt( Draw, Transform );
	} );

	// 画像を貼った四角形（FBillboard）
	Scheduler.AddDraw( "Render.Billboard.Draw", []( FDrawContext& Draw, const FTransformView& Transform, const FBillboard& Board )
	{
		if ( Board.bAutoDraw ) Board.RecordAt( Draw, Transform );
	} );

	// エフェクト（CEffectComponent が付ける FEffectPlayer）。位置・時間は、コンポーネントの Tick が進めたもの
	Scheduler.AddDraw( "Effect.Draw", []( FDrawContext& Draw, const FTransformView& Transform, const FEffectPlayer& Player )
	{
		(void)Transform;
		if ( Player.bAutoDraw && Player.Playback.IsValid() ) Player.Playback->Record( Draw );
	} );

	// 編集メッシュ（FMeshRenderer）
	Scheduler.AddDraw( "Render.Mesh.Draw", []( FDrawContext& Draw, const FTransformView& Transform, const FMeshRenderer& Mesh )
	{
		if ( Mesh.bAutoDraw ) Mesh.RecordAt( Draw, Transform );
	} );
}
