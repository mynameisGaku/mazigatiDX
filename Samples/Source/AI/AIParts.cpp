// SPDX-License-Identifier: MIT
#include "AIParts.h"

#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Math/MathUtils.h"
#include "Source/AI/AIActor.h"
#include "Source/AI/AIGuard.h"
#include "Source/AI/AIHero.h"

namespace
{
	// AI の文脈の持ち主は、このサンプルでは必ず CAIActor（ステートマシン・ビヘイビアツリーを付けるのは CAIHero / CAIGuard だけ）
	CAIActor* ActorOf( FAIContext& Context )
	{
		return static_cast<CAIActor*>( Context.Owner );
	}

	//=========================================================================
	// ステートの処理（Hero.gxstate）
	//=========================================================================
	class FAction_Halt final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( CAIActor* Actor = ActorOf( Context ) )
			{
				Actor->SetVelocity( FVector2( 0.0f, 0.0f ) );
			}
		}
	};

	class FAction_Move final : public FAIStateAction
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Speed = Params.GetFloat( "Speed" );
		}

		void OnTick( FAIContext& Context, float32 ) override
		{
			CAIActor* Actor = ActorOf( Context );
			if ( Actor == nullptr || Context.Blackboard == nullptr )
			{
				return;
			}
			const int32 Axis = Context.Blackboard->GetInt( Context.Blackboard->FindKey( "MoveAxis" ) );
			Actor->SetVelocity( FVector2( static_cast<float32>( Axis ) * Speed, 0.0f ) );
			if ( Axis != 0 )
			{
				Actor->FaceToward( Actor->GetPosition().x + static_cast<float32>( Axis ) );
			}
		}

		void OnExit( FAIContext& Context ) override
		{
			if ( CAIActor* Actor = ActorOf( Context ) )
			{
				Actor->SetVelocity( FVector2( 0.0f, 0.0f ) );
			}
		}

	private:
		float32 Speed = 180.0f;
	};

	// 攻撃の出来事（Hit）で、前にいる見張りを殴る
	class FAction_HeroStrike final : public FAIStateAction
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Reach = Params.GetFloat( "Reach" );
		}

		void OnEvent( FAIContext& Context, const FString& ) override
		{
			CAIActor* Actor = ActorOf( Context );
			if ( Actor == nullptr || Context.Scene == nullptr )
			{
				return;
			}
			for ( const TSharedPtr<CAIGuard>& Guard : Context.Scene->FindObjects<CAIGuard>() )
			{
				const float32 Offset = ( Guard->GetPosition().x - Actor->GetPosition().x ) * Actor->GetFacing();
				if ( Offset > 0.0f && Offset <= Reach && NMath::Abs( Guard->GetPosition().y - Actor->GetPosition().y ) < 40.0f )
				{
					Guard->ApplyDamage( 1 );
				}
			}
		}

	private:
		float32 Reach = 90.0f;
	};

	//=========================================================================
	// Task（Guard.gxbt）
	//=========================================================================
	class FTask_PlayClip final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Clip = Params.GetText( "Clip" );
			bWait = Params.GetBool( "WaitUntilFinished" );
		}

		void OnEnter( FAIContext& Context ) override
		{
			if ( CAIActor* Actor = ActorOf( Context ) )
			{
				Actor->SetVelocity( FVector2( 0.0f, 0.0f ) );
			}
			if ( Context.Animation != nullptr )
			{
				Context.Animation->PlayClip( Clip, true );
			}
		}

		EBTStatus OnTick( FAIContext& Context, float32 ) override
		{
			if ( !bWait || Context.Animation == nullptr )
			{
				return EBTStatus::Success;
			}
			return Context.Animation->IsClipFinished() ? EBTStatus::Success : EBTStatus::Running;
		}

	private:
		FString Clip;
		bool bWait = false;
	};

	// 巡回: Home を中心に、左右 Range の 2 点を行き来する
	class FTask_MoveToPatrolPoint final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Speed = Params.GetFloat( "Speed" );
			Range = Params.GetFloat( "Range" );
		}

		void OnEnter( FAIContext& Context ) override
		{
			CAIActor* Actor = ActorOf( Context );
			if ( Actor == nullptr )
			{
				return;
			}
			if ( !bHomeSet )
			{
				bHomeSet = true;
				HomeX = Actor->GetPosition().x;
			}
			bGoRight = !bGoRight;
			PointX = HomeX + ( bGoRight ? Range : -Range );
			if ( Context.Animation != nullptr )
			{
				Context.Animation->PlayClip( "Run", false );
			}
		}

		EBTStatus OnTick( FAIContext& Context, float32 ) override
		{
			CAIActor* Actor = ActorOf( Context );
			if ( Actor == nullptr )
			{
				return EBTStatus::Failure;
			}
			const float32 Offset = PointX - Actor->GetPosition().x;
			if ( NMath::Abs( Offset ) < 6.0f )
			{
				Actor->SetVelocity( FVector2( 0.0f, 0.0f ) );
				if ( Context.Animation != nullptr )
				{
					Context.Animation->PlayClip( "Idle", false );
				}
				return EBTStatus::Success;
			}
			Actor->FaceToward( PointX );
			Actor->SetVelocity( FVector2( Offset > 0.0f ? Speed : -Speed, 0.0f ) );
			return EBTStatus::Running;
		}

		void OnAbort( FAIContext& Context ) override
		{
			if ( CAIActor* Actor = ActorOf( Context ) )
			{
				Actor->SetVelocity( FVector2( 0.0f, 0.0f ) );
			}
		}

	private:
		float32 Speed = 70.0f;
		float32 Range = 160.0f;
		float32 HomeX = 0.0f;
		float32 PointX = 0.0f;
		bool bHomeSet = false;
		bool bGoRight = false;
	};

	// 目標（ブラックボードの TargetPos）へ近づく。着いても終わらず（Running）、目標を失う（HasTarget が偽）と中断される
	class FTask_MoveToTarget final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Speed = Params.GetFloat( "Speed" );
			AcceptRange = Params.GetFloat( "AcceptRange" );
		}

		EBTStatus OnTick( FAIContext& Context, float32 ) override
		{
			CAIActor* Actor = ActorOf( Context );
			if ( Actor == nullptr || Context.Blackboard == nullptr )
			{
				return EBTStatus::Failure;
			}
			const FVector3 Target = Context.Blackboard->GetVector( Context.Blackboard->FindKey( "TargetPos" ) );
			const float32 Offset = Target.x - Actor->GetPosition().x;
			Actor->FaceToward( Target.x );
			if ( NMath::Abs( Offset ) <= AcceptRange )
			{
				Actor->SetVelocity( FVector2( 0.0f, 0.0f ) );
				if ( Context.Animation != nullptr )
				{
					Context.Animation->PlayClip( "Idle", false );
				}
			}
			else
			{
				Actor->SetVelocity( FVector2( Offset > 0.0f ? Speed : -Speed, 0.0f ) );
				if ( Context.Animation != nullptr )
				{
					Context.Animation->PlayClip( "Run", false );
				}
			}
			return EBTStatus::Running;
		}

		void OnAbort( FAIContext& Context ) override
		{
			if ( CAIActor* Actor = ActorOf( Context ) )
			{
				Actor->SetVelocity( FVector2( 0.0f, 0.0f ) );
			}
		}

	private:
		float32 Speed = 140.0f;
		float32 AcceptRange = 55.0f;
	};

	class FTask_FaceTarget final : public FAITask
	{
	public:
		EBTStatus OnTick( FAIContext& Context, float32 ) override
		{
			CAIActor* Actor = ActorOf( Context );
			if ( Actor == nullptr || Context.Blackboard == nullptr )
			{
				return EBTStatus::Failure;
			}
			Actor->FaceToward( Context.Blackboard->GetVector( Context.Blackboard->FindKey( "TargetPos" ) ).x );
			return EBTStatus::Success;
		}
	};

	// 殴る: Attack のクリップを再生し、出来事 Hit の瞬間に、近くにいる主人公へ傷を与える。クリップが終わると成功
	class FTask_Strike final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Reach = Params.GetFloat( "Reach" );
		}

		void OnEnter( FAIContext& Context ) override
		{
			if ( CAIActor* Actor = ActorOf( Context ) )
			{
				Actor->SetVelocity( FVector2( 0.0f, 0.0f ) );
			}
			if ( Context.Animation != nullptr )
			{
				Context.Animation->PlayClip( "Attack", true );
			}
		}

		EBTStatus OnTick( FAIContext& Context, float32 ) override
		{
			CAIActor* Actor = ActorOf( Context );
			if ( Actor == nullptr || Context.Animation == nullptr )
			{
				return EBTStatus::Failure;
			}
			if ( Context.Animation->ConsumeClipEvent( "Hit" ) && Context.Scene != nullptr && Context.Blackboard != nullptr )
			{
				const TSharedPtr<CGameObject> Target = Context.Scene->FindObjectById( Context.Blackboard->GetObjectId( Context.Blackboard->FindKey( "Target" ) ) );
				if ( Target.IsValid() )
				{
					CAIHero* Hero = static_cast<CAIHero*>( Target.Get() );
					const float32 Offset = ( Hero->GetPosition().x - Actor->GetPosition().x ) * Actor->GetFacing();
					if ( Offset > 0.0f && Offset <= Reach )
					{
						Hero->ApplyDamage( 1 );
					}
				}
			}
			return Context.Animation->IsClipFinished() ? EBTStatus::Success : EBTStatus::Running;
		}

		void OnAbort( FAIContext& Context ) override
		{
			if ( Context.Animation != nullptr )
			{
				Context.Animation->PlayClip( "Idle", false );
			}
		}

	private:
		float32 Reach = 80.0f;
	};

	//=========================================================================
	// Service（Guard.gxbt）
	//=========================================================================
	// 主人公との距離を測って、見えているか・殴れる距離かを、ブラックボードへ書く（条件は、センサーのコードを知らず、ブラックボードだけを読む）
	class FService_SensePlayer final : public FAIService
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			SightRange = Params.GetFloat( "SightRange" );
			AttackRange = Params.GetFloat( "AttackRange" );
		}

		void Tick( FAIContext& Context, float32 ) override
		{
			CAIActor* Actor = ActorOf( Context );
			CBlackboard* Blackboard = Context.Blackboard;
			if ( Actor == nullptr || Blackboard == nullptr || Context.Scene == nullptr )
			{
				return;
			}
			const TSharedPtr<CGameObject> Target = Context.Scene->FindObjectById( Blackboard->GetObjectId( Blackboard->FindKey( "Target" ) ) );
			if ( !Target.IsValid() )
			{
				Blackboard->SetBool( Blackboard->FindKey( "HasTarget" ), false );
				Blackboard->SetBool( Blackboard->FindKey( "InAttackRange" ), false );
				return;
			}
			const FVector2 Position = static_cast<CAIActor*>( Target.Get() )->GetPosition();
			const float32 Distance = NMath::Abs( Position.x - Actor->GetPosition().x );
			Blackboard->SetVector( Blackboard->FindKey( "TargetPos" ), FVector3( Position.x, Position.y, 0.0f ) );
			Blackboard->SetFloat( Blackboard->FindKey( "DistanceToTarget" ), Distance );
			Blackboard->SetBool( Blackboard->FindKey( "HasTarget" ), Distance <= SightRange );
			Blackboard->SetBool( Blackboard->FindKey( "InAttackRange" ), Distance <= AttackRange );
		}

	private:
		float32 SightRange = 320.0f;
		float32 AttackRange = 70.0f;
	};
}

void NAISample::RegisterParts()
{
	static bool bRegistered = false;
	if ( bRegistered )
	{
		return;
	}
	bRegistered = true;

	NAI::RegisterStateAction<FAction_Halt>( "Halt", FParamSchema(), "Movement", "止まる" );
	NAI::RegisterStateAction<FAction_Move>( "Move", FParamSchema().Float( "Speed", 180.0f ).Range( 0.0f, 600.0f ).Hint( "歩く速さ（ピクセル / 秒）" ), "Movement", "MoveAxis の向きに歩く" );
	NAI::RegisterStateAction<FAction_HeroStrike>( "HeroStrike", FParamSchema().Float( "Reach", 90.0f ).Range( 0.0f, 400.0f ).Hint( "殴れる距離" ), "Combat", "攻撃の出来事で、前の見張りを殴る" );

	NAI::RegisterTask<FTask_PlayClip>( "PlayClip", FParamSchema().Clip( "Clip" ).Required().Bool( "WaitUntilFinished", true ), "Animation", "クリップを再生する" );
	NAI::RegisterTask<FTask_MoveToPatrolPoint>( "MoveToPatrolPoint", FParamSchema().Float( "Speed", 70.0f ).Range( 0.0f, 600.0f ).Float( "Range", 160.0f ).Range( 0.0f, 600.0f ).Hint( "出発点の左右へ行く距離" ), "Movement", "巡回の次の地点へ行く" );
	NAI::RegisterTask<FTask_MoveToTarget>( "MoveToTarget", FParamSchema().Float( "Speed", 140.0f ).Range( 0.0f, 600.0f ).Float( "AcceptRange", 55.0f ).Range( 0.0f, 400.0f ), "Movement", "TargetPos へ近づく（終わらない）" );
	NAI::RegisterTask<FTask_FaceTarget>( "FaceTarget", FParamSchema(), "Movement", "TargetPos の向きを見る" );
	NAI::RegisterTask<FTask_Strike>( "Strike", FParamSchema().Float( "Reach", 80.0f ).Range( 0.0f, 400.0f ), "Combat", "殴る" );
	NAI::RegisterService<FService_SensePlayer>( "SensePlayer", FParamSchema().Float( "SightRange", 320.0f ).Range( 0.0f, 2000.0f ).Float( "AttackRange", 70.0f ).Range( 0.0f, 400.0f ), "Sensor", "主人公との距離を測る" );
}
