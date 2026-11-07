#include "AdventureParts.h"

#include "Library/Framework/AI/Registry/AIRegistry.h"
#include "Source/Adventure/AdventureEnemy.h"
#include "Source/Adventure/AdventurePlayer.h"

namespace
{
	CAdventurePlayer* PlayerOf( FAIContext& Context )
	{
		return dynamic_cast<CAdventurePlayer*>( Context.Owner );
	}

	CAdventureEnemy* EnemyOf( FAIContext& Context )
	{
		return dynamic_cast<CAdventureEnemy*>( Context.Owner );
	}

	// ステートの処理を、「入る」「いる間」「出る」の、持ち主の関数へ、つなぐだけの部品（どれも、無くてよい）
	template<void ( CAdventurePlayer::*Enter )(), void ( CAdventurePlayer::*Tick )( float32 ), void ( CAdventurePlayer::*Exit )()>
	class TPlayerAction final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if constexpr ( Enter != nullptr )
			{
				if ( CAdventurePlayer* Player = PlayerOf( Context ) )
				{
					( Player->*Enter )();
				}
			}
		}

		void OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if constexpr ( Tick != nullptr )
			{
				if ( CAdventurePlayer* Player = PlayerOf( Context ) )
				{
					( Player->*Tick )( DeltaTime );
				}
			}
		}

		void OnExit( FAIContext& Context ) override
		{
			if constexpr ( Exit != nullptr )
			{
				if ( CAdventurePlayer* Player = PlayerOf( Context ) )
				{
					( Player->*Exit )();
				}
			}
		}
	};

	template<void ( CAdventureEnemy::*Enter )(), void ( CAdventureEnemy::*Tick )( float32 )>
	class TEnemyAction final : public FAIStateAction
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if constexpr ( Enter != nullptr )
			{
				if ( CAdventureEnemy* Enemy = EnemyOf( Context ) )
				{
					( Enemy->*Enter )();
				}
			}
		}

		void OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			if constexpr ( Tick != nullptr )
			{
				if ( CAdventureEnemy* Enemy = EnemyOf( Context ) )
				{
					( Enemy->*Tick )( DeltaTime );
				}
			}
		}
	};

	//=========================================================================
	// 敵の頭脳の Task（Enemy.gxbt）
	//=========================================================================
	// 追いかける（条件が偽になるまで、続く）
	class FTask_EnemyChase final : public FAITask
	{
	public:
		EBTStatus OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			CAdventureEnemy* Enemy = EnemyOf( Context );
			if ( Enemy == nullptr )
			{
				return EBTStatus::Failure;
			}
			Enemy->StepChase( DeltaTime );
			return EBTStatus::Running;
		}
	};

	// 巡回: 家の近くの点へ歩いて、着いたら成功（次に、少し待つ）
	class FTask_EnemyPatrol final : public FAITask
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( CAdventureEnemy* Enemy = EnemyOf( Context ) )
			{
				Enemy->BeginPatrol();
			}
		}

		EBTStatus OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			CAdventureEnemy* Enemy = EnemyOf( Context );
			if ( Enemy == nullptr )
			{
				return EBTStatus::Failure;
			}
			return Enemy->StepPatrol( DeltaTime ) ? EBTStatus::Success : EBTStatus::Running;
		}
	};

	// 殴る: 構え → 当たり → 戻る。終わったら成功
	class FTask_EnemyStrike final : public FAITask
	{
	public:
		void OnEnter( FAIContext& Context ) override
		{
			if ( CAdventureEnemy* Enemy = EnemyOf( Context ) )
			{
				Enemy->BeginStrike();
			}
		}

		EBTStatus OnTick( FAIContext& Context, float32 DeltaTime ) override
		{
			CAdventureEnemy* Enemy = EnemyOf( Context );
			if ( Enemy == nullptr )
			{
				return EBTStatus::Failure;
			}
			return Enemy->StepStrike( DeltaTime ) ? EBTStatus::Success : EBTStatus::Running;
		}
	};
}

void NAdventureParts::Register()
{
	static bool bRegistered = false;
	if ( bRegistered )
	{
		return;
	}
	bRegistered = true;

	using FIdle = TPlayerAction<nullptr, &CAdventurePlayer::StepIdle, nullptr>;
	using FRun = TPlayerAction<nullptr, &CAdventurePlayer::StepRun, nullptr>;
	using FAttack = TPlayerAction<&CAdventurePlayer::BeginAttack, &CAdventurePlayer::StepAttack, nullptr>;
	using FRoll = TPlayerAction<&CAdventurePlayer::BeginRoll, &CAdventurePlayer::StepRoll, &CAdventurePlayer::EndRoll>;
	using FHurt = TPlayerAction<&CAdventurePlayer::BeginHurt, &CAdventurePlayer::StepHurt, nullptr>;
	using FDead = TPlayerAction<&CAdventurePlayer::BeginDead, nullptr, nullptr>;
	NAI::RegisterStateAction<FIdle>( "Adv.Player.Idle", FParamSchema(), "Player", "Stand still" );
	NAI::RegisterStateAction<FRun>( "Adv.Player.Run", FParamSchema(), "Player", "Run in the input direction" );
	NAI::RegisterStateAction<FAttack>( "Adv.Player.Attack", FParamSchema(), "Player", "Swing the sword (hits enemies in front)" );
	NAI::RegisterStateAction<FRoll>( "Adv.Player.Roll", FParamSchema(), "Player", "Roll (invincible)" );
	NAI::RegisterStateAction<FHurt>( "Adv.Player.Hurt", FParamSchema(), "Player", "Get knocked back" );
	NAI::RegisterStateAction<FDead>( "Adv.Player.Dead", FParamSchema(), "Player", "Fall down" );

	using FEnemyHurt = TEnemyAction<&CAdventureEnemy::BeginHurt, &CAdventureEnemy::StepHurt>;
	using FEnemyDead = TEnemyAction<&CAdventureEnemy::BeginDead, nullptr>;
	using FEnemyRemove = TEnemyAction<&CAdventureEnemy::Remove, nullptr>;
	NAI::RegisterStateAction<FEnemyHurt>( "Adv.Enemy.Hurt", FParamSchema(), "Enemy", "Get knocked back" );
	NAI::RegisterStateAction<FEnemyDead>( "Adv.Enemy.Dead", FParamSchema(), "Enemy", "Fall (drops an item, records the defeat)" );
	NAI::RegisterStateAction<FEnemyRemove>( "Adv.Enemy.Remove", FParamSchema(), "Enemy", "Disappear" );

	NAI::RegisterTask<FTask_EnemyChase>( "Adv.Enemy.Chase", FParamSchema(), "Enemy", "Walk toward the player (keeps running)" );
	NAI::RegisterTask<FTask_EnemyPatrol>( "Adv.Enemy.Patrol", FParamSchema(), "Enemy", "Walk to a point near home" );
	NAI::RegisterTask<FTask_EnemyStrike>( "Adv.Enemy.Strike", FParamSchema(), "Enemy", "Wind up, hit the player, recover" );
}
