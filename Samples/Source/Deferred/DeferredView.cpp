#include "DeferredView.h"

#include "Library/Framework/Render/Deferred/GBufferView.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Source/Common/ShaderMeshes.h"
#include "Source/Deferred/DeferredSources.h"

namespace
{
	// 夜空の色（Forward の描画先を消す色）。ライティングのシェーダーの SkyColor と同じ値
	const FColor32 SkyColor( 6, 8, 18, 255 );

	// 動く光: 届く半径・明るさ・光る玉の大きさ・玉が自分で光る明るさ
	constexpr float32 LightRadius = 11.0f;
	constexpr float32 LightIntensity = 1.6f;
	constexpr float32 OrbSize = 0.3f;
	constexpr float32 OrbGlow = 6.0f;
	const FVector3 LightColors[6] = {
		FVector3( 1.0f, 0.55f, 0.2f ),
		FVector3( 0.3f, 0.7f, 1.0f ),
		FVector3( 1.0f, 0.3f, 0.6f ),
		FVector3( 0.5f, 1.0f, 0.4f ),
		FVector3( 1.0f, 0.85f, 0.4f ),
		FVector3( 0.6f, 0.4f, 1.0f ),
	};

	// 光を受ける物 1 つ: 形・位置・大きさ・地の色
	struct FStageObject
	{
		bool bCube;
		FVector3 Position;
		FVector3 Scale;
		FVector3 Albedo;
	};

	const FStageObject StageObjects[] = {
		{ false, FVector3( -6.0f, 1.6f, 2.0f ), FVector3( 1.6f, 1.6f, 1.6f ), FVector3( 0.9f, 0.85f, 0.8f ) },
		{ false, FVector3( 3.0f, 2.2f, 4.0f ), FVector3( 2.2f, 2.2f, 2.2f ), FVector3( 0.7f, 0.8f, 0.9f ) },
		{ false, FVector3( 6.5f, 1.3f, -3.5f ), FVector3( 1.3f, 1.3f, 1.3f ), FVector3( 0.9f, 0.75f, 0.6f ) },
		{ false, FVector3( -3.0f, 1.0f, -6.0f ), FVector3( 1.0f, 1.0f, 1.0f ), FVector3( 0.8f, 0.9f, 0.75f ) },
		{ false, FVector3( 10.0f, 1.8f, 6.0f ), FVector3( 1.8f, 1.8f, 1.8f ), FVector3( 0.85f, 0.7f, 0.85f ) },
		{ false, FVector3( -11.0f, 1.5f, -1.0f ), FVector3( 1.5f, 1.5f, 1.5f ), FVector3( 0.75f, 0.8f, 0.95f ) },
		{ true, FVector3( -1.0f, 3.0f, -7.0f ), FVector3( 3.0f, 6.0f, 3.0f ), FVector3( 0.6f, 0.6f, 0.7f ) },
		{ true, FVector3( 8.0f, 2.5f, -9.0f ), FVector3( 2.0f, 5.0f, 2.0f ), FVector3( 0.7f, 0.65f, 0.6f ) },
		{ true, FVector3( -9.0f, 2.0f, 7.0f ), FVector3( 2.5f, 4.0f, 2.5f ), FVector3( 0.65f, 0.7f, 0.65f ) },
	};

	// 光 Index の位置（3 つの輪の上を、輪ごとに違う向きと速さで回り、上下に揺れる）
	FVector3 GetLightPosition( int32 Index, float32 Time )
	{
		const int32 Ring = Index % 3;
		const int32 InRing = Index / 3;
		const float32 Radius = 5.0f + 5.5f * static_cast<float32>( Ring );
		const float32 Direction = Ring % 2 == 0 ? 1.0f : -1.0f;
		const float32 Angle = Direction * Time * ( 0.45f - 0.1f * static_cast<float32>( Ring ) ) + static_cast<float32>( InRing ) * NMath::TWO_PI / 8.0f + static_cast<float32>( Ring ) * 0.7f;
		const float32 Height = 1.4f + 0.6f * NMath::Sin( Time * 1.1f + static_cast<float32>( Index ) );
		return FVector3( NMath::Cos( Angle ) * Radius, Height, NMath::Sin( Angle ) * Radius );
	}

	// 形を 1 つ描く（物の定数を、頂点側とピクセル側に置いて、インデックスつきで描く）
	void RecordMesh( FDrawContext& CommandList, const NShaderMeshes::FMesh& Mesh, const FMatrix4x4& World, const FVector3& Albedo, const FVector3& Emission )
	{
		NDeferredSources::FObjectConstants Object;
		Object.World = World;
		Object.Albedo = FVector4( Albedo, 1.0f );
		Object.Emission = FVector4( Emission, 0.0f );
		CommandList.ShaderConstants( EShaderStage::Vertex, 5, Object );
		CommandList.ShaderConstants( EShaderStage::Pixel, 5, Object );
		CommandList.PrimitiveIndexedShader3D( Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num(), EDrawPrimitive::Triangles );
	}

	// 場面の定数（カメラの行列・時間・見せるもの・動く光）
	NDeferredSources::FSceneConstants MakeSceneConstants( const FDeferredDemoState& State )
	{
		NDeferredSources::FSceneConstants Scene;
		Scene.ViewProjection = State.Camera.GetViewMatrix() * State.Camera.GetProjectionMatrix( State.GetSceneViewport() );
		Scene.CameraPosition = FVector4( State.Camera.Position, State.Camera.Far );
		Scene.Parameters = FVector4( State.Time, static_cast<float32>( State.ViewMode ), 0.0f, 0.0f );
		for ( int32 Index = 0; Index < FDeferredDemoState::NumLights; ++Index )
		{
			Scene.LightPositions[Index] = FVector4( GetLightPosition( Index, State.Time ), LightRadius );
			Scene.LightColors[Index] = FVector4( LightColors[Index % 6] * LightIntensity, 0.0f );
		}
		return Scene;
	}
}

CDeferredView::CDeferredView( const TSharedPtr<FDeferredDemoState>& InState )
	: State( InState )
{
}

void CDeferredView::RecordScene( FDrawContext& CommandList, bool bGeometryPass ) const
{
	const NDeferredSources::FSceneConstants Scene = MakeSceneConstants( *State );
	CommandList.ShaderConstants( EShaderStage::Vertex, 4, Scene );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Scene );
	CommandList.Culling( EDrawCulling::None );
	CommandList.UseVertexShader( State->SceneVertex );

	// 地面
	CommandList.UsePixelShader( bGeometryPass ? State->GeometryGroundPixel : State->ForwardGroundPixel );
	RecordMesh( CommandList, NShaderMeshes::GetGround(), FMatrix4x4::Identity, FVector3( 1.0f, 1.0f, 1.0f ), FVector3( 0.0f, 0.0f, 0.0f ) );

	// 光を受ける物（球と柱）。柱はゆっくり回る
	CommandList.UsePixelShader( bGeometryPass ? State->GeometryObjectPixel : State->ForwardObjectPixel );
	for ( const FStageObject& Object : StageObjects )
	{
		FMatrix4x4 World = FMatrix4x4::Scale( Object.Scale );
		if ( Object.bCube )
		{
			World = World * FMatrix4x4::RotateY( State->Time * 0.3f );
		}
		World = World * FMatrix4x4::Translate( Object.Position );
		RecordMesh( CommandList, Object.bCube ? NShaderMeshes::GetCube() : NShaderMeshes::GetSphere(), World, Object.Albedo, FVector3( 0.0f, 0.0f, 0.0f ) );
	}

	// 光る玉（地の色は黒で、光る色だけ。光の位置に置く）
	for ( int32 Index = 0; Index < FDeferredDemoState::NumLights; ++Index )
	{
		const FMatrix4x4 World = FMatrix4x4::Scale( FVector3( OrbSize, OrbSize, OrbSize ) ) * FMatrix4x4::Translate( GetLightPosition( Index, State->Time ) );
		RecordMesh( CommandList, NShaderMeshes::GetSphere(), World, FVector3( 0.0f, 0.0f, 0.0f ), LightColors[Index % 6] * OrbGlow );
	}
}

void CDeferredView::RecordLighting( FDrawContext& CommandList ) const
{
	FGBufferView View;
	if ( !FGBufferView::Make( State->Camera, State->GetSceneViewport(), View ) || !State->GBuffer.UseForLighting( CommandList ) )
	{
		return;
	}
	const NDeferredSources::FSceneConstants Scene = MakeSceneConstants( *State );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Scene );
	CommandList.ShaderConstants( EShaderStage::Pixel, CGBuffer::ViewConstantSlot, View );
	CommandList.UsePixelShader( State->LightingPixel );
	if ( CommandList.PushTarget( State->SceneColor ) )
	{
		CommandList.ShaderRect( 0.0f, 0.0f, static_cast<float32>( FDeferredDemoState::ViewWidth ), static_cast<float32>( FDeferredDemoState::ViewHeight ) );
		CommandList.PopTarget();
	}
	CommandList.StopUsingPixelShader();
}

void CDeferredView::RecordComposite( FDrawContext& CommandList ) const
{
	NDeferredSources::FCompositeConstants Composite;
	Composite.Parameters = FVector4( State->Exposure, State->ViewMode == 0 ? 1.0f : 0.0f, 0.0f, 0.0f );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Composite );
	CommandList.UsePixelShader( State->CompositePixel );
	CommandList.ShaderTexture( 0, State->SceneColor );
	CommandList.ShaderRect( static_cast<float32>( FDeferredDemoState::ViewLeft ), static_cast<float32>( FDeferredDemoState::ViewTop ), static_cast<float32>( FDeferredDemoState::ViewWidth ), static_cast<float32>( FDeferredDemoState::ViewHeight ) );
	CommandList.StopUsingPixelShader();
}

void CDeferredView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !State->SceneColor.IsValid() || !State->GBuffer.IsValid() || !State->Camera.IsValid() )
	{
		return;
	}

	if ( State->bDeferred )
	{
		// ① ジオメトリのパス: 物の面の情報を、G-buffer の 4 枚へ 1 回の描画で書く
		if ( !State->GBuffer.BeginGeometryPass( CommandList ) )
		{
			return;
		}
		RecordScene( CommandList, true );
		State->GBuffer.EndGeometryPass( CommandList );
	}
	else
	{
		// ① Forward: 光の計算つきで、場面の色へ直接描く
		if ( !CommandList.PushTarget( State->SceneColor ) )
		{
			return;
		}
		CommandList.Clear( SkyColor );
		CommandList.ClearDepth();
		RecordScene( CommandList, false );
		CommandList.StopUsingVertexShader();
		CommandList.StopUsingPixelShader();
		CommandList.PopTarget();
	}

	// ここからは、画面全体にかける処理なので、奥行きは比べない。画像は、1 画素ずつ正確に読む（G-buffer も、場面の色も）
	FDepthState NoDepth;
	NoDepth.bTestDepth = false;
	NoDepth.bWriteDepth = false;
	CommandList.Depth( NoDepth );
	CommandList.Filter( EDrawFilter::Nearest );
	CommandList.TextureAddress( EDrawTextureAddress::Clamp );

	if ( State->bDeferred )
	{
		// ② ライティングのパス
		RecordLighting( CommandList );
	}
	// ③ 露出とトーンマップを掛けて、画面へ
	RecordComposite( CommandList );
}
