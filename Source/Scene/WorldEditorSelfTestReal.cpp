#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/WorldEditor.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/Billboard.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/Editor/EditorPicking.h"
#include "Library/Framework/World/WorldRender.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "WorldEditorSelfTestSupport.h"

using namespace NAssetTest;
using namespace NWorldEditorTest;

//=============================================================================
// ワールドエディタの計算を、本物の DxLib の絵と突き合わせる
//   - カメラ: FEditorCamera の画面への投影・光線が、DxLib の ConvWorldPosToScreenPos / ConvScreenPosToWorldPos と合う
//   - ビルボード: 描いた絵（ピクセル）の範囲が、エディタが「選べる」と考える板（EditorShapes）と合う。板の上をクリックすれば、板が選ばれる
//   - モデル: 読み込んだモデルの大きさ（箱）と、エディタが出す行列で描いた絵の範囲が合う
//   画面に描いた結果を数で比べる（見た目の確認でなく、座標の約束が、描画側と編集側で食い違っていないことの確認）。
//=============================================================================
namespace
{
	struct FShot
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
		// 光っている（黒でない）ピクセルの外接矩形。OutRight / OutBottom は、最後に光っているピクセルの次（辺の位置）
		bool LitBounds( float32& OutLeft, float32& OutTop, float32& OutRight, float32& OutBottom ) const
		{
			int Left = Width, Top = Height, Right = -1, Bottom = -1;
			for ( int Y = 0; Y < Height; ++Y )
			{
				for ( int X = 0; X < Width; ++X )
				{
					int R = 0, G = 0, B = 0, A = 0;
					GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A );
					if ( R + G + B <= 6 ) continue;
					Left = NMath::Min( Left, X );
					Top = NMath::Min( Top, Y );
					Right = NMath::Max( Right, X );
					Bottom = NMath::Max( Bottom, Y );
				}
			}
			if ( Right < 0 ) return false;
			OutLeft = static_cast<float32>( Left );
			OutTop = static_cast<float32>( Top );
			OutRight = static_cast<float32>( Right + 1 );
			OutBottom = static_cast<float32>( Bottom + 1 );
			return true;
		}
		void End()
		{
			if ( Soft >= 0 ) DeleteSoftImage( Soft );
			if ( Screen >= 0 ) DeleteGraph( Screen );
			Soft = -1;
			Screen = -1;
		}
		~FShot() { End(); }
	};

	void SetDxCamera( const FEditorCamera& Camera, const FEditorViewport& View )
	{
		SetCameraNearFar( 0.1f, 1000.0f );
		SetupCamera_Perspective( View.FovY );
		SetCameraPositionAndTargetAndUpVec( Camera.GetEye(), Camera.Target, FVector3::Up );
	}

	// 点の集まりを投影した、画面での外接矩形（辺の位置）
	bool ProjectedBounds( const FEditorCamera& Camera, const FEditorViewport& View, const TArray<FVector3>& Points, float32& OutLeft, float32& OutTop, float32& OutRight, float32& OutBottom )
	{
		OutLeft = OutTop = NMath::MaxFloat;
		OutRight = OutBottom = -NMath::MaxFloat;
		for ( const FVector3& Point : Points )
		{
			FVector2 Pixel;
			if ( !Camera.WorldToScreen( View, Point, Pixel ) ) return false;
			OutLeft = NMath::Min( OutLeft, Pixel.x );
			OutTop = NMath::Min( OutTop, Pixel.y );
			OutRight = NMath::Max( OutRight, Pixel.x );
			OutBottom = NMath::Max( OutBottom, Pixel.y );
		}
		return true;
	}

	bool SameBounds( float32 AL, float32 AT, float32 AR, float32 AB, float32 BL, float32 BT, float32 BR, float32 BB, float32 Tolerance )
	{
		return NMath::Abs( AL - BL ) <= Tolerance && NMath::Abs( AT - BT ) <= Tolerance && NMath::Abs( AR - BR ) <= Tolerance && NMath::Abs( AB - BB ) <= Tolerance;
	}

	//=========================================================================
	// カメラ
	//=========================================================================
	void TestCameraAgainstDxLib( NAssetTest::FChecker& C )
	{
		C.Group = "editor-dxlib-camera";
		FShot Shot;
		if ( !Shot.Begin( 400, 200 ) )
		{
			C.Skip( "no capture surface" );
			return;
		}
		const FEditorViewport View = MakeViewport( 400, 200 );
		const FEditorCamera Cameras[3] = { MakeCamera( 0.6f, 0.45f, 12.0f, FVector3( 1, 2, 3 ) ), MakeCamera( -2.0f, -0.3f, 5.0f, FVector3( -4, 0, 5 ) ), MakeCamera( 3.5f, 1.0f, 30.0f ) };
		const FVector3 Offsets[6] = { FVector3( 0, 0, 0 ), FVector3( 1.5f, 0, 0 ), FVector3( 0, 1.2f, 0 ), FVector3( 0, 0, 2.0f ), FVector3( -2.0f, -1.0f, 1.0f ), FVector3( 3.0f, 2.0f, -2.0f ) };

		float32 MaxPixelError = 0.0f;
		float32 MaxRayError = 0.0f;
		int32 NumPoints = 0;
		for ( const FEditorCamera& Camera : Cameras )
		{
			SetDxCamera( Camera, View );
			for ( const FVector3& Offset : Offsets )
			{
				const FVector3 Point = Camera.Target + Offset;
				FVector2 Mine;
				if ( !Camera.WorldToScreen( View, Point, Mine ) ) continue;
				const VECTOR Theirs = ConvWorldPosToScreenPos( Point );
				MaxPixelError = NMath::Max( MaxPixelError, NMath::Max( NMath::Abs( Theirs.x - Mine.x ), NMath::Abs( Theirs.y - Mine.y ) ) );
				++NumPoints;

				// 逆: 画面の点から、近い面の上の点へ。視点からその点への向きが、ScreenToRay の向き
				const FEditorRay Ray = Camera.ScreenToRay( View, Mine.x, Mine.y );
				const VECTOR Near = ConvScreenPosToWorldPos( VGet( Mine.x, Mine.y, 0.0f ) );
				const FVector3 Direction = ( FVector3( Near ) - Camera.GetEye() ).Normalized();
				MaxRayError = NMath::Max( MaxRayError, ( Direction - Ray.Direction ).Length() );
			}
		}
		C.Check( NumPoints >= 15, "enough points were in front of the cameras (%d)", NumPoints );
		C.Check( MaxPixelError < 0.75f, "FEditorCamera::WorldToScreen agrees with DxLib's ConvWorldPosToScreenPos to within %.3f pixels (3 cameras x up to 6 points, a 400x200 view)", MaxPixelError );
		C.Check( MaxRayError < 2.0e-3f, "FEditorCamera::ScreenToRay points where DxLib's ConvScreenPosToWorldPos says (largest difference %.5f)", MaxRayError );

		// 縦横の比が違う画面でも
		Shot.End();
		if ( Shot.Begin( 200, 400 ) )
		{
			const FEditorViewport Tall = MakeViewport( 200, 400 );
			const FEditorCamera& Camera = Cameras[0];
			SetDxCamera( Camera, Tall );
			float32 Error = 0.0f;
			for ( const FVector3& Offset : Offsets )
			{
				FVector2 Mine;
				if ( !Camera.WorldToScreen( Tall, Camera.Target + Offset, Mine ) ) continue;
				const VECTOR Theirs = ConvWorldPosToScreenPos( Camera.Target + Offset );
				Error = NMath::Max( Error, NMath::Max( NMath::Abs( Theirs.x - Mine.x ), NMath::Abs( Theirs.y - Mine.y ) ) );
			}
			C.Check( Error < 0.75f, "and in a tall 200x400 view (the field of view is the vertical one) (%.3f pixels)", Error );
		}
		Shot.End();
	}

	//=========================================================================
	// ビルボード: 描いた絵と、エディタが選べる板
	//=========================================================================
	void TestBillboardAgainstPixels( NAssetTest::FChecker& C, IDrawExecutor& Executor, const TSharedPtr<CTexture>& Texture )
	{
		C.Group = "editor-dxlib-billboard";
		FShot Shot;
		if ( !Texture.IsValid() || !Shot.Begin( 320, 240 ) )
		{
			C.Skip( "no texture or capture surface" );
			return;
		}
		const FEditorViewport View = MakeViewport( 320, 240 );
		const FEditorCamera Camera = MakeCamera( 0.6f, 0.45f, 8.0f, FVector3( 0, 1, 0 ) );

		struct FCase
		{
			const char* Name;
			EWorldBillboardMode Mode;
			FTransform Transform;
		};
		const FCase Cases[] = {
			{ "Camera", EWorldBillboardMode::Camera, FTransform( FVector3( 1, 1, 2 ), FQuaternion::FromAxisAngle( FVector3::Forward, 0.35f ) ) },
			{ "CameraY", EWorldBillboardMode::CameraY, FTransform( FVector3( -1, 0.5f, 1 ), FQuaternion::Identity, FVector3( 1.3f, 1.3f, 1.0f ) ) },
			{ "World", EWorldBillboardMode::World, FTransform( FVector3( 0, 1, 0 ), FQuaternion::EulerToQuat( FVector3( 0.25f, 0.7f, 0.0f ) ) ) },
		};

		for ( const FCase& Case : Cases )
		{
			FWorldBillboard Spec;
			Spec.Texture = "quad.bmp";
			Spec.Size = FVector2( 2.0f, 1.5f );
			Spec.Pivot = FVector2( 0.3f, 0.7f );
			Spec.Mode = Case.Mode;

			Shot.End();
			Shot.Begin( 320, 240 );
			SetDxCamera( Camera, View );
			SetUseLighting( FALSE );
			FDrawBuffer Buffer;
			FDrawContext Draw( Buffer );
			NWorld::MakeBillboard( Spec, Texture ).Record( Draw, Case.Transform );
			Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
			Shot.Read();
			float32 LitL, LitT, LitR, LitB;
			const bool bLit = Shot.LitBounds( LitL, LitT, LitR, LitB );

			FWorldObject Object;
			Object.Id = 1;
			Object.Billboard = Spec;
			FEditorShapeContext Context;
			Context.Camera = &Camera;
			Context.Viewport = View;
			const TArray<FEditorShape> Shapes = NWorldEditor::GatherShapes( Object, Case.Transform, Context );
			TArray<FVector3> Corners;
			if ( Shapes.Num() == 1 && Shapes[0].Kind == EEditorShapeKind::Quad )
			{
				const FEditorShape& Quad = Shapes[0];
				for ( const float32 A : { -1.0f, 1.0f } )
					for ( const float32 B : { -1.0f, 1.0f } ) Corners.Add( Quad.Center + Quad.HalfRight * A + Quad.HalfUp * B );
			}
			float32 EditL, EditT, EditR, EditB;
			const bool bProjected = !Corners.IsEmpty() && ProjectedBounds( Camera, View, Corners, EditL, EditT, EditR, EditB );
			C.Check( bLit && bProjected && SameBounds( LitL, LitT, LitR, LitB, EditL, EditT, EditR, EditB, 2.5f ),
					 "%s: the pixels drawn (x %.1f..%.1f, y %.1f..%.1f) are the area the editor projects for the billboard (x %.1f..%.1f, y %.1f..%.1f)", Case.Name, LitL, LitR, LitT, LitB, EditL, EditR, EditT, EditB );
			if ( !bLit || !bProjected ) continue;

			// 絵の上をクリックすると板が選ばれ、絵の外ならはずれる
			float32 T = 0.0f;
			const FEditorRay Inside = Camera.ScreenToRay( View, ( LitL + LitR ) * 0.5f, ( LitT + LitB ) * 0.5f );
			const FEditorRay Outside = Camera.ScreenToRay( View, NMath::Max( LitL - 8.0f, 1.0f ), ( LitT + LitB ) * 0.5f );
			C.Check( NWorldEditor::RayVsShape( Inside, Shapes[0], T ) && !NWorldEditor::RayVsShape( Outside, Shapes[0], T ), "%s: a click on the drawn billboard hits its shape, and a click 8 pixels left of it does not", Case.Name );
		}
		Shot.End();
		SetUseLighting( TRUE );
	}

	//=========================================================================
	// モデル: 読み込んだ大きさと、エディタが出す行列で描いた絵
	//=========================================================================
	void TestModelAgainstPixels( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
	{
		C.Group = "editor-dxlib-model";
		// 底が y=0、幅・奥行き 2、高さ 2 の箱（OBJ）。読み込むとき .mv1 に変換される
		{
			const FString Path = Dir + "\\cube.obj";
			FILE* File = nullptr;
			if ( fopen_s( &File, *Path, "wb" ) != 0 || File == nullptr )
			{
				C.Skip( "cannot write the model file" );
				return;
			}
			const char* Text = "o Cube\nv -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv -1 2 -1\nv 1 2 -1\nv 1 2 1\nv -1 2 1\n"
							   "f 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 4 8 5 1\n";
			fwrite( Text, 1, std::strlen( Text ), File );
			fclose( File );
		}

		FAssetEditorInfo Info( Assets );
		FVector3 Min, Max;
		if ( !Info.GetModelBounds( "cube.obj", Min, Max ) )
		{
			C.Check( false, "the model's bounds could not be read (the model did not load?)" );
			return;
		}
		C.Check( Near3( Min, FVector3( -1, 0, -1 ), 2e-3f ) && Near3( Max, FVector3( 1, 2, 1 ), 2e-3f ), "the bounds of the loaded box are (-1,0,-1)..(1,2,1) (got %s .. %s)", *Min.ToString(), *Max.ToString() );
		const TSharedPtr<CModelInstance> Instance = Info.GetModelInstance( "cube.obj" );
		C.Check( Instance.IsValid(), "the editor gets an instance to draw" );
		C.Check( Info.GetModelInstance( "cube.obj" ).Get() == Instance.Get() && !Info.GetModelInstance( "missing.mv1" ).IsValid() && !Info.GetModelBounds( "missing.mv1", Min, Max ), "the same instance each time, and nothing for a missing file (remembered, so the log has one line)" );
		if ( !Instance.IsValid() ) return;

		// 当たり判定のための三角形（エディタが、読み込んだモデルから取る）
		const FEditorCollisionMesh* CubeMesh = Info.GetCollisionMesh( "cube.obj" );
		C.Check( CubeMesh != nullptr && CubeMesh->GetNumTriangles() >= 12 && Info.GetCollisionMesh( "cube.obj" ) == CubeMesh && Info.GetCollisionMesh( "missing.mv1" ) == nullptr && Info.GetCollisionMesh( "missing.mv1" ) == nullptr,
				 "the editor gets the box's triangles (at least 12), the same ones each time, and none for a missing file" );
		TArray<FVector3> MeshLines;
		const bool bAllDrawn = CubeMesh != nullptr && NWorldEditor::AppendMeshWireLines( *CubeMesh, FTransform::Identity, 1000, MeshLines );
		bool bInsideBounds = !MeshLines.IsEmpty();
		for ( const FVector3& Point : MeshLines ) bInsideBounds = bInsideBounds && Point.x >= Min.x - 2e-3f && Point.y >= Min.y - 2e-3f && Point.z >= Min.z - 2e-3f && Point.x <= Max.x + 2e-3f && Point.y <= Max.y + 2e-3f && Point.z <= Max.z + 2e-3f;
		C.Check( bAllDrawn && bInsideBounds && MeshLines.Num() >= 24, "and its outline (%d points) stays within the model's bounds", MeshLines.Num() );
		Info.Clear();
		C.Check( Info.GetCollisionMesh( "cube.obj" ) != nullptr && Info.GetModelBounds( "cube.obj", Min, Max ), "after Clear the triangles are read again" );

		FShot Shot;
		if ( !Shot.Begin( 320, 240 ) )
		{
			C.Skip( "no capture surface" );
			return;
		}
		const FEditorViewport View = MakeViewport( 320, 240 );
		const FEditorCamera Camera = MakeCamera( 0.6f, 0.45f, 10.0f, FVector3( 1, 1, 1 ) );
		const FTransform Placed( FVector3( 2, 0.5f, 1 ), FQuaternion::FromAxisAngle( FVector3::Up, 0.52f ), FVector3( 1.0f, 1.5f, 1.2f ) );

		SetDxCamera( Camera, View );
		SetUseLighting( FALSE );
		SetUseZBuffer3D( TRUE );
		SetWriteZBuffer3D( TRUE );
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Draw.Model( Instance->GetHandle(), Placed.ToMatrix() );
		Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		Shot.Read();
		SetUseZBuffer3D( FALSE );
		SetWriteZBuffer3D( FALSE );
		SetUseLighting( TRUE );
		float32 LitL, LitT, LitR, LitB;
		const bool bLit = Shot.LitBounds( LitL, LitT, LitR, LitB );

		// エディタの箱（ワールドでの 8 つの角）を投影した範囲
		FWorldObject Object;
		Object.Id = 1;
		FWorldModel Spec;
		Spec.Path = "cube.obj";
		Object.Model = Spec;
		FEditorShapeContext Context;
		Context.Assets = &Info;
		Context.Camera = &Camera;
		Context.Viewport = View;
		const TArray<FEditorShape> Shapes = NWorldEditor::GatherShapes( Object, Placed, Context );
		TArray<FVector3> Corners;
		if ( Shapes.Num() == 1 && Shapes[0].Kind == EEditorShapeKind::Box )
		{
			TArray<FVector3> Lines;
			NWorldEditor::AppendShapeWireLines( Shapes[0], 8, Lines );
			Corners = Lines;
		}
		float32 EditL, EditT, EditR, EditB;
		const bool bProjected = !Corners.IsEmpty() && ProjectedBounds( Camera, View, Corners, EditL, EditT, EditR, EditB );
		C.Check( bLit && bProjected && SameBounds( LitL, LitT, LitR, LitB, EditL, EditT, EditR, EditB, 2.5f ),
				 "the pixels of the model drawn with the editor's matrix (x %.1f..%.1f, y %.1f..%.1f) are the area of its bounds box (x %.1f..%.1f, y %.1f..%.1f)", LitL, LitR, LitT, LitB, EditL, EditR, EditT, EditB );
		if ( !bLit || Shapes.IsEmpty() ) return;
		float32 T = 0.0f;
		C.Check( NWorldEditor::RayVsShape( Camera.ScreenToRay( View, ( LitL + LitR ) * 0.5f, ( LitT + LitB ) * 0.5f ), Shapes[0], T ) && !NWorldEditor::RayVsShape( Camera.ScreenToRay( View, NMath::Max( LitL - 8.0f, 1.0f ), ( LitT + LitB ) * 0.5f ), Shapes[0], T ),
				 "a click on the drawn model hits its box, and a click 8 pixels beside it does not" );
		Shot.End();
	}
}

namespace
{
	// ワールドを開く・新規作成するとき、同じ Id の粒子や終了後の待ち時間を引き継がないことを実際の再生で確かめる。
	// 保存・読み込みに失敗した場合は、その項目を失敗として記録して抜ける。
	void TestEffectPreviewDocumentReplacement( NAssetTest::FChecker& C, CAssetManager& Assets )
	{
		C.Group = "editor-effect-document";
		// 生まれたワールド位置に留まる、長生きする粒子を一度だけ出す。
		FParticleEmitterDefinition Emitter;
		Emitter.Rate = 0.0f;
		Emitter.Speed = { 0.0f, 0.0f };
		Emitter.Lifetime = { 10.0f, 10.0f };
		Emitter.Gravity = FVector3::Zero;
		Emitter.Space = EParticleSpace::World;
		Emitter.Bursts.Add( FParticleBurst { 0.0f, 1 } );
		// 両方のワールドで同じ定義を使い、部品のハッシュが変わらない条件を作る。
		FParticleEffectDefinition Effect;
		Effect.Emitters.Add( Emitter );
		// Id と部品を共通にし、置く位置だけが異なるワールドを用意する。
		FWorldObject Object;
		Object.Id = 1;
		Object.Transform.Position = FVector3( 1.0f, 2.0f, 3.0f );
		Object.Effect = FWorldEffect();
		Object.Effect.GetValue().Path = "fx/editor-document.effect.json";
		// 最初に読み込むワールド。
		FWorldDefinition First;
		First.Objects.Add( Object );
		// 同じ Id と部品を持ち、位置だけが違う切り替え先。
		FWorldDefinition Second = First;
		Second.Objects[0].Transform.Position = FVector3( 40.0f, 50.0f, 60.0f );
		// 保存ができなければ、その後の再生の確認は行わない。
		const bool bSaved = Assets.SaveEffect( "fx/editor-document.effect.json", Effect ) && Assets.SaveWorld( "Levels/EffectFirst.world", First ) && Assets.SaveWorld( "Levels/EffectSecond.world", Second );
		C.Check( bSaved, "文書を切り替える確認用のエフェクトと二つのワールドを保存できる" );
		if ( !bSaved ) return;

		// 画面のプレビューと同じ経路で、最初のワールドの粒子を出す。
		FWorldEditorState State;
		// 読み込みの成否も、再生結果と一緒に確認する。
		const bool bOpenedFirst = NDebugUI::OpenWorldFile( State, "Levels/EffectFirst.world" );
		NWorldEditorUI::UpdateEffectPreviews( State, 0.1f );
		FWorldEditorState::FEffectPreview* Preview = State.EffectPreviews.Find( Object.Id );   // 現在の Id に対応する再生
		C.Check( bOpenedFirst && Preview != nullptr && Preview->Playback.IsValid() && Preview->Playback->GetNumParticles() == 1, "最初のワールドで一つの粒子を再生する" );
		if ( Preview == nullptr || !Preview->Playback.IsValid() ) return;
		// 弱参照で、文書を置き換えた直後に以前の再生が解放されることも確認する。
		TWeakPtr<IEffectPlayback> Previous = Preview->Playback;
		C.Check( !NDebugUI::OpenWorldFile( State, "Levels/EffectMissing.world" ) && Previous.IsValid() && State.EffectPreviews.Find( Object.Id ) != nullptr && State.EffectPreviews.Find( Object.Id )->Playback == Previous.Pin(), "開く処理の失敗では、いまのワールドの再生を保つ" );
		C.Check( NDebugUI::OpenWorldFile( State, "Levels/EffectSecond.world" ) && State.EffectPreviews.IsEmpty() && !Previous.IsValid(), "別のワールドを開いた直後に、以前の粒子の再生を解放する" );
		NWorldEditorUI::UpdateEffectPreviews( State, 0.1f );
		Preview = State.EffectPreviews.Find( Object.Id );
		// 内蔵の再生を使う条件なので、粒子の実際のワールド位置を取り出して比べる。
		TArray<NParticle::FDrawParticle> Particles;
		if ( Preview != nullptr && Preview->Playback.IsValid() ) NParticle::CollectDrawParticles( static_cast<const FParticlePlayback*>( Preview->Playback.Get() )->GetSimulation(), Particles );
		C.Check( Particles.Num() == 1 && Near3( Particles[0].Position, Second.Objects[0].Transform.Position ), "Id と部品が同じでも、新しいワールドの位置から粒子が生まれる" );
		if ( Preview == nullptr || !Preview->Playback.IsValid() ) return;

		// 終了後の待ち時間がある状態でも、開き直した文書は最初から再生する。
		Preview->Playback->Stop( true );
		NWorldEditorUI::UpdateEffectPreviews( State, 0.1f );
		C.Check( Preview->FinishedTime > 0.0f && Preview->Playback->GetNumParticles() == 0, "終了した再生は、やり直すまでの待ち時間を持つ" );
		Previous = Preview->Playback;
		C.Check( NDebugUI::OpenWorldFile( State, "Levels/EffectFirst.world" ) && !Previous.IsValid(), "開き直すと、終了後の待ち時間を持つ再生も解放する" );
		NWorldEditorUI::UpdateEffectPreviews( State, 0.1f );
		Preview = State.EffectPreviews.Find( Object.Id );
		C.Check( Preview != nullptr && Preview->FinishedTime == 0.0f && Preview->Playback.IsValid() && Preview->Playback->GetNumParticles() == 1, "開き直したワールドは、以前の終了時刻に関係なく最初から再生する" );
		if ( Preview == nullptr || !Preview->Playback.IsValid() ) return;

		// 新規作成も、次のフレームを待たずに前の粒子を手放す。
		Previous = Preview->Playback;
		NDebugUI::NewWorld( State );
		C.Check( State.Session.Document.Get().Objects.IsEmpty() && State.EffectPreviews.IsEmpty() && !Previous.IsValid(), "新規作成の直後に、前の文書の再生を解放する" );
	}

	//=========================================================================
	// ファイル操作（新規・保存・開く）
	//=========================================================================
	void TestFileOperations( NAssetTest::FChecker& C, CAssetManager& Assets )
	{
		C.Group = "editor-files";
		FWorldEditorState A;
		A.Session.Reset( FWorldDefinition(), FString() );
		const int32 Spawn = A.Session.CreateEmpty( FVector3( 1, 2, 3 ), "Spawn" );
		A.Session.CreateCollider( EWorldColliderShape::Box, true, FVector3( 0, 0, 5 ) );
		NWorldEdit::AddTag( A.Session.Document, Spawn, "PlayerStart" );
		NWorldEdit::SetCollisionRule( A.Session.Document, "Decoration", false );
		C.Check( A.Session.Document.IsDirty() && A.Session.Document.Get().Objects.Num() == 2, "a new session with two objects is unsaved" );

		C.Check( !NDebugUI::SaveWorldFile( A ) && A.Session.FilePath.IsEmpty() && A.Session.Document.IsDirty(), "saving a world that has no path does nothing" );
		C.Check( NDebugUI::SaveWorldFile( A, "Levels\\Out" ) && A.Session.FilePath == "Levels/Out.world" && !A.Session.Document.IsDirty() && !A.bStatusIsError, "Save As adds the .world extension and forward slashes, and marks the document saved (%s)", *A.StatusMessage );

		FWorldEditorState B;
		C.Check( NDebugUI::OpenWorldFile( B, "Levels/Out.world" ) && B.Session.FilePath == "Levels/Out.world" && !B.Session.Document.IsDirty() && !B.Session.Document.CanUndo(), "the saved file opens as a clean session without history" );
		C.Check( B.Session.Document.Get().ToJson() == A.Session.Document.Get().ToJson(), "and holds exactly what was saved (objects, tags, collision rules)" );
		const FWorldObject* Reloaded = B.Session.Document.Get().FindObject( Spawn );
		C.Check( Reloaded != nullptr && Reloaded->HasTag( "PlayerStart" ) && B.Session.Document.Get().Settings.CollisionRules.Num() == 1, "tags and rules survive the round trip" );

		// 開けないファイルは、いまの内容を壊さない
		const FString JsonBefore = FString( B.Session.Document.Get().ToJson().dump().c_str() );
		C.Check( !NDebugUI::OpenWorldFile( B, "Levels/missing.world" ) && B.bStatusIsError && B.Session.Document.Get().Objects.Num() == 2, "opening a missing file fails, says so, and keeps the current world" );
		C.Check( FString( B.Session.Document.Get().ToJson().dump().c_str() ) == JsonBefore && B.Session.FilePath == "Levels/Out.world", "(and the file name too)" );

		// 保存したあとの編集と元に戻す
		B.Session.CreateEmpty( FVector3::Zero, "Extra" );
		C.Check( B.Session.Document.IsDirty(), "an edit after saving makes it unsaved" );
		C.Check( NDebugUI::SaveWorldFile( B ) && !B.Session.Document.IsDirty(), "Save with no argument saves to the open file" );
		B.Session.Undo();
		C.Check( B.Session.Document.IsDirty(), "undoing past the saved state makes it unsaved again" );
		B.Session.Redo();
		C.Check( !B.Session.Document.IsDirty(), "and redoing back to it makes it clean" );

		// 保存すると、ギズモのドラッグは確定する
		B.View = MakeViewport();
		B.Session.Camera = MakeCamera( 0.6f, 0.45f, 10.0f );
		B.Session.Select( Spawn );
		FEditorGizmoFrame Frame;
		B.Session.GetGizmoFrame( B.View, Frame );
		FVector3 Start, End;
		NWorldEditor::GetGizmoAxisSegment( Frame, 0, Start, End );
		const FVector3 Grab = Start + ( End - Start ) * 0.6f;
		B.Session.BeginGizmoDrag( B.View, EEditorGizmoHandle::AxisX, RayThrough( B.Session.Camera, B.View, Grab ) );
		B.Session.UpdateGizmoDrag( RayThrough( B.Session.Camera, B.View, Grab + FVector3( 1, 0, 0 ) ) );
		C.Check( NDebugUI::SaveWorldFile( B ) && !B.Session.IsGizmoDragging(), "saving while a drag is in progress ends the drag first" );

		// 新規
		NDebugUI::NewWorld( B );
		C.Check( B.Session.Document.Get().Objects.IsEmpty() && B.Session.FilePath.IsEmpty() && !B.Session.Document.IsDirty() && B.Session.GetSelection().IsEmpty() && !B.Session.Document.CanUndo(), "New starts an empty, unsaved-path, clean session" );

		// 開き直すと、保存した内容に戻る（開いた直後の履歴は空）
		C.Check( NDebugUI::OpenWorldFile( B, "Levels/Out.world" ) && B.Session.Document.Get().Objects.Num() == 3, "opening the saved file again shows the three objects saved last" );
	}
}

void NWorldTest::RunEditorRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir, const TSharedPtr<CTexture>& QuadrantTexture )
{
	TestCameraAgainstDxLib( C );
	TestBillboardAgainstPixels( C, Executor, QuadrantTexture );
	TestModelAgainstPixels( C, Assets, Executor, Dir );
	SetDrawScreen( DX_SCREEN_BACK );
	TestFileOperations( C, Assets );
	TestEffectPreviewDocumentReplacement( C, Assets );
}
