// SPDX-License-Identifier: MIT
#include "BuiltinParts.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/AILog.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"

namespace
{
	class FTask_Wait final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Seconds = Params.GetFloat( "Seconds" );
		}

		void OnEnter( FAIContext& ) override
		{
			Remaining = Seconds;
		}

		EBTStatus OnTick( FAIContext&, float32 DeltaTime ) override
		{
			Remaining -= DeltaTime;
			return Remaining <= 0.0f ? EBTStatus::Success : EBTStatus::Running;
		}

	private:
		float32 Seconds = 1.0f;
		float32 Remaining = 0.0f;
	};

	class FTask_SetBlackboard final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			KeyName = Params.GetText( "Key" );
			Value = Params.GetJson( "Value" );
		}

		EBTStatus OnTick( FAIContext& Context, float32 ) override
		{
			if ( Context.Blackboard == nullptr )
			{
				return EBTStatus::Failure;
			}
			const FBlackboardKey Key = Context.Blackboard->FindKey( KeyName );
			if ( !Key.IsValid() )
			{
				LOG( LogAI, Warning, "SetBlackboard: the key '%s' does not exist.", *KeyName );
				return EBTStatus::Failure;
			}
			FBlackboardValue Converted;
			if ( !FBlackboardDefinition::ValueFromJson( Context.Blackboard->GetDefinition().Keys[Key.Index], Value, Converted ) )
			{
				LOG( LogAI, Warning, "SetBlackboard: the Value does not fit the key '%s'.", *KeyName );
				return EBTStatus::Failure;
			}
			Context.Blackboard->SetValue( Key, Converted );
			return EBTStatus::Success;
		}

	private:
		FString KeyName;
		NlohmannJson Value;
	};

	class FTask_Log final : public FAITask
	{
	public:
		void Initialize( const FParamSet& Params ) override
		{
			Message = Params.GetText( "Message" );
		}

		EBTStatus OnTick( FAIContext&, float32 ) override
		{
			LOG( LogAI, Log, "%s", *Message );
			return EBTStatus::Success;
		}

	private:
		FString Message;
	};

	class FTask_Succeed final : public FAITask
	{
	public:
		EBTStatus OnTick( FAIContext&, float32 ) override
		{
			return EBTStatus::Success;
		}
	};

	class FTask_Fail final : public FAITask
	{
	public:
		EBTStatus OnTick( FAIContext&, float32 ) override
		{
			return EBTStatus::Failure;
		}
	};
}

void NAI::RegisterBuiltinParts( CAIRegistry& Registry )
{
	Registry.RegisterTask<FTask_Wait>( "Wait", FParamSchema().Float( "Seconds", 1.0f ).Range( 0.0f, 3600.0f ).Hint( "待つ秒数" ), "Flow", "指定の秒数が過ぎるまで Running" );
	Registry.RegisterTask<FTask_SetBlackboard>( "SetBlackboard", FParamSchema().BlackboardKey( "Key" ).Required().Scalar( "Value" ).Required().Hint( "キーの型に合わせた値（Bool は true / false、Enum は選択肢の名前）" ), "Blackboard", "ブラックボードのキーに値を書く" );
	Registry.RegisterTask<FTask_Log>( "Log", FParamSchema().String( "Message", "" ), "Debug", "ログに出す" );
	Registry.RegisterTask<FTask_Succeed>( "Succeed", FParamSchema(), "Flow", "すぐ Success" );
	Registry.RegisterTask<FTask_Fail>( "Fail", FParamSchema(), "Flow", "すぐ Failure" );
}
