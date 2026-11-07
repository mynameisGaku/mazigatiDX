#include "PlatformerParts.h"

#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/AI/Registry/AIRegistry.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Common/PlatformerBody.h"

namespace
{
	IPlatformerPlayerBody* PlayerOf( FAIContext& Context )
	{
		return dynamic_cast<IPlatformerPlayerBody*>( Context.Owner );
	}

	IPlatformerEnemyBody* EnemyOf( FAIContext& Context )
	{
		return dynamic_cast<IPlatformerEnemyBody*>( Context.Owner );
	}

	//=========================================================================
	// 主人公
	//=========================================================================
	class FAction_EnterAlive final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerPlayerBody* Body = PlayerOf( Context ) )
			{
				Body->EnterAlive();
			}
		}
	};

	class FAction_SetForm final : public FAIStateAction
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			const FString& Name = Params.GetText( "Form" );
			Form = Name == "Fire" ? EPlatformerForm::Fire : ( Name == "Big" ? EPlatformerForm::Big : EPlatformerForm::Small );
		}

		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerPlayerBody* Body = PlayerOf( Context ) )
			{
				Body->OnFormEntered( Form );
			}
		}

	private:
		EPlatformerForm Form = EPlatformerForm::Small;
	};

	class FAction_HurtBreather final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerPlayerBody* Body = PlayerOf( Context ) )
			{
				Body->BeginHurtBreather();
			}
		}
	};

	class FAction_PlayerAlive final : public FAIStateAction
	{
	public:
		void OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if ( IPlatformerPlayerBody* Body = PlayerOf( Context ) )
			{
				Body->StepAlive( DeltaTime );
			}
		}
	};

	class FAction_PlayerDeath final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerPlayerBody* Body = PlayerOf( Context ) )
			{
				Body->BeginDeath();
			}
		}

		void OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if ( IPlatformerPlayerBody* Body = PlayerOf( Context ) )
			{
				Body->StepDying( DeltaTime );
			}
		}
	};

	class FAction_PlayerGoal final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerPlayerBody* Body = PlayerOf( Context ) )
			{
				Body->BeginGoal();
			}
		}

		void OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if ( IPlatformerPlayerBody* Body = PlayerOf( Context ) )
			{
				Body->StepGoal( DeltaTime );
			}
		}
	};

	class FAction_PlayerGameOver final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerPlayerBody* Body = PlayerOf( Context ) )
			{
				Body->BeginGameOver();
			}
		}
	};

	//=========================================================================
	// 敵
	//=========================================================================
	class FAction_EnemyWalk final : public FAIStateAction
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Speed = Params.GetFloat( "Speed" );
			bAvoidEdges = Params.GetBool( "AvoidEdges" );
		}

		void OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->StepWalk( DeltaTime, Speed, bAvoidEdges );
			}
		}

	private:
		float32 Speed = 1.0f;
		bool bAvoidEdges = false;
	};

	// 止まった甲羅: 入るとき、甲羅にこもる。いる間は、止まったまま（重力だけ）
	class FAction_EnemyShell final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->BeginShell();
			}
		}

		void OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->StepRest( DeltaTime );
			}
		}
	};

	class FAction_EnemySlide final : public FAIStateAction
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Speed = Params.GetFloat( "Speed" );
		}

		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->BeginSlide();
			}
		}

		void OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->StepSlide( DeltaTime, Speed );
			}
		}

	private:
		float32 Speed = 10.0f;
	};

	class FAction_EnemySquash final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->BeginSquash();
			}
		}
	};

	class FAction_EnemyRemove final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->Remove();
			}
		}
	};

	//=========================================================================
	// 敵の頭脳（ビヘイビアツリー Chaser3D.gxbt）の Task
	//=========================================================================
	// 主人公へ向かって歩く（条件が偽になるまで、続く）
	class FTask_EnemyChaseTarget final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Speed = Params.GetFloat( "Speed" );
		}

		EBTStatus OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->StepChase( DeltaTime, Speed );
				return EBTStatus::Running;
			}
			return EBTStatus::Failure;
		}

	private:
		float32 Speed = 3.0f;
	};

	// Seconds 秒、いまの向きに歩く（壁と崖で、向きを変える）
	class FTask_EnemyWander final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Speed = Params.GetFloat( "Speed" );
			Seconds = Params.GetFloat( "Seconds" );
		}

		void OnEnter( FAIContext& ) override
		{
			Elapsed = 0.0f;
		}

		EBTStatus OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			IPlatformerEnemyBody* Body = EnemyOf( Context );
			if ( Body == nullptr )
			{
				return EBTStatus::Failure;
			}
			Body->StepWalk( DeltaTime, Speed, true );
			Elapsed += DeltaTime;
			return Elapsed >= Seconds ? EBTStatus::Success : EBTStatus::Running;
		}

	private:
		float32 Speed = 1.0f;
		float32 Seconds = 2.0f;
		float32 Elapsed = 0.0f;
	};

	//=========================================================================
	// 自動操作（ビヘイビアツリー Autopilot*.gxbt）。操作は、ブラックボードへ書く（人のキー入力と同じ入口）
	//=========================================================================
	void SetFloat( CBlackboard& Blackboard, const char* Name, float32 Value )
	{
		Blackboard.SetFloat( Blackboard.FindKey( Name ), Value );
	}

	void SetBool( CBlackboard& Blackboard, const char* Name, bool bValue )
	{
		Blackboard.SetBool( Blackboard.FindKey( Name ), bValue );
	}

	// 決まった向きへ走り続ける（操作の下地）
	class FTask_PilotCruise final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			MoveX = Params.GetFloat( "MoveX" );
			MoveY = Params.GetFloat( "MoveY" );
			bRun = Params.GetBool( "Run" );
		}

		EBTStatus OnTick( FAIContext& Context, float32 ) override
		{
			if ( Context.Blackboard == nullptr )
			{
				return EBTStatus::Failure;
			}
			SetFloat( *Context.Blackboard, "MoveX", MoveX );
			SetFloat( *Context.Blackboard, "MoveY", MoveY );
			SetBool( *Context.Blackboard, "Run", bRun );
			return EBTStatus::Running;
		}

	private:
		float32 MoveX = 0.0f;
		float32 MoveY = 0.0f;
		bool bRun = true;
	};

	// 近くのアイテムへ向かう（歩いて）
	class FTask_PilotSeekItem final : public FAITask
	{
	public:
		EBTStatus OnTick( FAIContext& Context, float32 ) override
		{
			CBlackboard* Blackboard = Context.Blackboard;
			if ( Blackboard == nullptr )
			{
				return EBTStatus::Failure;
			}
			SetFloat( *Blackboard, "MoveX", Blackboard->GetFloat( Blackboard->FindKey( "ItemMoveX" ) ) );
			SetFloat( *Blackboard, "MoveY", Blackboard->GetFloat( Blackboard->FindKey( "ItemMoveY" ) ) );
			SetBool( *Blackboard, "Run", false );
			return EBTStatus::Running;
		}
	};

	// ジャンプ: 押して（JumpPressed）、HoldSeconds 秒、押し続ける（JumpHeld。高く跳ぶ）
	class FTask_PilotJump final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			HoldSeconds = Params.GetFloat( "HoldSeconds" );
		}

		void OnEnter( FAIContext& Context ) override
		{
			Elapsed = 0.0f;
			if ( Context.Blackboard != nullptr )
			{
				SetBool( *Context.Blackboard, "JumpPressed", true );
			}
		}

		EBTStatus OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if ( Context.Blackboard == nullptr )
			{
				return EBTStatus::Failure;
			}
			Elapsed += DeltaTime;
			if ( Elapsed >= HoldSeconds )
			{
				// 押すのをやめる（終わったときの後始末は、OnTick の中で済ませる）
				SetBool( *Context.Blackboard, "JumpHeld", false );
				return EBTStatus::Success;
			}
			SetBool( *Context.Blackboard, "JumpHeld", true );
			return EBTStatus::Running;
		}

		void OnAbort( FAIContext& Context ) override
		{
			if ( Context.Blackboard != nullptr )
			{
				SetBool( *Context.Blackboard, "JumpHeld", false );
			}
		}

	private:
		float32 HoldSeconds = 0.5f;
		float32 Elapsed = 0.0f;
	};

	class FTask_PilotFire final : public FAITask
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( Context.Blackboard != nullptr )
			{
				SetBool( *Context.Blackboard, "FirePressed", true );
			}
		}

		EBTStatus OnTick( FAIContext&, float32 ) override
		{
			return EBTStatus::Success;
		}
	};

	// センサー: 主人公のまわりの様子（跳ぶべきか・撃てるか・近くのアイテム）を、ブラックボードへ書く
	class FService_PilotSense final : public FAIService
	{
	public:
		void Tick( FAIContext& Context, float32 ) override
		{
			IPlatformerPilotBody* Body = dynamic_cast<IPlatformerPilotBody*>( Context.Owner );
			if ( Body != nullptr && Context.Blackboard != nullptr )
			{
				Body->Sense( *Context.Blackboard );
			}
		}
	};

	class FAction_EnemyFly final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->BeginFly();
			}
		}

		void OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if ( IPlatformerEnemyBody* Body = EnemyOf( Context ) )
			{
				Body->StepFly( DeltaTime );
			}
		}
	};
}

void NPlatformerParts::Register()
{
	static bool bRegistered = false;
	if ( bRegistered )
	{
		return;
	}
	bRegistered = true;

	NAI::RegisterStateAction<FAction_EnterAlive>( "Player.EnterAlive", FParamSchema(), "Player", "Enter the alive state (go back to the start after dying)" );
	NAI::RegisterStateAction<FAction_SetForm>( "Player.SetForm", FParamSchema().String( "Form", "Small" ).Hint( "Small / Big / Fire" ), "Player", "The form the player has now" );
	NAI::RegisterStateAction<FAction_HurtBreather>( "Player.HurtBreather", FParamSchema(), "Player", "Short invincibility after shrinking" );
	NAI::RegisterStateAction<FAction_PlayerAlive>( "Player.Alive", FParamSchema(), "Player", "Run, jump, stomp, hit blocks, collect (every frame while alive)" );
	NAI::RegisterStateAction<FAction_PlayerDeath>( "Player.Death", FParamSchema(), "Player", "Lose a life and fall" );
	NAI::RegisterStateAction<FAction_PlayerGoal>( "Player.Goal", FParamSchema(), "Player", "Reach the goal" );
	NAI::RegisterStateAction<FAction_PlayerGameOver>( "Player.GameOver", FParamSchema(), "Player", "Game over" );

	NAI::RegisterStateAction<FAction_EnemyWalk>( "Enemy.Walk", FParamSchema().Float( "Speed", 1.0f ).Range( 0.0f, 2000.0f ).Hint( "Walking speed" ).Bool( "AvoidEdges", false ), "Enemy", "Walk forward (turn at walls, and at edges if asked)" );
	NAI::RegisterStateAction<FAction_EnemyShell>( "Enemy.Shell", FParamSchema(), "Enemy", "Hide in the shell and stay still" );
	NAI::RegisterStateAction<FAction_EnemySlide>( "Enemy.Slide", FParamSchema().Float( "Speed", 10.0f ).Range( 0.0f, 2000.0f ), "Enemy", "Slide away, knocking other enemies off" );
	NAI::RegisterStateAction<FAction_EnemySquash>( "Enemy.Squash", FParamSchema(), "Enemy", "Become flat" );
	NAI::RegisterStateAction<FAction_EnemyRemove>( "Enemy.Remove", FParamSchema(), "Enemy", "Disappear" );
	NAI::RegisterStateAction<FAction_EnemyFly>( "Enemy.Fly", FParamSchema(), "Enemy", "Be knocked off: fly and fall" );

	// ビヘイビアツリーの Task と Service
	NAI::RegisterTask<FTask_EnemyChaseTarget>( "Enemy.ChaseTarget", FParamSchema().Float( "Speed", 3.0f ).Range( 0.0f, 2000.0f ), "Enemy", "Walk toward the player (keeps running)" );
	NAI::RegisterTask<FTask_EnemyWander>( "Enemy.Wander", FParamSchema().Float( "Speed", 1.0f ).Range( 0.0f, 2000.0f ).Float( "Seconds", 2.0f ).Range( 0.0f, 60.0f ), "Enemy", "Walk for some seconds" );
	NAI::RegisterTask<FTask_PilotCruise>( "Platformer.Cruise", FParamSchema().Float( "MoveX", 0.0f ).Range( -1.0f, 1.0f ).Float( "MoveY", 0.0f ).Range( -1.0f, 1.0f ).Bool( "Run", true ), "Autopilot", "Keep running in one direction (the base of the controls)" );
	NAI::RegisterTask<FTask_PilotSeekItem>( "Platformer.SeekItem", FParamSchema(), "Autopilot", "Walk toward a nearby item" );
	NAI::RegisterTask<FTask_PilotJump>( "Platformer.Jump", FParamSchema().Float( "HoldSeconds", 0.5f ).Range( 0.0f, 2.0f ), "Autopilot", "Press jump, and hold it for a while" );
	NAI::RegisterTask<FTask_PilotFire>( "Platformer.Fire", FParamSchema(), "Autopilot", "Press the fireball button" );
	NAI::RegisterService<FService_PilotSense>( "Platformer.Sense", FParamSchema(), "Autopilot", "Look around: should jump, can shoot, item nearby" );
}
