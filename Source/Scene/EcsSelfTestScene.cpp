#include "EcsSelfTestScene.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Ecs/BuiltinSystems.h"
#include "Library/Framework/Ecs/System.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Math/Math.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

DEFINE_LOG_CATEGORY_STATIC( LogEcsSelfTest, Log )

namespace
{
	constexpr int32 NumNodes = 30000;
	constexpr int32 NumFrames = 30;
	constexpr int32 NumSystemNodes = 40000;
	constexpr int32 NumSystemFrames = 30;
	constexpr int32 NumDrawFrames = 10;

	// 毎回同じ並びになる乱数（結果をワーカーの数や実行環境によらず比べられるように）
	struct FRandom
	{
		uint32 State = 12345u;
		uint32 Next() { State = State * 1664525u + 1013904223u; return State >> 8; }
		float32 Range( float32 Lo, float32 Hi ) { return Lo + ( Hi - Lo ) * ( static_cast<float32>( Next() & 0xFFFF ) / 65535.0f ); }
		int32 Int( int32 N ) { return static_cast<int32>( Next() % static_cast<uint32>( N ) ); }
	};

	// 85% は CTransformComponent を持つ（持たないオブジェクトを親にしたとき、その上の祖先へたどることも確かめる）
	class CTestNode : public CGameObject
	{
	public:
		explicit CTestNode( bool bInTransform ) : bTransform( bInTransform ) {}
		void OnSpawn() override { if ( bTransform ) AddComponent<CTransformComponent>(); }

	private:
		bool bTransform;
	};

	// 以前の CTransformComponent::GetWorldTransform と同じ計算（親のコンポーネントをたどる再帰）
	FTransform LegacyWorld( const CTransformComponent* Transform )
	{
		const CTransformComponent* ParentTransform = Transform->FindParentTransform();
		if ( ParentTransform == nullptr ) return Transform->GetLocalTransform();

		const FTransform Parent = LegacyWorld( ParentTransform );
		const FTransform& Local = Transform->GetLocalTransform();
		FTransform World;
		World.Position = Parent.Rotation.Rotate( Parent.Scale * Local.Position ) + Parent.Position;
		World.Rotation = Parent.Rotation * Local.Rotation;
		World.Scale = Parent.Scale * Local.Scale;
		return World;
	}

	// 比べるのはパディングを含まない 10 個の float だけ（FTransform の隙間には不定の値が入る）
	void ToFloats( const FTransform& Transform, float ( &Out )[10] )
	{
		Out[0] = Transform.Position.x; Out[1] = Transform.Position.y; Out[2] = Transform.Position.z;
		Out[3] = Transform.Rotation.x; Out[4] = Transform.Rotation.y; Out[5] = Transform.Rotation.z; Out[6] = Transform.Rotation.w;
		Out[7] = Transform.Scale.x; Out[8] = Transform.Scale.y; Out[9] = Transform.Scale.z;
	}
}

//=============================================================================
// システム（データコンポーネント + 自動スケジュール）の自己確認
//   組み込みのシステム（FAngularMotion / FLinearMotion）と、衝突のあるテスト用システムを、並列に動かした結果が、
//   同じ処理を 1 つずつ順番に行った結果と 1 ビットも違わないこと、実行の段が読み書きの衝突から正しく決まること、
//   システムからの破棄の予約が反映されることを確かめる。
//=============================================================================
struct FTestCounter { int32 Count = 0; float32 Sum = 0.0f; };
struct FTestScale { float32 Factor = 1.0f; };
struct FTestOther { int32 Value = 0; };

class CSystemTestNode : public CGameObject
{
public:
	void OnSpawn() override { AddComponent<CTransformComponent>(); }
};

class CBenchSpinNode : public CGameObject
{
public:
	void OnSpawn() override { Transform = AddComponent<CTransformComponent>(); }
	// COrbitMarker の Tick と同じ処理（ふつうに書いた Tick で、シーンが 1 つずつ呼ぶ場合の比較用）
	void Tick( float32 DeltaTime ) override
	{
		const FQuaternion Spin = FQuaternion::FromAxisAngle( FVector3::Forward, 1.5f * DeltaTime );
		Transform->SetLocalRotation( ( Spin * Transform->GetLocalRotation() ).Normalized() );
	}

private:
	CTransformComponent* Transform = nullptr;
};

struct FSystemTestNode
{
	TSharedPtr<CGameObject> Object;
	bool bAlive = true;
	int32 Entity = INDEX_NONE;
	FTransform Local;
	bool bAngular = false;
	FAngularMotion Angular;
	bool bLinear = false;
	FLinearMotion Linear;
	bool bCounter = false;
	FTestCounter Counter;
	bool bScale = false;
	FTestScale Scale;
	bool bOther = false;
	FTestOther Other;
};

struct FSystemTest
{
	FRandom Random;
	TArray<FSystemTestNode> Nodes;
	bool bSetup = false;
	float32 LastDelta = 0.0f;
	int64 NumChecked = 0;
	int64 NumMismatch = 0;
	uint64 Hash = 1469598103934665603ull;
	int32 NumDestroyed = 0;

	// テスト用のシステムを登録する（シーンの OnEnter で 1 回）。ラムダは何も取り込まない（状態はコンポーネントにある）
	static void Register( CScene& Scene )
	{
		// 書き込む: FTestCounter / 読む: FTestScale
		Scene.AddSystem( NEcs::EPhase::PreUpdate, "Test.Count", []( float32 DeltaTime, FTestCounter& Counter, const FTestScale& Scale )
		{
			Counter.Count += 1;
			Counter.Sum += Scale.Factor * DeltaTime;
		} );
		// 書き込む: FTestScale（上の Test.Count が読むので、必ず Test.Count より後の段になる）
		Scene.AddSystem( NEcs::EPhase::PreUpdate, "Test.ScaleDrift", []( FTestScale& Scale )
		{
			Scale.Factor = Scale.Factor * 0.99f + 0.01f;
		} );
		// 誰とも衝突しない（Test.Count と同じ段で、同時に動く）
		Scene.AddSystem( NEcs::EPhase::PreUpdate, "Test.Independent", []( FTestOther& Other )
		{
			Other.Value += 2;
		} );
		// 20 回数えたものの一部を、システムの中から破棄する（遅延）
		Scene.AddSystem( NEcs::EPhase::PostUpdate, "Test.Destroy", []( FEntityRef Entity, const FTestCounter& Counter )
		{
			if ( Counter.Count == 20 && Entity.GetEntity() % 7 == 0 ) Entity.RequestDestroy();
		} );
	}

	void Check( bool bOk, const char* What )
	{
		++NumChecked;
		if ( bOk ) return;
		if ( NumMismatch < 5 ) LOG( LogEcsSelfTest, Error, "Systems: %s", What );
		++NumMismatch;
	}

	void CheckStages( CScene& Scene )
	{
		const NEcs::FSystemScheduler& Systems = Scene.GetSystems();
		Check( Systems.GetStage( "Motion.Angular" ) == 0, "stage of Motion.Angular should be 0" );
		Check( Systems.GetStage( "Motion.Linear" ) == 1, "stage of Motion.Linear should be 1 (it writes the same transform)" );
		Check( Systems.GetStage( "Test.Count" ) == 0, "stage of Test.Count should be 0" );
		Check( Systems.GetStage( "Test.ScaleDrift" ) == 1, "stage of Test.ScaleDrift should be 1 (Test.Count reads what it writes)" );
		Check( Systems.GetStage( "Test.Independent" ) == 0, "stage of Test.Independent should be 0 (nothing conflicts)" );
		Check( Systems.GetStage( "Test.Destroy" ) == 0, "stage of Test.Destroy should be 0" );
	}

	void Setup( CScene& Scene )
	{
		CheckStages( Scene );

		const FVector3 Axes[3] = { FVector3::Forward, FVector3::Up, FVector3::Right };
		for ( int32 i = 0; i < NumSystemNodes; ++i )
		{
			FSystemTestNode Node;
			TSharedPtr<CSystemTestNode> Object = Scene.SpawnObject<CSystemTestNode>();
			Node.Object = Object;
			Node.Entity = Object->GetEntitySlot();
			Node.Local = FTransform( FVector3( Random.Range( -5, 5 ), Random.Range( -5, 5 ), Random.Range( -5, 5 ) ),
									 FQuaternion::FromAxisAngle( FVector3::Forward, Random.Range( -3.1f, 3.1f ) ), FVector3( 1.0f, 1.0f, 1.0f ) );
			Object->GetComponent<CTransformComponent>()->SetLocalTransform( Node.Local );

			if ( Random.Int( 100 ) < 70 )
			{
				Node.bAngular = true;
				Node.Angular = FAngularMotion{ Axes[Random.Int( 3 )], Random.Range( -3.0f, 3.0f ) };
				Object->AddData( Node.Angular );
			}
			if ( Random.Int( 100 ) < 50 )
			{
				Node.bLinear = true;
				Node.Linear = FLinearMotion{ FVector3( Random.Range( -2, 2 ), Random.Range( -2, 2 ), Random.Range( -2, 2 ) ) };
				Object->AddData( Node.Linear );
			}
			if ( Random.Int( 100 ) < 60 )
			{
				Node.bCounter = true;
				Object->AddData( Node.Counter );
			}
			if ( Random.Int( 100 ) < 60 )
			{
				Node.bScale = true;
				Node.Scale.Factor = Random.Range( 0.5f, 2.0f );
				Object->AddData( Node.Scale );
			}
			if ( Random.Int( 100 ) < 40 )
			{
				Node.bOther = true;
				Object->AddData( Node.Other );
			}
			Nodes.Add( Node );
		}
		bSetup = true;
	}

	// 前のフレームのシステムが行ったはずのことを、1 つずつ順番に（ふつうのループで）行う
	void StepReference( float32 DeltaTime )
	{
		for ( FSystemTestNode& Node : Nodes )
		{
			if ( !Node.bAlive ) continue;
			if ( Node.bAngular )
			{
				const FQuaternion Spin = FQuaternion::FromAxisAngle( Node.Angular.Axis, Node.Angular.RadiansPerSecond * DeltaTime );
				Node.Local.Rotation = ( Spin * Node.Local.Rotation ).Normalized();
			}
			if ( Node.bLinear ) Node.Local.Position = Node.Local.Position + Node.Linear.Velocity * DeltaTime;
			if ( Node.bCounter && Node.bScale )
			{
				Node.Counter.Count += 1;
				Node.Counter.Sum += Node.Scale.Factor * DeltaTime;
			}
			if ( Node.bScale ) Node.Scale.Factor = Node.Scale.Factor * 0.99f + 0.01f;
			if ( Node.bOther ) Node.Other.Value += 2;
			if ( Node.bCounter && Node.Counter.Count == 20 && Node.Entity % 7 == 0 )
			{
				Node.bAlive = false;
				++NumDestroyed;
			}
		}
	}

	void CheckAll()
	{
		for ( FSystemTestNode& Node : Nodes )
		{
			if ( !Node.bAlive )
			{
				Check( Node.Object->GetScene() == nullptr, "an object that a system asked to destroy is still in the scene" );
				continue;
			}
			if ( Node.Object->GetScene() == nullptr )
			{
				Check( false, "an object disappeared although no system asked to destroy it" );
				continue;
			}

			float Expected[10], Actual[10];
			ToFloats( Node.Local, Expected );
			ToFloats( Node.Object->GetComponent<CTransformComponent>()->GetLocalTransform(), Actual );
			Check( std::memcmp( Expected, Actual, sizeof( Expected ) ) == 0, "local transform differs from the sequential result" );

			if ( Node.bCounter )
			{
				const FTestCounter* Counter = Node.Object->GetData<FTestCounter>();
				Check( Counter != nullptr && Counter->Count == Node.Counter.Count && std::memcmp( &Counter->Sum, &Node.Counter.Sum, sizeof( float32 ) ) == 0,
					   "FTestCounter differs from the sequential result" );
			}
			if ( Node.bScale )
			{
				const FTestScale* Scale = Node.Object->GetData<FTestScale>();
				Check( Scale != nullptr && std::memcmp( &Scale->Factor, &Node.Scale.Factor, sizeof( float32 ) ) == 0, "FTestScale differs from the sequential result" );
			}
			if ( Node.bOther )
			{
				const FTestOther* Other = Node.Object->GetData<FTestOther>();
				Check( Other != nullptr && Other->Value == Node.Other.Value, "FTestOther differs from the sequential result" );
			}
		}
	}

	// 1 フレーム（OnTick の中。このあと同じフレームでシステムが動く）
	void Step( CScene& Scene, float32 DeltaTime )
	{
		if ( !bSetup )
		{
			Setup( Scene );
		}
		else
		{
			StepReference( LastDelta );
			CheckAll();
		}
		LastDelta = DeltaTime;
	}

	// 最後の結果をハッシュにする（DeltaTime に依存しない値だけ: 生き残り・回数・加算した値）
	void Finish()
	{
		StepReference( LastDelta );
		CheckAll();
		for ( const FSystemTestNode& Node : Nodes )
		{
			const int32 Values[3] = { Node.bAlive ? 1 : 0, Node.Counter.Count, Node.Other.Value };
			const unsigned char* Bytes = reinterpret_cast<const unsigned char*>( Values );
			for ( size_t i = 0; i < sizeof( Values ); ++i ) Hash = ( Hash ^ Bytes[i] ) * 1099511628211ull;
		}
	}
};

//=============================================================================
// 描画システム（描画コマンドの記録 -> 実行）の自己確認
//   1) 記録したコマンドが、「描画順に 1 つずつ素直に書いた場合」のコマンドと 1 ワードも違わないこと（ワーカーの数・実行の順番に依存しない）。
//      可視性・DrawOrder・データの付け外し・生成と破棄・ふつうの Draw()（描画順の途中に割り込む）・カメラのカリング（ラムダが取り込む読み取り専用データ）を
//      ランダムに変えながら確かめる
//   2) 同じ描画を、(A) コマンドにして CDxLibDrawExecutor で実行した場合と、(B) DxLib を直接呼んだ場合とで、画面のピクセルが 1 つも違わないこと
//      （2D・3D・ブレンド・WorldMatrix・画像・文字列。MV1 モデルも、読み込めれば）
//=============================================================================
struct FDrawTestShape
{
	int32 Kind = 0;           // 0:Box 1:Circle 2:Line 3:Text 4:Sprite 5:Sphere3D 6:Capsule3D 7:Cube3D 8:Line3D 9:Triangle3D 10:Billboard3D 11:Model3D
	float32 Size = 1.0f;
	FColor32 Color = FColor32( 255, 255, 255 );
	int32 Variant = 0;        // bit0: 塗りつぶし / bit1: アルファブレンド / bit2: WorldMatrix（3D 図形をローカル座標で描く）/ bit3: 反転 / bit4-5: 太さ / bit6-12: 不透明度
};
struct FDrawTestLabel { int32 Number = 0; };
struct FDrawTestCulled { float32 Radius = 1.0f; FColor32 Color = FColor32( 255, 255, 255 ); };
struct FDrawTestCamera { FVector3 Position = FVector3::Zero; float32 Radius = 10.0f; };   // カリング用（描画システムのラムダが読むだけ）
struct FDrawBenchBox { float32 HalfSize = 3.0f; FColor32 Color = FColor32( 255, 255, 255 ); int32 bHeavy = 0; };

namespace
{
	// 描画システムが使う画像 / モデルのハンドル（記録だけの確認では、ただの数）
	struct FDrawTestHandles
	{
		int32 Graph = 7;
		int32 Model = 9;
		int32 AnimIndex = 0;
	};
	const FDrawTestHandles GStreamHandles{};

	constexpr int32 DrawPixelScale = 12;   // 1 ワールド単位 = 12 ピクセル
	constexpr int32 DrawCenterX = 160;
	constexpr int32 DrawCenterY = 120;

	// 描画の中身。TDraw は FDrawContext（記録する）か、FDirectBackend（DxLib を直接呼ぶ）。同じ関数で両方を動かして、結果を比べる
	template<typename TDraw>
	void EmitShape( TDraw& Draw, const FTransform& World, const FDrawTestShape& Shape, const FDrawTestHandles& Handles )
	{
		const FVector3 P = World.Position;
		const int32 X = DrawCenterX + static_cast<int32>( P.x * DrawPixelScale );
		const int32 Y = DrawCenterY + static_cast<int32>( P.y * DrawPixelScale );
		const int32 R = static_cast<int32>( Shape.Size * World.Scale.x * 3.0f ) + 2;
		const bool bFill = ( Shape.Variant & 1 ) != 0;
		const bool bFlip = ( Shape.Variant & 8 ) != 0;
		const int32 Thickness = 1 + ( ( Shape.Variant >> 4 ) & 3 );

		if ( Shape.Variant & 2 ) Draw.Blend( EDrawBlend::Alpha, 64 + ( ( Shape.Variant >> 6 ) & 127 ) );
		const bool bLocal = ( Shape.Variant & 4 ) != 0 && Shape.Kind >= 5 && Shape.Kind <= 9;
		if ( bLocal ) Draw.WorldMatrix( World.ToMatrix() );
		const FVector3 Origin = bLocal ? FVector3::Zero : P;

		switch ( Shape.Kind )
		{
		case 0: Draw.Box( X - R, Y - R, X + R, Y + R, Shape.Color, bFill, Thickness ); break;
		case 1: Draw.Circle( X, Y, R, Shape.Color, bFill, Thickness ); break;
		case 2: Draw.Line( X - R, Y - R, X + 2 * R, Y + R, Shape.Color, Thickness ); break;
		case 3: Draw.TextF( X, Y, Shape.Color, "#%d z=%.1f", Shape.Variant, P.z ); break;
		case 4: Draw.Sprite( static_cast<float32>( X ), static_cast<float32>( Y ), Handles.Graph, Shape.Size * 0.5f, World.Rotation.z * 3.0f, false, bFlip ); break;
		case 5: Draw.Sphere3D( Origin, Shape.Size, 8, Shape.Color, FColor32::White, bFill ); break;
		case 6: Draw.Capsule3D( Origin, Origin + FVector3( 0.0f, Shape.Size, 0.0f ), Shape.Size * 0.5f, 8, Shape.Color, FColor32::White, bFill ); break;
		case 7: Draw.Cube3D( Origin - FVector3( Shape.Size ), Origin + FVector3( Shape.Size ), Shape.Color, FColor32::White, bFill ); break;
		case 8: Draw.Line3D( Origin, Origin + FVector3( Shape.Size * 2.0f, 0.0f, 0.0f ), Shape.Color ); break;
		case 9: Draw.Triangle3D( Origin, Origin + FVector3( Shape.Size, 0.0f, 0.0f ), Origin + FVector3( 0.0f, Shape.Size, 0.0f ), Shape.Color, bFill ); break;
		case 10: Draw.Billboard3D( P, Shape.Size, Handles.Graph, World.Rotation.z, false, bFlip ); break;
		default: Draw.Model( Handles.Model, World.ToMatrix(), Handles.AnimIndex, Shape.Size ); break;
		}
	}

	template<typename TDraw>
	void EmitLabel( TDraw& Draw, const FDrawTestLabel& Label )
	{
		Draw.TextF( 8 + ( Label.Number % 50 ) * 4, 8 + ( Label.Number / 50 ) % 100, FColor32( 200, 200, 80 ), "n=%d", Label.Number );
	}

	// カメラの近くにあるものだけを描く（視錐台カリングの代わり）
	template<typename TDraw>
	void EmitCulled( TDraw& Draw, const FVector3& Position, const FDrawTestCulled& Culled, const FDrawTestCamera& Camera )
	{
		const FVector3 Delta = Position - Camera.Position;
		if ( Delta.x * Delta.x + Delta.y * Delta.y + Delta.z * Delta.z > Camera.Radius * Camera.Radius ) return;
		Draw.Sphere3D( Position, Culled.Radius, 6, Culled.Color, FColor32::Black, false );
	}

	// ベンチマーク用（light: 座標変換と四角形だけ / heavy: 手続き的なアニメーション相当の計算を足す）
	template<typename TDraw>
	void EmitBenchBox( TDraw& Draw, const FVector3& P, const FDrawBenchBox& Box )
	{
		float32 Wobble = 0.0f;
		if ( Box.bHeavy != 0 )
		{
			for ( int32 i = 1; i <= 24; ++i ) Wobble += std::sin( P.x * static_cast<float32>( i ) ) * std::cos( P.y * static_cast<float32>( i ) );
		}
		const int32 X = 320 + static_cast<int32>( P.x * 10.0f + Wobble );
		const int32 Y = 240 + static_cast<int32>( P.y * 10.0f - Wobble );
		const int32 H = static_cast<int32>( Box.HalfSize + ( Box.bHeavy != 0 ? NMath::Abs( Wobble ) : 0.0f ) );
		Draw.Box( X - H, Y - H, X + H, Y + H, Box.Color );
	}

	// 描画順の途中に割り込む「ふつうの Draw()」の出力の目印（Id を座標に入れた 1 本の四角形）
	void EmitMarker( FDrawContext& Draw, uint64 Id )
	{
		Draw.Box( static_cast<int32>( Id ), -1, static_cast<int32>( Id ), -2, FColor32( 255, 0, 255 ), true, 1 );
	}

	// 記録されたコマンドを、そのまま溜めるだけの実行先（コマンドの並びを比べるため）
	class FRecordingExecutor final : public IDrawExecutor
	{
	public:
		FDrawBuffer Buffer;
		void Execute( const FDrawSpan& Span ) override { Buffer.Words.insert( Buffer.Words.end(), Span.Words, Span.Words + Span.NumWords ); }
	};
	FRecordingExecutor* GDrawRecorder = nullptr;

	// DxLib を直接呼ぶ側（FDrawContext と同じ関数名・引数。ふつうに DxLib を呼んだ場合の結果を作る）
	struct FDirectBackend
	{
		bool bBlendChanged = false;
		bool bMatrixChanged = false;

		static int ToDxBlend( EDrawBlend Mode )
		{
			switch ( Mode )
			{
			case EDrawBlend::Alpha: return DX_BLENDMODE_ALPHA;
			case EDrawBlend::Add: return DX_BLENDMODE_ADD;
			case EDrawBlend::Subtract: return DX_BLENDMODE_SUB;
			case EDrawBlend::Multiply: return DX_BLENDMODE_MUL;
			default: return DX_BLENDMODE_NOBLEND;
			}
		}

		void Box( int32 X0, int32 Y0, int32 X1, int32 Y1, const FColor32& Color, bool bFill = true, int32 Thickness = 1 ) { DrawBox( X0, Y0, X1, Y1, Color.ToDxColor(), bFill ? TRUE : FALSE, Thickness ); }
		void Circle( int32 X, int32 Y, int32 Radius, const FColor32& Color, bool bFill = true, int32 Thickness = 1 ) { DrawCircle( X, Y, Radius, Color.ToDxColor(), bFill ? TRUE : FALSE, Thickness ); }
		void Line( int32 X0, int32 Y0, int32 X1, int32 Y1, const FColor32& Color, int32 Thickness = 1 ) { DrawLine( X0, Y0, X1, Y1, Color.ToDxColor(), Thickness ); }
		void TextF( int32 X, int32 Y, const FColor32& Color, const char* Format, ... )
		{
			char Text[512];
			va_list Args;
			va_start( Args, Format );
			std::vsnprintf( Text, sizeof( Text ), Format, Args );
			va_end( Args );
			DrawString( X, Y, Text, Color.ToDxColor() );
		}
		void Sprite( float32 X, float32 Y, int32 Graph, float32 Scale, float32 Angle, bool bFlipX, bool bFlipY ) { DrawRotaGraphF( X, Y, Scale, Angle, Graph, TRUE, bFlipX ? TRUE : FALSE, bFlipY ? TRUE : FALSE ); }
		void Blend( EDrawBlend Mode, int32 Param )
		{
			SetDrawBlendMode( ToDxBlend( Mode ), Param );
			bBlendChanged = Mode != EDrawBlend::Opaque;
		}
		void Line3D( const FVector3& A, const FVector3& B, const FColor32& Color ) { DrawLine3D( A, B, Color.ToDxColor() ); }
		void Triangle3D( const FVector3& A, const FVector3& B, const FVector3& C, const FColor32& Color, bool bFill ) { DrawTriangle3D( A, B, C, Color.ToDxColor(), bFill ? TRUE : FALSE ); }
		void Sphere3D( const FVector3& Center, float32 Radius, int32 Div, const FColor32& Diffuse, const FColor32& Specular, bool bFill ) { DrawSphere3D( Center, Radius, Div, Diffuse.ToDxColor(), Specular.ToDxColor(), bFill ? TRUE : FALSE ); }
		void Capsule3D( const FVector3& A, const FVector3& B, float32 Radius, int32 Div, const FColor32& Diffuse, const FColor32& Specular, bool bFill ) { DrawCapsule3D( A, B, Radius, Div, Diffuse.ToDxColor(), Specular.ToDxColor(), bFill ? TRUE : FALSE ); }
		void Cube3D( const FVector3& Min, const FVector3& Max, const FColor32& Diffuse, const FColor32& Specular, bool bFill ) { DrawCube3D( Min, Max, Diffuse.ToDxColor(), Specular.ToDxColor(), bFill ? TRUE : FALSE ); }
		void Billboard3D( const FVector3& Position, float32 Size, int32 Graph, float32 Angle, bool bFlipX, bool bFlipY ) { DrawBillboard3D( Position, 0.5f, 0.5f, Size, Angle, Graph, TRUE, bFlipX ? TRUE : FALSE, bFlipY ? TRUE : FALSE ); }
		void WorldMatrix( const FMatrix4x4& Matrix )
		{
			const MATRIX Dx = Matrix;
			SetTransformToWorld( &Dx );
			bMatrixChanged = true;
		}
		void Model( int32 Handle, const FMatrix4x4& World, int32 AnimIndex, float32 AnimTime )
		{
			MV1SetMatrix( Handle, World );
			if ( AnimIndex >= 0 ) MV1SetAttachAnimTime( Handle, AnimIndex, AnimTime );
			MV1DrawModel( Handle );
		}

		// 1 つのオブジェクトの分が終わったとき（コマンド側の Execute が、Span の終わりでしていることと同じ）
		void EndShape()
		{
			if ( bBlendChanged ) SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 0 );
			if ( bMatrixChanged )
			{
				const MATRIX Identity = MGetIdent();
				SetTransformToWorld( &Identity );
			}
			bBlendChanged = false;
			bMatrixChanged = false;
		}
	};

	// 「ふつうの Draw()」として DxLib を直接呼ぶオブジェクト（ベンチマークの比較用）
	class CDrawBenchLegacyNode : public CGameObject
	{
	public:
		void OnSpawn() override { Transform = AddComponent<CTransformComponent>(); }
		void Draw() override
		{
			FDirectBackend Direct;
			EmitBenchBox( Direct, Transform->GetWorldPosition(), Box );
		}

		FDrawBenchBox Box;

	private:
		CTransformComponent* Transform = nullptr;
	};

	// 描画の確認用のオブジェクト。bLegacy なら、ふつうの Draw() も持つ（描画順の途中に割り込む目印を出す）
	class CDrawTestNode : public CGameObject
	{
	public:
		// bStartHidden: コンストラクタで見えなくしておく（シーンに入る前。エンティティの記録は、入るときの値で作られる）
		CDrawTestNode( bool bInLegacy, bool bStartHidden ) : bLegacy( bInLegacy )
		{
			if ( bStartHidden ) SetVisible( false );
		}
		void OnSpawn() override { AddComponent<CTransformComponent>(); }
		void Draw() override
		{
			if ( !bLegacy || GDrawRecorder == nullptr ) return;
			FDrawContext Context( GDrawRecorder->Buffer );
			EmitMarker( Context, GetId() );
		}

	private:
		bool bLegacy;
	};

	// 画面のピクセルを読む（幅 x 高さ。0xRRGGBB）
	void ReadPixels( int Soft, int Width, int Height, std::vector<uint32>& Out )
	{
		Out.resize( static_cast<size_t>( Width ) * static_cast<size_t>( Height ) );
		for ( int y = 0; y < Height; ++y )
		{
			for ( int x = 0; x < Width; ++x )
			{
				int R = 0, G = 0, B = 0, A = 0;
				GetPixelSoftImage( Soft, x, y, &R, &G, &B, &A );
				Out[static_cast<size_t>( y ) * static_cast<size_t>( Width ) + static_cast<size_t>( x )] = ( static_cast<uint32>( R ) << 16 ) | ( static_cast<uint32>( G ) << 8 ) | static_cast<uint32>( B );
			}
		}
	}
}

struct FDrawTestNode
{
	TSharedPtr<CGameObject> Object;
	bool bLegacy = false;
	bool bVisible = true;
	int32 DrawOrder = 0;
	bool bShape = false;
	FDrawTestShape Shape;
	bool bLabel = false;
	FDrawTestLabel Label;
	bool bCulled = false;
	FDrawTestCulled Culled;
};

struct FDrawTest
{
	FRandom Random;
	FRecordingExecutor Recorder;
	FDrawTestCamera Camera;
	TArray<FDrawTestNode> Nodes;
	bool bSetup = false;
	bool bArmed = false;       // この Tick のあとの Draw で、記録を確かめる
	bool bPixelDone = false;
	int64 NumChecked = 0;
	int64 NumMismatch = 0;
	int64 NumWordsSeen = 0;
	uint64 Hash = 1469598103934665603ull;
	FString PixelSummary;

	static constexpr int32 NumDrawNodes = 30000;

	// 描画システムを登録する（シーンの OnEnter で 1 回）。Camera は読むだけ（ラムダが取り込むのは、このオブジェクトへのポインタ）
	void Register( CScene& Scene )
	{
		const FDrawTestCamera* CameraPtr = &Camera;
		Scene.AddDrawSystem( "Test.Draw.Shape", []( FDrawContext& Draw, const FTransformView& Transform, const FDrawTestShape& Shape )
		{
			EmitShape( Draw, Transform.GetWorld(), Shape, GStreamHandles );
		} );
		Scene.AddDrawSystem( "Test.Draw.Label", []( FDrawContext& Draw, const FDrawTestLabel& Label )
		{
			EmitLabel( Draw, Label );
		} );
		Scene.AddDrawSystem( "Test.Draw.Culled", [CameraPtr]( FDrawContext& Draw, const FTransformView& Transform, const FDrawTestCulled& Culled )
		{
			EmitCulled( Draw, Transform.GetWorldPosition(), Culled, *CameraPtr );
		} );
		Scene.AddDrawSystem( "Bench.Draw.Box", []( FDrawContext& Draw, const FTransformView& Transform, const FDrawBenchBox& Box )
		{
			EmitBenchBox( Draw, Transform.GetWorldPosition(), Box );
		} );
	}

	void Check( bool bOk, const char* What )
	{
		++NumChecked;
		if ( bOk ) return;
		if ( NumMismatch < 5 ) LOG( LogEcsSelfTest, Error, "Draw: %s", What );
		++NumMismatch;
	}

	FColor32 RandomColor() { return FColor32( static_cast<uint8>( 40 + Random.Int( 215 ) ), static_cast<uint8>( 40 + Random.Int( 215 ) ), static_cast<uint8>( 40 + Random.Int( 215 ) ) ); }

	FDrawTestShape RandomShape()
	{
		FDrawTestShape Shape;
		Shape.Kind = Random.Int( 12 );
		Shape.Size = Random.Range( 0.5f, 2.0f );
		Shape.Color = RandomColor();
		Shape.Variant = Random.Int( 1 << 13 );
		return Shape;
	}

	void CheckStages( CScene& Scene )
	{
		const NEcs::FSystemScheduler& Systems = Scene.GetSystems();
		Check( Systems.HasDrawSystems(), "the scene should have draw systems" );
		// 描画システムは読むだけなので、互いに衝突せず、すべて同じ段（同時に動く）
		Check( Systems.GetStage( "Test.Draw.Shape" ) == 0, "stage of Test.Draw.Shape should be 0" );
		Check( Systems.GetStage( "Test.Draw.Label" ) == 0, "stage of Test.Draw.Label should be 0" );
		Check( Systems.GetStage( "Test.Draw.Culled" ) == 0, "stage of Test.Draw.Culled should be 0" );
		Check( Systems.GetNumStages( NEcs::EPhase::Draw ) == 1, "draw systems should all run in one stage" );
	}

	void SpawnNode( CScene& Scene )
	{
		FDrawTestNode Node;
		Node.bLegacy = Random.Int( 100 ) < 10;
		const bool bStartHidden = Random.Int( 100 ) < 5;
		TSharedPtr<CDrawTestNode> Object = Scene.SpawnObject<CDrawTestNode>( Node.bLegacy, bStartHidden );
		Node.Object = Object;
		Node.bVisible = !bStartHidden;

		Object->GetComponent<CTransformComponent>()->SetLocalTransform( FTransform(
			FVector3( Random.Range( -9, 9 ), Random.Range( -7, 7 ), Random.Range( -5, 5 ) ),
			FQuaternion::FromAxisAngle( FVector3::Forward, Random.Range( -3.1f, 3.1f ) ), FVector3( Random.Range( 0.5f, 1.5f ), 1.0f, 1.0f ) ) );
		// 一部は、最初の 64 個（壊さない）のどれかの子にする（ワールド値が親の影響を受ける）
		if ( Nodes.Num() >= 64 && Random.Int( 100 ) < 25 ) Object->AttachToParent( Nodes[Random.Int( 64 )].Object.Get() );

		Node.DrawOrder = Random.Int( 7 ) - 3;
		Object->SetDrawOrder( Node.DrawOrder );
		if ( Random.Int( 100 ) < 4 )   // シーンに入ったあとで隠す
		{
			Node.bVisible = false;
			Object->SetVisible( false );
		}
		if ( Random.Int( 100 ) < 85 )
		{
			Node.bShape = true;
			Node.Shape = RandomShape();
			Object->AddData( Node.Shape );
		}
		if ( Random.Int( 100 ) < 30 )
		{
			Node.bLabel = true;
			Node.Label.Number = Random.Int( 5000 );
			Object->AddData( Node.Label );
		}
		if ( Random.Int( 100 ) < 25 )
		{
			Node.bCulled = true;
			Node.Culled.Radius = Random.Range( 0.3f, 1.0f );
			Node.Culled.Color = RandomColor();
			Object->AddData( Node.Culled );
		}
		Nodes.Add( Node );
	}

	void Mutate( CScene& Scene )
	{
		const int32 Num = Nodes.Num();
		for ( int32 k = 0; k < Num / 20; ++k )   // 見える / 見えない
		{
			FDrawTestNode& Node = Nodes[Random.Int( Num )];
			if ( !Node.Object->IsAlive() ) continue;
			Node.bVisible = !Node.bVisible;
			Node.Object->SetVisible( Node.bVisible );
		}
		for ( int32 k = 0; k < Num / 30; ++k )   // 描画順
		{
			FDrawTestNode& Node = Nodes[Random.Int( Num )];
			if ( !Node.Object->IsAlive() ) continue;
			Node.DrawOrder = Random.Int( 7 ) - 3;
			Node.Object->SetDrawOrder( Node.DrawOrder );
		}
		for ( int32 k = 0; k < Num / 30; ++k )   // データの付け外し（Dense の並びが変わる）
		{
			FDrawTestNode& Node = Nodes[Random.Int( Num )];
			if ( !Node.Object->IsAlive() ) continue;
			if ( Node.bShape )
			{
				Node.Object->RemoveData<FDrawTestShape>();
				Node.bShape = false;
			}
			else
			{
				Node.bShape = true;
				Node.Shape = RandomShape();
				Node.Object->AddData( Node.Shape );
			}
		}
		for ( int32 k = 0; k < Num / 10; ++k )   // 動く
		{
			FDrawTestNode& Node = Nodes[Random.Int( Num )];
			if ( !Node.Object->IsAlive() ) continue;
			Node.Object->GetComponent<CTransformComponent>()->SetLocalPosition( FVector3( Random.Range( -9, 9 ), Random.Range( -7, 7 ), Random.Range( -5, 5 ) ) );
		}
		for ( int32 k = 0; k < Num / 100; ++k )   // 破棄（最初の 64 個は親になるので残す）
		{
			const int32 Index = 64 + Random.Int( Num - 64 );
			if ( Nodes[Index].Object->IsAlive() ) Nodes[Index].Object->Destroy();
		}
		for ( int32 k = 0; k < Num / 100; ++k ) SpawnNode( Scene );

		Camera.Position = FVector3( Random.Range( -6, 6 ), Random.Range( -5, 5 ), Random.Range( -8, 0 ) );
		Camera.Radius = Random.Range( 6.0f, 14.0f );
	}

	// OnTick で 1 フレーム分（このあとの描画で、記録を確かめる）
	void Step( CScene& Scene )
	{
		if ( !bSetup )
		{
			CheckStages( Scene );
			Camera.Position = FVector3( 0.0f, 0.0f, -4.0f );
			Camera.Radius = 10.0f;
			for ( int32 i = 0; i < NumDrawNodes; ++i ) SpawnNode( Scene );
			bSetup = true;
		}
		else
		{
			Mutate( Scene );
		}
		Recorder.Buffer.Clear();
		bArmed = true;
	}

	// 描画のあと（OnDraw）に、記録されたコマンドを、素直に作ったものと比べる
	void Verify()
	{
		bArmed = false;

		// 描画順（DrawOrder -> 生成順）に、生きていて見えているものだけ
		std::vector<int32> Order;
		for ( int32 i = 0; i < Nodes.Num(); ++i )
		{
			if ( Nodes[i].Object->IsAlive() && Nodes[i].bVisible ) Order.push_back( i );
		}
		std::sort( Order.begin(), Order.end(), [this]( int32 A, int32 B )
		{
			const FDrawTestNode& NodeA = Nodes[A];
			const FDrawTestNode& NodeB = Nodes[B];
			if ( NodeA.DrawOrder != NodeB.DrawOrder ) return NodeA.DrawOrder < NodeB.DrawOrder;
			return NodeA.Object->GetId() < NodeB.Object->GetId();
		} );

		FDrawBuffer Expected;
		FDrawContext Context( Expected );
		for ( const int32 Index : Order )
		{
			const FDrawTestNode& Node = Nodes[Index];
			const FTransform World = LegacyWorld( Node.Object->GetComponent<CTransformComponent>() );
			if ( Node.bLegacy ) EmitMarker( Context, Node.Object->GetId() );   // ふつうの Draw() は、そのオブジェクトの描画システムの出力より前
			if ( Node.bShape ) EmitShape( Context, World, Node.Shape, GStreamHandles );
			if ( Node.bLabel ) EmitLabel( Context, Node.Label );
			if ( Node.bCulled ) EmitCulled( Context, World.Position, Node.Culled, Camera );
		}

		const std::vector<uint32>& Actual = Recorder.Buffer.Words;
		bool bSame = Actual.size() == Expected.Words.size();
		size_t FirstDifference = 0;
		if ( bSame )
		{
			bSame = std::memcmp( Actual.data(), Expected.Words.data(), Actual.size() * sizeof( uint32 ) ) == 0;
			if ( !bSame )
			{
				while ( Actual[FirstDifference] == Expected.Words[FirstDifference] ) ++FirstDifference;
			}
		}
		if ( !bSame )
		{
			LOG( LogEcsSelfTest, Error, "Draw: the recorded command stream differs from the sequential one (%zu vs %zu word(s), first difference at word %zu)",
				 Actual.size(), Expected.Words.size(), FirstDifference );
		}
		Check( bSame, "recorded draw commands differ from the sequential result" );
		NumChecked += static_cast<int64>( Expected.Words.size() );
		NumWordsSeen += static_cast<int64>( Actual.size() );

		const unsigned char* Bytes = reinterpret_cast<const unsigned char*>( Actual.data() );
		for ( size_t i = 0; i < Actual.size() * sizeof( uint32 ); ++i ) Hash = ( Hash ^ Bytes[i] ) * 1099511628211ull;
	}

	// DxLib で実際に描いて、コマンド経由と直接呼びの画面を比べる
	void RunPixelTest()
	{
		bPixelDone = true;
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
		if ( Executor == nullptr )
		{
			Check( false, "no IDrawExecutor subsystem (the DxLib one should be created automatically)" );
			return;
		}

		constexpr int Width = 320;
		constexpr int Height = 240;
		const float32 NearClip = GetCameraNear();
		const float32 FarClip = GetCameraFar();
		const int ScreenA = MakeScreen( Width, Height, FALSE );
		const int ScreenB = MakeScreen( Width, Height, FALSE );
		const int Soft = MakeXRGB8ColorSoftImage( Width, Height );
		FDrawTestHandles Handles;
		Handles.AnimIndex = -1;

		// 画像: 16x16 の模様（透過あり）
		const int Pattern = MakeARGB8ColorSoftImage( 16, 16 );
		for ( int y = 0; y < 16; ++y )
			for ( int x = 0; x < 16; ++x ) DrawPixelSoftImage( Pattern, x, y, 255 - x * 16, y * 16, ( x ^ y ) * 16, ( x + y ) % 5 == 0 ? 0 : 255 );
		Handles.Graph = CreateGraphFromSoftImage( Pattern );
		DeleteSoftImage( Pattern );

		// モデル: 三角形 1 枚のテキスト形式（.mqo）を一時ファイルに書いて読み込む（読み込めなければ、モデルの確認だけ省く）
		char ModelPath[MAX_PATH + 32] = {};
		GetTempPathA( MAX_PATH, ModelPath );
		strcat_s( ModelPath, "ecs_selftest_triangle.mqo" );
		FILE* File = nullptr;
		if ( fopen_s( &File, ModelPath, "wb" ) == 0 && File != nullptr )
		{
			std::fputs( "Metasequoia Document\r\nFormat Text Ver 1.0\r\n\r\n"
						"Material 1 {\r\n\t\"mat1\" shader(3) col(0.900 0.300 0.300 1.000) dif(0.800) amb(0.600) emi(0.000) spc(0.000) power(5.00)\r\n}\r\n"
						"Object \"obj1\" {\r\n\tvisible 15\r\n\tlocking 0\r\n\tshading 1\r\n\tfacet 59.5\r\n\tcolor 0.898 0.498 0.698\r\n\tcolor_type 0\r\n"
						"\tvertex 3 {\r\n\t\t0.0 3.0 0.0\r\n\t\t-3.0 -3.0 0.0\r\n\t\t3.0 -3.0 0.0\r\n\t}\r\n"
						"\tface 1 {\r\n\t\t3 V(0 1 2) M(0)\r\n\t}\r\n}\r\nEof\r\n", File );
			std::fclose( File );
		}
		Handles.Model = MV1LoadModel( ModelPath );
		const bool bModel = Handles.Model >= 0;
		const int32 NumKinds = bModel ? 12 : 11;

		if ( ScreenA < 0 || ScreenB < 0 || Soft < 0 || Handles.Graph < 0 )
		{
			Check( false, "could not create the off-screen buffers for the pixel test" );
		}
		else
		{
			// 描くもの: 2D・3D・ブレンド・WorldMatrix・画像・文字列を、ランダムな位置・向きで
			std::vector<FDrawTestShape> Shapes;
			std::vector<FTransform> Worlds;
			for ( int32 i = 0; i < 260; ++i )
			{
				FDrawTestShape Shape = RandomShape();
				Shape.Kind = i % NumKinds;   // どの種類も同じ回数ずつ
				Shape.Size = Random.Range( 0.6f, 1.6f );
				Shapes.push_back( Shape );
				Worlds.push_back( FTransform( FVector3( Random.Range( -12, 12 ), Random.Range( -9, 9 ), Random.Range( -5, 5 ) ),
											  FQuaternion::FromAxisAngle( FVector3::Forward, Random.Range( -3.1f, 3.1f ) ) * FQuaternion::FromAxisAngle( FVector3::Up, Random.Range( -1.0f, 1.0f ) ),
											  FVector3( Random.Range( 0.5f, 1.5f ), 1.0f, 1.0f ) ) );
			}

			const auto Prepare = [&]( int Screen )
			{
				SetDrawScreen( Screen );
				SetCameraNearFar( 1.0f, 500.0f );
				SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -40.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
				ClearDrawScreen();
			};

			// A: コマンドに記録して、CDxLibDrawExecutor で実行（1 つのオブジェクト = 1 つの Span）
			Prepare( ScreenA );
			for ( size_t i = 0; i < Shapes.size(); ++i )
			{
				FDrawBuffer Buffer;
				FDrawContext Context( Buffer );
				EmitShape( Context, Worlds[i], Shapes[i], Handles );
				Executor->Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
			}
			GetDrawScreenSoftImage( 0, 0, Width, Height, Soft );
			std::vector<uint32> PixelsA;
			ReadPixels( Soft, Width, Height, PixelsA );

			// B: DxLib を直接呼ぶ
			Prepare( ScreenB );
			for ( size_t i = 0; i < Shapes.size(); ++i )
			{
				FDirectBackend Direct;
				EmitShape( Direct, Worlds[i], Shapes[i], Handles );
				Direct.EndShape();
			}
			GetDrawScreenSoftImage( 0, 0, Width, Height, Soft );
			std::vector<uint32> PixelsB;
			ReadPixels( Soft, Width, Height, PixelsB );

			int64 NumDrawn = 0, NumDifferent = 0;
			for ( size_t i = 0; i < PixelsA.size(); ++i )
			{
				if ( PixelsA[i] != 0 ) ++NumDrawn;
				if ( PixelsA[i] != PixelsB[i] ) ++NumDifferent;
			}
			NumChecked += static_cast<int64>( PixelsA.size() );
			Check( NumDifferent == 0, "pixels drawn through the command list differ from the direct DxLib calls" );
			Check( NumDrawn > 3000, "the pixel test drew (almost) nothing, so it proves nothing" );
			PixelSummary = FString::Printf( "%d kind(s) incl. 3D%s, %lld/%lld pixel(s) drawn, %lld differ", NumKinds, bModel ? " + MV1 model" : " (MV1 model not loaded)",
											static_cast<long long>( NumDrawn ), static_cast<long long>( PixelsA.size() ), static_cast<long long>( NumDifferent ) );
		}

		if ( bModel ) MV1DeleteModel( Handles.Model );
		if ( Handles.Graph >= 0 ) DeleteGraph( Handles.Graph );
		DeleteSoftImage( Soft );
		DeleteGraph( ScreenA );
		DeleteGraph( ScreenB );
		std::remove( ModelPath );
		// 描画先とカメラを元に戻す（SetDrawScreen はカメラをリセットするので、先に描画先）
		SetDrawScreen( DX_SCREEN_BACK );
		SetCameraNearFar( NearClip, FarClip );
	}

	void Finish()
	{
		for ( FDrawTestNode& Node : Nodes )
		{
			if ( Node.Object->IsAlive() ) Node.Object->Destroy();
		}
	}
};

struct CEcsSelfTestScene::FState
{
	int32 Phase = 0;   // 0: CTransformComponent のワールド値 / 1: システム / 2: 描画システム / 3: 計測
	FSystemTest Systems;
	FDrawTest Draw;
	FRandom Random;
	TArray<TSharedPtr<CGameObject>> Nodes;
	int32 Frame = 0;
	int64 NumChecked = 0;
	int64 NumMismatch = 0;
	uint64 Hash = 1469598103934665603ull;
	bool bFinished = false;
	bool bExitWhenDone = false;

	FTransform RandomLocal()
	{
		return FTransform( FVector3( Random.Range( -5, 5 ), Random.Range( -5, 5 ), Random.Range( -5, 5 ) ),
						   FQuaternion::FromAxisAngle( FVector3::Forward, Random.Range( -3.1f, 3.1f ) ) * FQuaternion::FromAxisAngle( FVector3::Up, Random.Range( -1.0f, 1.0f ) ),
						   FVector3( Random.Range( 0.5f, 2.0f ), Random.Range( 0.5f, 2.0f ), Random.Range( 0.5f, 2.0f ) ) );
	}

	void Spawn( CScene& Scene )
	{
		TSharedPtr<CTestNode> Node = Scene.SpawnObject<CTestNode>( Random.Int( 100 ) < 85 );
		if ( CTransformComponent* Transform = Node->GetComponent<CTransformComponent>() ) Transform->SetLocalTransform( RandomLocal() );
		// 親を付ける確率を抑えて、浅い階層をたくさんの要素にする（並列に計算する経路を通るように）
		if ( Nodes.Num() > 0 && Random.Int( 100 ) < 45 ) Node->AttachToParent( Nodes[Random.Int( Nodes.Num() )].Get() );
		Nodes.Add( Node );
	}

	// 全オブジェクトのワールド値を、以前の計算と比べる
	void CheckAll( const char* What, bool bHash )
	{
		for ( const TSharedPtr<CGameObject>& Node : Nodes )
		{
			if ( Node->GetScene() == nullptr ) continue;
			CTransformComponent* Transform = Node->GetComponent<CTransformComponent>();
			if ( Transform == nullptr ) continue;

			float Expected[10], Actual[10];
			ToFloats( LegacyWorld( Transform ), Expected );
			ToFloats( Transform->GetWorldTransform(), Actual );
			++NumChecked;
			if ( std::memcmp( Expected, Actual, sizeof( Expected ) ) != 0 )
			{
				if ( NumMismatch < 5 ) LOG( LogEcsSelfTest, Error, "Mismatch (%s) at frame %d: %s", What, Frame, *Node->GetDebugName() );
				++NumMismatch;
			}
			if ( bHash )
			{
				const unsigned char* Bytes = reinterpret_cast<const unsigned char*>( Actual );
				for ( size_t i = 0; i < sizeof( Actual ); ++i ) Hash = ( Hash ^ Bytes[i] ) * 1099511628211ull;
			}
		}
	}

	// 1 フレーム分の変更（値の変更・付け替え・破棄・生成）
	void Mutate( CScene& Scene )
	{
		const int32 Num = Nodes.Num();
		for ( int32 k = 0; k < Num / 10; ++k )
		{
			CGameObject* Node = Nodes[Random.Int( Num )].Get();
			CTransformComponent* Transform = Node->GetScene() != nullptr ? Node->GetComponent<CTransformComponent>() : nullptr;
			if ( Transform == nullptr ) continue;
			switch ( Random.Int( 4 ) )
			{
			case 0: Transform->SetLocalPosition( FVector3( Random.Range( -5, 5 ), Random.Range( -5, 5 ), Random.Range( -5, 5 ) ) ); break;
			case 1: Transform->SetLocalRotation( FQuaternion::FromAxisAngle( FVector3::Forward, Random.Range( -3.1f, 3.1f ) ) ); break;
			case 2: Transform->SetLocalScale( FVector3( Random.Range( 0.5f, 2.0f ), Random.Range( 0.5f, 2.0f ), Random.Range( 0.5f, 2.0f ) ) ); break;
			default: Transform->SetLocalTransform( RandomLocal() ); break;
			}
		}
		for ( int32 k = 0; k < Num / 100; ++k )
		{
			CGameObject* Node = Nodes[Random.Int( Num )].Get();
			CGameObject* Parent = Random.Int( 5 ) == 0 ? nullptr : Nodes[Random.Int( Num )].Get();
			if ( Node->GetScene() != nullptr && ( Parent == nullptr || Parent->GetScene() != nullptr ) ) Node->AttachToParent( Parent );
		}
		for ( int32 k = 0; k < Num / 200; ++k )
		{
			CGameObject* Node = Nodes[Random.Int( Num )].Get();
			if ( Node->GetScene() != nullptr ) Node->Destroy();
		}
		for ( int32 k = 0; k < Num / 200; ++k )
		{
			CGameObject* Node = Nodes[Random.Int( Num )].Get();
			CTransformComponent* Transform = Node->GetScene() != nullptr ? Node->GetComponent<CTransformComponent>() : nullptr;
			if ( Transform != nullptr ) Transform->Destroy();   // コンポーネントだけ取り外し予約（親としては数えない）
		}
		for ( int32 k = 0; k < Num / 100; ++k ) Spawn( Scene );
	}
};

bool CEcsSelfTestScene::IsRequestedOnCommandLine()
{
	const char* CommandLine = GetCommandLineA();
	return CommandLine != nullptr && std::strstr( CommandLine, "--selftest=ecs" ) != nullptr;
}

CEcsSelfTestScene::CEcsSelfTestScene() = default;
CEcsSelfTestScene::~CEcsSelfTestScene() = default;

void CEcsSelfTestScene::OnEnter()
{
	State = MakeUnique<FState>();
	State->bExitWhenDone = IsRequestedOnCommandLine();
	FSystemTest::Register( *this );
	State->Draw.Register( *this );
	// この確認では、描画システムの出力を、DxLib ではなく記録用の実行先に流す（コマンドの並びを比べるため）
	GDrawRecorder = &State->Draw.Recorder;
	SetDrawExecutor( &State->Draw.Recorder );

	for ( int32 i = 0; i < NumNodes; ++i ) State->Spawn( *this );
	IJobSystem* Jobs = GetAppSubsystem<IJobSystem>();
	LOG( LogEcsSelfTest, Log, "Started: %d object(s), %d transform(s), %d job worker(s)", State->Nodes.Num(), GetTransformWorld().GetNumNodes(),
		 Jobs ? Jobs->GetNumWorkers() : -1 );
}

void CEcsSelfTestScene::OnTick( float32 DeltaTime )
{
	if ( State->bFinished ) return;
	FState& Test = *State;

	if ( Test.Phase == 0 )
	{
		if ( Test.Frame > 0 && Test.Frame < NumFrames ) Test.Mutate( *this );

		// 1) 描画前の一括計算を待たずに読む（古ければ、その場で計算される）
		Test.CheckAll( "lazy", false );
		// 2) ジョブシステムで一斉に計算してから読む
		GetTransformWorld().Update( GetAppSubsystem<IJobSystem>() );
		Test.CheckAll( "update", true );
		++Test.Frame;

		if ( Test.Frame < NumFrames ) return;
		Test.Phase = 1;
		Test.Frame = 0;
		return;
	}

	// システム
	if ( Test.Phase == 1 )
	{
		if ( Test.Frame < NumSystemFrames )
		{
			Test.Systems.Step( *this, DeltaTime );
			++Test.Frame;
			return;
		}
		Test.Systems.Finish();
		Test.Phase = 2;
		Test.Frame = 0;
		return;
	}

	// 描画システム（ここで状態を変えて、そのフレームの OnDraw で、記録されたコマンドを確かめる）
	if ( Test.Phase == 2 )
	{
		if ( Test.Frame < NumDrawFrames )
		{
			Test.Draw.Step( *this );
			++Test.Frame;
			return;
		}

		Test.Draw.Finish();   // オブジェクトを破棄する（このフレームの終わりに片付く。計測には残さない）
		IJobSystem* Jobs = GetAppSubsystem<IJobSystem>();

		// 使い勝手用の API（SpawnEntity / ForEach / 任意コンポーネント T* / 除外 TWithout / CountData / FindFirstData）の自己確認
		int64 ErgoChecked = 0, ErgoMismatch = 0;
		{
			struct FErgoHp { int32 Value = 0; };
			struct FErgoArmor { int32 Reduction = 0; };
			struct FErgoInvincible {};

			auto CheckErgo = [&]( bool bOk, const char* What )
			{
				++ErgoChecked;
				if ( !bOk )
				{
					LOG( LogEcsSelfTest, Error, "Ergonomics: %s", What );
					++ErgoMismatch;
				}
			};

			CScene MiniScene;
			TSharedPtr<CGameObject> E0 = MiniScene.SpawnEntityAt( FVector3( 1.0f, 2.0f, 3.0f ), FErgoHp{ 100 } );
			TSharedPtr<CGameObject> E1 = MiniScene.SpawnEntity( FErgoHp{ 100 }, FErgoArmor{ 4 } );
			TSharedPtr<CGameObject> E2 = MiniScene.SpawnEntity( FErgoHp{ 100 }, FErgoInvincible{} );

			CheckErgo( E0->HasData<FErgoHp>() && !E0->HasData<FErgoArmor>(), "SpawnEntityAt should attach FErgoHp" );
			CheckErgo( ( E1->HasAllData<FErgoHp, FErgoArmor>() ) && !( E1->HasAllData<FErgoHp, FErgoInvincible>() ), "HasAllData should check all component types" );
			CheckErgo( ( E2->HasAnyData<FErgoArmor, FErgoInvincible>() ), "HasAnyData should check any component type" );
			CheckErgo( MiniScene.CountData<FErgoHp>() == 3, "CountData<FErgoHp> should be 3" );
			CheckErgo( ( MiniScene.CountData<FErgoHp, FErgoArmor>() ) == 1, "CountData<FErgoHp, FErgoArmor> should be 1" );
			CheckErgo( MiniScene.FindFirstData<FErgoArmor>() != nullptr && MiniScene.FindFirstData<FErgoArmor>()->Reduction == 4, "FindFirstData should find FErgoArmor" );
			CheckErgo( MiniScene.FindFirstObjectWithData<FErgoInvincible>() == E2.Get(), "FindFirstObjectWithData should find E2" );

			FErgoArmor& AddedArmor = E0->GetOrAddData<FErgoArmor>( FErgoArmor{ 2 } );
			CheckErgo( AddedArmor.Reduction == 2 && ( MiniScene.CountData<FErgoHp, FErgoArmor>() ) == 2, "GetOrAddData should add missing component" );
			E0->RemoveData<FErgoArmor>();
			CheckErgo( !E0->HasData<FErgoArmor>() && ( MiniScene.CountData<FErgoHp, FErgoArmor>() ) == 1, "RemoveData should update component mask and storage" );

			// ForEach: 必須 (FErgoHp&) + 任意 (const FErgoArmor*) + 除外 (NEcs::TWithout<FErgoInvincible>)
			const int32 Visited = MiniScene.ForEach( []( FErgoHp& Hp, const FErgoArmor* Armor, NEcs::TWithout<FErgoInvincible> )
			{
				Hp.Value -= ( Armor != nullptr ? 10 - Armor->Reduction : 10 );
			} );
			CheckErgo( Visited == 2, "ForEach should visit E0 and E1 and skip E2 (TWithout<FErgoInvincible>)" );
			CheckErgo( E0->GetData<FErgoHp>()->Value == 90, "E0 (no armor) should take 10 damage" );
			CheckErgo( E1->GetData<FErgoHp>()->Value == 94, "E1 (armor 4) should take 6 damage" );
			CheckErgo( E2->GetData<FErgoHp>()->Value == 100, "E2 (invincible) should be untouched" );

			// ForEach: const FTransformView& と bool 戻り値による途中終了
			FVector3 ReadPos = FVector3::Zero;
			const int32 EarlyVisited = MiniScene.ForEach( [&]( const FTransformView& Transform, const FErgoHp& )
			{
				ReadPos = Transform.GetWorldPosition();
				return false;
			} );
			CheckErgo( EarlyVisited == 1 && ReadPos.x == 1.0f && ReadPos.y == 2.0f && ReadPos.z == 3.0f, "ForEach with bool return should stop after 1 entity and read world position" );

			// AddSystem（フェーズ省略 = PreUpdate）でも T* と TWithout が並列に動くことを確かめる
			MiniScene.AddSystem( "Ergo.Damage", []( FErgoHp& Hp, const FErgoArmor* Armor, NEcs::TWithout<FErgoInvincible> )
			{
				Hp.Value -= ( Armor != nullptr ? 10 - Armor->Reduction : 10 );
			} );
			MiniScene.GetSystems().Run( NEcs::EPhase::PreUpdate, 0.016f, ~0ull, MiniScene.GetEcsWorld(), MiniScene.GetTransformWorld(), Jobs );
			CheckErgo( E0->GetData<FErgoHp>()->Value == 80 && E1->GetData<FErgoHp>()->Value == 88 && E2->GetData<FErgoHp>()->Value == 100,
					   "AddSystem with optional pointer and TWithout should match ForEach result" );
		}

		const int64 NumMismatch = Test.NumMismatch + Test.Systems.NumMismatch + Test.Draw.NumMismatch + ErgoMismatch;
		const int64 NumChecked = Test.NumChecked + Test.Systems.NumChecked + Test.Draw.NumChecked + ErgoChecked;
		if ( NumMismatch == 0 )
		{
			LOG( LogEcsSelfTest, Log, "SELFTEST ecs PASS: %lld value(s) checked (%d destroyed by systems; %lld draw command word(s) in %d frame(s), pixels: %s), hash %016llx, job workers %d",
				 static_cast<long long>( NumChecked ), Test.Systems.NumDestroyed, static_cast<long long>( Test.Draw.NumWordsSeen ), NumDrawFrames, *Test.Draw.PixelSummary,
				 static_cast<unsigned long long>( Test.Hash ^ ( Test.Systems.Hash * 31u ) ^ ( Test.Draw.Hash * 131u ) ), Jobs ? Jobs->GetNumWorkers() : -1 );
		}
		else
		{
			LOG( LogEcsSelfTest, Error, "SELFTEST ecs FAIL: %lld mismatch(es) in %lld value(s)", static_cast<long long>( NumMismatch ), static_cast<long long>( NumChecked ) );
		}
		Test.Phase = 3;
		return;
	}

	// 計測（結果はログに出るだけ。合否には関係しない）
	Test.bFinished = true;
	RunBenchmark();
	RunDrawBenchmark();

	if ( Test.bExitWhenDone )
	{
		if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->RequestExit();
	}
}

void CEcsSelfTestScene::RunBenchmark()
{
	IJobSystem* Jobs = GetAppSubsystem<IJobSystem>();
	FTransformWorld& World = GetTransformWorld();

	// 並列化の効果が見えるよう、階層のあるオブジェクトを 10 万個足す（90% は、先に作ったものの子になる）
	const int32 FirstBench = State->Nodes.Num();
	for ( int32 i = 0; i < 100000; ++i )
	{
		TSharedPtr<CTestNode> Node = SpawnObject<CTestNode>( true );
		if ( i > 0 && State->Random.Int( 100 ) < 90 ) Node->AttachToParent( State->Nodes[FirstBench + State->Random.Int( i )].Get() );
		State->Nodes.Add( Node );
	}

	// 全ノードの値を書き換えて dirty にし、Update の時間を測る（何回かやって最短を取る）
	constexpr int32 NumRuns = 8;
	float64 BestMs = 1.0e30;
	for ( int32 Run = 0; Run < NumRuns; ++Run )
	{
		for ( const TSharedPtr<CGameObject>& Node : State->Nodes )
		{
			if ( Node->GetScene() == nullptr ) continue;
			if ( CTransformComponent* Transform = Node->GetComponent<CTransformComponent>() ) Transform->SetLocalTransform( State->RandomLocal() );
		}
		const int64 Start = NPlatformTime::GetTicks();
		World.Update( Jobs );
		BestMs = NMath::Min( BestMs, NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - Start ) );
	}
	LOG( LogEcsSelfTest, Log, "BENCH: Update of %d transform(s) (max depth %d): %.3f ms (best of %d), job workers %d", World.GetNumNodes(), World.GetMaxDepth(), BestMs, NumRuns,
		 Jobs ? Jobs->GetNumWorkers() : -1 );

	// 同じ回転の処理を、(A) システム（自動で並列）と (B) ふつうの Tick（1 つずつ呼ぶ）で比べる
	constexpr int32 NumBench = 100000;
	TArray<TSharedPtr<CGameObject>> TickNodes;
	for ( int32 i = 0; i < NumBench; ++i )
	{
		TSharedPtr<CSystemTestNode> Node = SpawnObject<CSystemTestNode>();
		Node->AddData( FAngularMotion{ FVector3::Forward, 1.5f } );
		State->Nodes.Add( Node );
		TickNodes.Add( SpawnObject<CBenchSpinNode>() );
	}
	float64 SystemMs = 1.0e30, TickMs = 1.0e30;
	for ( int32 Run = 0; Run < NumRuns; ++Run )
	{
		int64 Start = NPlatformTime::GetTicks();
		GetSystems().Run( NEcs::EPhase::PreUpdate, 0.016f, ~0ull, GetEcsWorld(), GetTransformWorld(), Jobs );
		SystemMs = NMath::Min( SystemMs, NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - Start ) );

		Start = NPlatformTime::GetTicks();
		for ( const TSharedPtr<CGameObject>& Node : TickNodes ) Node->Tick( 0.016f );
		TickMs = NMath::Min( TickMs, NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - Start ) );
	}
	LOG( LogEcsSelfTest, Log, "BENCH: rotating %d object(s): system %.3f ms, virtual Tick loop %.3f ms (best of %d), job workers %d", NumBench, SystemMs, TickMs, NumRuns,
		 Jobs ? Jobs->GetNumWorkers() : -1 );
	for ( TSharedPtr<CGameObject>& Node : TickNodes ) Node->Destroy();
}

void CEcsSelfTestScene::RunDrawBenchmark()
{
	IJobSystem* Jobs = GetAppSubsystem<IJobSystem>();
	IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();   // 本物の DxLib 実行先（このシーンの記録用ではなく）
	if ( Executor == nullptr ) return;

	// 同じ四角形 10 万個を、(A) 描画システム（記録は並列）-> DxLib に流す（直列）と、(B) ふつうの Draw() で DxLib を直接呼ぶ、で比べる
	constexpr int32 NumBench = 100000;
	constexpr int32 NumRuns = 6;
	FRandom Random;
	TArray<TSharedPtr<CGameObject>> SystemNodes;
	TArray<TSharedPtr<CDrawBenchLegacyNode>> LegacyNodes;
	std::vector<int32> Entities;
	for ( int32 i = 0; i < NumBench; ++i )
	{
		const FVector3 Position( Random.Range( -30, 30 ), Random.Range( -22, 22 ), 0.0f );
		FDrawBenchBox Box;
		Box.HalfSize = Random.Range( 1.0f, 4.0f );
		Box.Color = FColor32( static_cast<uint8>( 60 + Random.Int( 195 ) ), static_cast<uint8>( 60 + Random.Int( 195 ) ), static_cast<uint8>( 60 + Random.Int( 195 ) ) );

		TSharedPtr<CSystemTestNode> SystemNode = SpawnObject<CSystemTestNode>();
		SystemNode->GetComponent<CTransformComponent>()->SetLocalPosition( Position );
		SystemNode->AddData( Box );
		Entities.push_back( SystemNode->GetEntitySlot() );
		SystemNodes.Add( SystemNode );

		TSharedPtr<CDrawBenchLegacyNode> LegacyNode = SpawnObject<CDrawBenchLegacyNode>();
		LegacyNode->GetComponent<CTransformComponent>()->SetLocalPosition( Position );
		LegacyNode->Box = Box;
		LegacyNodes.Add( LegacyNode );
	}

	const float32 NearClip = GetCameraNear();
	const float32 FarClip = GetCameraFar();
	const int Screen = MakeScreen( 640, 480, FALSE );
	if ( Screen >= 0 )
	{
		SetDrawScreen( Screen );
		GetTransformWorld().Update( Jobs );   // 最初の 1 回の計算を、計測に入れない

		for ( int32 Heavy = 0; Heavy <= 1; ++Heavy )
		{
			for ( const TSharedPtr<CGameObject>& Node : SystemNodes ) Node->GetData<FDrawBenchBox>()->bHeavy = Heavy;
			for ( const TSharedPtr<CDrawBenchLegacyNode>& Node : LegacyNodes ) Node->Box.bHeavy = Heavy;

			float64 RecordMs = 1.0e30, ReplayMs = 1.0e30, SystemTotalMs = 1.0e30, LegacyMs = 1.0e30;
			for ( int32 Run = 0; Run < NumRuns; ++Run )
			{
				ClearDrawScreen();
				int64 Start = NPlatformTime::GetTicks();
				GetSystems().Run( NEcs::EPhase::Draw, 0.0f, ~0ull, GetEcsWorld(), GetTransformWorld(), Jobs );
				const int64 Recorded = NPlatformTime::GetTicks();
				for ( const int32 Entity : Entities ) GetSystems().ReplayDraw( Entity, *Executor );
				RenderVertex();   // DxLib に溜まった頂点を吐き出すところまで含める
				const int64 Replayed = NPlatformTime::GetTicks();
				RecordMs = NMath::Min( RecordMs, NPlatformTime::TicksToMs( Recorded - Start ) );
				ReplayMs = NMath::Min( ReplayMs, NPlatformTime::TicksToMs( Replayed - Recorded ) );
				SystemTotalMs = NMath::Min( SystemTotalMs, NPlatformTime::TicksToMs( Replayed - Start ) );

				ClearDrawScreen();
				Start = NPlatformTime::GetTicks();
				for ( const TSharedPtr<CDrawBenchLegacyNode>& Node : LegacyNodes ) Node->Draw();
				RenderVertex();
				LegacyMs = NMath::Min( LegacyMs, NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - Start ) );
			}
			LOG( LogEcsSelfTest, Log, "BENCH: drawing %d box(es) [%s per-object work]: draw system = record %.3f ms (parallel) + DxLib replay %.3f ms = %.3f ms, direct virtual Draw() loop %.3f ms (best of %d), job workers %d",
				 NumBench, Heavy != 0 ? "heavy" : "light", RecordMs, ReplayMs, SystemTotalMs, LegacyMs, NumRuns, Jobs ? Jobs->GetNumWorkers() : -1 );
		}
		SetDrawScreen( DX_SCREEN_BACK );
		SetCameraNearFar( NearClip, FarClip );
		DeleteGraph( Screen );
	}

	for ( TSharedPtr<CGameObject>& Node : SystemNodes ) Node->Destroy();
	for ( TSharedPtr<CDrawBenchLegacyNode>& Node : LegacyNodes ) Node->Destroy();
}

void CEcsSelfTestScene::OnDraw()
{
	// 描画システムの確認: このフレームの描画で記録されたコマンドを、素直に作ったものと比べる（その後、一度だけ DxLib で実際に描いて比べる）
	if ( State && State->Phase == 2 && State->Draw.bArmed )
	{
		State->Draw.Verify();
		if ( !State->Draw.bPixelDone ) State->Draw.RunPixelTest();
	}

	const int32 Frame = State ? State->Frame : 0;
	DrawFormatString( 16, 40, FColor32::White.ToDxColor(), "ECS SELF TEST   %s   frame %d   objects: %d", State && State->Phase == 1 ? "systems" : "transforms", Frame, GetNumObjects() );
}

void CEcsSelfTestScene::OnExit()
{
	// 保持しているオブジェクトを手放す（シーンの破棄より先に）
	GDrawRecorder = nullptr;
	SetDrawExecutor( nullptr );
	if ( State ) State->Draw.Nodes.Reset();
	if ( State ) State->Nodes.Reset();
}
