#include "PostEffectsView.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Source/Common/ShaderMeshes.h"
#include "Source/PostEffects/PostEffectsSources.h"

namespace
{
	// 夜空の色（場面の描画先を消す色）と、霧の色
	const FColor32 SkyColor( 6, 8, 18, 255 );
	const FVector3 FogColor( 0.03f, 0.035f, 0.07f );

	// 月の光が進む向き（長さ 1。斜め下）
	const FVector3 LightDirection( -0.4969f, -0.7951f, -0.3478f );

	// 明るい所を抜き出す境目・ブルームの強さ・霧の濃さ（距離 1 あたり）
	constexpr float32 BloomThreshold = 1.0f;
	constexpr float32 BloomIntensity = 1.2f;
	constexpr float32 FogDensity = 0.022f;

	// ぼかしの回数（横と縦で 1 回）と、回ごとの広がり（画素）
	constexpr int32 NumBlurPasses = 3;
	const float32 BlurSpreads[NumBlurPasses] = { 1.0f, 2.0f, 4.0f };

	// 光る玉: 回る円の半径・高さ・大きさ・自分で光る明るさ・まわりを照らす明るさ
	constexpr float32 OrbRadius = 9.0f;
	constexpr float32 OrbHeight = 1.6f;
	constexpr float32 OrbSize = 0.45f;
	constexpr float32 OrbGlow = 8.0f;
	constexpr float32 OrbLight = 2.5f;
	const FVector3 OrbColors[FPostEffectsDemoState::NumOrbs] = {
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
		{ true, FVector3( -1.0f, 3.0f, -7.0f ), FVector3( 3.0f, 6.0f, 3.0f ), FVector3( 0.6f, 0.6f, 0.7f ) },
	};

	// 光る玉 Index の位置（時間とともに円の上を回り、上下に揺れる）
	FVector3 GetOrbPosition( int32 Index, float32 Time )
	{
		const float32 Angle = Time * 0.35f + static_cast<float32>( Index ) * NMath::TWO_PI / static_cast<float32>( FPostEffectsDemoState::NumOrbs );
		const float32 Height = OrbHeight + 0.5f * NMath::Sin( Time * 1.3f + static_cast<float32>( Index ) );
		return FVector3( NMath::Cos( Angle ) * OrbRadius, Height, NMath::Sin( Angle ) * OrbRadius );
	}

	// 形を 1 つ描く（物の定数を、頂点側とピクセル側に置いて、インデックスつきで描く）
	void RecordMesh( FDrawContext& CommandList, const NShaderMeshes::FMesh& Mesh, const FMatrix4x4& World, const FVector3& Albedo, const FVector3& Emission )
	{
		NPostEffectsSources::FObjectConstants Object;
		Object.World = World;
		Object.Albedo = FVector4( Albedo, 1.0f );
		Object.Emission = FVector4( Emission, 0.0f );
		CommandList.ShaderConstants( EShaderStage::Vertex, 5, Object );
		CommandList.ShaderConstants( EShaderStage::Pixel, 5, Object );
		CommandList.PrimitiveIndexedShader3D( Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num(), EDrawPrimitive::Triangles );
	}

	// いまの描画先（大きさ Width x Height）の全体に、いま決まっているピクセルシェーダーで描く
	void RecordFullRect( FDrawContext& CommandList, int32 Width, int32 Height )
	{
		CommandList.ShaderRect( 0.0f, 0.0f, static_cast<float32>( Width ), static_cast<float32>( Height ) );
	}
}

CPostEffectsView::CPostEffectsView( const TSharedPtr<FPostEffectsDemoState>& InState )
	: State( InState )
{
}

void CPostEffectsView::RecordScene( FDrawContext& CommandList ) const
{
	const FCamera& Camera = State->Camera;

	// 場面の定数: カメラの行列・月の光・時間・光る玉（頂点シェーダーとピクセルシェーダーの両方に置く）
	NPostEffectsSources::FSceneConstants Scene;
	Scene.ViewProjection = Camera.GetViewMatrix() * Camera.GetProjectionMatrix( State->GetSceneViewport() );
	Scene.LightDirection = FVector4( LightDirection, 0.0f );
	Scene.CameraPosition = FVector4( Camera.Position, 1.0f );
	Scene.Time = FVector4( State->Time, Camera.Far, 0.0f, 0.0f );
	for ( int32 Index = 0; Index < FPostEffectsDemoState::NumOrbs; ++Index )
	{
		Scene.OrbPositions[Index] = FVector4( GetOrbPosition( Index, State->Time ), 1.0f );
		Scene.OrbColors[Index] = FVector4( OrbColors[Index] * OrbLight, 0.0f );
	}
	CommandList.ShaderConstants( EShaderStage::Vertex, 4, Scene );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Scene );
	CommandList.Culling( EDrawCulling::None );
	CommandList.UseVertexShader( State->SceneVertex );

	// 地面
	CommandList.UsePixelShader( State->GroundPixel );
	RecordMesh( CommandList, NShaderMeshes::GetGround(), FMatrix4x4::Identity, FVector3( 1.0f, 1.0f, 1.0f ), FVector3( 0.0f, 0.0f, 0.0f ) );

	// 光を受ける物（球 3 つと柱）。柱はゆっくり回る
	CommandList.UsePixelShader( State->ObjectPixel );
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

	// 光る玉（地の色は黒で、光る色だけ。明るさ 8 は、16 ビットの浮動小数点の描画先に、そのまま残る）
	for ( int32 Index = 0; Index < FPostEffectsDemoState::NumOrbs; ++Index )
	{
		const FMatrix4x4 World = FMatrix4x4::Scale( FVector3( OrbSize, OrbSize, OrbSize ) ) * FMatrix4x4::Translate( GetOrbPosition( Index, State->Time ) );
		RecordMesh( CommandList, NShaderMeshes::GetSphere(), World, FVector3( 0.0f, 0.0f, 0.0f ), OrbColors[Index] * OrbGlow );
	}
}

void CPostEffectsView::RecordBloom( FDrawContext& CommandList ) const
{
	NPostEffectsSources::FPostConstants Post;
	Post.Parameters = FVector4( State->Exposure, BloomThreshold, BloomIntensity, FogDensity );

	// ② 明るい所を、半分の大きさの BloomA へ抜き出す（読む画像は、場面の色。1 画素の大きさは、場面の描画先のもの）
	Post.Sampling = FVector4( 1.0f / static_cast<float32>( FPostEffectsDemoState::ViewWidth ), 1.0f / static_cast<float32>( FPostEffectsDemoState::ViewHeight ), 0.0f, 0.0f );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Post );
	CommandList.PushTarget( State->BloomA );
	CommandList.UsePixelShader( State->BrightPixel );
	CommandList.ShaderTexture( 0, State->SceneColor );
	RecordFullRect( CommandList, FPostEffectsDemoState::BloomWidth, FPostEffectsDemoState::BloomHeight );
	CommandList.PopTarget();

	// ③ 横（A → B）・縦（B → A）にぼかす。回を重ねるごとに広げる
	CommandList.UsePixelShader( State->BlurPixel );
	const float32 TexelX = 1.0f / static_cast<float32>( FPostEffectsDemoState::BloomWidth );
	const float32 TexelY = 1.0f / static_cast<float32>( FPostEffectsDemoState::BloomHeight );
	for ( int32 Pass = 0; Pass < NumBlurPasses; ++Pass )
	{
		Post.Sampling = FVector4( TexelX, TexelY, BlurSpreads[Pass], 0.0f );
		CommandList.ShaderConstants( EShaderStage::Pixel, 4, Post );
		CommandList.PushTarget( State->BloomB );
		CommandList.ShaderTexture( 0, State->BloomA );
		RecordFullRect( CommandList, FPostEffectsDemoState::BloomWidth, FPostEffectsDemoState::BloomHeight );
		CommandList.PopTarget();

		Post.Sampling = FVector4( TexelX, TexelY, 0.0f, BlurSpreads[Pass] );
		CommandList.ShaderConstants( EShaderStage::Pixel, 4, Post );
		CommandList.PushTarget( State->BloomA );
		CommandList.ShaderTexture( 0, State->BloomB );
		RecordFullRect( CommandList, FPostEffectsDemoState::BloomWidth, FPostEffectsDemoState::BloomHeight );
		CommandList.PopTarget();
	}
}

void CPostEffectsView::RecordComposite( FDrawContext& CommandList ) const
{
	NPostEffectsSources::FPostConstants Post;
	Post.Parameters = FVector4( State->Exposure, BloomThreshold, BloomIntensity, FogDensity );
	Post.Sampling = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	Post.Modes = FVector4( static_cast<float32>( State->ViewMode ), static_cast<float32>( State->ToneMap ), State->bBloom ? 1.0f : 0.0f, State->bFog ? 1.0f : 0.0f );
	Post.FogColor = FVector4( FogColor, State->Camera.Far );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Post );
	CommandList.UsePixelShader( State->CompositePixel );
	CommandList.ShaderTexture( 0, State->SceneColor );
	CommandList.ShaderTexture( 1, State->BloomA );
	CommandList.ShaderTexture( 2, State->SceneDepth );
	CommandList.ShaderRect( static_cast<float32>( FPostEffectsDemoState::ViewLeft ), static_cast<float32>( FPostEffectsDemoState::ViewTop ), static_cast<float32>( FPostEffectsDemoState::ViewWidth ), static_cast<float32>( FPostEffectsDemoState::ViewHeight ) );
}

void CPostEffectsView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !State->SceneColor.IsValid() || !State->SceneDepth.IsValid() || !State->BloomA.IsValid() || !State->BloomB.IsValid() || !State->Camera.IsValid() )
	{
		return;
	}

	// ① 深度を「いちばん遠い」（1）で消してから、場面の色の描画先へ描く。深度は、出力 1 で同時に書く
	CommandList.PushTarget( State->SceneDepth );
	CommandList.Clear( FColor32( 255, 255, 255, 255 ) );
	CommandList.PopTarget();
	if ( !CommandList.PushTarget( State->SceneColor ) )
	{
		return;
	}
	CommandList.Clear( SkyColor );
	CommandList.ClearDepth();
	CommandList.ShaderOutputTarget( 1, State->SceneDepth );
	RecordScene( CommandList );
	// 深度は、このあと画像として読むので、出力から外す（書きながら読まない）
	CommandList.StopUsingShaderOutputTarget( 1 );
	CommandList.StopUsingVertexShader();
	CommandList.PopTarget();

	// ここからは、全体にかける効果なので、奥行きは比べない。画像は、なめらかに読む。
	// 画像の外（ぼかしで端の先を読む所）は、端の色を伸ばす（既定の「繰り返す」だと、反対側の端の明るさが回り込んで、上の端に下の溝の光がにじむ）
	FDepthState NoDepth;
	NoDepth.bTestDepth = false;
	NoDepth.bWriteDepth = false;
	CommandList.Depth( NoDepth );
	CommandList.Filter( EDrawFilter::Bilinear );
	CommandList.TextureAddress( EDrawTextureAddress::Clamp );
	RecordBloom( CommandList );
	RecordComposite( CommandList );
}
