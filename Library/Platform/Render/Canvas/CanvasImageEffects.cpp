// SPDX-License-Identifier: MIT
#include "CanvasImageEffects.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/BackendSupport.h"

namespace
{
	using FMaskState = FCanvasMaskState;
}

CCanvasImageEffects::CCanvasImageEffects( FCanvasExecution& InRun, CCanvasTargetScopes& InTargetScopes )
	: Run( InRun )
	, TargetScopes( InTargetScopes )
	, Surface( Run.Surface )
	, Targets( Run.Targets )
	, Shaders( Run.Shaders )
	, Geometry( Run.Geometry )
	, bStopped( Run.bStopped )
	, MaskStack( Run.State.MaskStack )
	, CurrentTarget( Run.TargetState.CurrentTarget )
	, bMrtActive( Run.TargetState.bMrtActive )
{
}

void CCanvasImageEffects::Unsupported( const FDrawCommandView& View, const char* Reason )
{
	FCanvasExecution::Unsupported( View, Reason );
}

bool CCanvasImageEffects::GetImage( int32 Handle, FTextureDrawView& Image )
{
	return Run.GetImage( Handle, Image );
}

// 画像の処理（フィルター・混ぜ合わせ）。変換先の画像のパスを開き、その矩形へ、組み込みのピクセルシェーダーで描いて、元の描画先へ戻る。描けなければ false。
// Constants は、シェーダーの定数（int4 を 4 つ。[0..1] = 種類・乗算済みか、[2] = 混ぜる強さ、[8..15] = 引数）。矩形の大きさと、元・先の左上は、ここで入れる。
bool CCanvasImageEffects::RunImageOperation( EBuiltInPixelShader Pixel, int32 DestHandle, int32 BaseHandle, int32 AuxHandle, bool bBlendToDest, bool bUseRect, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, int32 DestX, int32 DestY, int32 ( &Constants )[16] )
{
	FRenderTargetDrawView Destination; // 変換先の描画先。
	FTextureDrawView BaseImage, AuxImage; // 元の画像と、混ぜる画像（グラデーションマップ）。
	// 読みながら同じ画像へは書けない。変換先は、色だけの RGBA8 の描画先に限る。
	if ( !Shaders || !Targets || BaseHandle == DestHandle || AuxHandle == DestHandle || !Targets->GetRenderTargetDrawView( DestHandle, Destination ) || Destination.Format != ERenderTargetColorFormat::RGBA8 || !GetImage( BaseHandle, BaseImage )
		|| ( AuxHandle >= 0 && !GetImage( AuxHandle, AuxImage ) ) )
	{
		return false;
	}
	int32 X0 = 0, Y0 = 0, X1 = Destination.Width, Y1 = Destination.Height; // 描く矩形（変換先の中）。
	if ( bUseRect )
	{
		if ( SrcX < 0 || SrcY < 0 || SrcW <= 0 || SrcH <= 0 || static_cast<int64>( SrcX ) + SrcW > BaseImage.Width || static_cast<int64>( SrcY ) + SrcH > BaseImage.Height )
		{
			return false;
		}
		X0 = NMath::Max( DestX, 0 );
		Y0 = NMath::Max( DestY, 0 );
		X1 = static_cast<int32>( NMath::Min( static_cast<int64>( DestX ) + SrcW, static_cast<int64>( Destination.Width ) ) );
		Y1 = static_cast<int32>( NMath::Min( static_cast<int64>( DestY ) + SrcH, static_cast<int64>( Destination.Height ) ) );
		if ( X1 <= X0 || Y1 <= Y0 )
		{
			return true; // 変換先の外へ出る矩形は、何も描かない。
		}
	}
	if ( Pixel == EBuiltInPixelShader::ImageFilter )
	{
		Constants[2] = bUseRect ? SrcW : Destination.Width; // 拡大・縮小の変換先の大きさ。
		Constants[3] = bUseRect ? SrcH : Destination.Height;
	}
	Constants[4] = bUseRect ? SrcX : 0; // 元の矩形の左上。
	Constants[5] = bUseRect ? SrcY : 0;
	Constants[6] = bUseRect ? DestX : 0; // 変換先の矩形の左上。
	Constants[7] = bUseRect ? DestY : 0;
	const FRenderTargetDrawView Previous = CurrentTarget; // 戻す先。
	const TArray<FMaskState> SavedMask = MaskStack; // 変換先のパスには、マスクを掛けない。
	MaskStack.Reset();
	if ( Previous.Color.IsValid() )
	{
		Surface.EndRenderTargetPass(); // 入れ子にできないので、いまの描画先のパスを閉じてから、変換先のパスを始める。
	}
	bMrtActive = false;
	if ( !TargetScopes.EnterTarget( Destination ) )
	{
		MaskStack = SavedMask;
		TargetScopes.EnterTarget( Previous );
		return false;
	}
	bool bDrawn = false; // 描けたか。
	IRenderGeometry3D* Target = Geometry.GetGeometry();
	if ( Target && Target->GetWidth() == Destination.Width && Target->GetHeight() == Destination.Height )
	{
		const float32 Width = static_cast<float32>( Destination.Width ), Height = static_cast<float32>( Destination.Height );
		const float32 Left = static_cast<float32>( X0 ) / Width * 2.0f - 1.0f, Right = static_cast<float32>( X1 ) / Width * 2.0f - 1.0f; // 画素の座標をクリップ座標へ。
		const float32 Top = 1.0f - static_cast<float32>( Y0 ) / Height * 2.0f, Bottom = 1.0f - static_cast<float32>( Y1 ) / Height * 2.0f;
		const float32 PositionsX[4] = { Left, Right, Left, Right }, PositionsY[4] = { Top, Top, Bottom, Bottom };
		FDrawShaderVertex3D Vertices[4];
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			FMemory::Memzero( &Vertices[Index], sizeof( FDrawShaderVertex3D ) );
			Vertices[Index].Position[0] = PositionsX[Index];
			Vertices[Index].Position[1] = PositionsY[Index];
			for ( uint8& Channel : Vertices[Index].Diffuse )
			{
				Channel = 255;
			}
		}
		const uint32 Indices[6] = { 0, 1, 2, 2, 1, 3 };
		FShaderConstantData Constant; // b5 へ置く定数。
		Constant.Stage = static_cast<int32>( EShaderStage::Pixel );
		Constant.Slot = 5;
		Constant.Bytes = TArrayView<const uint8>( reinterpret_cast<const uint8*>( Constants ), sizeof( int32 ) * 16 );
		FShaderDrawRequest Request;
		Request.Builtin = EBuiltInVertexShader::Rect;
		Request.BuiltinPixel = Pixel;
		Request.Topology = EGeometryTopology::Triangles;
		Request.bUseDepth = false;
		Request.State.Culling = EGeometryCulling::None;
		Request.State.bAlphaBlend = bBlendToDest; // false なら、変換先を置き換える。
		Request.NumTargets = 1;
		Request.TargetFormats[0] = ERenderTargetColorFormat::RGBA8;
		Request.Vertices = reinterpret_cast<const uint8*>( Vertices );
		Request.NumVertices = 4;
		Request.Indices = Indices;
		Request.NumIndices = 6;
		Request.Constants = TArrayView<const FShaderConstantData>( &Constant, 1 );
		Request.Textures[0] = BaseHandle;
		Request.Textures[2] = AuxHandle;
		bDrawn = Shaders->DrawShader( Request, *Target );
	}
	Surface.EndRenderTargetPass();
	MaskStack = SavedMask;
	if ( !TargetScopes.EnterTarget( Previous ) )
	{
		bStopped = true; // 元の描画先へ戻れない。これ以上は描けない。
	}
	return bDrawn;
}

// 画像の処理。元の画像（か、混ぜる画像）が変換先と同じなら、読みながら書けないので、一時の描画先へ処理してから、変換先へ写す（DxLibと同じく、その場の処理は、置き換える）。
bool CCanvasImageEffects::RunImageOperationBuffered( EBuiltInPixelShader Pixel, int32 DestHandle, int32 BaseHandle, int32 AuxHandle, bool bBlendToDest, bool bUseRect, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, int32 DestX, int32 DestY, int32 ( &Constants )[16] )
{
	if ( BaseHandle != DestHandle && AuxHandle != DestHandle )
	{
		return RunImageOperation( Pixel, DestHandle, BaseHandle, AuxHandle, bBlendToDest, bUseRect, SrcX, SrcY, SrcW, SrcH, DestX, DestY, Constants );
	}
	FRenderTargetDrawView Destination; // 変換先と同じ大きさの、一時の描画先を使う。
	int32 Scratch = -1;
	if ( !Targets || !Targets->GetRenderTargetDrawView( DestHandle, Destination ) || !Targets->AcquireScratchTarget( Destination.Width, Destination.Height, Scratch ) || Scratch == BaseHandle || Scratch == AuxHandle )
	{
		return false;
	}
	if ( !RunImageOperation( Pixel, Scratch, BaseHandle, AuxHandle, false, bUseRect, SrcX, SrcY, SrcW, SrcH, DestX, DestY, Constants ) )
	{
		return false;
	}
	int32 Copy[16] = { 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // 種類 99 は、そのまま写す（シェーダーは、どの種類にも当たらず、元の色を返す）。
	return RunImageOperation( EBuiltInPixelShader::ImageFilter, DestHandle, Scratch, -1, false, bUseRect, bUseRect ? DestX : 0, bUseRect ? DestY : 0, SrcW, SrcH, DestX, DestY, Copy );
}

bool CCanvasImageEffects::Execute( const FDrawCommandView& View )
{
	switch ( View.Type )
	{
	case EDrawCommand::ImageFilter:
	{
		if ( !View.CanRead<FDrawImageFilterData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawImageFilterData>(); // 固定部を所有コピーする。
		// DxLibと同じく、使えない種類・画像の番号は、何もしない。
		if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawGraphFilter::FloatColorScale ) || Data.Source < 0 || Data.Dest < 0 || ( Data.Type == static_cast<int32>( EDrawGraphFilter::GradientMap ) && Data.Map < 0 ) )
		{
			break;
		}
		int32 Constants[16] = { Data.Type, Data.Premultiplied, 0, 0, 0, 0, 0, 0, Data.Params[0], Data.Params[1], Data.Params[2], Data.Params[3], Data.Params[4], Data.Params[5], Data.Params[6], Data.Params[7] };
		if ( !RunImageOperationBuffered( EBuiltInPixelShader::ImageFilter, Data.Dest, Data.Source, Data.Type == static_cast<int32>( EDrawGraphFilter::GradientMap ) ? Data.Map : -1, Data.Blend != 0, Data.HasRect != 0, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH,
				Data.DestX, Data.DestY, Constants ) )
		{
			Unsupported( View, "The image filter failed (the destination is not an RGBA8 render target) and is skipped" );
		}
		break;
	}
	case EDrawCommand::ImageBlend:
	{
		if ( !View.CanRead<FDrawImageBlendData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawImageBlendData>(); // 固定部を所有コピーする。
		if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawGraphBlend::Mask ) || Data.Base < 0 || Data.Blend < 0 || Data.Dest < 0 )
		{
			break;
		}
		int32 Constants[16] = { Data.Type, Data.Premultiplied, NMath::Clamp( Data.Ratio, 0, 255 ), 0, 0, 0, 0, 0, Data.Select[0], Data.Select[1], Data.Select[2], Data.Select[3], 0, 0, 0, 0 };
		if ( !RunImageOperationBuffered( EBuiltInPixelShader::ImageBlend, Data.Dest, Data.Base, Data.Blend, false, false, 0, 0, 0, 0, 0, 0, Constants ) )
		{
			Unsupported( View, "The image blend failed (the destination is not an RGBA8 render target) and is skipped" );
		}
		break;
	}
	default:
		return false;
	}
	return true;
}
