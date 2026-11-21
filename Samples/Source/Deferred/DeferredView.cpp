#include "DeferredView.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Deferred/GBufferView.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Source/Common/ShaderMeshes.h"
#include "Source/Deferred/DeferredSources.h"

DEFINE_LOG_CATEGORY_STATIC( LogDeferredView, Log )

namespace
{
	// 夜空の色（Forward の描画先を消す色）。ライティングのシェーダーの SkyColor と同じ値
	const FColor32 SkyColor( 6, 8, 18, 255 );

	// 動く光: 点の光は、届く半径・明るさ。スポットライトは、高い所から内側の地面を照らすので、遠くまで届く半径と、円錐の外側・内側の角（半角。ラジアン）を持つ。光る玉の大きさ・玉が自分で光る明るさ
	constexpr float32 LightRadius = 7.0f;
	constexpr float32 LightIntensity = 2.6f;
	constexpr float32 SpotRadius = 22.0f;
	constexpr float32 SpotIntensity = 8.0f;
	constexpr float32 SpotOuterAngle = 0.28f;
	constexpr float32 SpotInnerAngle = 0.15f;
	constexpr float32 SpotHeight = 6.0f;
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

	// 半透明の物 1 つ（ガラス）: 形・位置・大きさ・横の回転（ラジアン）・色・不透明度。地面や光る玉の輪から離して、不透明の物と食い込まないように置く
	struct FGlassObject
	{
		bool bCube;
		FVector3 Position;
		FVector3 Scale;
		float32 Yaw;
		FVector3 Color;
		float32 Opacity;
	};

	// 位置は、カメラの手前側（不透明の物と重なって見える）。地面から離し、光る玉の輪（半径 5・10.5・16）と、不透明の物の外にして、どれとも食い込まない
	const FGlassObject GlassObjects[] = {
		{ false, FVector3( 4.5f, 1.9f, -12.4f ), FVector3( 1.5f, 1.5f, 1.5f ), 0.0f, FVector3( 0.55f, 0.9f, 1.0f ), 0.4f },
		{ false, FVector3( 5.5f, 1.5f, -6.5f ), FVector3( 1.0f, 1.0f, 1.0f ), 0.0f, FVector3( 1.0f, 0.55f, 0.8f ), 0.45f },
		{ true, FVector3( -2.0f, 1.7f, 0.5f ), FVector3( 2.6f, 2.6f, 0.15f ), 2.55f, FVector3( 0.7f, 1.0f, 0.65f ), 0.35f },
	};
	constexpr int32 NumGlassObjects = static_cast<int32>( sizeof( GlassObjects ) / sizeof( GlassObjects[0] ) );

	// 光の輪: 半径・輪ごとの光の数。光の番号は、内側の輪から順に付ける。輪の半径は、半透明の物（ガラス）と食い込まない値にしてある。
	// 内側の 3 つの輪は点の光、いちばん外の輪は、スポットライト（高い所から内側の地面を照らす）
	constexpr int32 NumRings = 4;
	constexpr float32 RingRadii[NumRings] = { 5.5f, 11.0f, 16.5f, 21.0f };
	constexpr int32 RingCounts[NumRings] = { 10, 16, 20, 18 };
	static_assert( RingCounts[0] + RingCounts[1] + RingCounts[2] + RingCounts[3] == FDeferredDemoState::NumLights, "the rings hold all the lights" );
	constexpr int32 FirstSpotLight = RingCounts[0] + RingCounts[1] + RingCounts[2];
	static_assert( FirstSpotLight + FDeferredDemoState::NumSpotLights == FDeferredDemoState::NumLights, "the outermost ring holds the spot lights" );

	// 動く光 1 つ
	struct FMovingLight
	{
		FVector3 Position;
		float32 Radius = 0.0f;
		// スポットライトの向き（単位ベクトル）と、円錐の外側・内側の角（半角）。点の光は、外側の角が π（全方向）
		FVector3 Direction;
		float32 OuterAngle = NMath::PI;
		float32 InnerAngle = NMath::PI;
		float32 Intensity = 0.0f;
	};

	// 光 Index の位置（4 つの輪の上を、輪ごとに違う向きと速さで回り、上下に揺れる）
	FVector3 GetLightPosition( int32 Index, float32 Time )
	{
		int32 Ring = 0;
		int32 InRing = Index;
		while ( Ring < NumRings - 1 && InRing >= RingCounts[Ring] )
		{
			InRing -= RingCounts[Ring];
			++Ring;
		}
		const float32 Direction = Ring % 2 == 0 ? 1.0f : -1.0f;
		const float32 Angle = Direction * Time * ( 0.45f - 0.08f * static_cast<float32>( Ring ) ) + static_cast<float32>( InRing ) * NMath::TWO_PI / static_cast<float32>( RingCounts[Ring] ) + static_cast<float32>( Ring ) * 0.7f;
		const bool bSpot = Index >= FirstSpotLight;
		const float32 Height = bSpot ? SpotHeight + 0.8f * NMath::Sin( Time * 0.9f + static_cast<float32>( Index ) ) : 1.4f + 0.6f * NMath::Sin( Time * 1.1f + static_cast<float32>( Index ) );
		return FVector3( NMath::Cos( Angle ) * RingRadii[Ring], Height, NMath::Sin( Angle ) * RingRadii[Ring] );
	}

	// 動く光 Index（点の光、または、内側の地面の動く点へ向くスポットライト）
	FMovingLight GetMovingLight( int32 Index, float32 Time )
	{
		FMovingLight Light;
		Light.Position = GetLightPosition( Index, Time );
		if ( Index < FirstSpotLight )
		{
			Light.Radius = LightRadius;
			Light.Intensity = LightIntensity;
			return Light;
		}
		// 狙う地面の点: 光の真下の方向（輪の中心へ）を中心に、横へ揺れ、中心からの距離（3〜9）も変わる
		const float32 TargetAngle = NMath::Atan2( Light.Position.z, Light.Position.x ) + 0.5f * NMath::Sin( Time * 0.7f + static_cast<float32>( Index ) );
		const float32 TargetRadius = 6.0f + 3.0f * NMath::Sin( Time * 0.5f + static_cast<float32>( Index ) * 2.0f );
		const FVector3 Target( NMath::Cos( TargetAngle ) * TargetRadius, 0.0f, NMath::Sin( TargetAngle ) * TargetRadius );
		Light.Radius = SpotRadius;
		Light.Intensity = SpotIntensity;
		Light.Direction = ( Target - Light.Position ).Normalized();
		Light.OuterAngle = SpotOuterAngle;
		Light.InnerAngle = SpotInnerAngle;
		return Light;
	}

	// 形を 1 つ描く（物の定数を、頂点側とピクセル側に置いて、インデックスつきで描く）。Opacity は、半透明の物の不透明度（不透明の物は 1）
	void RecordMesh( FDrawContext& CommandList, const NShaderMeshes::FMesh& Mesh, const FMatrix4x4& World, const FVector3& Albedo, const FVector3& Emission, float32 Opacity = 1.0f )
	{
		NDeferredSources::FObjectConstants Object;
		Object.World = World;
		Object.Albedo = FVector4( Albedo, Opacity );
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
		Scene.Parameters = FVector4( State.Time, static_cast<float32>( State.ViewMode ), static_cast<float32>( FDeferredDemoState::ViewWidth ), static_cast<float32>( FDeferredDemoState::ViewHeight ) );
		Scene.TileInfo = FVector4( static_cast<float32>( FDeferredDemoState::TileSize ), static_cast<float32>( State.TileGrid.GetTexelsPerTile() ), State.bTiles ? 1.0f : 0.0f, 0.0f );
		Scene.ClusterInfo = FVector4( static_cast<float32>( State.TileGrid.GetNumSlices() ), static_cast<float32>( State.TileGrid.GetNumTilesY() ), State.Camera.Near, State.Camera.Far );
		for ( int32 Index = 0; Index < FDeferredDemoState::NumLights; ++Index )
		{
			const FMovingLight Light = GetMovingLight( Index, State.Time );
			const bool bSpot = Index >= FirstSpotLight;
			Scene.LightPositions[Index] = FVector4( Light.Position, Light.Radius );
			// 消したスポットライト（--deferred-no-spots）は、色を 0 にする（光らない）
			const float32 Intensity = bSpot && !State.bSpotLights ? 0.0f : Light.Intensity;
			Scene.LightColors[Index] = FVector4( LightColors[Index % 6] * Intensity, bSpot ? NMath::Cos( Light.InnerAngle ) : 0.0f );
			// 点の光は、外側の角の cos を -2（-1 以下）にして、全方向の印にする
			Scene.LightSpots[Index] = FVector4( Light.Direction, bSpot ? NMath::Cos( Light.OuterAngle ) : -2.0f );
		}
		return Scene;
	}
}

CDeferredView::CDeferredView( const TSharedPtr<FDeferredDemoState>& InState )
	: State( InState )
{
}

bool CDeferredView::UpdateTileLights()
{
	TArray<FCullLight> Lights;
	for ( int32 Index = 0; Index < FDeferredDemoState::NumLights; ++Index )
	{
		const FMovingLight Moving = GetMovingLight( Index, State->Time );
		FCullLight Light;
		Light.Position = Moving.Position;
		Light.Radius = Index >= FirstSpotLight && !State->bSpotLights ? 0.0f : Moving.Radius; // 消したスポットライトは、半径 0（どのクラスタにも入らない）
		Light.Direction = Moving.Direction;
		Light.ConeHalfAngle = Moving.OuterAngle;
		Lights.Add( Light );
	}
	FTileLightGridDesc Desc;
	Desc.TileSize = FDeferredDemoState::TileSize;
	Desc.MaxLightsPerTile = State->TileCapacity;
	Desc.NumDepthSlices = State->NumSlices;
	if ( !State->TileGrid.Build( State->Camera, State->GetSceneViewport(), Lights.GetData(), Lights.Num(), Desc ) || !State->TileGrid.WriteImage( State->TileImage ) )
	{
		return false;
	}
	if ( !State->TileTexture.IsValid() )
	{
		CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
		State->TileTexture = Assets != nullptr ? Assets->CreateDynamicTexture( "deferred-tile-lights", State->TileImage ) : TSharedPtr<CDynamicTexture>();
		return State->TileTexture.IsValid();
	}
	return State->TileTexture->Update( State->TileImage );
}

void CDeferredView::RecordScene( FDrawContext& CommandList, bool bGeometryPass ) const
{
	const NDeferredSources::FSceneConstants Scene = MakeSceneConstants( *State );
	CommandList.ShaderConstants( EShaderStage::Vertex, 4, Scene );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Scene );
	CommandList.Culling( EDrawCulling::None );
	CommandList.UseVertexShader( State->SceneVertex );
	if ( !bGeometryPass )
	{
		// Forward（Forward+）: 光を計算する物のシェーダーが、タイルごとの光の一覧を t0 から読む
		CommandList.ShaderTexture( 0, State->TileTexture );
	}

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

void CDeferredView::RecordTransparent( FDrawContext& CommandList, bool bDeferred ) const
{
	// 奥から手前の順（カメラからの距離の 2 乗が大きい順）に並べる。物の中心で並べる簡単な方法（1 つの物の中の表裏の順は、メッシュの順）
	int32 Order[NumGlassObjects];
	float32 DistanceSquared[NumGlassObjects];
	for ( int32 Index = 0; Index < NumGlassObjects; ++Index )
	{
		Order[Index] = Index;
		DistanceSquared[Index] = ( GlassObjects[Index].Position - State->Camera.Position ).LengthSquared();
	}
	for ( int32 Index = 1; Index < NumGlassObjects; ++Index )
	{
		const int32 Moving = Order[Index];
		int32 Position = Index;
		while ( Position > 0 && DistanceSquared[Order[Position - 1]] < DistanceSquared[Moving] )
		{
			Order[Position] = Order[Position - 1];
			--Position;
		}
		Order[Position] = Moving;
	}

	const NDeferredSources::FSceneConstants Scene = MakeSceneConstants( *State );
	CommandList.ShaderConstants( EShaderStage::Vertex, 4, Scene );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Scene );
	if ( bDeferred )
	{
		// Deferred 経路: 場面の色の描き先は、不透明の物の奥行きを持たない。G-buffer の深度をシェーダーが読んで、奥の画素を省く
		FGBufferView View;
		if ( !FGBufferView::Make( State->Camera, State->GetSceneViewport(), View ) || !State->GBuffer.UseForLighting( CommandList ) )
		{
			return;
		}
		CommandList.ShaderConstants( EShaderStage::Pixel, CGBuffer::ViewConstantSlot, View );
	}
	// Forward 経路: ハードウェアの奥行きバッファと比べる（不透明の物の奥行きを持っている）。半透明の物は、奥行きを書かない（あとの半透明の物の邪魔をしない）
	FDepthState Depth;
	Depth.bTestDepth = !bDeferred;
	Depth.bWriteDepth = false;
	CommandList.Depth( Depth );
	CommandList.Blend( EDrawBlend::Alpha );
	CommandList.Culling( EDrawCulling::None );
	CommandList.UseVertexShader( State->SceneVertex );
	CommandList.UsePixelShader( bDeferred ? State->TransparentPixel : State->ForwardObjectPixel );
	// タイルごとの光の一覧: Forward のシェーダーは t0、Deferred の半透明は G-buffer（t0〜t3）の次の t4
	CommandList.ShaderTexture( bDeferred ? 4 : 0, State->TileTexture );
	for ( int32 Index = 0; Index < NumGlassObjects; ++Index )
	{
		const FGlassObject& Glass = GlassObjects[Order[Index]];
		const FMatrix4x4 World = FMatrix4x4::Scale( Glass.Scale ) * FMatrix4x4::RotateY( Glass.Yaw ) * FMatrix4x4::Translate( Glass.Position );
		RecordMesh( CommandList, Glass.bCube ? NShaderMeshes::GetCube() : NShaderMeshes::GetSphere(), World, Glass.Color, FVector3( 0.0f, 0.0f, 0.0f ), Glass.Opacity );
	}
	CommandList.Blend( EDrawBlend::Opaque );
	CommandList.StopUsingVertexShader();
	CommandList.StopUsingPixelShader();
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
	CommandList.ShaderTexture( 4, State->TileTexture ); // タイルごとの光の一覧は、G-buffer（t0〜t3）の次
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
	if ( !State.IsValid() || !State->SceneColor.IsValid() || !State->GBuffer.IsValid() || !State->Camera.IsValid() || !UpdateTileLights() )
	{
		return;
	}
	if ( State->bLogTileStats )
	{
		// 検査用: 一覧の統計（画面に描いた絵の比べと一緒に、絞れていること・溢れを確かめる）
		static int32 FrameCounter = 0;
		if ( FrameCounter++ % 6 == 0 )
		{
			const int32 NumClusters = State->TileGrid.GetNumClusters();
			LOG( LogDeferredView, Log, "tile lights: clusters=%d slices=%d entries=%d average=%.3f overflowed=%d culling=%d", NumClusters, State->TileGrid.GetNumSlices(), State->TileGrid.GetTotalEntries(), static_cast<float64>( State->TileGrid.GetTotalEntries() ) / static_cast<float64>( NumClusters ), State->TileGrid.GetNumOverflowedTiles(), State->bTiles ? 1 : 0 );
		}
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
		RecordTransparent( CommandList, false );
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
		// ③ 半透明の物を、光を当てた場面の色へ重ねる
		if ( CommandList.PushTarget( State->SceneColor ) )
		{
			RecordTransparent( CommandList, true );
			CommandList.PopTarget();
		}
	}
	// ④ 露出とトーンマップを掛けて、画面へ
	RecordComposite( CommandList );
}
