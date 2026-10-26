#include "WorldSelfTestScene.h"
#include "WorldMeshSelfTestSupport.h"
#include "WorldMeshMaterialSelfTestSupport.h"
#include "WorldModelMaterialSelfTest.h"
#include "WorldModelPreviewSelfTest.h"
#include "WorldModelMaterialInspectorSelfTest.h"
#include "WorldSelfTestSupport.h"
#include "WorldEffectForceFieldEditorSelfTest.h"
#include "WorldEffectForceFieldEditorGeometrySelfTest.h"
#include "WorldEffectTargetSelfTest.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Audio/AudioComponents.h"
#include "Library/Framework/Audio/AudioSystem.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Billboard.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Render/MeshRender.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldCollision.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Framework/World/WorldRender.h"
#include "Library/Framework/World/WorldSpawner.h"
#include "Library/Framework/World/WorldValidate.h"
#include "Library/Physics/WorldPhysics.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/FileSystem.h"

// 剛体の形・センサーかどうかを、Jolt から直接読む（windows.h の min / max マクロが Jolt と衝突するので、一時退避する）
#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

DEFINE_LOG_CATEGORY_STATIC( LogWorldSelfTest, Log )

using namespace NAssetTest;

namespace
{
	class CTestHolder : public CGameObject
	{
	public:
		void OnSpawn() override { AddComponent<CTransformComponent>(); }
	};

	// タグに結び付けて作るオブジェクト
	class CTestEnemy : public CGameObject
	{
	public:
		int32 Hp = 0;
		void OnSpawn() override { AddComponent<CTransformComponent>(); }
	};

	bool Near3( const FVector3& A, const FVector3& B, float32 Tolerance = 1.0e-3f )
	{
		return std::fabs( A.x - B.x ) <= Tolerance && std::fabs( A.y - B.y ) <= Tolerance && std::fabs( A.z - B.z ) <= Tolerance;
	}

	FWorldObject MakeObject( int32 Id, const char* Name, int32 Parent = WorldNoId )
	{
		FWorldObject Object;
		Object.Id = Id;
		Object.Name = Name;
		Object.Parent = Parent;
		return Object;
	}

	FWorldBillboard MakeBoard( EWorldBillboardMode Mode )
	{
		FWorldBillboard Board;
		Board.Texture = "a.png";
		Board.Mode = Mode;
		return Board;
	}

	FWorldCollider MakeBox( const FVector3& Size, const FVector3& Offset )
	{
		FWorldCollider Collider;
		Collider.Shape = EWorldColliderShape::Box;
		Collider.Size = Size;
		Collider.Offset = Offset;
		return Collider;
	}

	// 設置の確認用のワールド
	FWorldDefinition BuildStageWorld()
	{
		FWorldDefinition World;
		World.Name = "Stage";
		FWorldCollisionRule Decoration;
		Decoration.Tag = "Decoration";
		Decoration.bCollide = false;
		World.Settings.CollisionRules.Add( Decoration );

		const auto Add = [&]( const FWorldObject& Object ) { World.Objects.Add( Object ); };

		FWorldObject Stage = MakeObject( 1, "Stage" );
		Stage.AddTag( "Root" );
		Add( Stage );

		FWorldObject Statue = MakeObject( 2, "Statue", 1 );
		Statue.AddTag( "Prop" );
		Statue.Transform.Position = FVector3( 1.0f, 0.0f, 2.0f );
		Statue.Transform.RotationDegrees = FVector3( 0.0f, 90.0f, 0.0f );
		Statue.Transform.Scale = FVector3( 2.0f, 2.0f, 2.0f );
		FWorldModel Model;
		Model.Path = "model.mv1";
		Model.Clip = "Idle";
		Model.Speed = 2.0f;
		Statue.Model = Model;
		Statue.Collider = MakeBox( FVector3( 1.0f, 2.0f, 1.0f ), FVector3( 0.0f, 1.0f, 0.0f ) );
		Add( Statue );

		FWorldObject Torch = MakeObject( 3, "Torch", 1 );
		Torch.AddTag( "Light" );
		Torch.AddTag( "Decoration" );
		Torch.Transform.Position = FVector3( 3.0f, 1.0f, 0.0f );
		FWorldBillboard Board = MakeBoard( EWorldBillboardMode::CameraY );
		Board.Size = FVector2( 2.0f, 1.0f );
		Board.Pivot = FVector2( 0.5f, 1.0f );
		Board.Tint = FColor32( 255, 128, 0, 200 );
		Board.bUseRect = true;
		Board.RectX = 16;
		Board.RectY = 8;
		Board.RectWidth = 32;
		Board.RectHeight = 16;
		Board.bFlipX = true;
		Torch.Billboard = Board;
		FWorldSound Sound;
		Sound.Path = "fire.wav";
		Sound.Volume = 0.5f;
		Sound.Pitch = 1.5f;
		Sound.MinDistance = 2.0f;
		Sound.MaxDistance = 30.0f;
		Torch.Sound = Sound;
		FWorldCollider Sphere;
		Sphere.Shape = EWorldColliderShape::Sphere;
		Sphere.Radius = 0.5f;
		Torch.Collider = Sphere;
		Add( Torch );

		FWorldObject Start = MakeObject( 4, "SpawnPoint", 1 );
		Start.AddTag( "PlayerStart" );
		Start.AddTag( "Marker" );
		Start.Transform.Position = FVector3( 0.0f, 5.0f, 0.0f );
		Add( Start );

		FWorldObject EnemyA = MakeObject( 5, "EnemyA", 1 );
		EnemyA.AddTag( "Enemy" );
		EnemyA.SetProp( FWorldProp::MakeInt( "Hp", 30 ) );
		EnemyA.Transform.Position = FVector3( 4.0f, 0.0f, 0.0f );
		Add( EnemyA );
		FWorldObject EnemyB = MakeObject( 6, "EnemyB", 1 );
		EnemyB.AddTag( "Enemy" );
		EnemyB.AddTag( "Boss" );
		EnemyB.SetProp( FWorldProp::MakeInt( "Hp", 100 ) );
		EnemyB.Transform.Position = FVector3( 6.0f, 0.0f, 0.0f );
		Add( EnemyB );

		FWorldObject Zone = MakeObject( 7, "Zone", 1 );
		Zone.AddTag( "Zone" );
		Zone.Transform.Position = FVector3( 0.0f, 0.0f, 10.0f );
		FWorldCollider Trigger = MakeBox( FVector3( 4.0f, 2.0f, 4.0f ), FVector3::Zero );
		Trigger.bTrigger = true;
		Zone.Collider = Trigger;
		Add( Zone );

		FWorldObject Hidden = MakeObject( 8, "Hidden", 1 );
		Hidden.bEnabled = false;
		Add( Hidden );
		Add( MakeObject( 9, "HiddenChild", 8 ) );

		FWorldObject Fx = MakeObject( 10, "Fx", 1 );
		FWorldRawComponent Raw;
		Raw.Type = "Hologram";
		Raw.Data = NlohmannJson::parse( "{\"File\":\"x.efk\"}" );
		Fx.RawComponents.Add( Raw );
		Add( Fx );

		FWorldObject Failing = MakeObject( 11, "Failing", 1 );
		Failing.AddTag( "Fails" );
		Add( Failing );
		Add( MakeObject( 12, "Orphan", 99 ) );
		Add( MakeObject( 13, "LoopA", 14 ) );
		Add( MakeObject( 14, "LoopB", 13 ) );

		FWorldObject NoModel = MakeObject( 15, "NoModelFile", 1 );
		FWorldModel Missing;
		Missing.Path = "missing.mv1";
		NoModel.Model = Missing;
		Add( NoModel );
		FWorldObject NoSound = MakeObject( 16, "NoSoundFile", 1 );
		FWorldSound MissingSound;
		MissingSound.Path = "nope.wav";
		NoSound.Sound = MissingSound;
		Add( NoSound );

		FWorldObject Sign = MakeObject( 17, "Sign", 1 );
		Sign.Transform.Position = FVector3( 0.0f, 0.0f, 5.0f );
		Sign.Transform.RotationDegrees = FVector3( 0.0f, 90.0f, 0.0f );
		Sign.Transform.Scale = FVector3( 2.0f, 3.0f, 1.0f );
		FWorldBillboard SignBoard = MakeBoard( EWorldBillboardMode::World );
		SignBoard.Size = FVector2( 1.5f, 1.0f );
		Sign.Billboard = SignBoard;
		Add( Sign );

		FWorldObject Roll = MakeObject( 18, "Roll", 1 );
		Roll.Transform.Position = FVector3( 0.0f, 3.0f, 0.0f );
		Roll.Transform.RotationDegrees = FVector3( 0.0f, 0.0f, 90.0f );
		Roll.Billboard = MakeBoard( EWorldBillboardMode::Camera );
		Add( Roll );

		FWorldObject AnimStatue = MakeObject( 19, "AnimStatue", 1 );
		FWorldModel AnimModel;
		AnimModel.Path = "model.mv1";
		AnimModel.Animations = "set.modelanim.json";
		AnimModel.Clip = "Sprint";
		AnimStatue.Model = AnimModel;
		Add( AnimStatue );

		FWorldObject Plain = MakeObject( 20, "PlainHolder", 1 );
		Plain.AddTag( "Plain" );
		Plain.Transform.Position = FVector3( 0.0f, 0.0f, 3.0f );
		Add( Plain );

		World.NextId = 21;
		return World;
	}

	// 物理の確認用のワールド: 規則に従って、床が付く / 付かない
	FWorldDefinition BuildPhysicsWorld()
	{
		FWorldDefinition World;
		World.Name = "Physics";
		FWorldCollisionRule Decoration;
		Decoration.Tag = "Decoration";
		Decoration.bCollide = false;
		World.Settings.CollisionRules.Add( Decoration );
		const FWorldCollider Floor = MakeBox( FVector3( 10.0f, 1.0f, 10.0f ), FVector3( 0.0f, -0.5f, 0.0f ) );

		FWorldObject Ground = MakeObject( 1, "Floor" );
		Ground.AddTag( "Ground" );
		Ground.Collider = Floor;
		World.Objects.Add( Ground );

		FWorldObject Ghost = MakeObject( 2, "Ghost" );   // タグ規則で、付かない
		Ghost.AddTag( "Decoration" );
		Ghost.Transform.Position = FVector3( 50.0f, 0.0f, 0.0f );
		Ghost.Collider = Floor;
		World.Objects.Add( Ghost );

		FWorldObject Forced = MakeObject( 3, "Forced" );   // タグ規則は付けないだが、オブジェクトの指定が勝つ
		Forced.AddTag( "Decoration" );
		Forced.Collision = EWorldCollisionMode::On;
		Forced.Transform.Position = FVector3( 100.0f, 0.0f, 0.0f );
		Forced.Collider = Floor;
		World.Objects.Add( Forced );

		FWorldObject Off = MakeObject( 4, "OffFloor" );   // オブジェクトの指定で、付かない
		Off.Collision = EWorldCollisionMode::Off;
		Off.Transform.Position = FVector3( 150.0f, 0.0f, 0.0f );
		Off.Collider = Floor;
		World.Objects.Add( Off );

		// 形の大きさの確認（球は置き物の拡大 2 が掛かる。カプセルは、半球を含む全体の高さ 2・半径 0.5）
		FWorldObject Capsule = MakeObject( 5, "Capsule" );
		Capsule.Transform.Position = FVector3( 200.0f, 0.0f, 0.0f );
		FWorldCollider CapsuleShape;
		CapsuleShape.Shape = EWorldColliderShape::Capsule;
		CapsuleShape.Radius = 0.5f;
		CapsuleShape.Height = 2.0f;
		Capsule.Collider = CapsuleShape;
		World.Objects.Add( Capsule );

		FWorldObject Ball = MakeObject( 6, "Sphere" );
		Ball.Transform.Position = FVector3( 250.0f, 0.0f, 0.0f );
		Ball.Transform.Scale = FVector3( 2.0f, 2.0f, 2.0f );
		FWorldCollider SphereShape;
		SphereShape.Shape = EWorldColliderShape::Sphere;
		SphereShape.Radius = 0.75f;
		Ball.Collider = SphereShape;
		World.Objects.Add( Ball );

		World.NextId = 7;
		return World;
	}

	// メッシュの当たり判定・トリガーの確認用のワールド（"floor.mv1" は、偽のバックエンドが、20 x 20 の床の三角形を答える）
	FWorldDefinition BuildMeshWorld()
	{
		FWorldDefinition World;
		World.Name = "Mesh";
		const auto Add = [&]( int32 Id, const char* Name, const FVector3& Position, const char* ModelPath, const FWorldCollider& Collider )
		{
			FWorldObject Object = MakeObject( Id, Name );
			Object.Transform.Position = Position;
			if ( ModelPath != nullptr )
			{
				FWorldModel Model;
				Model.Path = ModelPath;
				Object.Model = Model;
			}
			Object.Collider = Collider;
			World.Objects.Add( Object );
			return static_cast<int32>( World.Objects.Num() ) - 1;
		};
		const auto MeshOf = [&]( const char* Path, bool bTrigger )
		{
			FWorldCollider Collider;
			Collider.Shape = EWorldColliderShape::Mesh;
			Collider.Mesh = Path;
			Collider.Offset = FVector3( 0.0f, 5.0f, 0.0f );   // メッシュでは使わない（モデルの座標のまま）
			Collider.bTrigger = bTrigger;
			return Collider;
		};
		const auto BoxOf = [&]( const FVector3& Size, const FVector3& Offset, bool bTrigger )
		{
			FWorldCollider Collider = MakeBox( Size, Offset );
			Collider.bTrigger = bTrigger;
			return Collider;
		};

		Add( 1, "MeshFloor", FVector3( 0.0f, 1.0f, 0.0f ), "floor.mv1", MeshOf( "", false ) );   // モデルの三角形（Mesh のファイルなし）
		const int32 Scaled = Add( 2, "ScaledFloor", FVector3( 100.0f, 0.0f, 0.0f ), "floor.mv1", MeshOf( "", false ) );
		World.Objects[Scaled].Transform.RotationDegrees = FVector3( 0.0f, 45.0f, 0.0f );
		World.Objects[Scaled].Transform.Scale = FVector3( 2.0f, 1.0f, 2.0f );
		Add( 3, "MeshOnly", FVector3( 200.0f, 0.0f, 0.0f ), nullptr, MeshOf( "floor.mv1", false ) );   // Model 部品なし。Mesh のファイルから
		Add( 4, "NoModel", FVector3( 300.0f, 0.0f, 0.0f ), nullptr, MeshOf( "", false ) );               // 三角形を取るものがない: 警告
		Add( 5, "NoTriangles", FVector3( 350.0f, 0.0f, 0.0f ), nullptr, MeshOf( "only.mv1", false ) );   // 三角形のないモデル: 警告
		Add( 6, "Zone", FVector3( 400.0f, 0.0f, 0.0f ), nullptr, BoxOf( FVector3( 4.0f, 4.0f, 4.0f ), FVector3( 0.0f, 2.0f, 0.0f ), true ) );            // y 0..4。下に床はない（通り抜ける）
		Add( 7, "ZoneFloor", FVector3( 400.0f, -5.0f, 0.0f ), nullptr, BoxOf( FVector3( 10.0f, 1.0f, 10.0f ), FVector3( 0.0f, -0.5f, 0.0f ), false ) );   // 上面 y = -5
		Add( 8, "MeshZone", FVector3( 500.0f, 0.0f, 0.0f ), nullptr, MeshOf( "floor.mv1", true ) );    // 面のトリガー（1 つの球に、いくつもの三角形が当たる）
		Add( 9, "MeshZoneFloor", FVector3( 500.0f, -5.0f, 0.0f ), nullptr, BoxOf( FVector3( 10.0f, 1.0f, 10.0f ), FVector3( 0.0f, -0.5f, 0.0f ), false ) );
		Add( 10, "KeepZone", FVector3( 600.0f, 0.0f, 0.0f ), nullptr, BoxOf( FVector3( 4.0f, 4.0f, 4.0f ), FVector3( 0.0f, 2.0f, 0.0f ), true ) );    // 球が中で止まる
		Add( 11, "KeepFloor", FVector3( 600.0f, 0.0f, 0.0f ), nullptr, BoxOf( FVector3( 10.0f, 1.0f, 10.0f ), FVector3( 0.0f, -0.5f, 0.0f ), false ) );
		Add( 12, "Reversed", FVector3( 700.0f, 0.0f, 0.0f ), nullptr, MeshOf( "floorrev.mv1", false ) );   // 三角形の向きが逆の床
		Add( 13, "CompoundZone", FVector3( 900.0f, 0.0f, 0.0f ), nullptr, BoxOf( FVector3( 4.0f, 4.0f, 4.0f ), FVector3( 0.0f, 2.0f, 0.0f ), true ) );   // 2 つの球をつないだ物が通る（1 組で、接触が 2 つ起きる）
		Add( 14, "CompoundFloor", FVector3( 900.0f, -5.0f, 0.0f ), nullptr, BoxOf( FVector3( 10.0f, 1.0f, 10.0f ), FVector3( 0.0f, -0.5f, 0.0f ), false ) );
		Add( 15, "LateZone", FVector3( 1000.0f, 0.0f, 0.0f ), nullptr, BoxOf( FVector3( 4.0f, 4.0f, 4.0f ), FVector3( 0.0f, 2.0f, 0.0f ), true ) );       // 球より後に作られるトリガー（物理が知らせる 2 つの剛体の並びが逆）
		Add( 16, "LateFloor", FVector3( 1000.0f, -5.0f, 0.0f ), nullptr, BoxOf( FVector3( 10.0f, 1.0f, 10.0f ), FVector3( 0.0f, -0.5f, 0.0f ), false ) );

		// 編集メッシュ（Mesh 部品）: 床（メッシュの当たり判定つき）、面のない空のもの、材質の違う面を持つ箱
		const int32 EditFloor = Add( 17, "EditFloor", FVector3( 1100.0f, 0.0f, 0.0f ), nullptr, MeshOf( "", false ) );
		World.Objects[EditFloor].Mesh = NEditMesh::MakeBox( FVector3( 20.0f, 1.0f, 20.0f ) );
		const int32 EmptyMesh = Add( 18, "EmptyMesh", FVector3( 1200.0f, 0.0f, 0.0f ), nullptr, FWorldCollider() );
		World.Objects[EmptyMesh].Collider.Reset();
		FEditMesh Hollow;
		Hollow.Vertices.Add( FVector3::Zero );
		World.Objects[EmptyMesh].Mesh = Hollow;
		const int32 Painted = Add( 19, "Painted", FVector3( 1300.0f, 0.0f, 0.0f ), nullptr, FWorldCollider() );
		World.Objects[Painted].Collider.Reset();
		FEditMesh Box = NEditMesh::MakeBox( FVector3( 2.0f, 2.0f, 2.0f ) );
		FEditMeshMaterial Red, Photo, Lost;
		Red.Color = FColor32( 255, 0, 0, 255 );
		Photo.Texture = "a.png";
		Photo.bTwoSided = true;
		Lost.Texture = "nope.png";
		Box.Materials.Add( Red );
		Box.Materials.Add( Photo );
		Box.Materials.Add( Lost );
		Box.Faces[1].Material = 1;
		Box.Faces[2].Material = 2;
		World.Objects[Painted].Mesh = Box;
		World.NextId = 20;
		return World;
	}

	//=========================================================================
	// 画面に描いたピクセルを読む（オフスクリーン）
	//=========================================================================
	struct FCapture
	{
		int Screen = -1;
		int Soft = -1;
		int Width = 0;
		int Height = 0;

		bool Begin( int InWidth, int InHeight )
		{
			Width = InWidth;
			Height = InHeight;
			Screen = MakeScreen( Width, Height, FALSE );
			Soft = MakeARGB8ColorSoftImage( Width, Height );
			if ( Screen < 0 || Soft < 0 ) return false;
			SetDrawScreen( Screen );
			ClearDrawScreen();
			return true;
		}
		void Read() { GetDrawScreenSoftImage( 0, 0, Width, Height, Soft ); }
		void At( int X, int Y, int& R, int& G, int& B ) const
		{
			int A = 0;
			GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A );
		}
		// (X, Y) の色が (R, G, B) か（許容差つき）
		bool IsColor( int X, int Y, int R, int G, int B, int Tolerance = 6 ) const
		{
			int PR = 0, PG = 0, PB = 0;
			At( X, Y, PR, PG, PB );
			return std::abs( PR - R ) <= Tolerance && std::abs( PG - G ) <= Tolerance && std::abs( PB - B ) <= Tolerance;
		}
		bool IsBlack( int X, int Y ) const { return IsColor( X, Y, 0, 0, 0, 2 ); }
		// 光っている（黒でない）ピクセルの外接矩形
		bool LitBounds( int& OutLeft, int& OutTop, int& OutRight, int& OutBottom ) const
		{
			OutLeft = Width;
			OutTop = Height;
			OutRight = -1;
			OutBottom = -1;
			for ( int Y = 0; Y < Height; ++Y )
			{
				for ( int X = 0; X < Width; ++X )
				{
					int R = 0, G = 0, B = 0;
					At( X, Y, R, G, B );
					if ( R + G + B <= 6 ) continue;
					OutLeft = std::min( OutLeft, X );
					OutTop = std::min( OutTop, Y );
					OutRight = std::max( OutRight, X );
					OutBottom = std::max( OutBottom, Y );
				}
			}
			return OutRight >= 0;
		}
		FString BoundsText() const
		{
			int L, T, R, B;
			return LitBounds( L, T, R, B ) ? FString::Printf( "x %d..%d, y %d..%d", L, R, T, B ) : FString( "(nothing)" );
		}
		void End()
		{
			if ( Soft >= 0 ) DeleteSoftImage( Soft );
			if ( Screen >= 0 ) DeleteGraph( Screen );
			Soft = -1;
			Screen = -1;
		}
		~FCapture() { End(); }
	};

	void ExecuteBuffer( IDrawExecutor& Executor, const FDrawBuffer& Buffer )
	{
		Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
	}

	// 4 x 4 の画像。4 つの象限が、左上が赤・右上が緑・左下が青・右下が黄色（黒を使わない: 光っている範囲を測れるように）
	std::vector<uint8> MakeQuadrantBmp()
	{
		// 三項演算子を重ねる書き方だと、MSVC 14.51 のリンク時コード生成が内部エラー（C1001）になるため、表から引く
		const uint32 Colors[4] = { Rgb( 255, 0, 0 ), Rgb( 0, 255, 0 ), Rgb( 0, 0, 255 ), Rgb( 255, 255, 0 ) };   // 左上・右上・左下・右下
		std::vector<uint32> Pixels( 16 );
		for ( int32 Y = 0; Y < 4; ++Y )
		{
			for ( int32 X = 0; X < 4; ++X ) Pixels[static_cast<size_t>( Y * 4 + X )] = Colors[( Y >= 2 ? 2 : 0 ) + ( X >= 2 ? 1 : 0 )];
		}
		return MakeBmp( 4, 4, Pixels );
	}
}

//=============================================================================
// 状態
//=============================================================================
struct CWorldSelfTestScene::FState
{
	FChecker Checker;
	bool bExitWhenDone = false;
	// 0: シーンでの確認（偽のバックエンド）/ 1: 物理の確認が終わった。消す / 5: エフェクトを設置 / 6: エフェクトの描画の確認 / 7: エフェクトの片付けの確認 / 2: 後片付け / 3: 本物の DxLib / 4: 終わり
	int32 Phase = 0;
	int32 EffectFrame = 0;   // エフェクトを置いたフレーム
	int32 Frame = 0;
	FString TempRoot;
	FString Dir;

	FFakeAssetBackend Fake;
	FRecordingAudioBackend Rec;
	FCommandLog Log;
	FWorldSpawnerRegistry Registry = FWorldSpawnerRegistry::CreateWithBuiltins();

	TSharedPtr<CWorldAsset> Level;
	FWorldInstance Stage;
	FWorldInstance PhysicsLevel;
	TSharedPtr<CGameObject> Listener;
	TSharedPtr<CTexture> Texture;
	bool bChecked = false;
	bool bPhysicsChecked = false;
	int32 DespawnFrame = 0;
	int32 NumStageBodies = 0;
	int32 NumPhysicsBodies = 0;

	// 物理の確認の球（床の上 / 規則で床の付かないところ / 指定で付くところ / 指定で付かないところ）
	FRigidBody* Balls[4] = { nullptr, nullptr, nullptr, nullptr };

	// メッシュの当たり判定・トリガーの確認
	enum EMeshBall { MeshFloorBall, ScaledHitBall, ScaledMissBall, MeshOnlyBall, ZoneBall, MeshZoneBall, KeepBall, ReversedBall, EditFloorBall, DumbbellBall, LateBall, NumMeshBalls };
	FWorldInstance MeshLevel;
	TSharedPtr<CModel> MeshModels[2];            // 三角形つきの偽のモデルを、読み込んだまま持つ（設置が、同じものを使う）
	FRigidBody* MeshBalls[NumMeshBalls] = {};
	int32 NumMeshBodiesBefore = 0;
	int32 ZoneEnter = 0, ZoneExit = 0, MeshZoneEnter = 0, MeshZoneExit = 0, KeepEnter = 0, KeepExit = 0, DumbbellEnter = 0, DumbbellExit = 0, LateEnter = 0, LateExit = 0;
	float32 DumbbellExitY = 999.0f;              // 出たと知らされたときの、2 つの球をつないだ物の中心の高さ
	const FRigidBody* ZoneOther = nullptr;       // 最後に Zone に入った剛体
	bool bMeshChecked = false;
	bool bMeshDone = false;
	int32 GhostFrame = 0;                        // 中にいた球を消したフレーム（そのあとの片付けを確かめる）
};

bool CWorldSelfTestScene::IsRequestedOnCommandLine()
{
	const char* CommandLine = GetCommandLineA();
	return CommandLine != nullptr && std::strstr( CommandLine, "--selftest=world" ) != nullptr;
}

CWorldSelfTestScene::CWorldSelfTestScene() = default;
CWorldSelfTestScene::~CWorldSelfTestScene() = default;

//=============================================================================
// 設置（偽のバックエンド）
//=============================================================================
void CWorldSelfTestScene::OnEnter()
{
	State = MakeUnique<FState>();
	State->bExitWhenDone = IsRequestedOnCommandLine();
	FState& S = *State;
	FChecker& C = S.Checker;
	S.TempRoot = MakeTempDirectory();

	NWorldTest::RunLogicTests( C );
	NWorldTest::RunWorldModelMaterialDefinitionTests( C );
	NWorldTest::RunEditorMathTests( C );
	NWorldTest::RunEditorSessionTests( C );
	NWorldTest::RunEditorInputTests( C );
	NWorldTest::RunMeshDataTests( C );
	NWorldTest::RunMeshOpsTests( C );
	NWorldTest::RunMeshMaterialEffectDefinitionTests( C );
	NWorldTest::RunMeshEditorTests( C );
	NWorldTest::RunPrefabLogicTests( C );
	NWorldTest::RunEffectDataTests( C );
	NWorldTest::RunEffectSimTests( C );
	NWorldTest::RunEffectEditingTests( C );
	NWorldTest::RunEffectPathTests( C );
	NWorldTest::RunEffectMaterialTests( C );
	NWorldTest::RunEffectUVDistortionTests( C );
	NWorldTest::RunEffectRibbonUVDistortionCommandTests( C );
	NWorldTest::RunEffectMeshCommandTests( C );
	NWorldTest::RunEffectAppearanceTests( C );
	NWorldTest::RunEffectFlipbookTests( C );
	NWorldTest::RunEffectTreeLightningTests( C );
	NWorldTest::RunEffectEditorInputTests( C );
	NWorldTest::RunEffectForceFieldEditorInputTests( C );
	NWorldTest::RunEffectForceFieldEditorGeometryTests( C );
	NWorldTest::RunEffectTargetWorldTests( C );
	NWorldTest::RunEffectMaterialInputTests( C );
	NWorldTest::RunEffectUVDistortionInputTests( C );
	NWorldTest::RunEffectAppearanceInputTests( C );
	NWorldTest::RunEffectFlipbookInputTests( C );
	NWorldTest::RunEffectTreeLightningInputTests( C );
	LOG( LogWorldSelfTest, Log, "Logic checks done: %lld check(s), %lld failure(s)", static_cast<long long>( C.NumChecked ), static_cast<long long>( C.NumFailed ) );

	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
	CPhysicsSubsystem* Physics = GetSubsystem<CPhysicsSubsystem>();
	if ( Assets == nullptr || Audio == nullptr || Physics == nullptr )
	{
		C.Check( false, "the asset manager / audio system / physics subsystems do not exist" );
		S.Phase = 4;
		return;
	}

	// 偽のバックエンドを差し込む（このフェーズの間だけ）
	S.Dir = S.TempRoot / "world";
	CreateDirectoryA( *S.Dir, nullptr );
	Assets->SetBackendOverride( &S.Fake );
	Assets->SetRootDirectory( S.Dir );
	NWorldTest::RunEffectFlipbookResourceTests( C, *Assets, S.Fake, S.Dir );
	NWorldTest::RunEffectUVDistortionResourceTests( C, *Assets, S.Fake, S.Dir );
	NWorldTest::RunEffectRibbonUVDistortionResourceTests( C, *Assets, S.Fake, S.Dir );
	NWorldTest::RunEffectMeshSelfTests( C, *Assets, S.Dir );
	NWorldTest::RunEffectMeshResourceTests( C, *Assets, S.Fake, S.Dir );
	NWorldTest::RunMeshMaterialResourceTests( C, *Assets, S.Fake, S.Dir );
	Audio->SetBackendOverride( &S.Rec );
	SetDrawExecutor( &S.Log );
	for ( const char* Name : { "model.mv1", "a.png", "fire.wav", "only.mv1" } ) WriteFileText( S.Dir / Name, "x" );
	S.Fake.TextureWidth = 128;
	S.Fake.TextureHeight = 64;
	S.Fake.NextModelData = { { "Idle", "Run" }, { 60.0f, 30.0f } };
	NWorldTest::RunWorldModelMaterialSpawnFakeChecks( C, *Assets, S.Fake, S.Dir );
	NWorldTest::RunWorldModelPreviewFakeChecks( C, *Assets, S.Fake, S.Dir );
	NWorldTest::RunWorldModelInspectorFakeChecks( C, *Assets, S.Fake, S.Dir );

	// アニメーション集（.modelanim.json）: クリップ "Sprint" は、モデルの "Run"
	FModelAnimationDefinition AnimSet;
	AnimSet.ModelPath = "model.mv1";
	FModelClipDef Sprint;
	Sprint.Name = "Sprint";
	Sprint.AnimName = "Run";
	AnimSet.Clips.Add( Sprint );
	C.Check( Assets->SaveModelAnimationDefinition( "set.modelanim.json", AnimSet ), "the animation set used by a world is saved" );

	//---------------------------------------------------------------------
	// アセットとして: 保存・読み込み・共有・入れ替え
	//---------------------------------------------------------------------
	C.Group = "world-asset";
	const FWorldDefinition StageDefinition = BuildStageWorld();
	C.Check( Assets->SaveWorld( "stage.world", StageDefinition ) && NFileSystem::FileExists( S.Dir / "stage.world" ), "a world is saved (relative to the asset root)" );
	S.Level = Assets->LoadWorld( "stage.world" );
	C.Check( S.Level.IsValid() && S.Level->GetAssetType() == EAssetType::World && S.Level->GetDefinition().ToJson() == StageDefinition.ToJson() && S.Level->GetLoadWarnings().IsEmpty(), "the saved world loads back identical (%d load warnings)",
			 S.Level.IsValid() ? S.Level->GetLoadWarnings().Num() : -1 );
	C.Check( Assets->LoadWorld( "stage.world" ) == S.Level, "the same file is shared" );
	C.Check( S.Level.IsValid() && S.Level->Describe().Contains( "20 object(s)" ) && S.Level->Describe().Contains( "(19 enabled)" ) && S.Level->GetMemoryBytes() > 0 && S.Level->GetFileBytes() > 0, "describe, memory and file size (%s)",
			 S.Level.IsValid() ? *S.Level->Describe() : "?" );
	Assets->UpdateStats();
	C.Check( Assets->GetStats().Get( EAssetType::World ).Count == 1 && FString( ToString( EAssetType::World ) ) == "World" && NumAssetTypes == 8, "the statistics count worlds" );
	C.Check( Assets->LoadWorld( "nope.world" ) == nullptr, "a missing world returns null" );
	WriteFileText( S.Dir / "broken.world", "this is not json" );
	C.Check( Assets->LoadWorld( "broken.world" ) == nullptr, "a broken world returns null" );
	WriteFileText( S.Dir / "notobject.world", "[1, 2, 3]" );
	C.Check( Assets->LoadWorld( "notobject.world" ) == nullptr, "a JSON that is not an object returns null" );
	WriteFileText( S.Dir / "repair.world", "{\"Objects\":[{\"Id\":1},{\"Id\":1}]}" );
	const TSharedPtr<CWorldAsset> Repaired = Assets->LoadWorld( "repair.world" );
	C.Check( Repaired.IsValid() && Repaired->GetLoadWarnings().Num() == 1 && Repaired->GetDefinition().Objects[0].Id != Repaired->GetDefinition().Objects[1].Id, "load-time repairs are reported on the asset" );

	FWorldDefinition Changed = StageDefinition;
	Changed.Name = "Changed";
	C.Check( Assets->SaveWorld( "stage_copy.world", StageDefinition ), "a world is saved again under another name" );
	const TSharedPtr<CWorldAsset> OldCopy = Assets->LoadWorld( "stage_copy.world" );   // 持っている間は、キャッシュの項目が生きている
	C.Check( OldCopy.IsValid() && OldCopy->GetDefinition().Name == "Stage", "and loaded" );
	C.Check( Assets->SaveWorld( "stage_copy.world", Changed ), "saved over a path that is loaded" );
	const TSharedPtr<CWorldAsset> Copy = Assets->LoadWorld( "stage_copy.world" );
	C.Check( Copy.IsValid() && Copy != OldCopy && Copy->GetDefinition().Name == "Changed" && OldCopy->GetDefinition().Name == "Stage", "after SaveWorld, the next load reads the new content (the cache entry is dropped); the old asset keeps its own" );

	FAssetWorldQuery Query( *Assets );
	C.Check( Query.Exists( "model.mv1" ) && Query.Exists( "a.png" ) && !Query.Exists( "nope.png" ) && !Query.Exists( "" ), "the asset query finds files under the asset root" );
	C.Check( Query.Exists( "only.fbx" ) && !Query.Exists( "none.fbx" ), "a .fbx whose .mv1 exists counts as present (a shipped game has only the .mv1)" );
	const TArray<FWorldIssue> StageIssues = NWorld::ValidateWorld( StageDefinition, &Query );
	int32 NumStageErrors = 0;
	for ( const FWorldIssue& Issue : StageIssues )
	{
		if ( Issue.Severity == EWorldIssueSeverity::Error ) ++NumStageErrors;
	}
	// 無いファイル（"missing.mv1" "nope.wav"）、親の Id の欠け・親の循環が、エラーとして出る
	C.Check( NumStageErrors == 5, "validating the stage world against the real asset query: missing model, missing sound, missing parent, and the two members of a loop (%d errors)", NumStageErrors );

	//---------------------------------------------------------------------
	// 設置
	//---------------------------------------------------------------------
	C.Group = "world-spawn";
	C.Check( FWorldSpawnerRegistry::Global().Has( "Model" ) && FWorldSpawnerRegistry::Global().Has( "Billboard" ) && FWorldSpawnerRegistry::Global().Has( "Sound" ) && FWorldSpawnerRegistry::Global().Has( "Mesh" ) && FWorldSpawnerRegistry::Global().Has( "Collider" ),
			 "the global registry has the framework's components, and the application registered physics at start" );
	C.Check( S.Registry.Num() == 5 && S.Registry.Has( "Effect" ) && !S.Registry.Has( "Collider" ), "a fresh registry has the five built-ins (Model, Billboard, Mesh, Sound, Effect)" );
	NPhysicsWorld::RegisterSpawners( S.Registry );
	C.Check( S.Registry.Num() == 6 && S.Registry.Has( "Collider" ), "physics registers its Collider spawner" );

	S.Listener = SpawnObject<CTestHolder>();
	S.Listener->GetComponent<CTransformComponent>()->SetLocalPosition( FVector3( 103.0f, 1.0f, 3.0f ) );
	S.Listener->AddComponent<CAudioListenerComponent>();

	FWorldBindings Bindings;
	Bindings.Add<CTestEnemy>( "Enemy" );
	Bindings.Add( "Boss", []( const FWorldBindContext& Context )
	{
		TSharedPtr<CTestEnemy> Boss = Context.Scene.SpawnObject<CTestEnemy>();
		if ( const FWorldProp* Hp = Context.Definition.FindProp( "Hp" ) ) Boss->Hp = static_cast<int32>( Hp->IntValue );
		return TSharedPtr<CGameObject>( Boss );
	} );
	Bindings.Add( "Fails", []( const FWorldBindContext& ) { return TSharedPtr<CGameObject>(); } );
	Bindings.Add( "Plain", []( const FWorldBindContext& Context ) { return TSharedPtr<CGameObject>( Context.Scene.SpawnObject<CGameObject>() ); } );   // 位置の部品の無いオブジェクト
	C.Check( Bindings.Num() == 4 && Bindings.Has( "Enemy" ) && !Bindings.Has( "enemy" ), "bindings are registered by tag (case sensitive)" );
	Bindings.Add( "Fails", []( const FWorldBindContext& ) { return TSharedPtr<CGameObject>(); } );
	C.Check( Bindings.Num() == 4, "adding the same tag again replaces it" );

	FWorldSpawnOptions Options;
	Options.Registry = &S.Registry;
	Options.Root = FTransform( FVector3( 100.0f, 0.0f, 0.0f ) );
	const int32 ObjectsBefore = GetNumObjects();
	S.Stage = NWorld::Spawn( *this, S.Level->GetDefinitionShared(), Bindings, Options );
	const FWorldSpawnReport& Report = S.Stage.GetReport();
	C.Check( Report.NumSpawned == 16 && S.Stage.GetNumSpawned() == 16 && Report.NumDisabled == 2 && Report.NumSkipped == 2 && Report.NumBound == 4, "report: 16 placed, 2 disabled, 2 skipped (a loop), 4 bound (%d, %d, %d, %d)", Report.NumSpawned,
			 Report.NumDisabled, Report.NumSkipped, Report.NumBound );
	C.Check( GetNumObjects() == ObjectsBefore + 16 + 4, "the scene got 16 placed objects and 4 bound ones (%d)", GetNumObjects() - ObjectsBefore );
	auto HasWarning = [&]( const char* Text )
	{
		for ( const FString& Warning : Report.Warnings )
		{
			if ( Warning.Contains( Text ) ) return true;
		}
		return false;
	};
	C.Check( Report.Warnings.Num() == 7, "seven warnings (orphan, two loop members, unsupported component, failed binding, missing model, missing sound): %d", Report.Warnings.Num() );
	C.Check( HasWarning( "'Orphan'" ) && HasWarning( "parent #99" ) && HasWarning( "'LoopA'" ) && HasWarning( "'LoopB'" ) && HasWarning( "'Hologram'" ) && HasWarning( "'Fails'" ) && HasWarning( "missing.mv1" ) && HasWarning( "nope.wav" ), "and each says what and where" );
	C.Check( S.Stage.GetDefinition() == S.Level->GetDefinitionShared(), "the instance shares the asset's definition" );

	// 検索
	const TSharedPtr<CGameObject> StagePlaceholder = S.Stage.FindById( 1 );
	const TSharedPtr<CGameObject> Statue = S.Stage.FindById( 2 );
	const TSharedPtr<CGameObject> Torch = S.Stage.FindByName( "Torch" );
	C.Check( StagePlaceholder.IsValid() && Statue.IsValid() && Torch.IsValid() && Statue->GetName() == "Statue" && Torch->GetName() == "Torch" && Statue->GetParent() == StagePlaceholder.Get() && StagePlaceholder->GetParent() == nullptr,
			 "placed objects take their names, and their parents from the definition" );
	C.Check( S.Stage.FindById( 8 ) == nullptr && S.Stage.FindById( 9 ) == nullptr && S.Stage.FindById( 13 ) == nullptr && S.Stage.FindById( 99 ) == nullptr && S.Stage.FindByName( "Hidden" ) == nullptr, "disabled objects and loops are not placed; unknown ids are not found" );
	C.Check( S.Stage.FindById( 12 ).IsValid() && S.Stage.FindById( 12 )->GetParent() == nullptr, "an orphan is placed as a top-level object" );
	const TSharedPtr<CGameObject> Start = S.Stage.FindFirst( "PlayerStart" );
	C.Check( Start.IsValid() && Start->GetName() == "SpawnPoint" && S.Stage.FindFirst( "Marker" ) == Start && S.Stage.FindFirst( "playerstart" ) == nullptr && S.Stage.FindFirst( "Nope" ) == nullptr, "FindFirst by tag (case sensitive)" );
	const TArray<TSharedPtr<CGameObject>> Enemies = S.Stage.FindAll( "Enemy" );
	C.Check( Enemies.Num() == 2 && Enemies[0]->GetName() == "EnemyA" && Enemies[1]->GetName() == "EnemyB" && S.Stage.FindAll( "Nope" ).IsEmpty(), "FindAll by tag, in the definition's order" );
	const TSharedPtr<CGameObject> StatueObject = Statue;
	const FWorldObjectRef* Ref = StatueObject.IsValid() ? StatueObject->GetData<FWorldObjectRef>() : nullptr;
	C.Check( Ref != nullptr && Ref->ObjectId == 2 && Ref->Get() != nullptr && Ref->Get()->Name == "Statue" && Ref->World == S.Level->GetDefinitionShared(), "a placed object can find its definition (FWorldObjectRef)" );

	// タグへの結び付け
	const TArray<TSharedPtr<CTestEnemy>> BoundEnemies = S.Stage.FindAllBound<CTestEnemy>( "Enemy" );
	C.Check( BoundEnemies.Num() == 2 && BoundEnemies[0]->GetParent() == Enemies[0].Get() && BoundEnemies[1]->GetParent() == Enemies[1].Get(), "an 'Enemy' object is created under each enemy placeholder" );
	const TSharedPtr<CTestEnemy> Boss = S.Stage.FindFirstBound<CTestEnemy>( "Boss" );
	C.Check( Boss.IsValid() && Boss->Hp == 100 && Boss->GetParent() == Enemies[1].Get() && S.Stage.FindAllBound<CTestEnemy>( "Boss" ).Num() == 1, "the 'Boss' factory read the Hp property" );
	C.Check( Boss.IsValid() && Boss->GetComponent<CTransformComponent>() != nullptr && S.Stage.FindFirstBound<CTestEnemy>( "Marker" ) == nullptr && S.Stage.FindAllBound<CTestEnemy>( "Fails" ).IsEmpty(), "a tag without a binding (or whose binding failed) has nothing bound" );
	C.Check( S.Stage.FindFirstBound<CGameObject>( "Enemy" ).IsValid() && S.Stage.FindAllBound<CTestHolder>( "Enemy" ).IsEmpty(), "FindFirstBound casts to the type asked for" );

	// 部品
	C.Check( Statue->GetData<FModelAnimator>() != nullptr && Statue->GetData<FModelAnimator>()->Instance.IsValid() && Statue->GetData<FModelAnimator>()->Speed == 2.0f && Statue->GetData<FModelAnimator>()->GetCurrentClip() == "Idle"
				 && Statue->GetData<FModelAnimator>()->HasClip( "Run" ),
			 "a model component: an animator with the model's clips, speed and the first clip playing" );
	const TSharedPtr<CGameObject> AnimStatue = S.Stage.FindById( 19 );
	C.Check( AnimStatue.IsValid() && AnimStatue->GetData<FModelAnimator>() != nullptr && AnimStatue->GetData<FModelAnimator>()->HasClip( "Sprint" ) && AnimStatue->GetData<FModelAnimator>()->GetCurrentClip() == "Sprint",
			 "a model with an animation set uses the set's clips" );
	C.Check( S.Stage.FindById( 15 ).IsValid() && S.Stage.FindById( 15 )->GetData<FModelAnimator>() == nullptr && S.Stage.FindById( 16 ).IsValid() && S.Stage.FindById( 16 )->GetComponent<CAudioSourceComponent>() == nullptr,
			 "a component whose file cannot be loaded is skipped (the object is still placed)" );
	const FBillboard* TorchBoard = Torch->GetData<FBillboard>();
	C.Check( TorchBoard != nullptr && TorchBoard->Texture.IsValid() && TorchBoard->Size.x == 2.0f && TorchBoard->Pivot.y == 1.0f && TorchBoard->Orient == EDrawQuadOrient::CameraY && TorchBoard->Tint.g == 128 && TorchBoard->Tint.a == 200 && TorchBoard->bFlipX && !TorchBoard->bFlipY,
			 "a billboard component carries its fields" );
	C.Check( TorchBoard != nullptr && std::fabs( TorchBoard->U0 - 0.125f ) < 1.0e-5f && std::fabs( TorchBoard->V0 - 0.125f ) < 1.0e-5f && std::fabs( TorchBoard->U1 - 0.375f ) < 1.0e-5f && std::fabs( TorchBoard->V1 - 0.375f ) < 1.0e-5f,
			 "the pixel rectangle (16, 8, 32, 16) of the 128 x 64 image is UV (0.125, 0.125)-(0.375, 0.375)" );
	C.Check( Torch->GetComponent<CAudioSourceComponent>() != nullptr, "a sound component becomes an audio source" );

	// 物理の部品（規則に従って付く）
	C.Check( Physics->GetBodyCount() == 2, "colliders: the statue and the trigger get bodies; the torch (tag rule: Decoration is off) does not (%d bodies)", Physics->GetBodyCount() );
	const CWorldColliderComponent* StatueCollider = Statue->GetComponent<CWorldColliderComponent>();
	C.Check( StatueCollider != nullptr && StatueCollider->GetBody() != nullptr && StatueCollider->GetBody()->GetBodyType() == EBodyType::Static && !StatueCollider->IsTrigger() && Torch->GetComponent<CWorldColliderComponent>() == nullptr,
			 "the statue has a static body; the torch has none" );
	if ( StatueCollider != nullptr && StatueCollider->GetBody() != nullptr )
	{
		FRigidBody* Body = StatueCollider->GetBody();
		// 位置: 全体の置き場所 (100,0,0) + (1,0,2)。形の中心のずれ (0,1,0) は、拡大 2 で (0,2,0)
		C.Check( Near3( Body->GetPosition(), FVector3( 101.0f, 2.0f, 2.0f ) ), "the body is at the world position plus the scaled offset (%s)", *Body->GetPosition().ToString() );
		C.Check( Near3( Body->GetRotation().Rotate( FVector3::Forward ), FVector3( 1.0f, 0.0f, 0.0f ) ), "and has the object's rotation (yaw 90 turns forward to +X)" );
		const JPH::Vec3 Extent = Physics->GetBodyInterface().GetShape( Body->GetBodyID() )->GetLocalBounds().GetExtent();
		C.Check( std::fabs( Extent.GetX() - 1.0f ) < 0.01f && std::fabs( Extent.GetY() - 2.0f ) < 0.01f && std::fabs( Extent.GetZ() - 1.0f ) < 0.01f, "its box is the collider size times the object's scale: half extents (1, 2, 1) (%.3f, %.3f, %.3f)", Extent.GetX(),
				 Extent.GetY(), Extent.GetZ() );
		C.Check( Body->GetUserDataAs<CGameObject>() == Statue.Get(), "the body's user data is the placed object" );
	}
	const TSharedPtr<CGameObject> Zone = S.Stage.FindFirst( "Zone" );
	const CWorldColliderComponent* ZoneCollider = Zone.IsValid() ? Zone->GetComponent<CWorldColliderComponent>() : nullptr;
	bool bSensor = false;
	if ( ZoneCollider != nullptr && ZoneCollider->GetBody() != nullptr )
	{
		JPH::BodyLockRead Lock( Physics->GetJoltSystem().GetBodyLockInterface(), ZoneCollider->GetBody()->GetBodyID() );
		bSensor = Lock.Succeeded() && Lock.GetBody().IsSensor();
	}
	C.Check( ZoneCollider != nullptr && ZoneCollider->IsTrigger() && bSensor, "a trigger collider is a sensor body (no collision response)" );
	S.NumStageBodies = Physics->GetBodyCount();

	// 物理の確認のワールド（別の場所に置く）: 球を落として、規則どおりに付く / すり抜けることを確かめる
	FWorldSpawnOptions PhysicsOptions;
	PhysicsOptions.Registry = &S.Registry;
	PhysicsOptions.Root = FTransform( FVector3( 1000.0f, 0.0f, 0.0f ) );
	S.PhysicsLevel = NWorld::Spawn( *this, BuildPhysicsWorld(), FWorldBindings(), PhysicsOptions );
	C.Check( S.PhysicsLevel.GetNumSpawned() == 6 && S.PhysicsLevel.GetReport().Warnings.IsEmpty() && Physics->GetBodyCount() == S.NumStageBodies + 4, "the physics world places 6 objects with 4 bodies: the floor, the one forced on, the capsule and the sphere (%d bodies)", Physics->GetBodyCount() );
	{
		// 形の大きさ（局所の外接の半分の大きさ）: カプセル (0.5, 1, 0.5)、球は半径 0.75 に拡大 2 で 1.5
		const auto HalfExtents = [&]( const char* Name, JPH::Vec3& Out )
		{
			const TSharedPtr<CGameObject> Object = S.PhysicsLevel.FindByName( Name );
			const CWorldColliderComponent* Collider = Object.IsValid() ? Object->GetComponent<CWorldColliderComponent>() : nullptr;
			if ( Collider == nullptr || Collider->GetBody() == nullptr ) return false;
			Out = Physics->GetBodyInterface().GetShape( Collider->GetBody()->GetBodyID() )->GetLocalBounds().GetExtent();
			return true;
		};
		JPH::Vec3 CapsuleExtent, SphereExtent;
		const bool bCapsule = HalfExtents( "Capsule", CapsuleExtent );
		const bool bSphere = HalfExtents( "Sphere", SphereExtent );
		C.Check( bCapsule && std::fabs( CapsuleExtent.GetX() - 0.5f ) < 0.01f && std::fabs( CapsuleExtent.GetY() - 1.0f ) < 0.01f && std::fabs( CapsuleExtent.GetZ() - 0.5f ) < 0.01f, "a capsule collider (radius 0.5, total height 2) is 1 unit tall from its centre (%.3f, %.3f, %.3f)",
				 CapsuleExtent.GetX(), CapsuleExtent.GetY(), CapsuleExtent.GetZ() );
		C.Check( bSphere && std::fabs( SphereExtent.GetX() - 1.5f ) < 0.01f && std::fabs( SphereExtent.GetY() - 1.5f ) < 0.01f, "a sphere collider takes the object's scale: radius 0.75 x 2 = 1.5 (%.3f)", SphereExtent.GetX() );
	}
	for ( int32 i = 0; i < 4; ++i )
	{
		S.Balls[i] = Physics->CreateBody( FCollisionShape::Sphere( 0.5f ), EBodyType::Dynamic, FTransform( FVector3( 1000.0f + 50.0f * static_cast<float32>( i ), 3.0f, 0.0f ) ) );
	}
	S.NumPhysicsBodies = Physics->GetBodyCount();
	C.Check( S.NumPhysicsBodies == S.NumStageBodies + 4 + 4 && S.Balls[0] != nullptr && S.Balls[3] != nullptr, "four test balls are created" );

	// メッシュの当たり判定とトリガー（三角形つきの偽のモデル。向きが正しい床と、逆の床）
	{
		C.Group = "world-mesh-collider";
		const FFakeAssetBackend::FModelData Original = S.Fake.NextModelData;
		for ( const char* Name : { "floor.mv1", "floorrev.mv1" } ) WriteFileText( S.Dir / Name, "x" );
		// 表から見て時計回り（DxLib の表）に、上向きの床: A(-10,0,-10) B(-10,0,10) C(10,0,10) D(10,0,-10)
		FFakeAssetBackend::FModelData Floor = Original;
		Floor.MeshPositions = { -10.0f, 0.0f, -10.0f, -10.0f, 0.0f, 10.0f, 10.0f, 0.0f, 10.0f, 10.0f, 0.0f, -10.0f };
		Floor.MeshIndices = { 0, 1, 2, 0, 2, 3 };
		FFakeAssetBackend::FModelData Reversed = Floor;
		Reversed.MeshIndices = { 0, 2, 1, 0, 3, 2 };
		S.Fake.NextModelData = Floor;
		S.MeshModels[0] = Assets->LoadModel( "floor.mv1" );
		S.Fake.NextModelData = Reversed;
		S.MeshModels[1] = Assets->LoadModel( "floorrev.mv1" );
		S.Fake.NextModelData = Original;
		TArray<FVector3> Vertices;
		TArray<uint32> Indices;
		C.Check( S.MeshModels[0].IsValid() && S.MeshModels[0]->GetCollisionMesh( Vertices, Indices ) && Vertices.Num() == 4 && Indices.Num() == 6 && Near3( Vertices[2], FVector3( 10.0f, 0.0f, 10.0f ), 1.0e-5f ) && Indices[5] == 3,
				 "a model gives its triangles (4 vertices, 6 indices) through the backend" );
		C.Check( S.MeshModels[1].IsValid() && S.MeshModels[1]->GetCollisionMesh( Vertices, Indices ) && Indices[1] == 2, "and the reversed one gives its own" );
		const TSharedPtr<CModel> Plain = Assets->LoadModel( "only.mv1" );
		C.Check( Plain.IsValid() && !Plain->GetCollisionMesh( Vertices, Indices ), "a model the backend has no triangles for says so" );

		// 球は、LateZone より先に作る（LateZone のトリガーは、球より後の番号になる）
		S.MeshBalls[FState::LateBall] = Physics->CreateBody( FCollisionShape::Sphere( 0.5f ), EBodyType::Dynamic, FTransform( FVector3( 4000.0f, 8.0f, 0.0f ) ) );
		S.NumMeshBodiesBefore = Physics->GetBodyCount();
		FWorldSpawnOptions MeshOptions;
		MeshOptions.Registry = &S.Registry;
		MeshOptions.Root = FTransform( FVector3( 3000.0f, 0.0f, 0.0f ) );
		// この World Spawn が作った分だけを数えるための累積値
		const int32 NumMeshBuffersMadeBeforeWorld = S.Fake.NumMeshBuffersMade;
		S.MeshLevel = NWorld::Spawn( *this, BuildMeshWorld(), FWorldBindings(), MeshOptions );
		const FWorldSpawnReport& MeshReport = S.MeshLevel.GetReport();
		bool bNoModelWarning = false, bNoTrianglesWarning = false;
		for ( const FString& Warning : MeshReport.Warnings )
		{
			bNoModelWarning = bNoModelWarning || ( Warning.Contains( "'NoModel'" ) && Warning.Contains( "no triangles to use" ) );
			bNoTrianglesWarning = bNoTrianglesWarning || ( Warning.Contains( "'NoTriangles'" ) && Warning.Contains( "no triangles" ) );
		}
		bool bEmptyMeshWarning = false, bMissingImageWarning = false;
		for ( const FString& Warning : MeshReport.Warnings )
		{
			bEmptyMeshWarning = bEmptyMeshWarning || ( Warning.Contains( "'EmptyMesh'" ) && Warning.Contains( "no usable faces" ) );
			bMissingImageWarning = bMissingImageWarning || ( Warning.Contains( "'Painted'" ) && Warning.Contains( "nope.png" ) );
		}
		C.Check( bEmptyMeshWarning && bMissingImageWarning && MeshReport.Warnings.Num() == 4, "a Mesh component with no usable faces, and a material whose image is not there, each warn too (%d warnings)", MeshReport.Warnings.Num() );
		C.Check( S.MeshLevel.GetNumSpawned() == 19 && bNoModelWarning && bNoTrianglesWarning, "the mesh world places 19 objects; the mesh collider without a model and the one without triangles each warn (%d placed, %d warnings)", S.MeshLevel.GetNumSpawned(),
				 MeshReport.Warnings.Num() );
		C.Check( Physics->GetBodyCount() == S.NumMeshBodiesBefore + 15, "and 15 bodies (the two warned ones have none: %d)", Physics->GetBodyCount() - S.NumMeshBodiesBefore );

		const auto Extent = [&]( const char* Name, JPH::Vec3& Out )
		{
			const TSharedPtr<CGameObject> Object = S.MeshLevel.FindByName( Name );
			const CWorldColliderComponent* Collider = Object.IsValid() ? Object->GetComponent<CWorldColliderComponent>() : nullptr;
			if ( Collider == nullptr || Collider->GetBody() == nullptr ) return false;
			Out = Physics->GetBodyInterface().GetShape( Collider->GetBody()->GetBodyID() )->GetLocalBounds().GetExtent();
			return true;
		};
		JPH::Vec3 FloorExtent, ScaledExtent;
		const bool bFloor = Extent( "MeshFloor", FloorExtent );
		const bool bScaled = Extent( "ScaledFloor", ScaledExtent );
		C.Check( bFloor && std::fabs( FloorExtent.GetX() - 10.0f ) < 0.05f && std::fabs( FloorExtent.GetZ() - 10.0f ) < 0.05f, "a mesh collider has the model's size (a 20 x 20 floor: half extents 10, 10)" );
		C.Check( bScaled && std::fabs( ScaledExtent.GetX() - 20.0f ) < 0.05f && std::fabs( ScaledExtent.GetZ() - 20.0f ) < 0.05f, "and the object's scale is baked in (scale 2: 20, 20)" );
		const TSharedPtr<CGameObject> MeshOnly = S.MeshLevel.FindByName( "MeshOnly" );
		C.Check( MeshOnly.IsValid() && MeshOnly->GetComponent<CWorldColliderComponent>() != nullptr && MeshOnly->GetData<FModelAnimator>() == nullptr, "an object with only a mesh collider has the body and no model" );

		// 編集メッシュの描画データ（材質ごとの範囲・色・画像・両面）と、渡したバッファ
		{
			C.Group = "world-mesh-component";
			const TSharedPtr<CGameObject> PaintedObject = S.MeshLevel.FindByName( "Painted" );
			const FMeshRenderer* Renderer = PaintedObject.IsValid() ? PaintedObject->GetData<FMeshRenderer>() : nullptr;
			C.Check( Renderer != nullptr && Renderer->IsValid() && Renderer->bVisible && Renderer->bAutoDraw && Renderer->Batches.Num() == 3, "a Mesh component becomes a FMeshRenderer with a batch per material (3)" );
			if ( Renderer != nullptr && Renderer->Batches.Num() == 3 )
			{
				const TArray<FMeshDrawBatch>& B = Renderer->Batches;
				C.Check( B[0].StartIndex == 0 && B[0].NumIndices == 24 && B[1].StartIndex == 24 && B[1].NumIndices == 6 && B[2].StartIndex == 30 && B[2].NumIndices == 6, "the batches follow one another: 4 faces of material 0 (24 indices), then 1 face each (6, 6)" );
				C.Check( !B[0].Texture.IsValid() && !B[0].bTwoSided, "material 0: no image, one-sided" );
				C.Check( B[1].Texture.IsValid() && B[1].Texture->GetHandle() >= 0 && B[1].bTwoSided, "material 1: the image loaded through the asset manager, two-sided" );
				C.Check( !B[2].Texture.IsValid(), "material 2: an image that is not there is left out" );
				const auto Record = S.Fake.MeshRecords.find( Renderer->Buffer->GetVertexBuffer() );
				bool bUnitNormals = Record != S.Fake.MeshRecords.end();
				if ( bUnitNormals )
				{
					for ( const FMeshVertex& V : Record->second.Vertices ) bUnitNormals = bUnitNormals && std::fabs( V.Normal[0] * V.Normal[0] + V.Normal[1] * V.Normal[1] + V.Normal[2] * V.Normal[2] - 1.0f ) < 1.0e-4f;
				}
				C.Check( Record != S.Fake.MeshRecords.end() && Record->second.NumVertices == 24 && Record->second.NumIndices == 36 && bUnitNormals && Renderer->Buffer->GetNumVertices() == 24 && Renderer->Buffer->GetNumIndices() == 36 && S.Fake.IndexBuffers.count( Renderer->Buffer->GetIndexBuffer() ) == 1,
						 "the backend got the box's 24 corner vertices and 36 indices (unit normals), in the buffers the component holds" );
				// 材質の色は、頂点の色になる: 先頭の範囲（材質 0 = 赤）の三角形の頂点は赤、2 つめ（材質 1）は既定の灰色
				int32 NumRedCorners = 0, NumGreyCorners = 0;
				if ( Record != S.Fake.MeshRecords.end() )
				{
					const auto ColorOf = [&]( int32 Batch, int32 Triangle ) { return Record->second.Vertices[Record->second.Indices[static_cast<size_t>( B[Batch].StartIndex + Triangle * 3 )]]; };
					for ( int32 Triangle = 0; Triangle < B[0].NumIndices / 3; ++Triangle ) NumRedCorners += ColorOf( 0, Triangle ).Color[0] == 255 && ColorOf( 0, Triangle ).Color[1] == 0 ? 1 : 0;
					for ( int32 Triangle = 0; Triangle < B[1].NumIndices / 3; ++Triangle ) NumGreyCorners += ColorOf( 1, Triangle ).Color[0] == 200 && ColorOf( 1, Triangle ).Color[1] == 200 ? 1 : 0;
				}
				C.Check( NumRedCorners == B[0].NumIndices / 3 && NumGreyCorners == B[1].NumIndices / 3 && NumRedCorners == 8, "the material's colour is stored in the vertices of its batch (8 red triangles, 2 grey)" );
			}
			C.Check( S.Fake.NumMeshBuffersMade - NumMeshBuffersMadeBeforeWorld == 2 && S.Fake.VertexBuffers.size() == 2 && S.Fake.IndexBuffers.size() == 2, "two meshes made buffers (the empty one made none): %d made, %d live", S.Fake.NumMeshBuffersMade - NumMeshBuffersMadeBeforeWorld, static_cast<int32>( S.Fake.VertexBuffers.size() ) );
			C.Group = "world-mesh-collider";
		}

		// 球（半径 0.5）
		const FVector3 Starts[] = {
			FVector3( 3003.0f, 4.0f, -2.0f ),   // 床の上（床は y = 1）。2 つの三角形の継ぎ目（対角線）から離す
			FVector3( 3118.0f, 3.0f, 0.0f ),    // 拡大 2・向き 45 度の床の上（元の大きさの床なら、はずれる）
			FVector3( 3118.0f, 3.0f, 18.0f ),   // 回した床の外（回していなければ、床の上）
			FVector3( 3200.0f, 3.0f, 0.0f ),    // Model 部品なしの床の上
			FVector3( 3400.0f, 8.0f, 0.0f ),    // Zone を通り抜けて、下の床へ
			FVector3( 3500.0f, 3.0f, 0.0f ),    // 面のトリガーを通り抜けて、下の床へ
			FVector3( 3600.0f, 4.0f, 0.0f ),    // KeepZone の中で止まる
			FVector3( 3700.0f, 3.0f, 0.0f ),    // 三角形の向きが逆の床
			FVector3( 4100.0f, 5.0f, 0.0f ),    // 編集メッシュの床（上面は y = 1）
		};
		for ( int32 i = 0; i <= FState::EditFloorBall; ++i ) S.MeshBalls[i] = Physics->CreateBody( FCollisionShape::Sphere( 0.5f ), EBodyType::Dynamic, FTransform( Starts[i] ) );
		// 2 つの球をつないだ物（縦に並ぶ）: 1 組の剛体に、2 つの形の接触が起きる
		{
			JPH::StaticCompoundShapeSettings DumbbellSettings;
			DumbbellSettings.AddShape( JPH::Vec3( 0.0f, 0.6f, 0.0f ), JPH::Quat::sIdentity(), new JPH::SphereShape( 0.5f ) );
			DumbbellSettings.AddShape( JPH::Vec3( 0.0f, -0.6f, 0.0f ), JPH::Quat::sIdentity(), new JPH::SphereShape( 0.5f ) );
			const JPH::ShapeSettings::ShapeResult Dumbbell = DumbbellSettings.Create();
			if ( Dumbbell.IsValid() ) S.MeshBalls[FState::DumbbellBall] = Physics->CreateBody( FCollisionShape( Dumbbell.Get() ), EBodyType::Dynamic, FTransform( FVector3( 3900.0f, 8.0f, 0.0f ) ) );
		}

		// トリガーの出入り
		const TSharedPtr<CGameObject> Zone = S.MeshLevel.FindByName( "Zone" );
		const TSharedPtr<CGameObject> MeshZone = S.MeshLevel.FindByName( "MeshZone" );
		const TSharedPtr<CGameObject> KeepZone = S.MeshLevel.FindByName( "KeepZone" );
		FState* Context = &S;
		C.Check( Zone.IsValid() && NPhysicsWorld::SetTriggerCallbacks( *Zone, [Context]( CGameObject&, FRigidBody& Other ) { ++Context->ZoneEnter; Context->ZoneOther = &Other; }, [Context]( CGameObject&, FRigidBody& ) { ++Context->ZoneExit; } ), "callbacks are set on a trigger" );
		C.Check( MeshZone.IsValid() && NPhysicsWorld::SetTriggerCallbacks( *MeshZone, [Context]( CGameObject&, FRigidBody& ) { ++Context->MeshZoneEnter; }, [Context]( CGameObject&, FRigidBody& ) { ++Context->MeshZoneExit; } ), "also on a mesh trigger" );
		C.Check( KeepZone.IsValid() && NPhysicsWorld::SetTriggerCallbacks( *KeepZone, [Context]( CGameObject&, FRigidBody& ) { ++Context->KeepEnter; }, [Context]( CGameObject&, FRigidBody& ) { ++Context->KeepExit; } ), "and on a trigger a ball stops in" );
		const TSharedPtr<CGameObject> CompoundZone = S.MeshLevel.FindByName( "CompoundZone" );
		const TSharedPtr<CGameObject> LateZone = S.MeshLevel.FindByName( "LateZone" );
		C.Check( CompoundZone.IsValid() && NPhysicsWorld::SetTriggerCallbacks( *CompoundZone, [Context]( CGameObject&, FRigidBody& ) { ++Context->DumbbellEnter; }, [Context]( CGameObject&, FRigidBody& Other ) { ++Context->DumbbellExit; Context->DumbbellExitY = Other.GetPosition().y; } )
					 && LateZone.IsValid() && NPhysicsWorld::SetTriggerCallbacks( *LateZone, [Context]( CGameObject&, FRigidBody& ) { ++Context->LateEnter; }, [Context]( CGameObject&, FRigidBody& ) { ++Context->LateExit; } ),
				 "and on the other triggers" );
		const TSharedPtr<CGameObject> NotTrigger = S.MeshLevel.FindByName( "MeshFloor" );
		C.Check( NotTrigger.IsValid() && !NPhysicsWorld::SetTriggerCallbacks( *NotTrigger, nullptr ), "callbacks cannot be set on an object that is not a trigger" );
	}

	// 画像の矩形（偽の画像: 128 x 64）
	S.Texture = Assets->LoadTexture( "a.png" );
	if ( S.Texture.IsValid() )
	{
		FBillboard Plain;
		Plain.Texture = S.Texture;
		C.Check( Plain.SetRect( 64, 32, 64, 32 ) && Plain.U0 == 0.5f && Plain.V0 == 0.5f && Plain.U1 == 1.0f && Plain.V1 == 1.0f, "FBillboard::SetRect converts pixels to UV" );
		C.Check( !Plain.SetRect( 0, 0, 0, 10 ) && !Plain.SetRect( 0, 0, 10, 0 ) && Plain.U0 == 0.5f, "an empty rectangle is refused and changes nothing" );
		Plain.ResetRect();
		C.Check( Plain.U0 == 0.0f && Plain.V0 == 0.0f && Plain.U1 == 1.0f && Plain.V1 == 1.0f, "ResetRect" );
		FBillboard NoTexture;
		C.Check( !NoTexture.SetRect( 0, 0, 4, 4 ), "SetRect needs a texture" );
		FBillboard Hidden;
		Hidden.Texture = S.Texture;
		Hidden.bVisible = false;
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Hidden.Record( Draw, FTransform::Identity );
		NoTexture.Record( Draw, FTransform::Identity );
		C.Check( Buffer.Words.empty(), "a hidden billboard, or one without an image, records nothing" );
	}

	LOG( LogWorldSelfTest, Log, "Scene checks started (%d object(s))", GetNumObjects() );
}

void CWorldSelfTestScene::OnTick( float32 DeltaTime )
{
	if ( !State ) return;
	FState& S = *State;
	++S.Frame;
	if ( S.Phase == 0 || S.Phase == 6 ) S.Log.Clear();   // このフレームの描画コマンドだけを見る
	// 画面スリープ等で垂直同期が効かず DeltaTime が極小でも、物理の落下確認には 1/60 秒ずつ進める
	if ( S.Phase == 0 && S.Frame >= 4 && ( !S.bMeshChecked || !S.bPhysicsChecked ) && DeltaTime < 1.0f / 60.0f )
	{
		if ( CPhysicsSubsystem* Physics = GetSubsystem<CPhysicsSubsystem>() ) Physics->Update( 1.0f / 60.0f - DeltaTime );
	}
}

void CWorldSelfTestScene::OnDraw()
{
	if ( !State ) return;
	FState& S = *State;
	FChecker& C = S.Checker;
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
	CPhysicsSubsystem* Physics = GetSubsystem<CPhysicsSubsystem>();

	//---------------------------------------------------------------------
	// フェーズ 0: 描画コマンド・位置・音（3 フレーム目）
	//---------------------------------------------------------------------
	if ( S.Phase == 0 && S.Frame == 3 && !S.bChecked )
	{
		S.bChecked = true;
		C.Group = "world-draw";
		const TSharedPtr<CGameObject> Torch = S.Stage.FindByName( "Torch" );
		const TSharedPtr<CGameObject> Statue = S.Stage.FindById( 2 );

		// ワールドでの位置
		const CTransformComponent* StartTransform = S.Stage.FindFirst( "PlayerStart" )->GetComponent<CTransformComponent>();
		C.Check( Near3( StartTransform->GetWorldPosition(), FVector3( 100.0f, 5.0f, 0.0f ) ), "the world's root placement is applied to top-level objects (%s)", *StartTransform->GetWorldPosition().ToString() );
		C.Check( Near3( Statue->GetComponent<CTransformComponent>()->GetWorldPosition(), FVector3( 101.0f, 0.0f, 2.0f ) ) && Near3( Torch->GetComponent<CTransformComponent>()->GetWorldPosition(), FVector3( 103.0f, 1.0f, 0.0f ) ),
				 "children follow their parents: statue (101,0,2), torch (103,1,0)" );
		const TSharedPtr<CTestEnemy> Boss = S.Stage.FindFirstBound<CTestEnemy>( "Boss" );
		const TSharedPtr<CGameObject> BossPlaceholder = S.Stage.FindByName( "EnemyB" );
		C.Check( Boss.IsValid() && Near3( Boss->GetComponent<CTransformComponent>()->GetWorldPosition(), BossPlaceholder->GetComponent<CTransformComponent>()->GetWorldPosition() ) && Near3( Boss->GetComponent<CTransformComponent>()->GetWorldPosition(), FVector3( 106.0f, 0.0f, 0.0f ) ),
				 "a bound object is at its placeholder's position" );
		const TSharedPtr<CGameObject> PlainBound = S.Stage.FindFirstBound<CGameObject>( "Plain" );
		C.Check( PlainBound.IsValid() && PlainBound->GetComponent<CTransformComponent>() != nullptr && Near3( PlainBound->GetComponent<CTransformComponent>()->GetWorldPosition(), FVector3( 100.0f, 0.0f, 3.0f ) ),
				 "a bound object that has no position component gets one, and follows its placeholder (100,0,3)" );

		// 描画コマンド
		C.Check( S.Log.Count( EDrawCommand::Quad3D ) == 3 && S.Log.Count( EDrawCommand::Model3D ) == 4, "three billboards and four models (two of the stage, and the two floors of the mesh world) are drawn by the built-in draw systems (%d quads, %d models)", S.Log.Count( EDrawCommand::Quad3D ),
				 S.Log.Count( EDrawCommand::Model3D ) );
		{
			// 編集メッシュの描画コマンド: 材質ごとに 1 つ（床 1 + 箱 3）。両面・画像・行列・色
			int32 NumDraws = 0, NumTwoSided = 0, NumTextured = 0, NumFloor = 0;
			float32 FloorX = 0.0f;
			S.Log.ForEach( [&]( const FDrawCommandView& Command )
			{
				if ( Command.Type != EDrawCommand::Mesh3D ) return;
				const FDrawMeshData Data = Command.Read<FDrawMeshData>();
				++NumDraws;
				NumTwoSided += Command.Has( FDrawFlag::TwoSided ) ? 1 : 0;
				NumTextured += Data.Texture >= 0 ? 1 : 0;
				if ( Data.NumIndices == 36 )
				{
					++NumFloor;
					FloorX = Data.M[9];
				}
			} );
			C.Group = "world-mesh-draw";
			C.Check( NumDraws == 4 && NumTwoSided == 1 && NumTextured == 1 && NumFloor == 1, "the built-in draw system records one Mesh3D per material: 4 draws, 1 two-sided, 1 with an image, 1 whole box (%d, %d, %d, %d)", NumDraws, NumTwoSided, NumTextured, NumFloor );
			C.Check( std::fabs( FloorX - 4100.0f ) < 1.0e-3f, "and the matrix places the floor at the object's world position (x %.2f)", FloorX );
			C.Group = "world-draw";
		}
		const int32 TextureHandle = S.Texture.IsValid() ? S.Texture->GetHandle() : -1;
		bool bTorch = false, bSign = false, bRoll = false;
		S.Log.ForEach( [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::Quad3D ) return;
			const FDrawQuad3DData D = Command.Read<FDrawQuad3DData>();
			const FVector3 Pos( D.Pos[0], D.Pos[1], D.Pos[2] );
			if ( D.Orient == static_cast<int32>( EDrawQuadOrient::CameraY ) )
			{
				bTorch = true;
				C.Check( Near3( Pos, FVector3( 103.0f, 1.0f, 0.0f ) ) && D.Handle == TextureHandle && D.Width == 2.0f && D.Height == 1.0f && D.PivotX == 0.5f && D.PivotY == 1.0f, "the torch's quad: position, image, size and pivot" );
				C.Check( D.Color.R == 255 && D.Color.G == 128 && D.Color.B == 0 && D.Color.A == 200, "its tint" );
				// 左右反転: U の始まりと終わりが入れ替わる（矩形 (16, 8, 32, 16) / 画像 128 x 64）
				C.Check( std::fabs( D.U0 - 0.375f ) < 1.0e-5f && std::fabs( D.U1 - 0.125f ) < 1.0e-5f && std::fabs( D.V0 - 0.125f ) < 1.0e-5f && std::fabs( D.V1 - 0.375f ) < 1.0e-5f, "flipped horizontally: U runs from 0.375 to 0.125 (%.3f..%.3f)", D.U0, D.U1 );
			}
			else if ( D.Orient == static_cast<int32>( EDrawQuadOrient::Fixed ) )
			{
				bSign = true;
				// 看板: 位置 (100,0,5)、Yaw 90（横の向きが -Z）、拡大 (2,3,1) -> 大きさ (1.5 x 2, 1 x 3)
				C.Check( Near3( Pos, FVector3( 100.0f, 0.0f, 5.0f ) ) && std::fabs( D.Width - 3.0f ) < 1.0e-4f && std::fabs( D.Height - 3.0f ) < 1.0e-4f, "the world-fixed quad: position, and size times the object's scale" );
				C.Check( Near3( FVector3( D.Right[0], D.Right[1], D.Right[2] ), FVector3( 0.0f, 0.0f, -1.0f ) ) && Near3( FVector3( D.Up[0], D.Up[1], D.Up[2] ), FVector3( 0.0f, 1.0f, 0.0f ) ), "its horizontal / vertical axes come from the object's rotation (%.2f %.2f %.2f)", D.Right[0],
						 D.Right[1], D.Right[2] );
				C.Check( D.U0 == 0.0f && D.U1 == 1.0f && D.V0 == 0.0f && D.V1 == 1.0f && D.Color.A == 255 && D.Color.R == 255, "no rectangle, no flip, white tint by default" );
			}
			else if ( D.Orient == static_cast<int32>( EDrawQuadOrient::Camera ) )
			{
				bRoll = true;
				C.Check( Near3( Pos, FVector3( 100.0f, 3.0f, 0.0f ) ) && std::fabs( std::fabs( D.Angle ) - 1.5707964f ) < 1.0e-3f, "the camera-facing quad uses the object's roll (90 degrees) as its angle (%.3f)", D.Angle );
			}
		} );
		C.Check( bTorch && bSign && bRoll, "all three kinds of quad were recorded" );

		// 環境音
		C.Group = "world-audio";
		C.Check( Audio != nullptr && Audio->GetNumVoices() == 1 && S.Rec.CountPlaying() == 1, "the torch's ambient sound is playing (auto play), and the one whose file is missing is not (%d voice(s))", Audio != nullptr ? Audio->GetNumVoices() : -1 );
		if ( S.Rec.LastStarted >= 0 )
		{
			const FRecordingAudioBackend::FVoiceState& Voice = S.Rec.Voices[S.Rec.LastStarted];
			C.Check( Voice.bLoop && std::fabs( Voice.Pitch - 1.5f ) < 0.01f && Voice.Volume > 0.0f && Voice.Volume <= 0.5f + 1.0e-4f, "looping, pitch 1.5, volume at most 0.5 and audible at 3 units (pitch %.3f, volume %.3f)", Voice.Pitch, Voice.Volume );
		}
	}

	//---------------------------------------------------------------------
	// フェーズ 0: メッシュの床とトリガー。球が落ちて、止まる（または時間切れ）まで待つ
	//---------------------------------------------------------------------
	if ( S.Phase == 0 && S.Frame >= 10 && !S.bMeshChecked && Physics != nullptr )
	{
		const auto Rests = [&]( int32 Index, float32 Y )
		{
			const FRigidBody* Ball = S.MeshBalls[Index];
			return Ball != nullptr && std::fabs( Ball->GetPosition().y - Y ) < 0.05f && Ball->GetLinearVelocity().Length() < 0.2f;
		};
		const auto Fell = [&]( int32 Index ) { return S.MeshBalls[Index] != nullptr && S.MeshBalls[Index]->GetPosition().y < -2.0f; };
		const bool bReady = Rests( FState::MeshFloorBall, 1.5f ) && Rests( FState::ScaledHitBall, 0.5f ) && Fell( FState::ScaledMissBall ) && Rests( FState::MeshOnlyBall, 0.5f ) && Rests( FState::ZoneBall, -4.5f )
							&& Rests( FState::MeshZoneBall, -4.5f ) && Rests( FState::KeepBall, 0.5f ) && ( Rests( FState::ReversedBall, 0.5f ) || Fell( FState::ReversedBall ) ) && Rests( FState::EditFloorBall, 1.5f ) && S.DumbbellExit >= 1 && Rests( FState::LateBall, -4.5f );
		if ( bReady || S.Frame >= 900 )
		{
			S.bMeshChecked = true;
			C.Group = "world-mesh-physics";
			const auto Y = [&]( int32 Index ) { return S.MeshBalls[Index] != nullptr ? S.MeshBalls[Index]->GetPosition().y : -999.0f; };
			C.Check( Rests( FState::MeshFloorBall, 1.5f ), "a ball dropped on a mesh floor (made of the model's triangles, placed at y = 1) comes to rest on it (y %.3f)", Y( FState::MeshFloorBall ) );
			C.Check( Rests( FState::ScaledHitBall, 0.5f ), "the scale (2) and the rotation (45 degrees) are applied to the triangles: a ball 18 away on the floor's diagonal side rests on it (y %.3f)", Y( FState::ScaledHitBall ) );
			C.Check( Fell( FState::ScaledMissBall ), "and a ball outside the rotated floor falls past it (y %.3f)", Y( FState::ScaledMissBall ) );
			C.Check( Rests( FState::MeshOnlyBall, 0.5f ), "a mesh collider can name its own file and have no Model component (y %.3f)", Y( FState::MeshOnlyBall ) );
			C.Check( Rests( FState::EditFloorBall, 1.5f ), "a mesh collider takes the triangles of the object's own Mesh component (a 20 x 1 x 20 box: a ball rests on its top, y = 1.5) (y %.3f)", Y( FState::EditFloorBall ) );
			C.Check( Rests( FState::ZoneBall, -4.5f ) && Rests( FState::MeshZoneBall, -4.5f ), "balls fall through both triggers (they do not block) to the floors below (%.3f, %.3f)", Y( FState::ZoneBall ), Y( FState::MeshZoneBall ) );

			C.Group = "world-trigger";
			C.Check( S.ZoneEnter == 1 && S.ZoneExit == 1 && S.ZoneOther == S.MeshBalls[FState::ZoneBall], "a ball passing through a trigger: one enter (with that ball's body) and one exit (%d, %d)", S.ZoneEnter, S.ZoneExit );
			C.Check( S.MeshZoneEnter == 1 && S.MeshZoneExit == 1, "also through a mesh trigger, where several triangles touch the ball: still one enter and one exit (%d, %d)", S.MeshZoneEnter, S.MeshZoneExit );
			C.Check( S.DumbbellEnter == 1 && S.DumbbellExit == 1 && S.DumbbellExitY < -0.9f, "a body of two shapes, both touching the trigger at once: one enter, and one exit only after the last shape left (the centre at %.2f; one shape leaves at about 0, both at about -1.1) (%d, %d)", S.DumbbellExitY,
					 S.DumbbellEnter, S.DumbbellExit );
			C.Check( S.LateEnter == 1 && S.LateExit == 1, "a trigger made after the ball that passes through it (the other order of the two bodies): one enter and one exit (%d, %d)", S.LateEnter, S.LateExit );
			const TSharedPtr<CGameObject> KeepZone = S.MeshLevel.FindByName( "KeepZone" );
			const CWorldColliderComponent* KeepCollider = KeepZone.IsValid() ? KeepZone->GetComponent<CWorldColliderComponent>() : nullptr;
			CWorldTriggerSubsystem* Triggers = GetSubsystem<CWorldTriggerSubsystem>();
			C.Check( S.KeepEnter == 1 && S.KeepExit == 0 && Triggers != nullptr && KeepCollider != nullptr && Triggers->GetNumOverlaps( *KeepCollider ) == 1, "a ball resting inside: entered once, not exited, and counted as overlapping (%d, %d)", S.KeepEnter, S.KeepExit );

			// 三角形は片面だけ（DxLib の表 = 時計回りの面）。裏から来たものは通り抜ける。表から落とした床は、上で止めている
			C.Check( Fell( FState::ReversedBall ), "a floor whose triangles face down does not hold a ball dropped on it: triangles are one-sided (y %.3f)", Y( FState::ReversedBall ) );

			// 中にいる球を消す: 出た、は知らせない。重なりの記録は片付く
			if ( S.MeshBalls[FState::KeepBall] != nullptr ) Physics->DestroyBody( S.MeshBalls[FState::KeepBall] );
			S.MeshBalls[FState::KeepBall] = nullptr;
			S.GhostFrame = S.Frame;
		}
	}
	if ( S.Phase == 0 && S.bMeshChecked && !S.bMeshDone && S.Frame >= S.GhostFrame + 3 )
	{
		C.Group = "world-trigger";
		const TSharedPtr<CGameObject> KeepZone = S.MeshLevel.FindByName( "KeepZone" );
		const CWorldColliderComponent* KeepCollider = KeepZone.IsValid() ? KeepZone->GetComponent<CWorldColliderComponent>() : nullptr;
		CWorldTriggerSubsystem* Triggers = GetSubsystem<CWorldTriggerSubsystem>();
		C.Check( S.KeepExit == 0 && Triggers != nullptr && KeepCollider != nullptr && Triggers->GetNumOverlaps( *KeepCollider ) == 0, "a ball destroyed while inside is not reported as exiting, and is no longer counted (%d exits)", S.KeepExit );
		for ( FRigidBody*& Ball : S.MeshBalls )
		{
			if ( Ball != nullptr ) Physics->DestroyBody( Ball );
			Ball = nullptr;
		}
		S.MeshLevel.Despawn();
		S.MeshModels[0].Reset();
		S.MeshModels[1].Reset();
		S.bMeshDone = true;   // 消したあとの確認（剛体が無くなる）は、フェーズ 1 の「球の 4 つだけが残る」が兼ねる
	}

	//---------------------------------------------------------------------
	// フェーズ 0: 球が落ちて、止まる（または時間切れ）まで待つ
	//---------------------------------------------------------------------
	if ( S.Phase == 0 && S.Frame >= 10 && !S.bPhysicsChecked && Physics != nullptr )
	{
		const auto Settled = [&]( int32 Index )
		{
			return S.Balls[Index] != nullptr && std::fabs( S.Balls[Index]->GetPosition().y - 0.5f ) < 0.05f && S.Balls[Index]->GetLinearVelocity().Length() < 0.2f;
		};
		const bool bBothSettled = Settled( 0 ) && Settled( 2 );
		const bool bFellThrough = S.Balls[1] != nullptr && S.Balls[3] != nullptr && S.Balls[1]->GetPosition().y < -2.0f && S.Balls[3]->GetPosition().y < -2.0f;
		if ( ( ( bBothSettled && bFellThrough ) || S.Frame >= 900 ) && ( S.bMeshDone || S.Frame >= 1000 ) )
		{
			S.bPhysicsChecked = true;
			C.Group = "world-physics";
			C.Check( Settled( 0 ), "a ball dropped on the floor comes to rest on top of it (y %.3f)", S.Balls[0] != nullptr ? S.Balls[0]->GetPosition().y : -999.0f );
			C.Check( S.Balls[1] != nullptr && S.Balls[1]->GetPosition().y < -2.0f, "a ball dropped where the tag rule turned the collider off falls through (y %.3f)", S.Balls[1] != nullptr ? S.Balls[1]->GetPosition().y : -999.0f );
			C.Check( Settled( 2 ), "a ball dropped on a collider that the object forced on rests on it, although its tag says off (y %.3f)", S.Balls[2] != nullptr ? S.Balls[2]->GetPosition().y : -999.0f );
			C.Check( S.Balls[3] != nullptr && S.Balls[3]->GetPosition().y < -2.0f, "and one dropped where the object turned its collider off falls through (y %.3f)", S.Balls[3] != nullptr ? S.Balls[3]->GetPosition().y : -999.0f );

			// 消す
			S.Stage.Despawn();
			S.PhysicsLevel.Despawn();
			C.Check( S.Stage.GetNumSpawned() == 0 && S.Stage.FindFirst( "PlayerStart" ) == nullptr, "after Despawn the instance finds nothing" );
			S.Phase = 1;
			S.DespawnFrame = S.Frame;
		}
	}

	//---------------------------------------------------------------------
	// フェーズ 1: 消えたあとの確認
	//---------------------------------------------------------------------
	if ( S.Phase == 1 && S.Frame >= S.DespawnFrame + 3 )
	{
		C.Group = "world-despawn";
		C.Check( FindObjects<CTestEnemy>().IsEmpty(), "objects bound to the world's tags are destroyed with their placeholders" );
		C.Check( Physics != nullptr && Physics->GetBodyCount() == 4, "the colliders' bodies (those of the mesh world as well) are removed with the placed objects (only the 4 test balls remain: %d)", Physics != nullptr ? Physics->GetBodyCount() : -1 );
		C.Check( Audio != nullptr && S.Rec.CountPlaying() == 0, "and the ambient sound is stopped (%d playing)", S.Rec.CountPlaying() );
		C.Check( GetNumObjects() == 1, "only the listener is left in the scene (%d)", GetNumObjects() );
		for ( FRigidBody*& Ball : S.Balls )
		{
			if ( Ball != nullptr && Physics != nullptr ) Physics->DestroyBody( Ball );
			Ball = nullptr;
		}
		S.Phase = 5;
		return;
	}

	//---------------------------------------------------------------------
	// フェーズ 5: エフェクトを置く（片付けの前の、アセットを使える間に）
	//---------------------------------------------------------------------
	if ( S.Phase == 5 )
	{
		if ( Assets != nullptr ) NWorldTest::RunEffectSceneStart( C, *this, *Assets, S.Registry );
		S.EffectFrame = S.Frame;
		S.Phase = 6;
		return;
	}

	//---------------------------------------------------------------------
	// フェーズ 6: 1 フレーム回ったあとの、エフェクトの描画（このフレームの描画コマンドは、OnDraw の前に流れている）
	//---------------------------------------------------------------------
	if ( S.Phase == 6 && S.Frame >= S.EffectFrame + 1 )
	{
		if ( Assets != nullptr ) NWorldTest::RunEffectSceneCheck( C, *this, *Assets, S.Log );
		S.Phase = 7;
		return;
	}

	//---------------------------------------------------------------------
	// フェーズ 7: エフェクトを消す（消えるのは、次のフレームのあと）
	//---------------------------------------------------------------------
	if ( S.Phase == 7 && S.Frame >= S.EffectFrame + 4 )
	{
		if ( Assets != nullptr ) NWorldTest::RunEffectSceneFinish( C, *this, *Assets );
		S.Phase = 2;
		return;
	}

	//---------------------------------------------------------------------
	// フェーズ 2: 偽のバックエンドを外す
	//---------------------------------------------------------------------
	if ( S.Phase == 2 )
	{
		// 入れ子のワールド（アセットとして保存して、設置して、消す。ほかの確認の数を変えないよう、後始末の前に）
		if ( Assets != nullptr ) NWorldTest::RunPrefabSceneTests( C, *this, *Assets, S.Registry );
		C.Group = "world-cleanup";
		S.Listener->Destroy();
		S.Listener.Reset();
		S.Level.Reset();
		S.Texture.Reset();
		if ( Assets != nullptr )
		{
			Assets->ReleaseUnused();
			Assets->Tick( 0.0f );
			C.Check( S.Fake.Models.empty() && S.Fake.Sounds.empty() && S.Fake.Textures.empty() && S.Fake.VertexBuffers.empty() && S.Fake.IndexBuffers.empty(), "everything the placed objects loaded or made is released (models %d, sounds %d, textures %d, mesh buffers %d)",
					 static_cast<int32>( S.Fake.Models.size() ), static_cast<int32>( S.Fake.Sounds.size() ), static_cast<int32>( S.Fake.Textures.size() ), static_cast<int32>( S.Fake.VertexBuffers.size() ) );
		}
		if ( Audio != nullptr ) Audio->SetBackendOverride( nullptr );
		if ( Assets != nullptr )
		{
			Assets->SetBackendOverride( nullptr );
			Assets->SetRootDirectory( "Assets" );
		}
		SetDrawExecutor( nullptr );
		S.Phase = 3;
		return;
	}

	//---------------------------------------------------------------------
	// フェーズ 3: 本物の DxLib
	//---------------------------------------------------------------------
	if ( S.Phase == 3 )
	{
		RunRealChecks();
		S.Phase = 4;
		Finish();
	}
}

void CWorldSelfTestScene::OnExit()
{
	State.Reset();
}


//=============================================================================
// 本物の DxLib: ビルボードのピクセル
//   画面 128 x 128、正射影のカメラ（縦 8 ワールド単位 = 1 単位 16 ピクセル）。大きさ 2 x 2 の四角形は、32 x 32 ピクセルになる。
//   画像は 4 象限（左上が赤・右上が緑・左下が青・右下が黄色）。向きが違えば、象限の位置が変わる。
//=============================================================================
void CWorldSelfTestScene::RunRealChecks()
{
	FState& S = *State;
	FChecker& C = S.Checker;
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
	if ( Assets == nullptr || Executor == nullptr )
	{
		C.Check( false, "the asset manager / draw executor do not exist" );
		return;
	}

	const FString Dir = S.TempRoot / "real";
	CreateDirectoryA( *Dir, nullptr );
	Assets->SetRootDirectory( Dir );
	const float32 NearClip = GetCameraNear();
	const float32 FarClip = GetCameraFar();
	WriteFileBytes( Dir / "quad.bmp", MakeQuadrantBmp() );

	C.Group = "billboard-pixels";
	const TSharedPtr<CTexture> Texture = Assets->LoadTexture( "quad.bmp" );
	C.Check( Texture.IsValid() && Texture->GetWidth() == 4 && Texture->GetHeight() == 4, "the quadrant image loads (4 x 4)" );
	FCapture Capture;
	if ( !Texture.IsValid() || !Capture.Begin( 128, 128 ) )
	{
		C.Skip( "no texture or capture surface" );
		return;
	}

	struct FRgb
	{
		int R, G, B;
	};
	const FRgb Red { 255, 0, 0 }, Green { 0, 255, 0 }, Blue { 0, 0, 255 }, Yellow { 255, 255, 0 };

	const VECTOR Front = VGet( 0.0f, 0.0f, -10.0f );
	const auto Render = [&]( const FBillboard& Board, const FTransform& World, const VECTOR& CameraPosition )
	{
		ClearDrawScreen();
		SetCameraNearFar( 0.1f, 100.0f );
		SetupCamera_Ortho( 8.0f );
		SetCameraPositionAndTarget_UpVecY( CameraPosition, VGet( 0.0f, 0.0f, 0.0f ) );
		SetUseLighting( FALSE );
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Board.Record( Draw, World );
		ExecuteBuffer( *Executor, Buffer );
		Capture.Read();
	};
	// 光っている範囲の、4 象限の中心の色（範囲の 1/4 と 3/4 の位置）
	const auto Quadrants = [&]( FRgb Out[4] ) -> bool
	{
		int L, T, R, B;
		if ( !Capture.LitBounds( L, T, R, B ) ) return false;
		const int W = R - L + 1, H = B - T + 1;
		const int Xs[2] = { L + W / 4, L + ( W * 3 ) / 4 };
		const int Ys[2] = { T + H / 4, T + ( H * 3 ) / 4 };
		for ( int i = 0; i < 4; ++i ) Capture.At( Xs[i % 2], Ys[i / 2], Out[i].R, Out[i].G, Out[i].B );
		return true;
	};
	const auto Near = [&]( const FRgb& A, const FRgb& B, int Tolerance = 8 ) { return std::abs( A.R - B.R ) <= Tolerance && std::abs( A.G - B.G ) <= Tolerance && std::abs( A.B - B.B ) <= Tolerance; };
	const auto QuadrantsAre = [&]( const FRgb& TL, const FRgb& TR, const FRgb& BL, const FRgb& BR, int Tolerance = 8 )
	{
		FRgb Pixels[4];
		return Quadrants( Pixels ) && Near( Pixels[0], TL, Tolerance ) && Near( Pixels[1], TR, Tolerance ) && Near( Pixels[2], BL, Tolerance ) && Near( Pixels[3], BR, Tolerance );
	};
	const auto SizeIs = [&]( int ExpectedWidth, int ExpectedHeight, int Tolerance = 2 )
	{
		int L, T, R, B;
		return Capture.LitBounds( L, T, R, B ) && std::abs( ( R - L + 1 ) - ExpectedWidth ) <= Tolerance && std::abs( ( B - T + 1 ) - ExpectedHeight ) <= Tolerance;
	};
	const auto BoundsAre = [&]( int ExpectedLeft, int ExpectedTop, int ExpectedRight, int ExpectedBottom, int Tolerance = 2 )
	{
		int L, T, R, B;
		return Capture.LitBounds( L, T, R, B ) && std::abs( L - ExpectedLeft ) <= Tolerance && std::abs( T - ExpectedTop ) <= Tolerance && std::abs( R - ExpectedRight ) <= Tolerance && std::abs( B - ExpectedBottom ) <= Tolerance;
	};

	FBillboard Base;
	Base.Texture = Texture;
	Base.Size = FVector2( 2.0f, 2.0f );

	// 1. カメラの方を向く（正面から）: 32 x 32 で、左右が反転しない
	FBillboard Camera = Base;
	Camera.Orient = EDrawQuadOrient::Camera;
	Render( Camera, FTransform::Identity, Front );
	C.Check( BoundsAre( 48, 48, 79, 79 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "a camera-facing quad seen from the front covers (48..79, 48..79) and is not mirrored: red green / blue yellow (%s)", *Capture.BoundsText() );
	C.Check( Capture.IsBlack( 40, 64 ) && Capture.IsBlack( 88, 64 ) && Capture.IsBlack( 64, 40 ) && Capture.IsBlack( 64, 88 ), "and nothing is drawn outside it" );

	// 2. 向き固定（正面から）: 同じ絵。裏から（Yaw 180）: 左右が入れ替わる（両面が見える）
	FBillboard Fixed = Base;
	Fixed.Orient = EDrawQuadOrient::Fixed;
	Render( Fixed, FTransform::Identity, Front );
	C.Check( BoundsAre( 48, 48, 79, 79 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "a world-fixed quad facing the camera looks the same (%s)", *Capture.BoundsText() );
	const FTransform Behind( FVector3::Zero, FQuaternion::FromAxisAngle( FVector3::Up, 3.14159265f ) );
	Render( Fixed, Behind, Front );
	C.Check( BoundsAre( 48, 48, 79, 79 ) && QuadrantsAre( Green, Red, Yellow, Blue ), "seen from behind (yaw 180) it is drawn too (both sides), mirrored: green red / yellow blue (%s)", *Capture.BoundsText() );
	// 呼び出し側が背面のカリングを有効にしていても、どちらの面も描かれる（描いている間だけ切る）
	for ( const int Culling : { DX_CULLING_LEFT, DX_CULLING_RIGHT } )
	{
		SetUseBackCulling( Culling );
		Render( Fixed, FTransform::Identity, Front );
		const bool bFront = BoundsAre( 48, 48, 79, 79 ) && QuadrantsAre( Red, Green, Blue, Yellow );
		SetUseBackCulling( Culling );
		Render( Fixed, Behind, Front );
		const bool bBack = BoundsAre( 48, 48, 79, 79 ) && QuadrantsAre( Green, Red, Yellow, Blue );
		C.Check( bFront && bBack, "with the caller's back-face culling on (mode %d), both the front and the back are still drawn", Culling );
	}
	SetUseBackCulling( DX_CULLING_NONE );

	// 3. 視線まわりの回転: 向き固定の板を同じだけ回したものと、同じ絵になる（回る向きが、オブジェクトの回転と同じ）
	const FQuaternion Roll = FQuaternion::FromAxisAngle( FVector3::Forward, 1.5707964f );
	Render( Fixed, FTransform( FVector3::Zero, Roll ), Front );
	FRgb FixedRolled[4] = {};
	const bool bFixedRolled = Quadrants( FixedRolled );
	Render( Camera, FTransform( FVector3::Zero, Roll ), Front );
	FRgb CameraRolled[4] = {};
	const bool bCameraRolled = Quadrants( CameraRolled );
	C.Check( bFixedRolled && bCameraRolled && Near( FixedRolled[0], CameraRolled[0] ) && Near( FixedRolled[1], CameraRolled[1] ) && Near( FixedRolled[2], CameraRolled[2] ) && Near( FixedRolled[3], CameraRolled[3] ),
			 "a camera-facing quad with a roll of 90 degrees looks like a facing world-fixed quad with the same rotation" );
	C.Check( bCameraRolled && !Near( CameraRolled[0], Red ) && SizeIs( 32, 32 ), "and it really turned (the top-left is no longer red), staying 32 x 32" );

	// 4. カメラの向き: 横から（水平に 45 度）と、上から（見下ろし 45 度）
	const VECTOR Side = VGet( 8.0f, 0.0f, -8.0f );
	const VECTOR Above = VGet( 0.0f, 8.0f, -8.0f );
	const VECTOR Diagonal = VGet( 8.0f, 8.0f, -8.0f );
	FBillboard CameraY = Base;
	CameraY.Orient = EDrawQuadOrient::CameraY;
	Render( Camera, FTransform::Identity, Side );
	C.Check( SizeIs( 32, 32 ), "from the side, a camera-facing quad is still 32 x 32 (%s)", *Capture.BoundsText() );
	Render( CameraY, FTransform::Identity, Side );
	C.Check( SizeIs( 32, 32 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "from the side, a Y-axis billboard turns to face the camera: 32 x 32, not mirrored (%s)", *Capture.BoundsText() );
	Render( Fixed, FTransform::Identity, Side );
	C.Check( SizeIs( 23, 32 ), "while a world-fixed quad is seen at an angle: 32 * cos 45 = 23 wide (%s)", *Capture.BoundsText() );
	Render( Camera, FTransform::Identity, Above );
	C.Check( SizeIs( 32, 32 ), "from above, a camera-facing quad is still 32 x 32 (%s)", *Capture.BoundsText() );
	Render( CameraY, FTransform::Identity, Above );
	C.Check( SizeIs( 32, 23 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "from above, a Y-axis billboard stays upright, so it looks 32 * cos 45 = 23 tall (%s)", *Capture.BoundsText() );
	Render( CameraY, FTransform::Identity, Diagonal );
	C.Check( SizeIs( 32, 26 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "from above and the side: 32 wide (it faces the camera), 32 * cos 35 = 26 tall (%s)", *Capture.BoundsText() );

	// 5. 基準点と大きさ
	FBillboard Pivot = Camera;
	Pivot.Pivot = FVector2( 0.0f, 0.0f );
	Render( Pivot, FTransform::Identity, Front );
	C.Check( BoundsAre( 64, 64, 95, 95 ), "pivot (0, 0): the image's top-left is at the object, so it extends right and down (%s)", *Capture.BoundsText() );
	Pivot.Pivot = FVector2( 1.0f, 1.0f );
	Render( Pivot, FTransform::Identity, Front );
	C.Check( BoundsAre( 32, 32, 63, 63 ), "pivot (1, 1): it extends left and up (%s)", *Capture.BoundsText() );
	Render( Camera, FTransform( FVector3( 1.0f, -2.0f, 0.0f ) ), Front );
	C.Check( BoundsAre( 64, 80, 95, 111 ), "a position of (1, -2) moves it 16 px right and 32 px down (%s)", *Capture.BoundsText() );
	Render( Camera, FTransform( FVector3::Zero, FQuaternion::Identity, FVector3( 2.0f, 0.5f, 1.0f ) ), Front );
	C.Check( SizeIs( 64, 16 ), "the object's scale (2, 0.5) multiplies the size: 64 x 16 (%s)", *Capture.BoundsText() );
	Render( Camera, FTransform( FVector3::Zero, FQuaternion::Identity, FVector3( -2.0f, 1.0f, 1.0f ) ), Front );
	C.Check( SizeIs( 64, 32 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "a negative scale only changes the size; it does not mirror (flipping is a setting)" );

	// 6. 画像の矩形・反転
	FBillboard Rect = Camera;
	C.Check( Rect.SetRect( 2, 0, 2, 2 ), "a pixel rectangle can be set once the size is known" );
	Render( Rect, FTransform::Identity, Front );
	C.Check( BoundsAre( 48, 48, 79, 79 ) && QuadrantsAre( Green, Green, Green, Green ), "the rectangle (2, 0, 2, 2) of the image is only the green quadrant" );
	Rect.SetRect( 0, 2, 4, 2 );
	Render( Rect, FTransform::Identity, Front );
	C.Check( QuadrantsAre( Blue, Yellow, Blue, Yellow ), "the bottom half of the image: blue yellow" );
	FBillboard FlipX = Camera;
	FlipX.bFlipX = true;
	Render( FlipX, FTransform::Identity, Front );
	C.Check( QuadrantsAre( Green, Red, Yellow, Blue ), "flip X swaps left and right" );
	FBillboard FlipY = Camera;
	FlipY.bFlipY = true;
	Render( FlipY, FTransform::Identity, Front );
	C.Check( QuadrantsAre( Blue, Yellow, Red, Green ), "flip Y swaps top and bottom" );
	FlipY.bFlipX = true;
	Render( FlipY, FTransform::Identity, Front );
	C.Check( QuadrantsAre( Yellow, Blue, Green, Red ), "both flips together turn it 180 degrees" );

	// 7. 色と不透明度
	FBillboard Tint = Camera;
	Tint.Tint = FColor32( 128, 128, 128, 255 );
	Render( Tint, FTransform::Identity, Front );
	C.Check( QuadrantsAre( FRgb { 128, 0, 0 }, FRgb { 0, 128, 0 }, FRgb { 0, 0, 128 }, FRgb { 128, 128, 0 }, 6 ), "a grey tint darkens the image to half" );
	Tint.Tint = FColor32( 255, 255, 255, 128 );
	Render( Tint, FTransform::Identity, Front );
	C.Check( QuadrantsAre( FRgb { 128, 0, 0 }, FRgb { 0, 128, 0 }, FRgb { 0, 0, 128 }, FRgb { 128, 128, 0 }, 12 ), "an alpha of 128 over black gives about half the brightness (the quad blends by itself)" );
	{
		int BlendMode = -1, BlendParam = -1;
		GetDrawBlendMode( &BlendMode, &BlendParam );
		C.Check( BlendMode == DX_BLENDMODE_NOBLEND, "and the blend mode it used is put back (%d)", BlendMode );
	}

	// 8. 描画の状態を、漏らさない
	{
		SetUseBackCulling( DX_CULLING_LEFT );
		SetDrawBlendMode( DX_BLENDMODE_ADD, 128 );
		Render( Camera, FTransform::Identity, Front );
		int BlendMode = 0, BlendParam = 0;
		GetDrawBlendMode( &BlendMode, &BlendParam );
		C.Check( GetUseBackCulling() == DX_CULLING_LEFT && BlendMode == DX_BLENDMODE_ADD && BlendParam == 128, "drawing a quad leaves the culling mode and the blend mode as they were (culling %d, blend %d / %d)", GetUseBackCulling(), BlendMode, BlendParam );
		SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 0 );
		SetUseBackCulling( DX_CULLING_NONE );
	}
	{
		// 画像の無いコマンド（ハンドル -1）は、何も描かず、壊れない
		ClearDrawScreen();
		SetCameraNearFar( 0.1f, 100.0f );
		SetupCamera_Ortho( 8.0f );
		SetCameraPositionAndTarget_UpVecY( Front, VGet( 0.0f, 0.0f, 0.0f ) );
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Draw.Quad3D( FDrawQuad3DData() );
		ExecuteBuffer( *Executor, Buffer );
		Capture.Read();
		int L, T, R, B;
		C.Check( !Capture.LitBounds( L, T, R, B ), "a quad without an image draws nothing" );
	}

	// 編集メッシュ: 本物の頂点バッファで描いた絵（向き・カリング・画像の貼り方・色・行列）
	{
		C.Group = "mesh-pixels";
		SetUseBackCulling( DX_CULLING_LEFT );   // DxLib の既定（時計回りが表）

		// 画像の貼り方を決める、1 枚の面（2 x 2 の大きさの画像を、4 x 4 の面に 1 回貼る: Tile 4、Offset 0.5）
		const auto MakeFace = [&]( std::initializer_list<FVector3> Corners, bool bReverse, const FString& TextureFile, const FColor32& Color, bool bTwoSided )
		{
			FEditMesh Mesh;
			for ( const FVector3& Corner : Corners ) Mesh.Vertices.Add( Corner );
			FEditMeshFace Face;
			for ( int32 i = 0; i < Mesh.Vertices.Num(); ++i ) Face.Indices.Add( bReverse ? Mesh.Vertices.Num() - 1 - i : i );
			Mesh.Faces.Add( Face );
			FEditMeshMaterial Material;
			Material.Color = Color;
			Material.Texture = TextureFile;
			Material.Tile = 4.0f;
			Material.Offset = FVector2( 0.5f, 0.5f );
			Material.bTwoSided = bTwoSided;
			Mesh.Materials.Add( Material );
			return Mesh;
		};
		const auto RenderMesh = [&]( const FEditMesh& Mesh, const FTransform& World, const VECTOR& CameraPosition, const VECTOR& CameraUp ) -> bool
		{
			const FMeshRenderer Renderer = NWorld::MakeMeshRenderer( Mesh, [&]( const FString& Path ) { return Assets->LoadTexture( Path ); } );
			if ( !Renderer.IsValid() ) return false;
			ClearDrawScreen();
			SetCameraNearFar( 0.1f, 100.0f );
			SetupCamera_Ortho( 8.0f );
			SetCameraPositionAndTargetAndUpVec( CameraPosition, VGet( 0.0f, 0.0f, 0.0f ), CameraUp );
			SetUseLighting( FALSE );
			FDrawBuffer Buffer;
			FDrawContext Draw( Buffer );
			Renderer.Record( Draw, World );
			ExecuteBuffer( *Executor, Buffer );
			Capture.Read();
			return true;
		};
		const FColor32 White( 255, 255, 255, 255 );
		const VECTOR Up = VGet( 0.0f, 1.0f, 0.0f );
		// 4 x 4 の面が、64 x 64 ピクセルを覆う（1 ワールド単位 = 16 ピクセル）
		const std::initializer_list<FVector3> FrontQuad = { FVector3( -2, -2, 0 ), FVector3( -2, 2, 0 ), FVector3( 2, 2, 0 ), FVector3( 2, -2, 0 ) };   // -Z を向く（カメラは -Z 側）

		bool bDrawn = RenderMesh( MakeFace( FrontQuad, false, FString(), FColor32( 255, 0, 0 ), false ), FTransform::Identity, Front, Up );
		C.Check( bDrawn && BoundsAre( 32, 32, 95, 95 ) && Capture.IsColor( 64, 64, 255, 0, 0, 4 ) && Capture.IsBlack( 24, 64 ) && Capture.IsBlack( 104, 64 ), "a red face looking at the camera covers 64 x 64 pixels (%s)", *Capture.BoundsText() );

		// 向き: 時計回りが表。裏から見ると、描かれない。両面の材質なら、描かれる
		bDrawn = RenderMesh( MakeFace( FrontQuad, true, FString(), FColor32( 255, 0, 0 ), false ), FTransform::Identity, Front, Up );
		int L, T, R, B;
		C.Check( bDrawn && !Capture.LitBounds( L, T, R, B ), "the same face listed the other way round (seen from its back) is not drawn: the front is the clockwise side" );
		bDrawn = RenderMesh( MakeFace( FrontQuad, true, FString(), FColor32( 255, 0, 0 ), true ), FTransform::Identity, Front, Up );
		C.Check( bDrawn && BoundsAre( 32, 32, 95, 95 ) && GetUseBackCulling() == DX_CULLING_LEFT, "a two-sided material draws its back too, and puts the culling mode back (%d)", GetUseBackCulling() );
		SetUseBackCulling( DX_CULLING_NONE );
		bDrawn = RenderMesh( MakeFace( FrontQuad, true, FString(), FColor32( 255, 0, 0 ), false ), FTransform::Identity, Front, Up );
		C.Check( bDrawn && BoundsAre( 32, 32, 95, 95 ), "(with the caller's culling off, nothing is hidden: the mesh uses the caller's setting)" );
		SetUseBackCulling( DX_CULLING_LEFT );

		// 画像の貼り方: どの向きの面でも、画像が反転せず、上が上になる（赤・緑 / 青・黄色）
		const FString Picture = "quad.bmp";
		bDrawn = RenderMesh( MakeFace( FrontQuad, false, Picture, White, false ), FTransform::Identity, Front, Up );
		C.Check( bDrawn && BoundsAre( 32, 32, 95, 95 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "a face towards -z with the picture once over it: red green / blue yellow, not mirrored (%s)", *Capture.BoundsText() );
		const std::initializer_list<FVector3> TopQuad = { FVector3( -2, 0, -2 ), FVector3( -2, 0, 2 ), FVector3( 2, 0, 2 ), FVector3( 2, 0, -2 ) };      // +Y を向く
		bDrawn = RenderMesh( MakeFace( TopQuad, false, Picture, White, false ), FTransform::Identity, VGet( 0.0f, 10.0f, 0.0f ), VGet( 0.0f, 0.0f, 1.0f ) );
		C.Check( bDrawn && BoundsAre( 32, 32, 95, 95 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "a face upwards, seen from above with +z up the screen: the same picture (u = x, v = -z)" );
		const std::initializer_list<FVector3> RightQuad = { FVector3( 0, -2, -2 ), FVector3( 0, 2, -2 ), FVector3( 0, 2, 2 ), FVector3( 0, -2, 2 ) };    // +X を向く
		bDrawn = RenderMesh( MakeFace( RightQuad, false, Picture, White, false ), FTransform::Identity, VGet( 10.0f, 0.0f, 0.0f ), Up );
		C.Check( bDrawn && BoundsAre( 32, 32, 95, 95 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "a face towards +x, seen from +x: the same picture (u = z, v = -y)" );
		const std::initializer_list<FVector3> LeftQuad = { FVector3( 0, -2, 2 ), FVector3( 0, 2, 2 ), FVector3( 0, 2, -2 ), FVector3( 0, -2, -2 ) };    // -X を向く
		bDrawn = RenderMesh( MakeFace( LeftQuad, false, Picture, White, false ), FTransform::Identity, VGet( -10.0f, 0.0f, 0.0f ), Up );
		C.Check( bDrawn && BoundsAre( 32, 32, 95, 95 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "a face towards -x, seen from -x: the same picture (u = -z, v = -y)" );
		const std::initializer_list<FVector3> BackQuad = { FVector3( 2, -2, 0 ), FVector3( 2, 2, 0 ), FVector3( -2, 2, 0 ), FVector3( -2, -2, 0 ) };     // +Z を向く
		bDrawn = RenderMesh( MakeFace( BackQuad, false, Picture, White, false ), FTransform::Identity, VGet( 0.0f, 0.0f, 10.0f ), Up );
		C.Check( bDrawn && BoundsAre( 32, 32, 95, 95 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "a face towards +z, seen from +z: the same picture (u = -x, v = -y)" );
		const std::initializer_list<FVector3> BottomQuad = { FVector3( 2, 0, -2 ), FVector3( 2, 0, 2 ), FVector3( -2, 0, 2 ), FVector3( -2, 0, -2 ) };  // -Y を向く
		bDrawn = RenderMesh( MakeFace( BottomQuad, false, Picture, White, false ), FTransform::Identity, VGet( 0.0f, -10.0f, 0.0f ), VGet( 0.0f, 0.0f, 1.0f ) );
		C.Check( bDrawn && BoundsAre( 32, 32, 95, 95 ) && QuadrantsAre( Red, Green, Blue, Yellow ), "a face downwards, seen from below with +z up the screen: the same picture (u = -x, v = -z)" );

		// 色: 材質の色が、頂点の色になって、画像に掛かる
		bDrawn = RenderMesh( MakeFace( FrontQuad, false, FString(), FColor32( 200, 100, 50 ), false ), FTransform::Identity, Front, Up );
		C.Check( bDrawn && Capture.IsColor( 64, 64, 200, 100, 50, 6 ), "the material colour (200, 100, 50) is the colour drawn" );
		bDrawn = RenderMesh( MakeFace( FrontQuad, false, Picture, FColor32( 128, 128, 128 ), false ), FTransform::Identity, Front, Up );
		C.Check( bDrawn && QuadrantsAre( FRgb { 128, 0, 0 }, FRgb { 0, 128, 0 }, FRgb { 0, 0, 128 }, FRgb { 128, 128, 0 }, 6 ), "a grey material colour darkens the picture to half" );

		// 行列: 位置と拡大・回転が、そのまま掛かる
		bDrawn = RenderMesh( MakeFace( FrontQuad, false, FString(), FColor32( 255, 0, 0 ), false ), FTransform( FVector3( 2.0f, 0.0f, 0.0f ), FQuaternion::Identity, FVector3( 0.5f, 0.5f, 1.0f ) ), Front, Up );
		C.Check( bDrawn && BoundsAre( 80, 48, 111, 79 ), "a position of 2 and a scale of 0.5 move and shrink it: 32 x 32 around (96, 64) (%s)", *Capture.BoundsText() );
		bDrawn = RenderMesh( MakeFace( FrontQuad, false, FString(), FColor32( 255, 0, 0 ), false ), FTransform( FVector3::Zero, FQuaternion::FromAxisAngle( FVector3::Up, 3.14159265f ), FVector3::One ), Front, Up );
		C.Check( bDrawn && !Capture.LitBounds( L, T, R, B ), "turned around (yaw 180), it shows its back and is not drawn" );
		{
			MATRIX Matrix;
			GetTransformToWorldMatrix( &Matrix );
			C.Check( Matrix.m[0][0] == 1.0f && Matrix.m[1][1] == 1.0f && Matrix.m[3][0] == 0.0f, "and the world matrix is back to the identity after the span" );
		}

		// 材質が 2 つ: バッチごとに、それぞれのインデックスの範囲から描く（2 つめは、インデックスの 6 つめから）
		{
			FEditMesh Two;
			const auto AddQuad = [&]( float32 X0, float32 X1, int32 Material )
			{
				const int32 First = Two.Vertices.Num();
				Two.Vertices.Add( FVector3( X0, -2, 0 ) );
				Two.Vertices.Add( FVector3( X0, 2, 0 ) );
				Two.Vertices.Add( FVector3( X1, 2, 0 ) );
				Two.Vertices.Add( FVector3( X1, -2, 0 ) );
				FEditMeshFace Face;
				for ( int32 i = 0; i < 4; ++i ) Face.Indices.Add( First + i );
				Face.Material = Material;
				Two.Faces.Add( Face );
			};
			AddQuad( -3, -1, 0 );
			AddQuad( 1, 3, 1 );
			FEditMeshMaterial RedMaterial, GreenMaterial;
			RedMaterial.Color = FColor32( 255, 0, 0 );
			GreenMaterial.Color = FColor32( 0, 255, 0 );
			Two.Materials.Add( RedMaterial );
			Two.Materials.Add( GreenMaterial );
			bDrawn = RenderMesh( Two, FTransform::Identity, Front, Up );
			C.Check( bDrawn && Capture.IsColor( 32, 64, 255, 0, 0, 6 ) && Capture.IsColor( 96, 64, 0, 255, 0, 6 ) && Capture.IsBlack( 64, 64 ), "two materials: the red quad on the left, the green one on the right (the second batch starts at its own indices)" );

			// 見えなくした描画データは、何も描かない
			FMeshRenderer Hidden = NWorld::MakeMeshRenderer( Two, nullptr );
			Hidden.bVisible = false;
			ClearDrawScreen();
			SetCameraNearFar( 0.1f, 100.0f );
			SetupCamera_Ortho( 8.0f );
			SetCameraPositionAndTargetAndUpVec( Front, VGet( 0.0f, 0.0f, 0.0f ), Up );
			SetUseLighting( FALSE );
			FDrawBuffer HiddenBuffer;
			FDrawContext HiddenDraw( HiddenBuffer );
			Hidden.Record( HiddenDraw, FTransform::Identity );
			ExecuteBuffer( *Executor, HiddenBuffer );
			Capture.Read();
			C.Check( Hidden.IsValid() && !Capture.LitBounds( L, T, R, B ), "a renderer that is not visible draws nothing" );

			// バッファは、3 の倍数で、すべてが頂点を指すインデックスからだけ作る
			FMeshVertex Corners[4] = {};
			const uint32 FourIndices[4] = { 0, 1, 2, 3 };
			const uint32 PastTheEnd[3] = { 0, 1, 4 };
			const uint32 Good[3] = { 0, 1, 3 };
			C.Check( !CMeshBuffer::Create( TArrayView<const FMeshVertex>( Corners, 4 ), TArrayView<const uint32>( FourIndices, 4 ) ) && !CMeshBuffer::Create( TArrayView<const FMeshVertex>( Corners, 4 ), TArrayView<const uint32>( PastTheEnd, 3 ) )
						 && CMeshBuffer::Create( TArrayView<const FMeshVertex>( Corners, 4 ), TArrayView<const uint32>( Good, 3 ) ).IsValid(),
					 "a buffer is made only from a multiple of 3 indices that all point at a vertex" );
		}

		// 行列: 同じ Span のあとの 3D 図形は、メッシュの行列の影響を受けない（WorldMatrix で決めた行列は、そのまま）
		{
			const FMeshRenderer Renderer = NWorld::MakeMeshRenderer( MakeFace( FrontQuad, false, FString(), FColor32( 255, 0, 0 ), false ), nullptr );
			ClearDrawScreen();
			SetCameraNearFar( 0.1f, 100.0f );
			SetupCamera_Ortho( 8.0f );
			SetCameraPositionAndTarget_UpVecY( Front, VGet( 0.0f, 0.0f, 0.0f ) );
			SetUseLighting( FALSE );
			FDrawBuffer Buffer;
			FDrawContext Draw( Buffer );
			Draw.WorldMatrix( FMatrix4x4::Translate( FVector3( 0.0f, -3.0f, 0.0f ) ) );   // 先に決めた行列: このあとの線は、3 下がる
			Renderer.Record( Draw, FTransform( FVector3( 2.0f, 2.0f, 0.0f ) ) );          // メッシュは、自分の行列で描く
			Draw.Line3D( FVector3( -1.0f, 0.0f, 0.0f ), FVector3( 1.0f, 0.0f, 0.0f ), FColor32( 0, 255, 0 ) );   // 先に決めた行列だけが掛かる: y = -3 のところ（画面の y = 112）
			ExecuteBuffer( *Executor, Buffer );
			Capture.Read();
			C.Check( Capture.IsColor( 64, 112, 0, 255, 0, 8 ) && Capture.IsBlack( 64, 64 ) && Capture.IsColor( 96, 32, 255, 0, 0, 6 ), "a mesh drawn between a WorldMatrix and a line: the mesh uses its own matrix (red at (96, 32)), and the line gets the earlier WorldMatrix only (green at y = 112, not moved by the mesh)" );
		}

		// 照明: 法線が、表の側を向いている（カメラの方を向く面が、カメラから差す光で明るい）
		{
			const VECTOR OldDirection = GetLightDirection();
			ChangeLightTypeDir( VGet( 0.0f, 0.0f, 1.0f ) );   // カメラ（-Z）から、+Z の向きに差す
			FMeshRenderer Renderer = NWorld::MakeMeshRenderer( MakeFace( FrontQuad, false, FString(), FColor32( 255, 255, 255 ), true ), nullptr );
			FMeshRenderer Back = NWorld::MakeMeshRenderer( MakeFace( FrontQuad, true, FString(), FColor32( 255, 255, 255 ), true ), nullptr );
			const auto Lit = [&]( const FMeshRenderer& Mesh )
			{
				ClearDrawScreen();
				SetCameraNearFar( 0.1f, 100.0f );
				SetupCamera_Ortho( 8.0f );
				SetCameraPositionAndTarget_UpVecY( Front, VGet( 0.0f, 0.0f, 0.0f ) );
				SetUseLighting( TRUE );
				FDrawBuffer Buffer;
				FDrawContext Draw( Buffer );
				Mesh.Record( Draw, FTransform::Identity );
				ExecuteBuffer( *Executor, Buffer );
				Capture.Read();
				SetUseLighting( FALSE );
				int R3 = 0, G3 = 0, B3 = 0;
				Capture.At( 64, 64, R3, G3, B3 );
				return R3;
			};
			const int FrontBrightness = Renderer.IsValid() ? Lit( Renderer ) : -1;
			const int BackBrightness = Back.IsValid() ? Lit( Back ) : -1;
			ChangeLightTypeDir( OldDirection );
			C.Check( FrontBrightness > 150 && BackBrightness < FrontBrightness - 60, "lit by a light from the camera, a face looking at it is bright (%d) and the same face turned around is dark (%d): the normals follow the front side", FrontBrightness, BackBrightness );
		}
		SetUseBackCulling( DX_CULLING_NONE );
	}

	Capture.End();

	// ワールドエディタの計算を、本物の DxLib の絵と突き合わせる
	NWorldTest::RunEditorRealTests( C, *Assets, *Executor, Dir, Texture );

	// エフェクトの粒子（組み込みの丸・加算 / アルファ・寿命の間の変化・回転・位置）を、本物の絵で
	NWorldTest::RunEffectRealTests( C, *Assets, *Executor, Dir );
	NWorldTest::RunEffectEditorRealTests( C, *Assets, *Executor, Dir );
	NWorldTest::RunEffectForceFieldPreviewRealTests( C, *Assets, *Executor );
	NWorldTest::RunEffectTargetSceneEditorTests( C, *Assets );
	NWorldTest::RunEffectForceFieldEditorPresetRealTests( C, *Assets, *Executor );
	NWorldTest::RunWorldMeshMaterialEditorTests( C, *Assets, Dir );
	NWorldTest::RunMeshMaterialPlaybackTests( C, *Assets, Dir );
	NWorldTest::RunWorldModelMaterialSpawnRealChecks( C, *Assets, *Executor, Dir );
	NWorldTest::RunWorldModelPreviewRealChecks( C, *Assets, *Executor, Dir );
	NWorldTest::RunWorldModelInspectorEditorChecks( C, *Assets, *Executor, Dir );
	NWorldTest::RunEffectPathPlaybackTests( C, *Assets, Dir );
	NWorldTest::RunEffectMaterialPlaybackTests( C, *Assets, Dir );
	NWorldTest::RunEffectUVDistortionPlaybackTests( C, *Assets, Dir );
	NWorldTest::RunEffectRibbonUVDistortionPlaybackTests( C, *Assets, Dir );
	NWorldTest::RunEffectMaterialPathPlaybackTests( C, *Assets, Dir );
	NWorldTest::RunEffectAppearancePlaybackTests( C, *Assets, Dir );
	NWorldTest::RunEffectFlipbookPlaybackTests( C, *Assets, Dir );
	NWorldTest::RunEffectAppearanceBoundaryTests( C, *Assets, Dir );
	NWorldTest::RunEffectRibbonRealTests( C, *Executor );
	NWorldTest::RunEffectMaterialMeshRealTests( C, *Executor );
	NWorldTest::RunMeshMaterialRealTests( C, *Assets, *Executor, Dir );
	NWorldTest::RunEffectMaterialRealTests( C, *Executor );
	NWorldTest::RunEffectUVDistortionRealTests( C, *Assets, *Executor, Dir );
	NWorldTest::RunEffectRibbonUVDistortionRealTests( C, *Assets, *Executor, Dir );
	NWorldTest::RunEffectMaterialRibbonRealTests( C, *Executor );
	NWorldTest::RunEffectAppearanceRealTests( C, *Assets, *Executor, Dir );
	NWorldTest::RunEffectFlipbookRealTests( C, *Assets, *Executor, Dir );
	NWorldTest::RunEffectTreeLightningPlaybackTests( C, *Assets, Dir );
	NWorldTest::RunEffectTreeLightningRealTests( C, *Assets, *Executor, Dir );

	// 本物のモデルの三角形（DxLib）で、メッシュの当たり判定を作り、本物の物理（Jolt）で受け止める。向き・大きさ・位置が、描くものと合っている
	{
		C.Group = "mesh-collider-real";
		CPhysicsSubsystem* Physics = GetSubsystem<CPhysicsSubsystem>();
		// 底が y = 0、幅・奥行き 2、高さ 2 の箱
		WriteFileText( Dir / "block.obj", "o Block\nv -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv -1 2 -1\nv 1 2 -1\nv 1 2 1\nv -1 2 1\nf 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 4 8 5 1\n" );
		const TSharedPtr<CModel> Block = Assets->LoadModel( "block.obj" );
		TArray<FVector3> Vertices;
		TArray<uint32> Indices;
		const bool bTriangles = Block.IsValid() && Block->GetCollisionMesh( Vertices, Indices );
		C.Check( bTriangles && Indices.Num() >= 36 && Indices.Num() % 3 == 0, "the DxLib model gives its triangles (a block has at least 12: %d)", Indices.Num() / 3 );
		FVector3 Low( 1.0e9f, 1.0e9f, 1.0e9f ), High( -1.0e9f, -1.0e9f, -1.0e9f );
		bool bIndicesOk = true;
		for ( const FVector3& Vertex : Vertices )
		{
			Low = FVector3( std::min( Low.x, Vertex.x ), std::min( Low.y, Vertex.y ), std::min( Low.z, Vertex.z ) );
			High = FVector3( std::max( High.x, Vertex.x ), std::max( High.y, Vertex.y ), std::max( High.z, Vertex.z ) );
		}
		for ( const uint32 Index : Indices ) bIndicesOk = bIndicesOk && Index < static_cast<uint32>( Vertices.Num() );
		FVector3 BoundsMin, BoundsMax;
		C.Check( bTriangles && bIndicesOk && Near3( Low, FVector3( -1.0f, 0.0f, -1.0f ), 2.0e-3f ) && Near3( High, FVector3( 1.0f, 2.0f, 1.0f ), 2.0e-3f ) && Block->GetBounds( BoundsMin, BoundsMax ) && Near3( BoundsMin, Low, 2.0e-3f ) && Near3( BoundsMax, High, 2.0e-3f ),
				 "the triangles span (-1,0,-1)..(1,2,1), the model's bounds, and every index is a vertex (%s .. %s)", *Low.ToString(), *High.ToString() );

		if ( bTriangles && Physics != nullptr )
		{
			// 位置 (0,1,0)・回転 30 度・拡大 (2,1,2) で置く: 上面は y = 3、幅は ±2
			FWorldDefinition World;
			FWorldObject Object = MakeObject( 1, "Block" );
			Object.Transform.Position = FVector3( 0.0f, 1.0f, 0.0f );
			Object.Transform.RotationDegrees = FVector3( 0.0f, 30.0f, 0.0f );
			Object.Transform.Scale = FVector3( 2.0f, 1.0f, 2.0f );
			FWorldModel Model;
			Model.Path = "block.obj";
			Object.Model = Model;
			FWorldCollider Collider;
			Collider.Shape = EWorldColliderShape::Mesh;
			Object.Collider = Collider;
			World.Objects.Add( Object );
			World.NextId = 2;
			FWorldSpawnOptions Options;
			Options.Registry = &S.Registry;
			Options.Root = FTransform( FVector3( 5000.0f, 0.0f, 0.0f ) );
			FWorldInstance Level = NWorld::Spawn( *this, World, FWorldBindings(), Options );
			const TSharedPtr<CGameObject> Placed = Level.FindByName( "Block" );
			const CWorldColliderComponent* Body = Placed.IsValid() ? Placed->GetComponent<CWorldColliderComponent>() : nullptr;
			C.Check( Level.GetReport().Warnings.IsEmpty() && Body != nullptr && Body->GetBody() != nullptr && Body->GetBody()->GetBodyType() == EBodyType::Static, "the block is placed with a static mesh body and no warnings (%d)", Level.GetReport().Warnings.Num() );
			if ( Body != nullptr && Body->GetBody() != nullptr )
			{
				const JPH::Vec3 Extent = Physics->GetBodyInterface().GetShape( Body->GetBody()->GetBodyID() )->GetLocalBounds().GetExtent();
				C.Check( std::fabs( Extent.GetX() - 2.0f ) < 0.05f && std::fabs( Extent.GetY() - 1.0f ) < 0.05f && std::fabs( Extent.GetZ() - 2.0f ) < 0.05f, "its shape is the block scaled by (2,1,2): half extents (2,1,2) (%.3f, %.3f, %.3f)", Extent.GetX(), Extent.GetY(), Extent.GetZ() );
			}
			FRigidBody* Hit = Physics->CreateBody( FCollisionShape::Sphere( 0.5f ), EBodyType::Dynamic, FTransform( FVector3( 5001.5f, 6.0f, 0.0f ) ) );
			FRigidBody* Miss = Physics->CreateBody( FCollisionShape::Sphere( 0.5f ), EBodyType::Dynamic, FTransform( FVector3( 5003.5f, 6.0f, 0.0f ) ) );
			for ( int32 i = 0; i < 240; ++i ) Physics->Update( 1.0f / 60.0f );
			C.Check( Hit != nullptr && std::fabs( Hit->GetPosition().y - 3.5f ) < 0.05f && Hit->GetLinearVelocity().Length() < 0.2f, "a ball dropped on the block's top face (the model drawn at y = 1 and 2 high) rests on it (y %.3f)", Hit != nullptr ? Hit->GetPosition().y : -999.0f );
			C.Check( Miss != nullptr && Miss->GetPosition().y < 0.0f, "and one dropped beside the turned, scaled block falls past it (y %.3f)", Miss != nullptr ? Miss->GetPosition().y : -999.0f );
			if ( Hit != nullptr ) Physics->DestroyBody( Hit );
			if ( Miss != nullptr ) Physics->DestroyBody( Miss );
			Level.Despawn();
		}
	}

	SetUseLighting( TRUE );
	SetDrawScreen( DX_SCREEN_BACK );
	SetCameraNearFar( NearClip, FarClip );
	Assets->SetRootDirectory( "Assets" );
}

void CWorldSelfTestScene::Finish()
{
	FState& S = *State;
	FChecker& C = S.Checker;
	RemoveDirectoryRecursive( S.TempRoot );
	if ( C.NumFailed == 0 )
	{
		LOG( LogWorldSelfTest, Log, "SELFTEST world PASS: %lld check(s), %lld skipped", static_cast<long long>( C.NumChecked ), static_cast<long long>( C.NumSkipped ) );
	}
	else
	{
		LOG( LogWorldSelfTest, Error, "SELFTEST world FAIL: %lld of %lld check(s) failed", static_cast<long long>( C.NumFailed ), static_cast<long long>( C.NumChecked ) );
	}
	if ( S.bExitWhenDone )
	{
		if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->RequestExit();
	}
}
