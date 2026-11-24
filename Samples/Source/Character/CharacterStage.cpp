// SPDX-License-Identifier: MIT
#include "CharacterStage.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Mesh/ModelPoseSnapshot.h"
#include "Library/Framework/Render/Scene/SceneDrawList.h"
#include "Library/Platform/System/CommandLine.h"

namespace
{
	// 通常の描画と姿勢の固定で同じカメラを使う。
	FCamera CharacterCamera()
	{
		return FCamera::MakePerspective( FVector3( 0.0f, 9.0f, -28.0f ), FVector3( 0.0f, 8.0f, 0.0f ) );
	}

	// モデルの置き場所（Assets からの相対）。
	const char* ModelPath = "Models/Faputa/Faputa.pmx";

	// PMX の標準の骨の名前（モデルを作った人が付けたもの。順番は EBone と同じ）。
	const char* BoneNames[] = {
		"センター", "上半身", "上半身2", "首", "頭", "左腕", "左ひじ", "右腕", "右ひじ", "EarL", "EarR", "TailC1", "TailC2", "TailL1", "TailL2", "TailR1", "TailR2",
	};
}

void CCharacterStage::OnSpawn()
{
	for ( int32 Index = 0; Index < NumBones; ++Index )
	{
		Frames[Index] = -1;
	}
	if ( CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>() )
	{
		Source = Assets->LoadModel( ModelPath );
	}
	if ( Source.IsValid() )
	{
		Instance = Source->CreateInstance();
		const TOptional<FString> CopiesOption = NCommandLine::GetValue( "character-copies" ); // 計測用: 同じモデルを、基準の姿のまま、N 体にする（描画命令の数・骨の行列の転送の計測）。
		const int32 NumCopies = CopiesOption.IsSet() ? NMath::Clamp( CopiesOption.GetValue().ToInt( 1 ), 1, 2000 ) : 1;
		for ( int32 Copy = 1; Copy < NumCopies; ++Copy )
		{
			if ( TSharedPtr<CModelInstance> Extra = Source->CreateInstance() )
			{
				Crowd.Add( Extra );
			}
		}
		for ( int32 Index = 0; Index < NumBones; ++Index )
		{
			Frames[Index] = Source->FindFrame( FString( BoneNames[Index] ) );
		}
		BlinkShape = Source->FindShape( FString( "まばたき" ) );
		SmileShape = Source->FindShape( FString( "にこり" ) );
	}
}

bool CCharacterStage::CaptureBindPose()
{
	if ( !Instance.IsValid() )
	{
		return false;
	}
	FMatrix4x4 Local[NumBones];
	for ( int32 Index = 0; Index < NumBones; ++Index )
	{
		if ( Frames[Index] >= 0 && !Instance->GetFrameMatrix( Frames[Index], EModelFrameSpace::Local, Local[Index] ) )
		{
			return false; // まだ読めない（次の更新で、また試す）。
		}
	}
	for ( int32 Index = 0; Index < NumBones; ++Index )
	{
		Bind[Index] = Local[Index];
	}
	return true;
}

void CCharacterStage::Rotate( EBone Bone, float32 X, float32 Y, float32 Z )
{
	const int32 Index = static_cast<int32>( Bone );
	if ( Frames[Index] >= 0 )
	{
		Instance->SetFrameLocalMatrixOverride( Frames[Index], FMatrix4x4::RotateX( X ) * FMatrix4x4::RotateY( Y ) * FMatrix4x4::RotateZ( Z ) * Bind[Index] );
	}
}

void CCharacterStage::Move( EBone Bone, const FVector3& Offset )
{
	const int32 Index = static_cast<int32>( Bone );
	if ( Frames[Index] >= 0 )
	{
		Instance->SetFrameLocalMatrixOverride( Frames[Index], Bind[Index] * FMatrix4x4::Translate( Offset ) );
	}
}

void CCharacterStage::Tick( float32 DeltaTime )
{
	// 絵を比べる検査のために、--model-time=<秒> で、時計を固定できる。
	static const TOptional<FString> FixedTime = NCommandLine::GetValue( "model-time" );
	Time = FixedTime.IsSet() ? static_cast<float32>( FixedTime.GetValue().ToFloat( 0.0 ) ) : Time + DeltaTime;
	if ( !Instance.IsValid() || ( !bBindReady && !( bBindReady = CaptureBindPose() ) ) )
	{
		return;
	}
	const float32 T = Time;
	// 体: 呼吸に合わせて、少し上下して、胸がうなずく。
	Move( EBone::Center, FVector3( 0.0f, NMath::Sin( T * 2.4f ) * 0.18f, 0.0f ) );
	Rotate( EBone::UpperBody, NMath::Sin( T * 2.4f ) * 0.03f, NMath::Sin( T * 0.8f ) * 0.08f, 0.0f );
	Rotate( EBone::UpperBody2, 0.0f, NMath::Sin( T * 0.8f ) * 0.06f, NMath::Sin( T * 1.3f ) * 0.03f );
	// 頭: ゆっくり左右を見て、小さくうなずく。
	Rotate( EBone::Neck, 0.0f, NMath::Sin( T * 0.9f ) * 0.18f, 0.0f );
	Rotate( EBone::Head, NMath::Sin( T * 1.8f ) * 0.07f, NMath::Sin( T * 0.9f ) * 0.3f, NMath::Sin( T * 0.9f + 1.0f ) * 0.06f );
	// 右腕: 手を上げて、ひじから先を振る（手を振る）。左腕は、ゆっくり揺らす。
	const float32 Wave = 0.5f + 0.5f * NMath::Sin( T * 0.7f ); // 0 = 下ろす、1 = 上げる。
	Rotate( EBone::RightArm, 0.0f, 0.0f, -( 0.2f + 1.25f * Wave ) );
	Rotate( EBone::RightElbow, 0.0f, 0.0f, -( 0.15f + 0.55f * Wave * NMath::Sin( T * 7.0f ) ) );
	Rotate( EBone::LeftArm, 0.0f, 0.0f, 0.12f + NMath::Sin( T * 1.6f ) * 0.08f );
	Rotate( EBone::LeftElbow, 0.0f, 0.0f, 0.1f + NMath::Sin( T * 1.6f + 0.8f ) * 0.08f );
	// 耳: ぴくぴく動かす。
	Rotate( EBone::LeftEar, 0.0f, 0.0f, NMath::Sin( T * 5.0f ) * 0.18f );
	Rotate( EBone::RightEar, 0.0f, 0.0f, -NMath::Sin( T * 5.0f + 1.2f ) * 0.18f );
	// しっぽ: 根元から先へ、遅れて左右に揺れる。
	Rotate( EBone::TailCenter1, 0.0f, NMath::Sin( T * 2.2f ) * 0.25f, 0.0f );
	Rotate( EBone::TailCenter2, 0.0f, NMath::Sin( T * 2.2f - 0.7f ) * 0.35f, 0.0f );
	Rotate( EBone::TailLeft1, 0.0f, NMath::Sin( T * 2.2f - 0.3f ) * 0.3f, 0.0f );
	Rotate( EBone::TailLeft2, 0.0f, NMath::Sin( T * 2.2f - 1.0f ) * 0.4f, 0.0f );
	Rotate( EBone::TailRight1, 0.0f, NMath::Sin( T * 2.2f - 0.5f ) * 0.3f, 0.0f );
	Rotate( EBone::TailRight2, 0.0f, NMath::Sin( T * 2.2f - 1.2f ) * 0.4f, 0.0f );

	// 表情（形状）: ときどきまばたきして、ゆっくり、にこりとする。--character-expression=<0 から 1> で、両方を、その値に固定できる（絵を比べる検査用）。
	if ( BlinkShape >= 0 || SmileShape >= 0 )
	{
		static const TOptional<FString> FixedExpression = NCommandLine::GetValue( "character-expression" );
		float32 Blink = 0.0f;
		float32 Smile = 0.0f;
		if ( FixedExpression.IsSet() )
		{
			Blink = static_cast<float32>( FixedExpression.GetValue().ToFloat( 0.0 ) );
			Smile = Blink;
		}
		else
		{
			const float32 Phase = NMath::Fmod( T, 3.0f ); // 3 秒に 1 回、0.25 秒で、まばたきする。
			Blink = Phase < 0.25f ? NMath::Sin( Phase / 0.25f * 3.14159265f ) : 0.0f;
			Smile = 0.8f * ( 0.5f + 0.5f * NMath::Sin( T * 0.5f ) );
		}
		FModelAppearance Look = Instance->GetAppearance();
		if ( BlinkShape >= 0 )
		{
			Look.SetShapeRate( BlinkShape, Blink );
		}
		if ( SmileShape >= 0 )
		{
			Look.SetShapeRate( SmileShape, Smile );
		}
	}

	const TOptional<FString> PoseMode = NCommandLine::GetValue( "character-pose" ); // 画像検査で固定時と変更後を比較する。
	if ( !PoseMode.IsSet() ) { return; }
	if ( PoseMode.GetValue() == "frozen" )
	{
		const FModelPoseSnapshot Pose = FModelPoseSnapshot::Capture( *Instance ); // 変更前の姿勢を借りずに保持する。
		CSceneDrawList List( CharacterCamera() ); // 通常描画と同じカメラを固定する。
		if ( !List.AddModel( Pose, ESceneDrawClass::Opaque, FVector3::Zero, FMatrix4x4::Identity, false ) ) { return; }
		Frozen = List.Freeze();
		Move( EBone::Center, FVector3( 4, 0, 0 ) );
		Instance.Reset();
		Source.Reset();
	}
	else if ( PoseMode.GetValue() == "moved" )
	{
		Move( EBone::Center, FVector3( 4, 0, 0 ) );
	}
}

void CCharacterStage::Draw( FDrawContext& Draw )
{
	if ( Frozen.IsValid() ) { return; }
	if ( Instance.IsValid() )
	{
		const FCamera Camera = CharacterCamera(); // 固定経路と同じビュー。
		if ( Draw.PushCamera( Camera ) )
		{
			Draw.UseLighting( false );
			Draw.Model( Instance->GetHandle(), FMatrix4x4() );
			const int32 Columns = NMath::Max( 1, static_cast<int32>( NMath::Sqrt( static_cast<float32>( Crowd.Num() ) ) ) + 1 );
			for ( int32 Index = 0; Index < Crowd.Num(); ++Index )
			{
				const float32 X = ( static_cast<float32>( Index % Columns ) - 0.5f * static_cast<float32>( Columns ) ) * 2.5f;
				const float32 Z = 3.0f + static_cast<float32>( Index / Columns ) * 2.5f; // カメラの奥へ並べる。
				Draw.Model( Crowd[Index]->GetHandle(), FMatrix4x4::Translate( FVector3( X, 0.0f, Z ) ) );
			}
			Draw.PopCamera();
		}
		Draw.Text( 20, 80, "A PMX model, with the bones and the facial shapes moved from code (no motion file)", FColor32( 255, 255, 255 ) );
	}
	else
	{
		Draw.Text( 20, 80, "Put the model at Assets/Models/Faputa/Faputa.pmx (it is not in the repository)", FColor32( 255, 255, 255 ) );
	}
}

void CCharacterStage::DrawFrozen( IDrawExecutor& Executor ) const
{
	if ( Frozen.IsValid() ) { Frozen.Replay( Executor ); }
}
