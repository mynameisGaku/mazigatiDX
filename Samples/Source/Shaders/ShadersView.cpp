#include "ShadersView.h"

#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Render/RenderState3D.h"
#include "Source/Shaders/ShadersMesh.h"
#include "Source/Shaders/ShadersSources.h"

namespace
{
	// 場面の空の色（夕方）
	const FColor32 SkyColor( 18, 22, 40, 255 );

	// 光が進む向き（長さ 1。斜め下）
	const FVector3 LightDirection( -0.4969f, -0.7951f, -0.3478f );

	// うねりの大きさ（ワールドの単位）
	constexpr float32 WobbleAmount = 0.35f;

	// 物 1 つ: 形・位置・大きさ・色
	struct FStageObject
	{
		bool bCube;
		FVector3 Position;
		FVector3 Scale;
		FVector4 Color;
	};

	const FStageObject StageObjects[] = {
		{ false, FVector3( -7.0f, 1.6f, 1.0f ), FVector3( 1.6f, 1.6f, 1.6f ), FVector4( 0.95f, 0.35f, 0.25f, 1.0f ) },
		{ false, FVector3( 0.0f, 2.2f, 3.0f ), FVector3( 2.2f, 2.2f, 2.2f ), FVector4( 0.25f, 0.75f, 0.65f, 1.0f ) },
		{ false, FVector3( 7.5f, 1.3f, -1.5f ), FVector3( 1.3f, 1.3f, 1.3f ), FVector4( 0.95f, 0.8f, 0.25f, 1.0f ) },
		{ true, FVector3( -1.0f, 3.0f, -8.0f ), FVector3( 3.0f, 6.0f, 3.0f ), FVector4( 0.55f, 0.45f, 0.9f, 1.0f ) },
	};

	// 形を 1 つ描く（物の定数を、頂点側とピクセル側に置いて、添字つきで描く）
	void RecordMesh( FDrawContext& CommandList, const NShadersMesh::FMesh& Mesh, const FMatrix4x4& World, const FVector4& Color, float32 Wobble )
	{
		NShadersSources::FObjectConstants Object;
		Object.World = World;
		Object.Color = Color;
		Object.Options = FVector4( Wobble, 0.0f, 0.0f, 0.0f );
		CommandList.ShaderConstants( EShaderStage::Vertex, 5, Object );
		CommandList.ShaderConstants( EShaderStage::Pixel, 5, Object );
		CommandList.PrimitiveIndexedShader3D( Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num(), EDrawPrimitive::Triangles );
	}
}

CShadersView::CShadersView( const TSharedPtr<FShadersDemoState>& InState )
	: State( InState )
{
}

void CShadersView::RecordScene( FDrawContext& CommandList ) const
{
	const FCamera& Camera = State->Camera;

	// 場面の定数: カメラの行列・光・時間（頂点シェーダーとピクセルシェーダーの両方に置く）
	NShadersSources::FSceneConstants Scene;
	Scene.ViewProjection = Camera.GetViewMatrix() * Camera.GetProjectionMatrix( State->GetSceneViewport() );
	Scene.LightDirection = FVector4( LightDirection, 0.0f );
	Scene.CameraPosition = FVector4( Camera.Position, 1.0f );
	Scene.Time = FVector4( State->Time, Camera.Near, Camera.Far, 0.0f );
	CommandList.ShaderConstants( EShaderStage::Vertex, 4, Scene );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Scene );
	CommandList.Culling( EDrawCulling::None );

	// 地面
	CommandList.UseVertexShader( State->ObjectVertex );
	CommandList.UsePixelShader( State->GroundPixels[State->GroundMode] );
	RecordMesh( CommandList, NShadersMesh::GetGround(), FMatrix4x4::Identity, FVector4( 1.0f, 1.0f, 1.0f, 1.0f ), 0.0f );

	// 物（球 3 つと柱）。同じ頂点シェーダーに、見え方ごとのピクセルシェーダーを使う。柱はゆっくり回る
	CommandList.UsePixelShader( State->ObjectPixels[State->ObjectMode] );
	const float32 Wobble = State->ObjectMode == 3 ? WobbleAmount : 0.0f;
	for ( const FStageObject& Object : StageObjects )
	{
		FMatrix4x4 World = FMatrix4x4::Scale( Object.Scale );
		if ( Object.bCube )
		{
			World = World * FMatrix4x4::RotateY( State->Time * 0.5f );
		}
		World = World * FMatrix4x4::Translate( Object.Position );
		RecordMesh( CommandList, Object.bCube ? NShadersMesh::GetCube() : NShadersMesh::GetSphere(), World, Object.Color, Wobble );
	}
}

void CShadersView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() || !State->SceneTarget.IsValid() || !State->Camera.IsValid() )
	{
		return;
	}

	// ① 場面を、描画先へ描く
	if ( !CommandList.PushTarget( State->SceneTarget ) )
	{
		return;
	}
	CommandList.Clear( SkyColor );
	CommandList.ClearDepth();
	RecordScene( CommandList );
	CommandList.StopUsingVertexShader();
	CommandList.StopUsingPixelShader();
	CommandList.PopTarget();

	// ② 描画先の絵を、ポストエフェクトのピクセルシェーダーを掛けて、見せる範囲に貼る。全体にかける効果なので、奥行きは比べない
	FDepthState NoDepth;
	NoDepth.bTestDepth = false;
	NoDepth.bWriteDepth = false;
	CommandList.Depth( NoDepth );
	CommandList.Filter( EDrawFilter::Bilinear );
	NShadersSources::FPostConstants Post;
	Post.Parameters = FVector4( State->Time, static_cast<float32>( FShadersDemoState::ViewWidth ), static_cast<float32>( FShadersDemoState::ViewHeight ), 0.0f );
	CommandList.ShaderConstants( EShaderStage::Pixel, 4, Post );
	CommandList.UsePixelShader( State->PostPixels[State->PostMode] );
	CommandList.ShaderTexture( 0, State->SceneTarget );
	CommandList.ShaderRect( static_cast<float32>( FShadersDemoState::ViewLeft ), static_cast<float32>( FShadersDemoState::ViewTop ), static_cast<float32>( FShadersDemoState::ViewWidth ), static_cast<float32>( FShadersDemoState::ViewHeight ) );
}
