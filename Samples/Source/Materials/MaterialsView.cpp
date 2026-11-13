#include "MaterialsView.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Source/Common/ShaderMeshes.h"

namespace
{
	// 場面の空の色（晴れた昼。環境マップの空 Engine:/Environments/Sky.png の、地平線の近くの色）
	const FColor32 SkyColor( 150, 182, 228, 255 );

	// 太陽（平行光）の色と、空の映り込みの明るさ。点光源（手前の球を、横から照らす、暖かい色の明かり）
	const FVector3 SunColor( 0.95f, 0.92f, 0.86f );
	constexpr float32 SkyIntensity = 0.55f;
	const FVector3 LampPosition( 5.9f, 2.6f, -9.4f );
	const FVector3 LampColor( 1.6f, 0.85f, 0.35f );
	constexpr float32 LampRadius = 7.0f;

	// 影を落とす範囲（場面の中心と、半径）
	const FVector3 ShadowCenter( 1.5f, 1.0f, -2.5f );
	constexpr float32 ShadowRadius = 17.0f;

	// 物 1 つ: 形・位置・大きさ・回るか
	struct FStageObject
	{
		bool bCube;
		FVector3 Position;
		FVector3 Scale;
		bool bSpin;
	};

	// 球 3 つと柱。マテリアルは、上から順に、大理石・車の塗装・縁の光・市松模様
	const FStageObject StageObjects[] = {
		{ false, FVector3( -7.0f, 1.6f, 1.0f ), FVector3( 1.6f, 1.6f, 1.6f ), false },
		{ false, FVector3( 0.0f, 2.2f, 3.0f ), FVector3( 2.2f, 2.2f, 2.2f ), false },
		{ false, FVector3( 7.5f, 1.3f, -1.5f ), FVector3( 1.3f, 1.3f, 1.3f ), false },
		{ true, FVector3( -1.0f, 3.0f, -8.0f ), FVector3( 3.0f, 6.0f, 3.0f ), true },
	};

	// 手前に並べる、Substrate 風の小さな球（奥の列の左から、磨いた金属・真珠・ろう・さびた金属。手前の列は、キャンディ塗装・槌目の金属）
	const FVector3 SubstratePositions[FMaterialsDemoState::NumSubstrateSpheres] = { FVector3( 2.0f, 1.0f, -6.5f ),  FVector3( 4.6f, 1.0f, -6.5f ),  FVector3( 7.2f, 1.0f, -6.5f ),
																					 FVector3( 9.8f, 1.0f, -6.5f ),  FVector3( 3.3f, 1.0f, -9.6f ),  FVector3( 8.5f, 1.0f, -9.6f ) };
	constexpr float32 SubstrateRadius = 1.0f;

	// 文字の行の高さと、説明の位置
	constexpr int32 LegendMargin = 16;
	constexpr int32 LegendLineHeight = 22;

	// 場面の形を、1 つずつ、Visit( マテリアル, メッシュ, ワールド行列 ) へ渡す（影のマップと、場面とで、同じものを描くため）
	template<typename TVisit>
	void VisitShapes( const FMaterialsDemoState& State, TVisit&& Visit )
	{
		// 地面と柱（市松模様）
		Visit( *State.Checker, NShaderMeshes::GetGround(), FMatrix4x4::Identity );
		// 球 3 つ。それぞれ別のマテリアル
		const TSharedPtr<CMaterial> SphereMaterials[3] = { State.Marble, State.CarPaint, State.RimGlow };
		int32 SphereIndex = 0;
		for ( const FStageObject& Object : StageObjects )
		{
			if ( Object.bCube )
			{
				FMatrix4x4 World = FMatrix4x4::Scale( Object.Scale ) * FMatrix4x4::RotateY( State.Time * 0.5f );
				World = World * FMatrix4x4::Translate( Object.Position );
				Visit( *State.Checker, NShaderMeshes::GetCube(), World );
			}
			else
			{
				Visit( *SphereMaterials[SphereIndex++], NShaderMeshes::GetSphere(), FMatrix4x4::Scale( Object.Scale ) * FMatrix4x4::Translate( Object.Position ) );
			}
		}
		// 手前の小さな球: Substrate 風のマテリアル（ゆっくり回して、光沢の動きを見せる）
		for ( int32 Index = 0; Index < FMaterialsDemoState::NumSubstrateSpheres; ++Index )
		{
			const FMatrix4x4 World = FMatrix4x4::Scale( FVector3( SubstrateRadius, SubstrateRadius, SubstrateRadius ) ) * FMatrix4x4::RotateY( State.Time * 0.3f ) * FMatrix4x4::Translate( SubstratePositions[Index] );
			Visit( *State.SubstrateSpheres[Index], NShaderMeshes::GetSphere(), World );
		}
	}
}

CMaterialsView::CMaterialsView( const TSharedPtr<FMaterialsDemoState>& InState, const TSharedPtr<CFont>& InFont )
	: State( InState )
	, Font( InFont )
{
}

void CMaterialsView::RecordShadow( FDrawContext& CommandList ) const
{
	if ( !State->Shadow.IsValid() )
	{
		return;
	}
	// 太陽の向き（マテリアルの既定の平行光）から見た、場面の深度を描く
	State->Shadow->SetLight( FMaterialSceneState().LightDirection, ShadowCenter, ShadowRadius );
	CommandList.Culling( EDrawCulling::None );
	if ( State->Shadow->Begin( CommandList ) )
	{
		VisitShapes( *State, [&]( CMaterial&, const NShaderMeshes::FMesh& Mesh, const FMatrix4x4& World )
		{
			State->Shadow->DrawMesh( CommandList, Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num(), World );
		} );
		State->Shadow->End( CommandList );
	}
}

void CMaterialsView::RecordScene( FDrawContext& CommandList ) const
{
	const FCamera& Camera = State->Camera;
	FMaterialSceneState Scene;
	Scene.ViewProjection = Camera.GetViewMatrix() * Camera.GetProjectionMatrix( State->GetSceneViewport() );
	Scene.CameraPosition = Camera.Position;
	Scene.Time = State->Time;
	Scene.NearPlane = Camera.Near;
	Scene.FarPlane = Camera.Far;
	// 光: 太陽（影つき）、手前の球を照らす点光源、空の映り込み
	Scene.LightColor = SunColor;
	Scene.NumPointLights = 1;
	Scene.PointLights[0].Position = LampPosition;
	Scene.PointLights[0].Color = LampColor;
	Scene.PointLights[0].Radius = LampRadius;
	Scene.EnvironmentMap = State->Environment;
	Scene.EnvironmentIntensity = SkyIntensity;
	Scene.ShadowMap = State->Shadow;

	FDepthState DepthState;
	DepthState.bTestDepth = true;
	DepthState.bWriteDepth = true;
	CommandList.Depth( DepthState );
	CommandList.Culling( EDrawCulling::None );

	VisitShapes( *State, [&]( CMaterial& Material, const NShaderMeshes::FMesh& Mesh, const FMatrix4x4& World )
	{
		if ( Material.BeginSurface( CommandList, Scene ) )
		{
			Material.DrawSurfaceMesh( CommandList, Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num(), World, FColor32( 255, 255, 255, 255 ) );
			Material.EndSurface( CommandList );
		}
	} );
}

void CMaterialsView::RecordUi( FDrawContext& CommandList ) const
{
	FMaterialFrameState Frame;
	Frame.Time = State->Time;
	Frame.Hover = State->bButtonHovered ? 1.0f : 0.0f;
	Frame.Press = State->bButtonPressed ? 1.0f : 0.0f;
	Frame.Value = 0.5f;

	FDepthState NoDepth;
	NoDepth.bTestDepth = false;
	NoDepth.bWriteDepth = false;
	CommandList.Depth( NoDepth );
	CommandList.Blend( EDrawBlend::Alpha, 255 );
	State->GlowButton->DrawRect( CommandList, static_cast<float32>( FMaterialsDemoState::ButtonLeft ), static_cast<float32>( FMaterialsDemoState::ButtonTop ), static_cast<float32>( FMaterialsDemoState::ButtonWidth ),
								 static_cast<float32>( FMaterialsDemoState::ButtonHeight ), FColor32( 255, 255, 255, 255 ), Frame );
	Frame.Hover = 0.0f;
	Frame.Press = 0.0f;
	State->Shimmer->DrawRect( CommandList, static_cast<float32>( FMaterialsDemoState::BandLeft ), static_cast<float32>( FMaterialsDemoState::BandTop ), static_cast<float32>( FMaterialsDemoState::BandWidth ),
							  static_cast<float32>( FMaterialsDemoState::BandHeight ), FColor32( 255, 255, 255, 255 ), Frame );
	CommandList.Blend( EDrawBlend::Opaque );
}

void CMaterialsView::RecordLegend( FDrawContext& CommandList ) const
{
	if ( !Font.IsValid() )
	{
		return;
	}
	FTextStyle Shadow;
	Shadow.bShadow = true;
	Shadow.ShadowColor = FColor32( 0, 0, 0 );
	const char* PassNames[FMaterialsDemoState::NumPostPasses] = { "Grayscale", "Vignette", "Pixelate" };
	const int32 Left = FMaterialsDemoState::ViewLeft + LegendMargin;
	const int32 Top = FMaterialsDemoState::ViewTop + LegendMargin;
	FString Lines[FMaterialsDemoState::NumPostPasses + 4];
	for ( int32 Index = 0; Index < FMaterialsDemoState::NumPostPasses && Index < State->PostStack.Num(); ++Index )
	{
		Lines[Index] = FString::Printf( "%d  Post process: %-10s %s", Index + 1, PassNames[Index], State->PostStack.GetEntry( Index ).bEnabled ? "on" : "off" );
	}
	Lines[FMaterialsDemoState::NumPostPasses] = FString::Printf( "4  Time: %s (%.1f s)", State->bTimeRunning ? "running" : "stopped", State->Time );
	Lines[FMaterialsDemoState::NumPostPasses + 1] = "Marble, CarPaint, RimGlow, Checker: node-graph materials (EngineContent/Materials)";
	Lines[FMaterialsDemoState::NumPostPasses + 2] = "Front rows (Substrate): BrushedMetal, Pearl, Wax, RustedMetal / CandyPaint, HammeredMetal";
	Lines[FMaterialsDemoState::NumPostPasses + 3] = "Light: sun with a shadow map, a warm point light, sky environment map";
	for ( int32 Index = 0; Index < FMaterialsDemoState::NumPostPasses + 4; ++Index )
	{
		const FColor32 Color = Index >= FMaterialsDemoState::NumPostPasses + 1 ? FColor32( 170, 176, 192 ) : FColor32( 226, 232, 246 );
		Font->RecordStyled( CommandList, static_cast<float32>( Left ), static_cast<float32>( Top + Index * LegendLineHeight ), *Lines[Index], Color, Shadow );
	}
	Font->RecordStyled( CommandList, static_cast<float32>( FMaterialsDemoState::ButtonLeft + 12 ), static_cast<float32>( FMaterialsDemoState::ButtonTop - 26 ), "GlowButton (hover / press)", FColor32( 226, 232, 246 ), Shadow );
	Font->RecordStyled( CommandList, static_cast<float32>( FMaterialsDemoState::BandLeft + 12 ), static_cast<float32>( FMaterialsDemoState::BandTop - 26 ), "Shimmer (time)", FColor32( 226, 232, 246 ), Shadow );
}

void CMaterialsView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !State->SceneTarget.IsValid() || !State->Camera.IsValid() )
	{
		return;
	}
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets == nullptr )
	{
		return;
	}

	// ⓪ 影を落とす形を、影のマップへ描く
	RecordShadow( CommandList );

	// ① 場面を、描画先へ描く
	if ( !CommandList.PushTarget( State->SceneTarget ) )
	{
		return;
	}
	CommandList.Clear( SkyColor );
	CommandList.ClearDepth();
	RecordScene( CommandList );
	CommandList.PopTarget();

	// ② 描画先の絵を、ポストプロセスのチェーンに通して貼る。有効なパスが無いときは、そのまま貼る
	FDepthState NoDepth;
	NoDepth.bTestDepth = false;
	NoDepth.bWriteDepth = false;
	CommandList.Depth( NoDepth );
	FMaterialFrameState Frame;
	Frame.Time = State->Time;
	Frame.NearPlane = State->Camera.Near;
	Frame.FarPlane = State->Camera.Far;
	const bool bPosted = State->PostStack.Record( CommandList, *Assets, State->SceneTarget, TSharedPtr<IImage>(), FMaterialsDemoState::ViewWidth, FMaterialsDemoState::ViewHeight, static_cast<float32>( FMaterialsDemoState::ViewLeft ),
												  static_cast<float32>( FMaterialsDemoState::ViewTop ), static_cast<float32>( FMaterialsDemoState::ViewWidth ), static_cast<float32>( FMaterialsDemoState::ViewHeight ), Frame );
	if ( !bPosted )
	{
		CommandList.Image( static_cast<float32>( FMaterialsDemoState::ViewLeft ), static_cast<float32>( FMaterialsDemoState::ViewTop ), State->SceneTarget );
	}

	// ③ UI のマテリアルと説明
	RecordUi( CommandList );
	RecordLegend( CommandList );
}
