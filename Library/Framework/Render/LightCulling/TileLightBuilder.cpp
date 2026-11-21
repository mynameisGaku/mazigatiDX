#include "TileLightBuilder.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/LightCulling/LightCullingShaders.h"

DEFINE_LOG_CATEGORY_STATIC( LogTileLightBuilder, Log )

namespace
{
	bool IsFiniteVector( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}

	// 画像の大きさを決められる設定か（画面の大きさ・タイル・上限・層の数が、範囲の中）
	bool IsImageSizeValid( const FCameraViewport& Viewport, const FTileLightGridDesc& Desc )
	{
		if ( !Viewport.IsValid() || Desc.TileSize < 8 || Desc.TileSize > 512 || Desc.MaxLightsPerTile < CTileLightGrid::LightsPerTexel || Desc.MaxLightsPerTile > 96 ||
			Desc.MaxLightsPerTile % CTileLightGrid::LightsPerTexel != 0 || Desc.NumDepthSlices < 1 || Desc.NumDepthSlices > CTileLightGrid::MaxDepthSlices )
		{
			return false;
		}
		const int32 TilesX = ( Viewport.Width + Desc.TileSize - 1 ) / Desc.TileSize;
		const int32 TilesY = ( Viewport.Height + Desc.TileSize - 1 ) / Desc.TileSize;
		return TilesX * ( Desc.MaxLightsPerTile / CTileLightGrid::LightsPerTexel ) <= FPixelImage::MaxSize && TilesY * Desc.NumDepthSlices <= FPixelImage::MaxSize;
	}
}

int32 CTileLightBuilder::GetImageWidth( const FCameraViewport& Viewport, const FTileLightGridDesc& Desc )
{
	return IsImageSizeValid( Viewport, Desc ) ? ( ( Viewport.Width + Desc.TileSize - 1 ) / Desc.TileSize ) * ( Desc.MaxLightsPerTile / CTileLightGrid::LightsPerTexel ) : 0;
}

int32 CTileLightBuilder::GetImageHeight( const FCameraViewport& Viewport, const FTileLightGridDesc& Desc )
{
	return IsImageSizeValid( Viewport, Desc ) ? ( ( Viewport.Height + Desc.TileSize - 1 ) / Desc.TileSize ) * Desc.NumDepthSlices : 0;
}

bool CTileLightBuilder::MakeData( const FCamera& Camera, const FCameraViewport& InViewport, const FCullLight* Lights, int32 NumLights, const FTileLightGridDesc& InDesc, FTileLightBuildData& Out )
{
	FGBufferView View;
	if ( !CTileLightGrid::CanBuild( Camera, InViewport, Lights, NumLights, InDesc ) || !FGBufferView::Make( Camera, InViewport, View ) )
	{
		return false;
	}
	// 入力の検査に通ったので、Out へ直接書く（8 KB の構造体の一時変数を作って、丸ごと複写しない。MSVC 14.51 の最適化が、その複写で内部エラー（C1001）を起こした）
	Out.View.View = View;
	Out.View.Forward = FVector4( Camera.GetForward(), Camera.Near );
	Out.View.Grid = FVector4( static_cast<float32>( InDesc.TileSize ), static_cast<float32>( InDesc.NumDepthSlices ), static_cast<float32>( InDesc.MaxLightsPerTile / CTileLightGrid::LightsPerTexel ), static_cast<float32>( NumLights ) );
	Out.View.Screen = FVector4( static_cast<float32>( InViewport.Width ), static_cast<float32>( InViewport.Height ), static_cast<float32>( InDesc.MaxLightsPerTile ), CTileLightGrid::SliceMargin );
	for ( int32 Index = 0; Index < CTileLightGrid::MaxLights; ++Index )
	{
		// 使わない光（光の数より後ろ）は、半径 0
		Out.Positions[Index] = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
		Out.Axes[Index] = FVector4( 0.0f, 0.0f, 0.0f, -2.0f );
	}
	for ( int32 Index = 0; Index < NumLights; ++Index )
	{
		const FCullLight& Light = Lights[Index];
		const bool bSpot = Light.IsSpot();
		// 半径が 0 以下の光と、半角が 0 以下のスポットライトは、どこにも入らない光（半径 0）
		const float32 Radius = Light.Radius > 0.0f && ( !bSpot || Light.ConeHalfAngle > 0.0f ) ? Light.Radius : 0.0f;
		Out.Positions[Index] = FVector4( Light.Position, Radius );
		if ( bSpot )
		{
			Out.Axes[Index] = FVector4( Light.Direction.Normalized(), NMath::Cos( Light.ConeHalfAngle ) );
		}
	}
	return true;
}

bool CTileLightBuilder::Create( CAssetSubsystem& Assets, const FString& Name, const FCameraViewport& InViewport, const FTileLightGridDesc& InDesc )
{
	Release();
	const int32 Width = GetImageWidth( InViewport, InDesc );
	const int32 Height = GetImageHeight( InViewport, InDesc );
	if ( Width <= 0 || Height <= 0 )
	{
		return false;
	}
	// ピクセルシェーダー: 画像の画素 1 つが、1 つのクラスタの 3 つの光の番号を作る（定数バッファは、FirstConstantSlot から View・Positions・Axes の順）
	const FString Source = FString::Printf( "#define TILE_LIGHT_BUILD_VIEW_REGISTER b%d\n#define TILE_LIGHT_BUILD_POSITION_REGISTER b%d\n#define TILE_LIGHT_BUILD_AXIS_REGISTER b%d\n#define TILE_LIGHT_BUILD_PIXEL\n", FirstConstantSlot, FirstConstantSlot + 1, FirstConstantSlot + 2 ) +
		FString( NLightCullingShaders::GetTileLightBuildSource() );
	FString Message;
	TSharedPtr<CShader> Compiled = CShader::Compile( EShaderStage::Pixel, Source, "main", &Message );
	if ( !Compiled.IsValid() )
	{
		LOG( LogTileLightBuilder, Error, "Cannot compile the tile light build shader: %s", Message.GetData() );
		return false;
	}
	// 一覧の画像: 8 ビット RGBA（光の番号 + 1 を、1 つずつ画素の値に詰める）。書き込むだけで、奥行きバッファは要らない
	FRenderTargetDesc TargetDesc;
	TargetDesc.Width = Width;
	TargetDesc.Height = Height;
	TargetDesc.bAlpha = false;
	TargetDesc.bDepth = false;
	TargetDesc.Format = ERenderTargetFormat::Color8;
	TargetDesc.Channels = 4;
	TSharedPtr<CRenderTarget> Made = TargetDesc.IsValid() ? Assets.CreateRenderTarget( Name, TargetDesc ) : TSharedPtr<CRenderTarget>();
	if ( !Made.IsValid() )
	{
		return false;
	}
	Pixel = Compiled;
	Texture = Made;
	Viewport = InViewport;
	Desc = InDesc;
	return true;
}

void CTileLightBuilder::Release()
{
	Pixel.Reset();
	Texture.Reset();
	Viewport = FCameraViewport();
	Desc = FTileLightGridDesc();
}

bool CTileLightBuilder::Record( FDrawContext& CommandList, const FCamera& Camera, const FCullLight* Lights, int32 NumLights ) const
{
	FTileLightBuildData Data;
	if ( !IsValid() || !MakeData( Camera, Viewport, Lights, NumLights, Desc, Data ) )
	{
		return false;
	}
	if ( !CommandList.PushTarget( Texture ) )
	{
		return false;
	}
	// 描画の状態（深度・合成・補間・画像の外側）は、変えない（続く描画の状態を乱さない）。ShaderRect は、奥行きを使わず、シェーダーは画像を読まず、出力のアルファは 1 なので、
	// 呼び出し側の状態のままで、画素の値がそのまま書かれる
	CommandList.UsePixelShader( Pixel );
	CommandList.ShaderConstants( EShaderStage::Pixel, FirstConstantSlot, &Data.View, static_cast<int32>( sizeof( Data.View ) ) );
	CommandList.ShaderConstants( EShaderStage::Pixel, FirstConstantSlot + 1, Data.Positions, static_cast<int32>( sizeof( Data.Positions ) ) );
	CommandList.ShaderConstants( EShaderStage::Pixel, FirstConstantSlot + 2, Data.Axes, static_cast<int32>( sizeof( Data.Axes ) ) );
	CommandList.ShaderRect( 0.0f, 0.0f, static_cast<float32>( GetImageWidth( Viewport, Desc ) ), static_cast<float32>( GetImageHeight( Viewport, Desc ) ) );
	CommandList.StopUsingPixelShader();
	CommandList.PopTarget();
	return true;
}
