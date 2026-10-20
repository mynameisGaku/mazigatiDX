#include "AssetSelfTestScene.h"
#include "AssetSelfTestSupport.h"
#include "AssetSelfTestFbx.h"
#include "AssetSelfTestModelMaterial.h"
#include "AssetSelfTestModelMaterialBoundary.h"
#include "AssetSelfTestModelSurface.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Animation/SpriteAnimator.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Audio/AudioComponents.h"
#include "Library/Framework/Audio/AudioSystem.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/DxLibAudioBackend.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/FileSystem.h"

DEFINE_LOG_CATEGORY( LogAssetSelfTest )

using namespace NAssetTest;

namespace
{
	class CTestHolder : public CGameObject
	{
	public:
		void OnSpawn() override { AddComponent<CTransformComponent>(); }
	};

	void SetPosition( const TSharedPtr<CGameObject>& Object, const FVector3& Position )
	{
		Object->GetComponent<CTransformComponent>()->SetLocalPosition( Position );
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
		uint32 At( int X, int Y ) const
		{
			int R = 0, G = 0, B = 0, A = 0;
			GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A );
			return ( static_cast<uint32>( R ) << 16 ) | ( static_cast<uint32>( G ) << 8 ) | static_cast<uint32>( B );
		}
		// [X0, X1) x [Y0, Y1) がすべて Color か（許容差つき）
		bool RegionIs( int X0, int Y0, int X1, int Y1, uint32 Color, int Tolerance = 0 ) const
		{
			for ( int Y = Y0; Y < Y1; ++Y )
			{
				for ( int X = X0; X < X1; ++X )
				{
					const uint32 Pixel = At( X, Y );
					for ( int Shift = 0; Shift <= 16; Shift += 8 )
					{
						if ( std::abs( static_cast<int>( ( Pixel >> Shift ) & 0xFF ) - static_cast<int>( ( Color >> Shift ) & 0xFF ) ) > Tolerance ) return false;
					}
				}
			}
			return true;
		}
		int CountLit( int X0, int Y0, int X1, int Y1 ) const
		{
			int Count = 0;
			for ( int Y = Y0; Y < Y1; ++Y )
			{
				for ( int X = X0; X < X1; ++X ) Count += At( X, Y ) != 0 ? 1 : 0;
			}
			return Count;
		}
		// 光っているピクセルの左端と右端（無ければ false）
		bool FindLitColumns( int& OutLeft, int& OutRight ) const
		{
			OutLeft = Width;
			OutRight = -1;
			for ( int Y = 0; Y < Height; ++Y )
			{
				for ( int X = 0; X < Width; ++X )
				{
					if ( At( X, Y ) == 0 ) continue;
					OutLeft = std::min( OutLeft, X );
					OutRight = std::max( OutRight, X );
				}
			}
			return OutRight >= 0;
		}
		// 光っているピクセルの外接矩形（確認の失敗を調べるため）
		FString LitBounds() const
		{
			int Left = Width, Top = Height, Right = -1, Bottom = -1;
			for ( int Y = 0; Y < Height; ++Y )
			{
				for ( int X = 0; X < Width; ++X )
				{
					if ( At( X, Y ) == 0 ) continue;
					Left = std::min( Left, X );
					Right = std::max( Right, X );
					Top = std::min( Top, Y );
					Bottom = std::max( Bottom, Y );
				}
			}
			return Right < 0 ? FString( "(nothing)" ) : FString::Printf( "x %d..%d, y %d..%d", Left, Right, Top, Bottom );
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

	// アニメーション付きのモデル（DirectX の .x、テキスト形式）。三角形が、2 つのアニメーションでそれぞれ別の向きに動く
	const char* AnimatedModelText()
	{
		return "xof 0303txt 0032\r\n"
			   "Frame Scene_Root {\r\n"
			   "  FrameTransformMatrix { 1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0;; }\r\n"
			   "  Frame Box {\r\n"
			   "    FrameTransformMatrix { 1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0;; }\r\n"
			   "    Mesh {\r\n"
			   "      3;\r\n"
			   "      0.0;3.0;0.0;,\r\n"
			   "      -3.0;-3.0;0.0;,\r\n"
			   "      3.0;-3.0;0.0;;\r\n"
			   "      1;\r\n"
			   "      3;0,1,2;;\r\n"
			   "      MeshMaterialList { 1; 1; 0;\r\n"
			   "        Material { 0.9;0.3;0.3;1.0;; 5.0; 0.0;0.0;0.0;; 0.0;0.0;0.0;; }\r\n"
			   "      }\r\n"
			   "    }\r\n"
			   "  }\r\n"
			   "}\r\n"
			   "AnimationSet Slide {\r\n"
			   "  Animation {\r\n"
			   "    { Box }\r\n"
			   "    AnimationKey { 2; 2; 0;3;0.0,0.0,0.0;;, 60;3;10.0,0.0,0.0;;; }\r\n"
			   "  }\r\n"
			   "}\r\n"
			   "AnimationSet Rise {\r\n"
			   "  Animation {\r\n"
			   "    { Box }\r\n"
			   "    AnimationKey { 2; 2; 0;3;0.0,0.0,0.0;;, 30;3;0.0,10.0,0.0;;; }\r\n"
			   "  }\r\n"
			   "}\r\n";
	}
}

//=============================================================================
// 状態
//=============================================================================
struct CAssetSelfTestScene::FState
{
	FChecker Checker;
	bool bExitWhenDone = false;
	// 0: シーンでの確認（偽のバックエンド）/ 1: 後片付け / 2: 本物の DxLib / 3: 本物のサウンドが鳴り終わるのを待つ
	int32 Phase = 0;
	int32 Frame = 0;
	FString TempRoot;

	//-------------------------------------------------------------------------
	// フェーズ 0
	//-------------------------------------------------------------------------
	FFakeAssetBackend Fake;
	FRecordingAudioBackend Rec;
	FCommandLog Log;
	FString Dir;
	bool bProfilerWasEnabled = false;

	TSharedPtr<CSpriteSheet> Sheet;
	TSharedPtr<CSound> ObjectSound;
	TSharedPtr<CModel> Model;
	TSharedPtr<CGameObject> Sprite, Parent, Child, Hidden, Finishing, ModelObject, Listener, Source;
	FVector3 ChildExpectedWorld;
	int32 NumSpriteObjects = 0;
	int32 SourceVoice = -1;
	int32 PhaseOneStartFrame = 0;

	//-------------------------------------------------------------------------
	// フェーズ 2〜3
	//-------------------------------------------------------------------------
	TSharedPtr<CSound> RealSound;
	FSoundHandle RealHandle;
	float32 RealWaited = 0.0f;
};

bool CAssetSelfTestScene::IsRequestedOnCommandLine()
{
	const char* CommandLine = GetCommandLineA();
	return CommandLine != nullptr && std::strstr( CommandLine, "--selftest=assets" ) != nullptr;
}

CAssetSelfTestScene::CAssetSelfTestScene() = default;
CAssetSelfTestScene::~CAssetSelfTestScene() = default;

//=============================================================================
// フェーズ 0: シーンの中での動作（偽のバックエンド）
//=============================================================================
void CAssetSelfTestScene::OnEnter()
{
	State = MakeUnique<FState>();
	State->bExitWhenDone = IsRequestedOnCommandLine();
	FState& S = *State;
	FChecker& C = S.Checker;
	S.TempRoot = MakeTempDirectory();

	RunLogicTests( C );
	RunConsoleLogicTests( C );
	RunStaticModelMaterialCoreChecks( C );
	LOG( LogAssetSelfTest, Log, "Logic checks done: %lld check(s), %lld failure(s)", static_cast<long long>( C.NumChecked ), static_cast<long long>( C.NumFailed ) );

	C.Group = "scene";
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
	if ( Assets == nullptr || Audio == nullptr )
	{
		C.Check( false, "the asset manager / audio system subsystems do not exist" );
		S.Phase = 3;
		return;
	}

	// 偽のバックエンドを差し込む（このフェーズの間だけ）
	S.Dir = S.TempRoot / "scene";
	CreateDirectoryA( *S.Dir, nullptr );
	Assets->SetBackendOverride( &S.Fake );
	Assets->SetRootDirectory( S.Dir );
	Audio->SetBackendOverride( &S.Rec );
	SetDrawExecutor( &S.Log );
	RunStaticModelSurfaceFakeChecks( C, *Assets, S.Fake, S.Dir );
	RunStaticModelMaterialFakeChecks( C, *Assets, S.Fake, S.Dir );
	RunStaticModelMaterialBoundaryFakeChecks( C, *Assets, S.Fake, S.Dir );

	auto Touch = [&]( const char* Name ) { WriteFileText( S.Dir / Name, "x" ); };
	Touch( "sheet.png" );
	Touch( "robot.mv1" );
	Touch( "loop.wav" );
	S.Fake.TextureWidth = 128;
	S.Fake.TextureHeight = 64;
	S.Fake.NextModelData = { { "Idle", "Run" }, { 60.0f, 30.0f } };

	FSpriteSheetGrid Grid;
	Grid.Columns = 4;
	Grid.Rows = 2;
	S.Sheet = Assets->CreateSpriteSheet( Assets->LoadTexture( "sheet.png" ), Grid );
	S.Sheet->AddClip( FSpriteClip::FromRange( "quick", 0, 2, 100.0f, EAnimLoopMode::Once ) );
	S.Model = Assets->LoadModel( "robot.mv1" );
	S.ObjectSound = Assets->LoadSound( "loop.wav" );

	// 描画システムの確認: ECS のデータとして付けるだけで、更新も描画も自動で動く
	const auto MakeAnimator = [&]()
	{
		FSpriteAnimator Animator( S.Sheet );
		Animator.Space.Origin = FVector2( 10.0f, 20.0f );
		Animator.Space.PixelsPerUnit = FVector2( 2.0f, -2.0f );
		return Animator;
	};

	S.Sprite = SpawnObject<CTestHolder>();
	SetPosition( S.Sprite, FVector3( 3.0f, 4.0f, 0.0f ) );
	FSpriteAnimator A1 = MakeAnimator();
	A1.ShowFrame( 5 );
	S.Sprite->AddData( A1 );

	// 親が Z 軸まわりに 0.5 ラジアン回り、子は自分の拡大率 (2, 3) を持つ
	S.Parent = SpawnObject<CTestHolder>();
	SetPosition( S.Parent, FVector3( 20.0f, 0.0f, 0.0f ) );
	S.Parent->GetComponent<CTransformComponent>()->SetLocalRotation( FQuaternion::FromAxisAngle( FVector3::Forward, 0.5f ) );
	S.Child = SpawnObject<CTestHolder>();
	S.Child->AttachToParent( S.Parent.Get() );
	SetPosition( S.Child, FVector3( 1.0f, 0.0f, 0.0f ) );
	S.Child->GetComponent<CTransformComponent>()->SetLocalScale( FVector3( 2.0f, 3.0f, 1.0f ) );
	FSpriteAnimator A2 = MakeAnimator();
	A2.ShowFrame( 2 );
	S.Child->AddData( A2 );

	S.Hidden = SpawnObject<CTestHolder>();
	SetPosition( S.Hidden, FVector3( 50.0f, 50.0f, 0.0f ) );
	FSpriteAnimator A3 = MakeAnimator();
	A3.ShowFrame( 1 );
	S.Hidden->AddData( A3 );
	S.Hidden->SetVisible( false );

	TSharedPtr<CGameObject> NoAuto = SpawnObject<CTestHolder>();
	SetPosition( NoAuto, FVector3( 60.0f, 60.0f, 0.0f ) );
	FSpriteAnimator A4 = MakeAnimator();
	A4.ShowFrame( 1 );
	A4.bAutoDraw = false;
	NoAuto->AddData( A4 );
	NoAuto->SetName( "NoAutoDraw" );

	S.Finishing = SpawnObject<CTestHolder>();
	SetPosition( S.Finishing, FVector3( 7.0f, 7.0f, 0.0f ) );
	FSpriteAnimator A5 = MakeAnimator();
	A5.Speed = 1000.0f;   // 実時間に依存せず、数フレームで終わるように
	A5.Play( "quick" );
	S.Finishing->AddData( A5 );
	S.NumSpriteObjects = 5;

	// 子のワールド位置（コンポーネントの API で読んだ値。描画システムが同じ値を使うか）
	S.ChildExpectedWorld = S.Child->GetComponent<CTransformComponent>()->GetWorldPosition();

	// モデル
	S.ModelObject = SpawnObject<CTestHolder>();
	SetPosition( S.ModelObject, FVector3( 5.0f, 6.0f, 7.0f ) );
	FModelAnimator ModelAnimator( S.Model->CreateInstance() );
	ModelAnimator.AddClipsFrom( S.Model );
	ModelAnimator.Speed = 1000.0f;
	ModelAnimator.Play( "Idle", 0.0f );
	S.ModelObject->AddData( ModelAnimator );

	// 音: リスナー（原点）と、(10, 0, 0) の音源
	S.Listener = SpawnObject<CTestHolder>();
	S.Listener->AddComponent<CAudioListenerComponent>();
	S.Source = SpawnObject<CTestHolder>();
	SetPosition( S.Source, FVector3( 10.0f, 0.0f, 0.0f ) );
	CAudioSourceComponent* SourceComponent = S.Source->AddComponent<CAudioSourceComponent>( S.ObjectSound );
	SourceComponent->SetRange( 1.0f, 21.0f );
	SourceComponent->SetLoop( true );

	// プロファイラー: 記録を始める（次のフレームから）
	if ( IProfiler* Profiler = GetAppSubsystem<IProfiler>() )
	{
		S.bProfilerWasEnabled = Profiler->IsEnabled();
		Profiler->SetEnabled( true );
	}
	LOG( LogAssetSelfTest, Log, "Scene checks started (%d object(s))", GetNumObjects() );
}

void CAssetSelfTestScene::OnTick( float32 DeltaTime )
{
	if ( !State ) return;
	FState& S = *State;
	++S.Frame;
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();

	if ( S.Phase == 0 )
	{
		// 記録中のフレームで読み込みを 1 回して、プロファイラーに Load のサンプルが残るか見る
		if ( S.Frame == 3 && Assets != nullptr )
		{
			WriteFileText( S.Dir / "probe.png", "x" );
			Assets->LoadTexture( "probe.png" );
		}
		if ( S.Frame == 5 && S.Source ) SetPosition( S.Source, FVector3( -10.0f, 0.0f, 0.0f ) );
		if ( S.Frame == 7 && S.Source )
		{
			S.Source->Destroy();
			S.Source.Reset();
		}
		if ( S.Frame == 8 )
		{
			if ( IProfiler* Profiler = GetAppSubsystem<IProfiler>() ) Profiler->SetEnabled( false );
		}
	}
	if ( S.Phase == 3 ) S.RealWaited += DeltaTime;
}

void CAssetSelfTestScene::OnDraw()
{
	if ( !State ) return;
	FState& S = *State;
	FChecker& C = S.Checker;
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();

	//---------------------------------------------------------------------
	// フェーズ 0: このフレームの描画で記録されたコマンドと、音の状態
	//---------------------------------------------------------------------
	if ( S.Phase == 0 )
	{
		C.Group = "scene";
		if ( S.Frame == 2 )
		{
			bool bSprite1 = false, bChild = false, bHidden = false, bNoAuto = false, bModel = false;
			int32 NumSpriteRects = 0;
			int32 NumModelLayers = 0;
			S.Log.ForEach( [&]( const FDrawCommandView& Command )
			{
				if ( Command.Type == EDrawCommand::SpriteRect )
				{
					++NumSpriteRects;
					const FDrawSpriteRectData D = Command.Read<FDrawSpriteRectData>();
					if ( D.X == 16.0f && D.Y == 12.0f )   // 10 + 3 * 2, 20 + 4 * -2
					{
						bSprite1 = true;
						C.Check( D.SrcX == 32 && D.SrcY == 32 && D.SrcW == 32 && D.SrcH == 32 && D.PivotX == 16.0f && D.ScaleX == 1.0f && D.ScaleY == 1.0f && D.Angle == 0.0f,
								 "the sprite of the first object: frame 5, scale 1, angle 0" );
					}
					if ( D.X == 110.0f && D.Y == -80.0f ) bHidden = true;
					if ( D.X == 130.0f && D.Y == -100.0f ) bNoAuto = true;
					if ( D.ScaleX == 2.0f && D.ScaleY == 3.0f )   // 子（拡大 2, 3）
					{
						bChild = true;
						C.Near( D.X, 10.0 + S.ChildExpectedWorld.x * 2.0, 1.0e-3, "the child's screen X follows the parent's transform" );
						C.Near( D.Y, 20.0 - S.ChildExpectedWorld.y * 2.0, 1.0e-3, "the child's screen Y follows the parent's transform" );
						C.Near( D.Angle, -0.5, 1.0e-3, "the rotation about Z is carried over (negated because the Y axis is flipped)" );
						C.Check( D.SrcX == 64 && D.SrcY == 0, "the child's frame 2" );
					}
				}
				if ( Command.Type == EDrawCommand::ModelAnim ) ++NumModelLayers;
				if ( Command.Type == EDrawCommand::Model3D )
				{
					const FDrawModelData D = Command.Read<FDrawModelData>();
					bModel = D.M[9] == 5.0f && D.M[10] == 6.0f && D.M[11] == 7.0f;
				}
			} );
			C.Check( bSprite1, "a sprite animator added with AddData is drawn automatically at its transform" );
			C.Check( bChild, "a child's sprite is drawn" );
			C.Check( !bHidden, "an invisible object's sprite is not drawn" );
			C.Check( !bNoAuto, "a sprite with bAutoDraw = false is not drawn" );
			C.Check( bModel && NumModelLayers >= 1, "a model animator is drawn with the object's world matrix and its animation layer" );
			C.Check( NumSpriteRects == 3, "three sprites are drawn (first, child, finishing; got %d)", NumSpriteRects );
		}

		if ( S.Frame == 4 && Audio != nullptr )
		{
			C.Check( S.Rec.CountPlaying() == 1, "an audio source component starts playing by itself (%d playing)", S.Rec.CountPlaying() );
			S.SourceVoice = S.Rec.LastStarted;
			const FRecordingAudioBackend::FVoiceState& V = S.Rec.Voices[S.SourceVoice];
			C.Near( V.Volume, 0.55, 1.0e-3, "the source 10 m away (range 1..21) is at volume 0.55" );
			C.Near( V.Pan, 1.0, 1.0e-3, "and on the right" );
			C.Check( V.bLoop, "the loop setting is used" );
		}
		if ( S.Frame == 6 && S.SourceVoice >= 0 )
		{
			const FRecordingAudioBackend::FVoiceState& V = S.Rec.Voices[S.SourceVoice];
			C.Near( V.Pan, -1.0, 1.0e-3, "the sound follows its object: moved to the left, it pans left" );
		}
		if ( S.Frame == 9 && S.SourceVoice >= 0 )
		{
			C.Check( !S.Rec.Voices[S.SourceVoice].bPlaying && S.Rec.CountPlaying() == 0, "destroying the object stops its sound" );
		}

		if ( S.Frame == 10 )
		{
			// ECS の更新（並列）が、Once のクリップを最後まで進めたか
			const FSpriteAnimator* Finished = S.Finishing->GetData<FSpriteAnimator>();
			C.Check( Finished != nullptr && Finished->IsFinished() && Finished->GetFrameIndex() == 2, "the built-in update system plays the clip to its end" );
			const FModelAnimator* Playing = S.ModelObject->GetData<FModelAnimator>();
			C.Check( Playing != nullptr && Playing->GetPlayTime() > 0.0f, "the built-in update system advances the model animation" );

			// プロファイラー: 記録された読み込み・カウンター
			if ( IProfiler* Profiler = GetAppSubsystem<IProfiler>() )
			{
				C.Group = "profiler";
				bool bLoadSample = false, bLabel = false, bAssetTick = false, bAudioUpdate = false;
				bool bCountTotal = false, bMemoryTotal = false, bSpriteEntities = false, bModelEntities = false, bVoices = false, bVram = false;
				for ( int32 Index = 0; Index < Profiler->GetNumFrames(); ++Index )
				{
					const FProfilerFrame& Frame = Profiler->GetFrame( Index );
					for ( const FProfileSample& Sample : Frame.Samples )
					{
						const FString& Name = Profiler->GetString( Sample.Name );
						if ( Name == "LoadTexture" )
						{
							bLoadSample = true;
							bLabel = bLabel || Profiler->GetString( Sample.Label ).EndsWith( "probe.png" );
						}
						if ( Name == "Tick" && Profiler->GetString( Sample.Category ) == "Asset" ) bAssetTick = true;
						if ( Name == "Update" && Profiler->GetString( Sample.Category ) == "Audio" ) bAudioUpdate = true;
					}
					for ( const FProfileCounter& Counter : Frame.Counters )
					{
						const FString& Name = Profiler->GetString( Counter.Name );
						if ( Name == "Asset.Count.Total" && Counter.Value >= 3.0 ) bCountTotal = true;
						if ( Name == "Asset.Memory.Total" && Counter.Value > 0.0 && Counter.Unit == EProfileCounterUnit::Bytes ) bMemoryTotal = true;
						if ( Name == "Ecs.Entities.Animation.Sprite" && static_cast<int32>( Counter.Value ) == S.NumSpriteObjects ) bSpriteEntities = true;
						if ( Name == "Ecs.Entities.Animation.Model" && static_cast<int32>( Counter.Value ) == 1 ) bModelEntities = true;
						if ( Name == "Audio.Voices" ) bVoices = true;
						if ( Name == "Memory.VRAM.Used" && Counter.Value == 250000.0 ) bVram = true;
					}
				}
				C.Check( Profiler->GetNumFrames() > 0, "the profiler recorded frames" );
				C.Check( bLoadSample && bLabel, "a texture load shows up as a profiler sample labelled with the file" );
				C.Check( bAssetTick && bAudioUpdate, "the asset manager and the audio system are profiled" );
				C.Check( bCountTotal && bMemoryTotal, "asset count and memory are recorded as counters" );
				C.Check( bSpriteEntities && bModelEntities, "the number of animated entities is recorded per system" );
				C.Check( bVoices && bVram, "audio voices and the backend's video memory are recorded" );

				const FString Formatted = NProfilerAnalyzer::FormatCounterValue( 1536.0, EProfileCounterUnit::Bytes );
				C.Check( Formatted == "1.5 KB" && NProfilerAnalyzer::FormatCounterValue( 5.0 * 1024.0 * 1024.0, EProfileCounterUnit::Bytes ) == "5.00 MB"
							 && NProfilerAnalyzer::FormatCounterValue( 1234567.0, EProfileCounterUnit::Count ) == "1,234,567",
						 "counter values are formatted (got '%s')", *Formatted );

				TArray<FProfileCounterRow> Rows;
				NProfilerAnalyzer::BuildCounterRows( *Profiler, 0, -1, "Asset.Memory", Rows );
				bool bSorted = Rows.Num() >= 2;
				for ( int32 i = 1; i < Rows.Num(); ++i ) bSorted = bSorted && Rows[i - 1].Label.Compare( Rows[i].Label, ESearchCase::IgnoreCase ) <= 0;
				C.Check( bSorted && Rows[0].Label.StartsWith( "Asset.Memory" ), "counter rows are filtered and sorted by name (%d rows)", Rows.Num() );
				TArray<float32> Series;
				if ( !Rows.IsEmpty() ) NProfilerAnalyzer::GetCounterSeries( *Profiler, Rows[0].Name, 0, -1, Series );
				C.Check( Series.Num() == Profiler->GetNumFrames(), "a counter series has one value per frame" );

				Profiler->Clear();
				Profiler->SetEnabled( S.bProfilerWasEnabled );
			}

			// 後片付け: オブジェクトを破棄し、アセットを手放す
			S.Sprite->Destroy();
			S.Child->Destroy();
			S.Parent->Destroy();
			S.Hidden->Destroy();
			S.Finishing->Destroy();
			S.ModelObject->Destroy();
			S.Listener->Destroy();
			S.Phase = 1;
			S.PhaseOneStartFrame = S.Frame;
		}
		S.Log.Clear();
		return;
	}

	//---------------------------------------------------------------------
	// フェーズ 1: 偽のバックエンドを外す（オブジェクトが消えて、アセットが手放されてから）
	//---------------------------------------------------------------------
	if ( S.Phase == 1 )
	{
		if ( S.Frame < S.PhaseOneStartFrame + 3 ) return;   // 破棄の予約が反映されるまで

		RunConsoleSceneChecks( C, *this );

		C.Group = "scene-cleanup";
		S.Sprite.Reset();
		S.Child.Reset();
		S.Parent.Reset();
		S.Hidden.Reset();
		S.Finishing.Reset();
		S.ModelObject.Reset();
		S.Listener.Reset();
		S.Source.Reset();
		S.Sheet.Reset();
		S.Model.Reset();
		S.ObjectSound.Reset();
		if ( Assets != nullptr )
		{
			Assets->ReleaseUnused();
			Assets->Tick( 0.0f );
			C.Check( S.Fake.Models.empty() && S.Fake.Sounds.empty(), "the scene checks released every model and sound (models %d, sounds %d)", static_cast<int32>( S.Fake.Models.size() ),
					 static_cast<int32>( S.Fake.Sounds.size() ) );
		}
		if ( Audio != nullptr ) Audio->SetBackendOverride( nullptr );
		if ( Assets != nullptr )
		{
			Assets->SetBackendOverride( nullptr );
			Assets->SetRootDirectory( "Assets" );
		}
		SetDrawExecutor( nullptr );
		S.Phase = 2;
		return;
	}

	//---------------------------------------------------------------------
	// フェーズ 2: 本物の DxLib
	//---------------------------------------------------------------------
	if ( S.Phase == 2 )
	{
		RunRealChecks();
		S.Phase = 3;
		return;
	}

	//---------------------------------------------------------------------
	// フェーズ 3: 本物のサウンドが鳴り終わるのを待って、結果を出す
	//---------------------------------------------------------------------
	if ( S.Phase == 3 ) FinishRealSound();
}

void CAssetSelfTestScene::OnExit()
{
	State.Reset();
}

//=============================================================================
// フェーズ 2: 本物の DxLib（ピクセル・フォント・サウンド・モデル）
//=============================================================================
void CAssetSelfTestScene::RunRealChecks()
{
	FState& S = *State;
	FChecker& C = S.Checker;
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
	CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();
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

	// ファイル
	WriteFileBytes( Dir / "sheet.bmp", MakeSheetBmp() );
	WriteFileBytes( Dir / "beep.wav", MakeWav( 0.5f, 22050 ) );
	WriteFileText( Dir / "model.x", AnimatedModelText() );
	RunStaticModelSurfaceRealChecks( C, *Assets, Dir );
	RunStaticModelMaterialRealChecks( C, *Assets, *Executor, Dir );
	RunStaticModelMaterialBoundaryRealChecks( C, *Assets, Dir );

	//---------------------------------------------------------------------
	// 画像
	//---------------------------------------------------------------------
	C.Group = "texture";
	TSharedPtr<CTexture> Texture = Assets->LoadTexture( "sheet.bmp" );
	C.Check( Texture.IsValid() && Texture->GetWidth() == SheetWidth && Texture->GetHeight() == SheetHeight, "a BMP file loads with the right size (%dx%d)", Texture.IsValid() ? Texture->GetWidth() : -1,
			 Texture.IsValid() ? Texture->GetHeight() : -1 );
	C.Check( Assets->LoadTexture( "sheet.bmp" ) == Texture, "the same file is shared" );
	if ( Texture.IsValid() )
	{
		C.Check( Texture->GetMemoryBytes() == static_cast<int64>( SheetWidth ) * SheetHeight * 4, "texture memory is width x height x 4" );
		C.Check( Texture->GetFileBytes() > 0 && Texture->GetLoadMs() >= 0.0f, "file size and load time are recorded" );
		const std::vector<uint8> Bytes = MakeSheetBmp();
		TSharedPtr<CTexture> FromMemory = Assets->CreateTextureFromMemory( "memory-sheet", Bytes.data(), Bytes.size() );
		C.Check( FromMemory.IsValid() && FromMemory->GetWidth() == SheetWidth, "a texture can be created from a file image in memory" );
	}
	C.Check( Assets->LoadTexture( "does-not-exist.png" ) == nullptr, "a missing texture returns null" );
	WriteFileText( Dir / "broken.png", "this is not an image" );
	C.Check( Assets->LoadTexture( "broken.png" ) == nullptr, "a broken image returns null" );

	//---------------------------------------------------------------------
	// スプライトシート: コマ割りどおりのピクセル
	//---------------------------------------------------------------------
	C.Group = "sprite-pixels";
	FCapture Capture;
	if ( Texture.IsValid() && Capture.Begin( 128, 96 ) )
	{
		FSpriteSheetGrid Grid;
		Grid.Columns = SheetColumns;
		Grid.Rows = SheetRows;
		Grid.MarginX = SheetMargin;
		Grid.MarginY = SheetMargin;
		Grid.SpacingX = SheetSpacing;
		Grid.SpacingY = SheetSpacing;
		TSharedPtr<CSpriteSheet> Sheet = Assets->CreateSpriteSheet( Texture, Grid );
		C.Check( Sheet.IsValid() && Sheet->GetNumFrames() == 8 && Sheet->GetFrame( 0 )->Width == SheetCell && Sheet->GetFrame( 5 )->X == SheetMargin + 1 * ( SheetCell + SheetSpacing ),
				 "the grid (4x2 with margin and spacing) is split into 16x16 cells" );

		// 8 コマすべて: 中心 (40, 30) に描くと、コマの色が 16x16 に敷かれ、その外は黒
		bool bAllFrames = true;
		bool bOutsideClean = true;
		for ( int32 Frame = 0; Frame < 8 && Sheet.IsValid(); ++Frame )
		{
			ClearDrawScreen();
			FSpriteAnimator Animator( Sheet );
			Animator.ShowFrame( Frame );
			FDrawBuffer Buffer;
			FDrawContext Draw( Buffer );
			Animator.Record( Draw, FVector2( 40.0f, 30.0f ) );
			ExecuteBuffer( *Executor, Buffer );
			Capture.Read();
			const uint32 Expected = SheetCellColor( Frame % SheetColumns, Frame / SheetColumns );
			bAllFrames = bAllFrames && Capture.RegionIs( 32, 22, 48, 38, Expected );
			bOutsideClean = bOutsideClean && Capture.CountLit( 0, 0, 128, 96 ) == 16 * 16;   // 余白（紫）や隣のコマが漏れていない
		}
		C.Check( bAllFrames, "each of the 8 cells is drawn with its own colour, exactly 16x16" );
		C.Check( bOutsideClean, "no margin / spacing / neighbouring cell leaks into a drawn frame" );

		// 拡大 2 倍
		{
			ClearDrawScreen();
			FSpriteAnimator Animator( Sheet );
			Animator.ShowFrame( 6 );
			FDrawBuffer Buffer;
			FDrawContext Draw( Buffer );
			Animator.Record( Draw, FVector2( 40.0f, 30.0f ), FVector2( 2.0f, 2.0f ) );
			ExecuteBuffer( *Executor, Buffer );
			Capture.Read();
			const uint32 Expected = SheetCellColor( 2, 1 );
			C.Check( Capture.RegionIs( 26, 16, 54, 44, Expected, 2 ), "scale 2 covers about 32x32" );
			C.Check( Capture.At( 22, 30 ) == 0 && Capture.At( 58, 30 ) == 0 && Capture.At( 40, 12 ) == 0 && Capture.At( 40, 48 ) == 0, "and nothing outside it" );
		}

		// 基準点 (0, 0) のコマ: 反転は基準点を軸にして、基準点が表示位置に残る
		{
			TSharedPtr<CSpriteSheet> Pivoted = Assets->CreateSpriteSheet( Texture );
			FSpriteFrame Frame;
			Frame.X = SheetMargin;
			Frame.Y = SheetMargin;
			Frame.Width = SheetCell;
			Frame.Height = SheetCell;
			Frame.PivotX = 0.0f;
			Frame.PivotY = 0.0f;
			Pivoted->AddFrame( Frame );
			const uint32 Expected = SheetCellColor( 0, 0 );

			const auto Draw = [&]( bool bFlipX, bool bFlipY, float32 Angle )
			{
				ClearDrawScreen();
				FSpriteAnimator Animator( Pivoted );
				Animator.ShowFrame( 0 );
				Animator.bFlipX = bFlipX;
				Animator.bFlipY = bFlipY;
				FDrawBuffer Buffer;
				FDrawContext Context( Buffer );
				Animator.Record( Context, FVector2( 60.0f, 40.0f ), FVector2( 1.0f, 1.0f ), Angle );
				ExecuteBuffer( *Executor, Buffer );
				Capture.Read();
			};
			Draw( false, false, 0.0f );
			C.Check( Capture.RegionIs( 60, 40, 76, 56, Expected ) && Capture.CountLit( 0, 0, 128, 96 ) == 256, "a top-left pivot: the sprite hangs right and down of the position" );
			Draw( true, false, 0.0f );
			C.Check( Capture.RegionIs( 44, 40, 60, 56, Expected ) && Capture.CountLit( 0, 0, 128, 96 ) == 256, "flip X mirrors about the pivot (the sprite is to the left)" );
			Draw( false, true, 0.0f );
			C.Check( Capture.RegionIs( 60, 24, 76, 40, Expected ) && Capture.CountLit( 0, 0, 128, 96 ) == 256, "flip Y mirrors about the pivot (the sprite is above)" );
			Draw( false, false, 1.5707963f );
			C.Check( Capture.RegionIs( 44, 40, 60, 56, Expected, 2 ), "a 90 degree rotation turns about the pivot (the sprite is to the left, below)" );
		}

		// 色を掛ける・半透明
		{
			const uint32 Cell = SheetCellColor( 3, 1 );
			const auto Draw = [&]( const FColor32& Tint, float32 Alpha )
			{
				ClearDrawScreen();
				FSpriteAnimator Animator( Sheet );
				Animator.ShowFrame( 7 );
				Animator.Tint = Tint;
				Animator.Alpha = Alpha;
				FDrawBuffer Buffer;
				FDrawContext Context( Buffer );
				Animator.Record( Context, FVector2( 40.0f, 30.0f ) );
				ExecuteBuffer( *Executor, Buffer );
				Capture.Read();
			};
			const auto Scaled = [&]( float32 Factor )
			{
				uint32 Result = 0;
				for ( int32 Shift = 0; Shift <= 16; Shift += 8 ) Result |= static_cast<uint32>( static_cast<float32>( ( Cell >> Shift ) & 0xFF ) * Factor + 0.5f ) << Shift;
				return Result;
			};
			Draw( FColor32( 128, 128, 128 ), 1.0f );
			C.Check( Capture.RegionIs( 32, 22, 48, 38, Scaled( 128.0f / 255.0f ), 2 ), "a grey tint darkens the sprite" );
			Draw( FColor32::White, 0.5f );
			C.Check( Capture.RegionIs( 32, 22, 48, 38, Scaled( 0.5f ), 3 ), "alpha 0.5 blends with the black background" );
			Draw( FColor32::White, 1.0f );
			C.Check( Capture.RegionIs( 32, 22, 48, 38, Cell ), "the tint and the blend mode do not leak into the next draw" );
		}
	}
	else
	{
		C.Check( false, "could not create the off-screen buffers" );
	}
	Capture.End();

	//---------------------------------------------------------------------
	// フォント
	//---------------------------------------------------------------------
	C.Group = "font";
	if ( Capture.Begin( 320, 120 ) )
	{
		TSharedPtr<CFont> Small = Assets->GetDefaultFont( 16 );
		TSharedPtr<CFont> Large = Assets->GetDefaultFont( 40 );
		C.Check( Small.IsValid() && Large.IsValid() && Small != Large, "default fonts of two sizes" );
		if ( Small.IsValid() && Large.IsValid() )
		{
			const int32 SmallWidth = Small->MeasureWidth( "Hello" );
			const int32 LargeWidth = Large->MeasureWidth( "Hello" );
			C.Check( SmallWidth > 0 && LargeWidth > SmallWidth * 2, "text width scales with the font size (%d -> %d)", SmallWidth, LargeWidth );
			C.Check( Small->GetMemoryBytes() > 0, "font memory is estimated" );

			ClearDrawScreen();
			Large->Draw( 10, 10, "Hello", FColor32::White );
			Capture.Read();
			C.Check( Capture.CountLit( 10, 10, 10 + LargeWidth + 4, 60 ) > 100, "text is drawn through the font" );
			int Left = 0, Right = 0;
			Capture.FindLitColumns( Left, Right );
			C.Check( Left >= 10 - 1 && Right <= 10 + LargeWidth + 2, "and stays inside the measured width (lit columns %d..%d, width %d)", Left, Right, LargeWidth );

			// 位置揃え: X = 200 を右端 / 中央にする
			const int32 WideWidth = Large->MeasureWidth( "WWWW" );
			ClearDrawScreen();
			Large->Draw( 200, 10, "WWWW", FColor32::White, EDrawTextAlign::Right );
			Capture.Read();
			Capture.FindLitColumns( Left, Right );
			C.Check( Right <= 200 + 2 && Right >= 200 - 6 && Left >= 200 - WideWidth - 2, "right alignment: the text ends at X (lit %d..%d, X 200, width %d)", Left, Right, WideWidth );
			ClearDrawScreen();
			Large->Draw( 200, 10, "WWWW", FColor32::White, EDrawTextAlign::Center );
			Capture.Read();
			Capture.FindLitColumns( Left, Right );
			C.Check( std::abs( ( Left + Right ) / 2 - 200 ) <= 6, "center alignment: the text is centred on X (lit %d..%d)", Left, Right );

			// 縁取り: 縁の色が出る
			FFontStyle Edge;
			Edge.EdgeSize = 3;
			TSharedPtr<CFont> Edged = Assets->GetDefaultFont( 40, Edge );
			if ( Edged.IsValid() )
			{
				ClearDrawScreen();
				Edged->Draw( 10, 10, "Hi", FColor32::White, EDrawTextAlign::Left, FColor32( 255, 0, 0 ) );
				Capture.Read();
				bool bRed = false;
				for ( int Y = 0; Y < 120 && !bRed; ++Y )
				{
					for ( int X = 0; X < 320 && !bRed; ++X ) bRed = Capture.At( X, Y ) == 0xFF0000;
				}
				C.Check( bRed, "an edged font draws its outline in the edge colour" );
			}
		}

		// フォントファイル（Windows の標準フォント）
		const FString ArialPath = "C:/Windows/Fonts/arial.ttf";
		if ( NFileSystem::FileExists( ArialPath ) )
		{
			TSharedPtr<CFont> Arial = Assets->LoadFont( ArialPath, 28 );
			C.Check( Arial.IsValid() && Arial->GetDesc().Face == "Arial", "arial.ttf registers and is named 'Arial'" );
			C.Check( Assets->LoadFont( ArialPath, 28 ) == Arial && Assets->LoadFont( ArialPath, 29 ) != Arial, "same size shares, another size does not" );
			if ( Arial.IsValid() )
			{
				ClearDrawScreen();
				Arial->Draw( 10, 10, "Arial", FColor32::White );
				Capture.Read();
				C.Check( Capture.CountLit( 0, 0, 320, 120 ) > 80, "text is drawn with a font loaded from a file" );
			}
		}
		else
		{
			C.Skip( "C:/Windows/Fonts/arial.ttf was not found" );
		}
	}
	Capture.End();
	SetDrawScreen( DX_SCREEN_BACK );
	SetCameraNearFar( NearClip, FarClip );

	//---------------------------------------------------------------------
	// モデル（アニメーション付きの .x）
	//---------------------------------------------------------------------
	C.Group = "model";
	TSharedPtr<CModel> Model = Assets->LoadModel( "model.x" );
	if ( !Model.IsValid() )
	{
		C.Skip( "the generated .x model could not be loaded" );
	}
	else
	{
		LOG( LogAssetSelfTest, Display, "Model: %d animation(s)", Model->GetClips().Num() );
		for ( const FModelClipInfo& Clip : Model->GetClips() ) LOG( LogAssetSelfTest, Display, "  animation %d '%s' length %.2f", Clip.AnimIndex, *Clip.Name, Clip.Length );

		C.Check( Model->GetClips().Num() >= 2 && Model->FindClip( "Slide" ) != nullptr && Model->FindClip( "Rise" ) != nullptr, "the animations are listed by name" );
		C.Check( Model->GetMemoryBytes() > 0, "model memory is estimated" );
		TSharedPtr<CModelInstance> Instance = Model->CreateInstance();
		C.Check( Instance.IsValid(), "an instance can be created" );
		if ( Instance.IsValid() && Model->FindClip( "Slide" ) != nullptr && Model->FindClip( "Rise" ) != nullptr )
		{
			FModelAnimator Animator( Instance );
			Animator.AddClipsFrom( Model );
			const float32 SlideLength = Animator.GetClipLength( "Slide" ) * Animator.UnitsPerSecond;
			const float32 RiseLength = Animator.GetClipLength( "Rise" ) * Animator.UnitsPerSecond;
			C.Check( SlideLength > 0.0f && RiseLength > 0.0f, "the clip lengths are known (%.1f and %.1f time units)", SlideLength, RiseLength );
			// このファイルのキーの時間は 60 と 30。DxLib が返す長さと同じ単位であること
			C.Near( SlideLength, 60.0, 1.0, "Slide is 60 time units long" );
			C.Near( RiseLength, 30.0, 1.0, "Rise is 30 time units long" );

			const int32 Handle = Instance->GetHandle();
			const int32 BoxFrame = MV1SearchFrame( Handle, "Box" );
			C.Check( BoxFrame >= 0, "the animated frame exists" );

			// Slide を再生: 半分（30 単位）で x = 5
			Animator.Play( "Slide", 0.0f );
			Animator.Update( 1.0f );   // 30 単位/秒 x 1 秒 = 30
			FDrawBuffer Buffer;
			FDrawContext Draw( Buffer );
			Animator.Record( Draw, FMatrix4x4::Identity );
			ExecuteBuffer( *Executor, Buffer );
			if ( BoxFrame >= 0 )
			{
				const VECTOR Position = MV1GetFramePosition( Handle, BoxFrame );
				C.Near( Position.x, 5.0, 0.2, "Slide halfway moves the frame to x = 5 (the times and the blend rate reach DxLib)" );
				C.Near( Position.y, 0.0, 0.2, "and not in y" );
			}

			// Slide から Rise へ 1 秒かけて: 半分の時点で、両方が重み 0.5
			Animator.Play( "Rise", 1.0f );
			Animator.Update( 0.5f );
			Buffer.Clear();
			Animator.Record( Draw, FMatrix4x4::Identity );
			ExecuteBuffer( *Executor, Buffer );
			const TArray<FModelAnimator::FLayerInfo> Layers = Animator.GetLayerInfos();
			C.Check( Layers.Num() == 2, "two animations are attached during the crossfade" );
			for ( const FModelAnimator::FLayerInfo& Layer : Layers )
			{
				C.Near( MV1GetAttachAnimBlendRate( Handle, Layer.AttachIndex ), 0.5, 1.0e-3, "each attached animation has blend rate 0.5" );
				C.Near( MV1GetAttachAnimTime( Handle, Layer.AttachIndex ), Layer.ClipTime, 1.0e-3, "and the time the animator computed" );
			}
			if ( BoxFrame >= 0 )
			{
				const VECTOR Position = MV1GetFramePosition( Handle, BoxFrame );
				C.Check( Position.x > 1.0f && Position.y > 1.0f, "the frame is blended between the two animations (%.2f, %.2f)", Position.x, Position.y );
			}

			// Rise だけに（フェードが終わった）
			Animator.Update( 0.6f );
			Buffer.Clear();
			Animator.Record( Draw, FMatrix4x4::Identity );
			ExecuteBuffer( *Executor, Buffer );
			int32 FullWeights = 0;
			for ( const FModelAnimator::FLayerInfo& Layer : Animator.GetLayerInfos() )
			{
				const float32 Rate = MV1GetAttachAnimBlendRate( Handle, Layer.AttachIndex );
				if ( Rate > 0.999f ) ++FullWeights;
				else C.Near( Rate, 0.0, 1.0e-3, "the finished fade leaves the old animation at weight 0 in DxLib" );
			}
			C.Check( FullWeights == 1, "after the fade, exactly one animation has full weight" );
		}

		// モデルのアニメーション定義（独自のデータアセット）を保存・読み込み
		FModelAnimationDefinition Definition;
		Definition.ModelPath = "model.x";
		FModelClipDef Slide;
		Slide.Name = "Slide";
		Slide.AnimName = "Slide";
		Slide.Loop = EAnimLoopMode::PingPong;
		Slide.Events.Add( FModelEventDef { 0.5f, "Middle" } );
		FModelClipDef Rise;
		Rise.Name = "Rise";
		Rise.AnimName = "Rise";
		Definition.Clips.Add( Slide );
		Definition.Clips.Add( Rise );
		C.Check( Assets->SaveModelAnimationDefinition( "hero.modelanim.json", Definition ), "the definition is saved" );
		TSharedPtr<CModelAnimationAsset> Asset = Assets->LoadModelAnimations( "hero.modelanim.json" );
		if ( Asset.IsValid() )
		{
			FModelAnimator Created = Asset->CreateAnimator();
			C.Check( Created.HasClip( "Slide" ) && Created.HasClip( "Rise" ) && Created.Play( "Slide" ), "a data asset creates a working animator for a real model" );
			C.Near( Created.GetClipLength( "Slide" ) * Created.UnitsPerSecond, 60.0, 1.0, "the clip was found by name in the real model" );
			Created.Update( 0.5f );
			C.Check( Created.GetNormalizedTime() > 0.2f, "and it plays" );
		}
		else
		{
			C.Check( false, "the saved model animation definition could not be loaded" );
		}
	}

	//---------------------------------------------------------------------
	// モデルの取り込み（FBX / OBJ -> .mv1）と、アニメーションだけのモデル
	//---------------------------------------------------------------------
	C.Group = "model-import";
	{
		WriteFileText( Dir / "bar.fbx", MakeBarFbx( FFbxSpec() ).c_str() );
		FFbxSpec MeshOnlySpec;
		MeshOnlySpec.bAnimation = false;
		WriteFileText( Dir / "bar_mesh.fbx", MakeBarFbx( MeshOnlySpec ).c_str() );
		FFbxSpec AnimOnlySpec;
		AnimOnlySpec.bMesh = false;
		AnimOnlySpec.TextureFile = nullptr;
		WriteFileText( Dir / "wave_only.fbx", MakeBarFbx( AnimOnlySpec ).c_str() );
		WriteFileText( Dir / "box.mtl", "newmtl BoxMaterial\nKd 0.8 0.4 0.2\nmap_Kd sheet.bmp\n" );
		WriteFileText( Dir / "box.obj", MakeBoxObj( "box.mtl" ).c_str() );

		TSharedPtr<CModel> Bar = Assets->LoadModel( "bar.fbx" );
		C.Check( Bar.IsValid(), "an FBX model with mesh, skin, and animation loads through CAssetManager" );
		C.Check( NFileSystem::FileExists( Dir / "bar.mv1" ), "loading bar.fbx automatically outputs bar.mv1 next to it" );
		if ( Bar.IsValid() )
		{
			C.Check( Bar->HasMesh() && !Bar->IsAnimationOnly() && Bar->GetMeshCount() >= 1, "the imported FBX model has a mesh (%d)", Bar->GetMeshCount() );
			C.Check( Bar->GetBoneNames().Contains( "Bone1" ) && Bar->GetBoneNames().Contains( "Bone2" ), "the skeleton frames (Bone1, Bone2) are preserved in DxLib" );
			const FModelClipInfo* Wave = Bar->FindClip( "Wave" );
			C.Check( Wave != nullptr, "the FBX animation stack 'Wave' is present in the .mv1" );
			if ( Wave != nullptr ) C.Near( Wave->Length, 30.0, 1.5, "the 1-second FBX animation is 30 frames long in DxLib" );

			const VECTOR Min = MV1GetMeshMinPosition( Bar->GetHandle(), 0 );
			const VECTOR Max = MV1GetMeshMaxPosition( Bar->GetHandle(), 0 );
			C.Near( Min.x, -0.5, 0.05, "FBX mesh bounds X min is -0.5" );
			C.Near( Max.x, 0.5, 0.05, "FBX mesh bounds X max is 0.5" );
			C.Near( Min.y, 0.0, 0.05, "FBX mesh bounds Y min is 0" );
			C.Near( Max.y, 2.0, 0.05, "FBX mesh bounds Y max is 2" );
			C.Near( Min.z, -1.5, 0.05, "FBX mesh bounds Z min is -1.5 (RH z in [-0.5, 1.5] mirrored to LH)" );
			C.Near( Max.z, 0.5, 0.05, "FBX mesh bounds Z max is 0.5" );

			TSharedPtr<CModelInstance> BarInst = Bar->CreateInstance();
			C.Check( BarInst.IsValid(), "an instance of the imported FBX model can be created" );
			if ( BarInst.IsValid() && Wave != nullptr )
			{
				const int32 H = BarInst->GetHandle();
				FModelAnimator Anim( BarInst );
				Anim.AddClipsFrom( Bar );
				Anim.Play( "Wave", 0.0f );
				Anim.Seek( 0.0f );
				FDrawBuffer Buffer;
				FDrawContext Draw( Buffer );
				Anim.Record( Draw, FMatrix4x4::Identity );
				ExecuteBuffer( *Executor, Buffer );

				MV1SetupReferenceMesh( H, -1, TRUE );
				MV1RefreshReferenceMesh( H, -1, TRUE );
				MV1_REF_POLYGONLIST Ref0 = MV1GetReferenceMesh( H, -1, TRUE );
				float32 MaxAbsXStart = 0.0f;
				for ( int32 v = 0; v < Ref0.VertexNum; ++v ) MaxAbsXStart = std::max( MaxAbsXStart, std::fabs( Ref0.Vertexs[v].Position.x ) );
				C.Near( MaxAbsXStart, 0.5, 0.08, "at the start of Wave, the skinned mesh stays within |x| <= 0.5" );

				Anim.Seek( 1.0f );
				Buffer.Clear();
				Anim.Record( Draw, FMatrix4x4::Identity );
				ExecuteBuffer( *Executor, Buffer );
				MV1RefreshReferenceMesh( H, -1, TRUE );
				MV1_REF_POLYGONLIST Ref1 = MV1GetReferenceMesh( H, -1, TRUE );
				float32 MaxAbsXEnd = 0.0f;
				for ( int32 v = 0; v < Ref1.VertexNum; ++v ) MaxAbsXEnd = std::max( MaxAbsXEnd, std::fabs( Ref1.Vertexs[v].Position.x ) );
				C.Check( MaxAbsXEnd > 0.85f, "at the end of Wave (90 deg bend), the top ring of the skinned mesh bends past |x| = 0.85 (got %.2f)", MaxAbsXEnd );
				MV1TerminateReferenceMesh( H, -1, TRUE );
			}
		}

		TSharedPtr<CModel> Box = Assets->LoadModel( "box.obj" );
		C.Check( Box.IsValid() && NFileSystem::FileExists( Dir / "box.mv1" ), "an OBJ model loads and outputs box.mv1" );
		if ( Box.IsValid() )
		{
			C.Check( Box->HasMesh() && !Box->IsAnimationOnly() && Box->GetClips().IsEmpty(), "the OBJ model has a mesh and no animations" );
			const VECTOR Min = MV1GetMeshMinPosition( Box->GetHandle(), 0 );
			const VECTOR Max = MV1GetMeshMaxPosition( Box->GetHandle(), 0 );
			C.Near( Min.x, -1.0, 0.05, "OBJ mesh bounds X min is -1" );
			C.Near( Max.x, 1.0, 0.05, "OBJ mesh bounds X max is 1" );
			C.Near( Min.y, 0.0, 0.05, "OBJ mesh bounds Y min is 0" );
			C.Near( Max.y, 2.0, 0.05, "OBJ mesh bounds Y max is 2" );
			C.Near( Min.z, -2.0, 0.05, "OBJ mesh bounds Z min is -2 (mirrored to LH)" );
			C.Near( Max.z, 1.0, 0.05, "OBJ mesh bounds Z max is 1" );
		}
	}

	C.Group = "animation-only";
	{
		TSharedPtr<CModel> WaveOnly = Assets->LoadModel( "wave_only.fbx" );
		TSharedPtr<CModel> BarMesh = Assets->LoadModel( "bar_mesh.fbx" );
		C.Check( WaveOnly.IsValid() && NFileSystem::FileExists( Dir / "wave_only.mv1" ), "an animation-only FBX converts to wave_only.mv1 and loads in DxLib" );
		C.Check( BarMesh.IsValid() && NFileSystem::FileExists( Dir / "bar_mesh.mv1" ), "a mesh-only FBX converts to bar_mesh.mv1 and loads in DxLib" );
		if ( WaveOnly.IsValid() && BarMesh.IsValid() )
		{
			C.Check( WaveOnly->IsAnimationOnly() && !WaveOnly->HasMesh() && WaveOnly->GetMeshCount() == 0, "wave_only.mv1 is detected as animation-only (0 meshes)" );
			C.Check( WaveOnly->CreateInstance() == nullptr, "an animation-only model does not create a drawable instance" );
			C.Check( WaveOnly->FindClip( "Wave" ) != nullptr, "wave_only.mv1 still carries the 'Wave' animation clip" );
			C.Check( BarMesh->HasMesh() && !BarMesh->IsAnimationOnly() && BarMesh->GetClips().IsEmpty(), "bar_mesh.mv1 has the mesh and 0 animations" );

			const FBoneMatch Match = BarMesh->CompareBones( *WaveOnly );
			C.Check( Match.NumAnimBones >= 2 && Match.NumMatched == Match.NumAnimBones && !Match.IsPoor(), "CompareBones confirms the animation-only skeleton matches the mesh skeleton (%d/%d)", Match.NumMatched, Match.NumAnimBones );

			FModelAnimationDefinition MotionDef;
			MotionDef.PreviewModelPath = "bar_mesh.fbx";
			FModelClipDef WaveClip;
			WaveClip.Name = "Wave";
			WaveClip.SourcePath = "wave_only.fbx";
			WaveClip.AnimName = "Wave";
			WaveClip.Loop = EAnimLoopMode::Once;
			MotionDef.Clips.Add( WaveClip );
			C.Check( Assets->SaveModelAnimationDefinition( "motions.modelanim.json", MotionDef ), "a model-less motion pack definition can be saved" );
			const FModelBatchExportResult Batch = Assets->ExportModelsUsedBy( MotionDef );
			C.Check( Batch.NumFailed == 0, "ExportModelsUsedBy succeeds for all models referenced by the definition" );

			TSharedPtr<CModelAnimationAsset> Motions = Assets->LoadModelAnimations( "motions.modelanim.json" );
			C.Check( Motions.IsValid() && Motions->IsReady() && !Motions->GetModel().IsValid(), "the motion pack asset is ready without a bound model" );
			if ( Motions.IsValid() )
			{
				FModelAnimator Anim = Motions->CreateAnimatorFor( BarMesh );
				C.Check( Anim.Instance.IsValid() && Anim.HasClip( "Wave" ) && Anim.Play( "Wave", 0.0f ), "CreateAnimatorFor attaches the animation-only .mv1 clip to the character mesh" );
				Anim.Seek( 1.0f );
				FDrawBuffer Buffer;
				FDrawContext Draw( Buffer );
				Anim.Record( Draw, FMatrix4x4::Identity );
				ExecuteBuffer( *Executor, Buffer );

				const int32 H = Anim.Instance->GetHandle();
				MV1SetupReferenceMesh( H, -1, TRUE );
				MV1RefreshReferenceMesh( H, -1, TRUE );
				MV1_REF_POLYGONLIST Ref = MV1GetReferenceMesh( H, -1, TRUE );
				float32 MaxAbsX = 0.0f;
				for ( int32 v = 0; v < Ref.VertexNum; ++v ) MaxAbsX = std::max( MaxAbsX, std::fabs( Ref.Vertexs[v].Position.x ) );
				C.Check( MaxAbsX > 0.85f, "the animation-only .mv1 deforms the separate character mesh in DxLib (max |x| = %.2f)", MaxAbsX );
				MV1TerminateReferenceMesh( H, -1, TRUE );
			}
		}
	}

	//---------------------------------------------------------------------
	// 取り込んだモデルの、姿勢ごとの座標（スキンの向き・オフセットが DxLib で正しく効いているか）
	//---------------------------------------------------------------------
	C.Group = "model-import-pose";
	{
		TSharedPtr<CModel> Bar = Assets->LoadModel( "bar.fbx" );   // 2 回目の読み込み: 保存した .mv1 を読む
		C.Check( Bar.IsValid() && Bar->GetFileBytes() == NFileSystem::GetFileSize( Dir / "bar.mv1" ), "the second load reads the saved .mv1 (its file size is the .mv1's)" );
		TSharedPtr<CModelInstance> Instance = Bar.IsValid() ? Bar->CreateInstance() : TSharedPtr<CModelInstance>();
		if ( Instance.IsValid() )
		{
			const int32 H = Instance->GetHandle();
			C.Check( MV1GetTextureNum( H ) >= 1 && MV1GetTextureGraphHandle( H, 0 ) > 0, "the FBX's texture reached the .mv1 and was loaded (%d texture(s))", MV1GetTextureNum( H ) );

			FModelAnimator Anim( Instance );
			Anim.AddClipsFrom( Bar );
			Anim.Play( "Wave", 0.0f );
			MV1SetupReferenceMesh( H, -1, TRUE );
			const auto Pose = [&]( float32 Normalized ) -> MV1_REF_POLYGONLIST
			{
				Anim.Seek( Normalized );
				FDrawBuffer Buffer;
				FDrawContext Draw( Buffer );
				Anim.Record( Draw, FMatrix4x4::Identity );
				ExecuteBuffer( *Executor, Buffer );
				MV1RefreshReferenceMesh( H, -1, TRUE );
				return MV1GetReferenceMesh( H, -1, TRUE );
			};
			const auto CheckBounds = [&]( const MV1_REF_POLYGONLIST& Ref, const float32 Expected[6], const char* What )
			{
				const bool bOk = std::fabs( Ref.MinPosition.x - Expected[0] ) < 0.03 && std::fabs( Ref.MinPosition.y - Expected[1] ) < 0.03 && std::fabs( Ref.MinPosition.z - Expected[2] ) < 0.03
								 && std::fabs( Ref.MaxPosition.x - Expected[3] ) < 0.03 && std::fabs( Ref.MaxPosition.y - Expected[4] ) < 0.03 && std::fabs( Ref.MaxPosition.z - Expected[5] ) < 0.03;
				C.Check( bOk, "%s: bounds (%.3f %.3f %.3f) - (%.3f %.3f %.3f), expected (%.3f %.3f %.3f) - (%.3f %.3f %.3f)", What, Ref.MinPosition.x, Ref.MinPosition.y, Ref.MinPosition.z, Ref.MaxPosition.x,
						 Ref.MaxPosition.y, Ref.MaxPosition.z, Expected[0], Expected[1], Expected[2], Expected[3], Expected[4], Expected[5] );
			};
			// 下の輪は Bone1、上の輪は Bone2、真ん中は半々。Bone2 が Z まわりに 0 -> 90 度（左手系にすると z は -1.5 .. 0.5）
			const float32 AtStart[6] = { -0.5f, 0.0f, -1.5f, 0.5f, 2.0f, 0.5f };
			CheckBounds( Pose( 0.0f ), AtStart, "Wave at 0%" );
			const float32 Half = std::sqrt( 0.5f );   // 45 度: 上の輪は (x cos - sin, 1 + x sin + cos)
			const float32 AtMiddle[6] = { -0.5f * Half - Half, 0.0f, -1.5f, 0.5f, 1.0f + 0.5f * Half + Half, 0.5f };
			CheckBounds( Pose( 0.5f ), AtMiddle, "Wave at 50% (45 degrees)" );
			const MV1_REF_POLYGONLIST End = Pose( 1.0f );
			const float32 AtEnd[6] = { -1.0f, 0.0f, -1.5f, 0.5f, 1.5f, 0.5f };
			CheckBounds( End, AtEnd, "Wave at 100% (90 degrees)" );
			// 真ん中の輪（元は y = 1）は、最後には (x / 2, 1 + x / 2) にいる。4 つの角すべてに頂点がある
			int32 NumMiddle = 0;
			const float32 Corners[4][3] = { { 0.25f, 1.25f, 0.5f }, { -0.25f, 0.75f, 0.5f }, { 0.25f, 1.25f, -1.5f }, { -0.25f, 0.75f, -1.5f } };
			for ( const auto& Corner : Corners )
			{
				for ( int32 v = 0; v < End.VertexNum; ++v )
				{
					const VECTOR& P = End.Vertexs[v].Position;
					if ( std::fabs( P.x - Corner[0] ) < 0.03f && std::fabs( P.y - Corner[1] ) < 0.03f && std::fabs( P.z - Corner[2] ) < 0.03f )
					{
						++NumMiddle;
						break;
					}
				}
			}
			C.Check( NumMiddle == 4, "the half-weighted middle ring blends the two bones (%d of 4 corners found)", NumMiddle );
			MV1TerminateReferenceMesh( H, -1, TRUE );
		}

		// 圧縮しない保存でも、同じモデル
		FModelExportOptions Raw;
		Raw.OutputPath = "bar_raw.mv1";
		Raw.bCompress = false;
		Raw.bForce = true;
		const FModelExportResult RawResult = Assets->ExportModelAsMV1( "bar.fbx", Raw );
		TSharedPtr<CModel> RawModel = Assets->LoadModel( "bar_raw.mv1" );
		C.Check( RawResult.bSuccess && RawModel.IsValid() && RawModel->GetMeshCount() == 1 && RawModel->FindClip( "Wave" ) != nullptr, "an uncompressed .mv1 is written and loads (%s)", *RawResult.Error );
		if ( RawModel.IsValid() )
		{
			const VECTOR Min = MV1GetMeshMinPosition( RawModel->GetHandle(), 0 );
			const VECTOR Max = MV1GetMeshMaxPosition( RawModel->GetHandle(), 0 );
			C.Check( std::fabs( Min.z + 1.5f ) < 1.0e-3f && std::fabs( Max.z - 0.5f ) < 1.0e-3f && std::fabs( Max.y - 2.0f ) < 1.0e-3f, "and keeps the coordinates exactly (z %.4f .. %.4f)", Min.z, Max.z );
		}

		// 上の軸が Z の FBX（軸の変換がフレームの行列に入る）: 同じ形で、Y が上になる
		FFbxSpec ZUp;
		ZUp.bSkeleton = false;
		ZUp.bAnimation = false;
		ZUp.bZUp = true;
		WriteFileText( Dir / "barz.fbx", MakeBarFbx( ZUp ).c_str() );
		TSharedPtr<CModel> BarZ = Assets->LoadModel( "barz.fbx" );
		TSharedPtr<CModelInstance> ZInstance = BarZ.IsValid() ? BarZ->CreateInstance() : TSharedPtr<CModelInstance>();
		if ( ZInstance.IsValid() )
		{
			const int32 H = ZInstance->GetHandle();
			MV1SetupReferenceMesh( H, -1, TRUE );
			MV1RefreshReferenceMesh( H, -1, TRUE );
			const MV1_REF_POLYGONLIST Ref = MV1GetReferenceMesh( H, -1, TRUE );
			C.Check( std::fabs( Ref.MinPosition.x + 0.5f ) < 0.03f && std::fabs( Ref.MinPosition.y ) < 0.03f && std::fabs( Ref.MinPosition.z + 1.5f ) < 0.03f && std::fabs( Ref.MaxPosition.x - 0.5f ) < 0.03f
						 && std::fabs( Ref.MaxPosition.y - 2.0f ) < 0.03f && std::fabs( Ref.MaxPosition.z - 0.5f ) < 0.03f,
					 "a Z-up FBX is upright in DxLib too: (%.3f %.3f %.3f) - (%.3f %.3f %.3f)", Ref.MinPosition.x, Ref.MinPosition.y, Ref.MinPosition.z, Ref.MaxPosition.x, Ref.MaxPosition.y, Ref.MaxPosition.z );
			MV1TerminateReferenceMesh( H, -1, TRUE );
		}
		else
		{
			C.Check( false, "the Z-up FBX loads and has an instance" );
		}
	}

	//---------------------------------------------------------------------
	// 配布時の流れ: 定義（.modelanim.json）と .mv1 だけで、時間を進めて再生する（元の .fbx は無い）
	//---------------------------------------------------------------------
	C.Group = "model-playback";
	{
		// 作る側: 定義を書いて保存（FBX から .mv1 を作る）
		FModelAnimationDefinition Authored;
		Authored.ModelPath = "bar.fbx";
		FModelClipDef Once;
		Once.Name = "Wave";
		Once.AnimName = "Wave";
		Once.Loop = EAnimLoopMode::Once;
		Once.FadeTime = 0.0f;
		Once.Events.Add( FModelEventDef { 0.5f, "Half" } );
		FModelClipDef Looping;
		Looping.Name = "WaveFromMotion";   // アニメーションだけのファイルから
		Looping.SourcePath = "wave_only.fbx";
		Looping.AnimName = "Wave";
		Looping.Loop = EAnimLoopMode::Loop;
		Looping.FadeTime = 0.4f;
		Authored.Clips = { Once, Looping };
		C.Check( Assets->SaveModelAnimationDefinition( "shipped.modelanim.json", Authored ), "the authored definition is saved" );
		const FModelBatchExportResult Exported = Assets->ExportModelsUsedBy( Authored );
		C.Check( Exported.NumFailed == 0 && NFileSystem::FileExists( Dir / "bar.mv1" ) && NFileSystem::FileExists( Dir / "wave_only.mv1" ), ".mv1 files are output for every model the definition uses (%s)", *Exported.GetSummary() );

		// 手放して、元のファイルを消す（配布版: .mv1 と定義だけ）
		Assets->Tick( 0.0f );
		Assets->ReleaseUnused();
		MoveFileA( *( Dir / "bar.fbx" ), *( Dir / "bar_source.fbx.bak" ) );
		MoveFileA( *( Dir / "wave_only.fbx" ), *( Dir / "wave_only_source.fbx.bak" ) );

		TSharedPtr<CModelAnimationAsset> Shipped = Assets->LoadModelAnimations( "shipped.modelanim.json" );
		C.Check( Shipped.IsValid() && Shipped->IsReady() && Shipped->GetModel().IsValid() && Shipped->GetModel()->HasMesh(), "the definition loads without the source FBX (the .mv1 is used)" );
		if ( Shipped.IsValid() )
		{
			FModelAnimator Anim = Shipped->CreateAnimator();
			C.Check( Anim.Instance.IsValid() && Anim.HasClip( "Wave" ) && Anim.HasClip( "WaveFromMotion" ), "the animator has both clips (one from the model, one from the animation-only .mv1)" );
			C.Near( Anim.GetClipLength( "Wave" ), 1.0, 0.05, "Wave is 1 second long" );
			if ( Anim.Instance.IsValid() )
			{
				const int32 H = Anim.Instance->GetHandle();
				MV1SetupReferenceMesh( H, -1, TRUE );
				const auto Render = [&]() -> MV1_REF_POLYGONLIST
				{
					FDrawBuffer Buffer;
					FDrawContext Draw( Buffer );
					Anim.Record( Draw, FMatrix4x4::Identity );
					ExecuteBuffer( *Executor, Buffer );
					MV1RefreshReferenceMesh( H, -1, TRUE );
					return MV1GetReferenceMesh( H, -1, TRUE );
				};
				const auto BoundsAre = [&]( const MV1_REF_POLYGONLIST& Ref, float32 MinX, float32 MaxY ) { return std::fabs( Ref.MinPosition.x - MinX ) < 0.04f && std::fabs( Ref.MaxPosition.y - MaxY ) < 0.04f; };
				const float32 Half = std::sqrt( 0.5f );

				// 1. Once: 0.1 秒ずつ進める。出来事は 1 回だけ、1 秒で終わり、最後の姿勢は 90 度
				Anim.Play( "Wave" );
				int32 NumHalf = 0;
				int32 FinishedStep = -1;
				bool bMonotonic = true;
				float32 PreviousMeanX = 1.0e9f;
				for ( int32 Step = 1; Step <= 14; ++Step )
				{
					Anim.Update( 0.1f );
					if ( Anim.HasEvent( "Half" ) ) ++NumHalf;
					if ( Anim.IsFinished() && FinishedStep < 0 ) FinishedStep = Step;
					const MV1_REF_POLYGONLIST Ref = Render();
					// 曲がるほど、頂点の x の平均は左へ行く（最小の x は 63 度あたりで折り返すので使えない）
					float32 MeanX = 0.0f;
					for ( int32 v = 0; v < Ref.VertexNum; ++v ) MeanX += Ref.Vertexs[v].Position.x;
					MeanX /= static_cast<float32>( std::max( Ref.VertexNum, 1 ) );
					bMonotonic = bMonotonic && MeanX <= PreviousMeanX + 1.0e-3f;
					PreviousMeanX = MeanX;
				}
				C.Check( NumHalf == 1, "the event at 50%% fires exactly once during the playback (%d)", NumHalf );
				C.Check( FinishedStep >= 10 && FinishedStep <= 11, "a Once clip finishes after about 1 second (step %d)", FinishedStep );
				C.Check( bMonotonic, "the mesh bends steadily while it plays" );
				C.Check( BoundsAre( Render(), -1.0f, 1.5f ), "and stays at the 90 degree pose after it finished" );

				// 2. アニメーションだけのファイルの clip を、時間を進めて再生（0.5 秒 = 45 度）
				FModelPlayParams Restart;
				Restart.FadeTime = 0.0f;
				Restart.bRestartIfPlaying = true;
				Anim.Play( "WaveFromMotion", Restart );
				Anim.Update( 0.5f );
				const MV1_REF_POLYGONLIST Middle = Render();
				C.Check( BoundsAre( Middle, -0.5f * Half - Half, 1.0f + 0.5f * Half + Half ), "0.5 s into the clip from the animation-only .mv1 the character is at 45 degrees (%.3f, %.3f)", Middle.MinPosition.x, Middle.MaxPosition.y );
				for ( int32 Step = 0; Step < 30; ++Step ) Anim.Update( 0.1f );   // 3 秒（3 周）
				C.Check( !Anim.IsFinished() && Anim.IsPlaying(), "a looping clip keeps playing" );

				// 3. クロスフェード: Once の Wave -> ループ（既定のフェード 0.4 秒）
				Anim.Play( "Wave", Restart );
				Anim.Update( 0.3f );
				Anim.Play( "WaveFromMotion" );
				Anim.Update( 0.2f );
				Render();   // 重み（ブレンド率）は、描くときにモデルへ入る
				const TArray<FModelAnimator::FLayerInfo> Layers = Anim.GetLayerInfos();
				float32 Sum = 0.0f;
				bool bBoth = Layers.Num() == 2;
				for ( const FModelAnimator::FLayerInfo& Layer : Layers )
				{
					Sum += Layer.Weight;
					bBoth = bBoth && Layer.Weight > 0.05f && Layer.Weight < 0.95f;
					bBoth = bBoth && std::fabs( MV1GetAttachAnimBlendRate( H, Layer.AttachIndex ) - Layer.Weight ) < 1.0e-3f;
				}
				C.Check( bBoth && std::fabs( Sum - 1.0f ) < 1.0e-3f, "mid-fade both animations are attached to the DxLib model with their weights (%d layer(s), total %.3f)", Layers.Num(), Sum );
				for ( int32 Step = 0; Step < 6; ++Step ) Anim.Update( 0.1f );
				C.Check( Anim.GetNumActiveLayers() == 1 && Anim.GetCurrentClip() == "WaveFromMotion", "after the fade only the new clip is left" );
				MV1TerminateReferenceMesh( H, -1, TRUE );
			}
		}
		Shipped.Reset();
		Assets->Tick( 0.0f );
		Assets->ReleaseUnused();
		MoveFileA( *( Dir / "bar_source.fbx.bak" ), *( Dir / "bar.fbx" ) );
		MoveFileA( *( Dir / "wave_only_source.fbx.bak" ), *( Dir / "wave_only.fbx" ) );
	}

	//---------------------------------------------------------------------
	// サウンド
	//---------------------------------------------------------------------
	C.Group = "sound";
	S.RealSound = Assets->LoadSound( "beep.wav" );
	if ( !S.RealSound.IsValid() )
	{
		C.Skip( "the generated WAV file could not be loaded (no audio device?)" );
	}
	else
	{
		C.Near( S.RealSound->GetLength(), 0.5, 0.05, "the length of the sound is 0.5 s" );
		C.Check( S.RealSound->GetMemoryBytes() > 0, "sound memory is estimated" );

		// バックエンドの対応: 音量・パン・ピッチが DxLib の値になる
		const int32 Voice = S.RealSound->AcquireVoice();
		C.Check( Voice >= 0, "a voice (a duplicated handle) can be borrowed" );
		if ( Voice >= 0 )
		{
			CDxLibAudioBackend* Backend = GetAppSubsystem<CDxLibAudioBackend>();
			C.Check( Backend != nullptr, "the DxLib audio backend exists" );
			if ( Backend != nullptr )
			{
				Backend->SetVolume( Voice, 1.0f );
				C.Check( GetVolumeSoundMem( Voice ) == 10000, "volume 1.0 is the maximum (10000, got %d)", GetVolumeSoundMem( Voice ) );
				Backend->SetVolume( Voice, 0.5f );
				C.Near( GetVolumeSoundMem( Voice ), 10000 - 602, 4.0, "volume 0.5 is about -6 dB" );
				Backend->SetVolume( Voice, 0.0f );
				C.Check( GetVolumeSoundMem( Voice ) == 0, "volume 0 is silent (got %d)", GetVolumeSoundMem( Voice ) );

				Backend->SetPan( Voice, 0.0f );
				C.Check( GetPanSoundMem( Voice ) == 0, "pan 0 is the centre (got %d)", GetPanSoundMem( Voice ) );
				Backend->SetPan( Voice, 0.5f );
				C.Near( GetPanSoundMem( Voice ), 602, 4.0, "pan +0.5 attenuates the left by about 6 dB" );
				Backend->SetPan( Voice, -0.5f );
				C.Near( GetPanSoundMem( Voice ), -602, 4.0, "pan -0.5 attenuates the right by about 6 dB (negative = left)" );

				Backend->SetPitch( Voice, 1.0f );
				const int Base = GetFrequencySoundMem( Voice );
				Backend->SetPitch( Voice, 2.0f );
				C.Check( GetFrequencySoundMem( Voice ) == Base * 2, "pitch 2.0 doubles the frequency (%d -> %d)", Base, GetFrequencySoundMem( Voice ) );
				Backend->SetPitch( Voice, 0.5f );
				C.Check( GetFrequencySoundMem( Voice ) == Base / 2, "pitch 0.5 halves it (relative to the original, not to the previous setting)" );
				Backend->SetPitch( Voice, 1.0f );
				C.Check( GetFrequencySoundMem( Voice ) == Base, "pitch 1.0 restores it" );

				Backend->SetVolume( Voice, 0.0f );   // 試しに鳴らすので、無音で
				if ( Backend->Play( Voice, false, true ) )
				{
					C.Check( Backend->IsPlaying( Voice ), "IsPlaying is true right after Play" );
					Backend->Stop( Voice );
					C.Check( !Backend->IsPlaying( Voice ), "and false after Stop" );
				}
				else
				{
					C.Skip( "the sound could not be played (no audio device?)" );
				}
			}
			S.RealSound->ReleaseVoice( Voice );
		}

		// オーディオシステム全体: 鳴らして、鳴り終わったら片付く（次のフェーズで待つ）
		if ( Audio != nullptr )
		{
			FSoundPlayParams Quiet;
			Quiet.Volume = 0.0f;   // テストで音を出さない
			S.RealHandle = Audio->Play( S.RealSound, Quiet );
			if ( S.RealHandle.IsValid() )
			{
				C.Check( Audio->IsPlaying( S.RealHandle ) && Audio->GetNumVoices() == 1, "the audio system plays a real sound" );
				FSoundPlayParams Second = Quiet;
				FSoundHandle Overlapping = Audio->Play( S.RealSound, Second );
				C.Check( Overlapping.IsValid() && Audio->GetNumVoices() == 2, "the same sound overlaps itself (duplicated handles)" );
			}
			else
			{
				C.Skip( "the audio system could not play the sound (no audio device?)" );
			}
		}

		// ストリーム: 同時に 1 つ
		TSharedPtr<CSound> Stream = Assets->LoadSound( "beep.wav", ESoundLoadMode::Stream );
		C.Check( Stream.IsValid() && Stream != S.RealSound, "the same file can also be loaded as a stream" );
		if ( Stream.IsValid() && Audio != nullptr )
		{
			FSoundPlayParams Quiet;
			Quiet.Volume = 0.0f;
			FSoundHandle First = Audio->Play( Stream, Quiet );
			if ( First.IsValid() ) C.Check( !Audio->Play( Stream, Quiet ).IsValid(), "a stream plays only once at a time" );
			Audio->Stop( First );
		}
	}

	//---------------------------------------------------------------------
	// 集計（本物のバックエンドの値）
	//---------------------------------------------------------------------
	C.Group = "stats";
	Assets->UpdateStats();
	const FAssetStats& Stats = Assets->GetStats();
	C.Check( Stats.TotalCount > 0 && Stats.TotalMemoryBytes > 0, "the manager counts the loaded assets and their memory" );
	C.Check( Stats.Backend.ProcessWorkingSet > 0, "the process working set is reported (%lld bytes)", static_cast<long long>( Stats.Backend.ProcessWorkingSet ) );
	C.Check( Stats.Backend.NumTextureHandles > 0, "DxLib's graph handle count is reported" );
	if ( Stats.Backend.VideoMemoryTotal > 0 ) C.Check( Stats.Backend.VideoMemoryUsed > 0 && Stats.Backend.VideoMemoryUsed <= Stats.Backend.VideoMemoryTotal, "video memory used / total are plausible" );
	else C.Skip( "video memory info is not available" );
	Assets->DumpToLog();

	Assets->SetRootDirectory( "Assets" );
	(void)Model;
}

void CAssetSelfTestScene::FinishRealSound()
{
	FState& S = *State;
	FChecker& C = S.Checker;
	CAudioSystem* Audio = GetAppSubsystem<CAudioSystem>();

	// 0.5 秒の音が鳴り終わるのを待つ（最大 3 秒）
	const bool bPlayed = S.RealHandle.IsValid();
	if ( bPlayed && Audio != nullptr && Audio->GetNumVoices() > 0 && S.RealWaited < 3.0f ) return;

	if ( bPlayed && Audio != nullptr )
	{
		C.Group = "sound";
		C.Check( Audio->GetNumVoices() == 0 && !Audio->IsPlaying( S.RealHandle ), "sounds that finished by themselves are freed (waited %.2f s)", S.RealWaited );
		C.Check( S.RealSound->GetNumVoicesInUse() == 0 && S.RealSound->GetNumIdleVoices() >= 1, "and their voices are back in the pool" );
	}
	S.RealSound.Reset();

	if ( S.Phase == 4 ) return;
	S.Phase = 4;
	RemoveDirectoryRecursive( S.TempRoot );

	if ( C.NumFailed == 0 )
	{
		LOG( LogAssetSelfTest, Log, "SELFTEST assets PASS: %lld check(s), %lld skipped", static_cast<long long>( C.NumChecked ), static_cast<long long>( C.NumSkipped ) );
	}
	else
	{
		LOG( LogAssetSelfTest, Error, "SELFTEST assets FAIL: %lld of %lld check(s) failed", static_cast<long long>( C.NumFailed ), static_cast<long long>( C.NumChecked ) );
	}
	if ( S.bExitWhenDone )
	{
		if ( IPlatform* Platform = GetAppSubsystem<IPlatform>() ) Platform->RequestExit();
	}
}
