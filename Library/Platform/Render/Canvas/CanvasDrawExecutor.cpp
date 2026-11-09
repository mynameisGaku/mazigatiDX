// SPDX-License-Identifier: MIT
#include "CanvasDrawExecutor.h"
#include "Library/Platform/Render/Geometry/GeometryCommandExecutor.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// 不正な座標で図形生成器へ過大な値を渡さない。
	bool Valid( float32 Value )
	{
		return NMath::IsFinite( Value ) && NMath::Abs( Value ) <= 1000000.0f;
	}
	// 頂点列を読む前に全座標を検査する。
	template<usize Count>
	bool ValidPoints( const float32 ( &Points )[Count] )
	{
		for ( float32 Value : Points )
		{
			if ( !Valid( Value ) )
			{
				return false;
			}
		}
		return true;
	}
}

FCanvasDrawExecutor::FCanvasDrawExecutor( IRenderSurface& InSurface, ITextureDrawSource* InTextures )
	: Surface( InSurface ), Textures( InTextures )
{
}

void FCanvasDrawExecutor::Execute( const FDrawSpan& Span )
{
	IRenderCanvas2D* Canvas = Surface.GetCanvas2D(); // フレーム内だけ借りる。
	if ( !Canvas || !Span.Words || Span.NumWords <= 0 )
	{
		return;
	}
	FGeometryCommandExecutor Geometry( Surface.GetGeometry3D() ); // このSpanだけの3D状態。
	bool bAlphaBlend = false; // 3Dへ渡す合成方式。
	FDrawRgb Bright; // このSpanだけの乗算色。
	int32 Alpha = 255; // このSpanだけの不透明度。
	int32 ClipDepth = 0; // 終了時に必ず戻すクリップ段数。
	bool bStopped = false; // 未対応命令以降を描かない印。
	const auto Color = [&]( FDrawRgb Value ) // 色と現在の描画状態を合成する。
	{
		return FColor32( static_cast<uint8>( Value.R * Bright.R / 255 ), static_cast<uint8>( Value.G * Bright.G / 255 ),
			static_cast<uint8>( Value.B * Bright.B / 255 ), static_cast<uint8>( Alpha ) );
	};
	int32 Offset = 0; // 読み終えたword数。
	while ( Offset < Span.NumWords && !bStopped )
	{
		const uint32 Header = Span.Words[Offset]; // 命令の固定ヘッダー。
		const int32 Count = static_cast<int32>( Header >> 16 ); // ヘッダー込みの長さ。
		if ( Count < 1 || Count > Span.NumWords - Offset )
		{
			NBackendSupport::ReportUnsupported( "DrawCommands", "Malformed command span" );
			break;
		}
		FDrawCommandView View; // 検査済みの範囲だけを読む。
		View.Type = static_cast<EDrawCommand>( Header & 255 );
		View.Flags = static_cast<uint8>( Header >> 8 );
		View.Data = Span.Words + Offset + 1;
		View.NumDataWords = Count - 1;
		const bool bFill = View.Has( FDrawFlag::Fill ); // 塗りつぶしか輪郭か。
		Canvas->SetAntiAlias( View.Has( FDrawFlag::AntiAlias ) || View.Type == EDrawCommand::BoxAA || View.Type == EDrawCommand::CircleAA || View.Type == EDrawCommand::LineAA );
		switch ( View.Type )
		{
		case EDrawCommand::Clear:
		{
			if ( !View.CanRead<FDrawClearData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawClearData>(); // 固定部を所有コピーする。
			Surface.RecordClear( FColor32( static_cast<uint8>( NMath::Clamp( Data.R, 0, 255 ) ), static_cast<uint8>( NMath::Clamp( Data.G, 0, 255 ) ), static_cast<uint8>( NMath::Clamp( Data.B, 0, 255 ) ), static_cast<uint8>( NMath::Clamp( Data.A, 0, 255 ) ) ) );
			break;
		}
		case EDrawCommand::Blend:
		{
			if ( !View.CanRead<FDrawBlendData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawBlendData>(); // 固定部を所有コピーする。
			if ( Data.Mode != static_cast<int32>( EDrawBlend::Opaque ) && Data.Mode != static_cast<int32>( EDrawBlend::Alpha ) )
			{
				bStopped = true;
				NBackendSupport::ReportUnsupported( "DrawCommands", "Blend mode", "Only Opaque and Alpha are supported" );
			}
			bAlphaBlend = Data.Mode == static_cast<int32>( EDrawBlend::Alpha );
			Alpha = Data.Mode == static_cast<int32>( EDrawBlend::Opaque ) ? 255 : NMath::Clamp( Data.Param, 0, 255 );
			break;
		}
		case EDrawCommand::Bright:
		{
			if ( !View.CanRead<FDrawBrightData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawBrightData>(); // 固定部を所有コピーする。
			Bright = Data.Color;
			break;
		}
		case EDrawCommand::PushClip:
		{
			if ( !View.CanRead<FDrawClipData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawClipData>(); // 固定部を所有コピーする。
			Canvas->PushClip( Data.X0, Data.Y0, NMath::Max( Data.X0, Data.X1 ), NMath::Max( Data.Y0, Data.Y1 ) );
			++ClipDepth;
			break;
		}
		case EDrawCommand::PopClip:
		{
			if ( !View.CanRead<FDrawPopClipData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawPopClipData>(); // 固定部を所有コピーする。
			(void)Data;
			if ( ClipDepth > 0 )
			{
				Canvas->PopClip();
				--ClipDepth;
			}
			break;
		}
		case EDrawCommand::Box:
		{
			if ( !View.CanRead<FDrawBoxData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawBoxData>(); // 固定部を所有コピーする。
			if ( Valid( static_cast<float32>( Data.X0 ) ) && Valid( static_cast<float32>( Data.Y0 ) ) && Valid( static_cast<float32>( Data.X1 ) ) && Valid( static_cast<float32>( Data.Y1 ) ) && Valid( static_cast<float32>( Data.Thickness ) ) && Data.Thickness > 0 )
			{
				Canvas->Box( static_cast<float32>( Data.X0 ), static_cast<float32>( Data.Y0 ), static_cast<float32>( Data.X1 ), static_cast<float32>( Data.Y1 ), Color( Data.Color ), bFill, static_cast<float32>( Data.Thickness ) );
			}
			break;
		}
		case EDrawCommand::BoxAA:
		{
			if ( !View.CanRead<FDrawBoxAAData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawBoxAAData>(); // 固定部を所有コピーする。
			if ( Valid( static_cast<float32>( Data.X0 ) ) && Valid( static_cast<float32>( Data.Y0 ) ) && Valid( static_cast<float32>( Data.X1 ) ) && Valid( static_cast<float32>( Data.Y1 ) ) && Valid( static_cast<float32>( Data.Thickness ) ) && Data.Thickness > 0 )
			{
				Canvas->Box( static_cast<float32>( Data.X0 ), static_cast<float32>( Data.Y0 ), static_cast<float32>( Data.X1 ), static_cast<float32>( Data.Y1 ), Color( Data.Color ), bFill, static_cast<float32>( Data.Thickness ) );
			}
			break;
		}
		case EDrawCommand::Line:
		{
			if ( !View.CanRead<FDrawLineData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawLineData>(); // 固定部を所有コピーする。
			if ( Valid( static_cast<float32>( Data.X0 ) ) && Valid( static_cast<float32>( Data.Y0 ) ) && Valid( static_cast<float32>( Data.X1 ) ) && Valid( static_cast<float32>( Data.Y1 ) ) && Valid( static_cast<float32>( Data.Thickness ) ) && Data.Thickness > 0 )
			{
				Canvas->Line( static_cast<float32>( Data.X0 ), static_cast<float32>( Data.Y0 ), static_cast<float32>( Data.X1 ), static_cast<float32>( Data.Y1 ), Color( Data.Color ), static_cast<float32>( Data.Thickness ) );
			}
			break;
		}
		case EDrawCommand::LineAA:
		{
			if ( !View.CanRead<FDrawLineAAData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawLineAAData>(); // 固定部を所有コピーする。
			if ( Valid( static_cast<float32>( Data.X0 ) ) && Valid( static_cast<float32>( Data.Y0 ) ) && Valid( static_cast<float32>( Data.X1 ) ) && Valid( static_cast<float32>( Data.Y1 ) ) && Valid( static_cast<float32>( Data.Thickness ) ) && Data.Thickness > 0 )
			{
				Canvas->Line( static_cast<float32>( Data.X0 ), static_cast<float32>( Data.Y0 ), static_cast<float32>( Data.X1 ), static_cast<float32>( Data.Y1 ), Color( Data.Color ), static_cast<float32>( Data.Thickness ) );
			}
			break;
		}
		case EDrawCommand::Circle:
		{
			if ( !View.CanRead<FDrawCircleData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawCircleData>(); // 固定部を所有コピーする。
			if ( Valid( static_cast<float32>( Data.X ) ) && Valid( static_cast<float32>( Data.Y ) ) && Valid( static_cast<float32>( Data.Radius ) ) && Data.Radius > 0 && Data.Thickness > 0 && Valid( static_cast<float32>( Data.Thickness ) ) )
			{
				Canvas->Circle( static_cast<float32>( Data.X ), static_cast<float32>( Data.Y ), static_cast<float32>( Data.Radius ), Color( Data.Color ), bFill, static_cast<float32>( Data.Thickness ), 0 );
			}
			break;
		}
		case EDrawCommand::CircleAA:
		{
			if ( !View.CanRead<FDrawCircleAAData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawCircleAAData>(); // 固定部を所有コピーする。
			if ( Valid( static_cast<float32>( Data.X ) ) && Valid( static_cast<float32>( Data.Y ) ) && Valid( static_cast<float32>( Data.Radius ) ) && Data.Radius > 0 && Data.Thickness > 0 && Valid( static_cast<float32>( Data.Thickness ) ) )
			{
				Canvas->Circle( static_cast<float32>( Data.X ), static_cast<float32>( Data.Y ), static_cast<float32>( Data.Radius ), Color( Data.Color ), bFill, static_cast<float32>( Data.Thickness ), NMath::Clamp( Data.NumSegments, 0, 4096 ) );
			}
			break;
		}
		case EDrawCommand::Pixel:
		{
			if ( !View.CanRead<FDrawPixelData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawPixelData>(); // 固定部を所有コピーする。
			Canvas->Box( static_cast<float32>( Data.X ), static_cast<float32>( Data.Y ), static_cast<float32>( Data.X ) + 1, static_cast<float32>( Data.Y ) + 1, Color( Data.Color ), true, 1 );
			break;
		}
		case EDrawCommand::Triangle:
		{
			if ( !View.CanRead<FDrawTriangleData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawTriangleData>(); // 固定部を所有コピーする。
			const float32 Points[6] = { Data.X0, Data.Y0, Data.X1, Data.Y1, Data.X2, Data.Y2 }; // XYの並び。
			if ( ValidPoints( Points ) && Valid( Data.Thickness ) && Data.Thickness > 0 )
			{
				Canvas->Triangle( Points, Color( Data.Color ), bFill, Data.Thickness );
			}
			break;
		}
		case EDrawCommand::Quad:
		{
			if ( !View.CanRead<FDrawQuadData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawQuadData>(); // 固定部を所有コピーする。
			const float32 Points[8] = { Data.X0, Data.Y0, Data.X1, Data.Y1, Data.X2, Data.Y2, Data.X3, Data.Y3 }; // XYの並び。
			if ( ValidPoints( Points ) && Valid( Data.Thickness ) && Data.Thickness > 0 )
			{
				Canvas->Quad( Points, Color( Data.Color ), bFill, Data.Thickness );
			}
			break;
		}
		case EDrawCommand::Text:
		{
			if ( !View.CanRead<FDrawTextData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawTextData>(); // 固定部を所有コピーする。
			const int32 Capacity = ( View.NumDataWords - static_cast<int32>( sizeof( Data ) / 4 ) ) * 4; // 命令に含まれる文字の範囲。
			if ( Data.Length >= 0 && Data.Length < Capacity )
			{
				const char* Text = View.GetTextChars(); // 検査した範囲だけを渡す。
				if ( Text[Data.Length] == '\0' )
				{
					Canvas->Text( static_cast<float32>( Data.X ), static_cast<float32>( Data.Y ), Color( Data.Color ), Text, Text + Data.Length );
				}
			}
			break;
		}
		case EDrawCommand::Image:
		{
			if ( !View.CanRead<FDrawImageData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImageData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !Textures || !Textures->GetTextureDrawView( Data.Handle, Image ) || Image.Width <= 0 || Image.Height <= 0 )
			{
				break;
			}
			float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。

			if ( View.Has( FDrawFlag::FlipX ) )
			{
				const float32 Old = UV[0]; // 左右を交換する。
				UV[0] = UV[2];
				UV[2] = Old;
			}
			if ( View.Has( FDrawFlag::FlipY ) )
			{
				const float32 Old = UV[1]; // 上下を交換する。
				UV[1] = UV[3];
				UV[3] = Old;
			}
			const float32 Points[8] = { Data.X, Data.Y, Data.X + static_cast<float32>( Image.Width ), Data.Y, Data.X + static_cast<float32>( Image.Width ), Data.Y + static_cast<float32>( Image.Height ), Data.X, Data.Y + static_cast<float32>( Image.Height ) }; // 描画先の4点。
			if ( ValidPoints( Points ) )
			{
				Canvas->Image( Image, Points, UV, Color( FDrawRgb() ) );
			}
			break;
		}
		case EDrawCommand::ImageStretch:
		{
			if ( !View.CanRead<FDrawImageStretchData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImageStretchData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !Textures || !Textures->GetTextureDrawView( Data.Handle, Image ) || Image.Width <= 0 || Image.Height <= 0 )
			{
				break;
			}
			float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。

			if ( View.Has( FDrawFlag::FlipX ) )
			{
				const float32 Old = UV[0]; // 左右を交換する。
				UV[0] = UV[2];
				UV[2] = Old;
			}
			if ( View.Has( FDrawFlag::FlipY ) )
			{
				const float32 Old = UV[1]; // 上下を交換する。
				UV[1] = UV[3];
				UV[3] = Old;
			}
			const float32 Points[8] = { Data.X0, Data.Y0, Data.X1, Data.Y0, Data.X1, Data.Y1, Data.X0, Data.Y1 }; // 描画先の4点。
			if ( ValidPoints( Points ) )
			{
				Canvas->Image( Image, Points, UV, Color( FDrawRgb() ) );
			}
			break;
		}
		case EDrawCommand::ImageQuad:
		{
			if ( !View.CanRead<FDrawImageQuadData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImageQuadData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !Textures || !Textures->GetTextureDrawView( Data.Handle, Image ) || Image.Width <= 0 || Image.Height <= 0 )
			{
				break;
			}
			float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。

			if ( View.Has( FDrawFlag::FlipX ) )
			{
				const float32 Old = UV[0]; // 左右を交換する。
				UV[0] = UV[2];
				UV[2] = Old;
			}
			if ( View.Has( FDrawFlag::FlipY ) )
			{
				const float32 Old = UV[1]; // 上下を交換する。
				UV[1] = UV[3];
				UV[3] = Old;
			}
			const float32 Points[8] = { Data.X[0], Data.Y[0], Data.X[1], Data.Y[1], Data.X[2], Data.Y[2], Data.X[3], Data.Y[3] }; // 描画先の4点。
			if ( ValidPoints( Points ) )
			{
				Canvas->Image( Image, Points, UV, Color( FDrawRgb() ) );
			}
			break;
		}
		case EDrawCommand::ImagePart:
		{
			if ( !View.CanRead<FDrawImagePartData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImagePartData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !Textures || !Textures->GetTextureDrawView( Data.Handle, Image ) || Image.Width <= 0 || Image.Height <= 0 )
			{
				break;
			}
			float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。
			if ( Data.SrcX < 0 || Data.SrcY < 0 || Data.SrcW <= 0 || Data.SrcH <= 0 || static_cast<int64>( Data.SrcX ) + Data.SrcW > Image.Width || static_cast<int64>( Data.SrcY ) + Data.SrcH > Image.Height )
			{
				break;
			}
			UV[0] = static_cast<float32>( Data.SrcX ) / Image.Width;
			UV[1] = static_cast<float32>( Data.SrcY ) / Image.Height;
			UV[2] = static_cast<float32>( Data.SrcX + Data.SrcW ) / Image.Width;
			UV[3] = static_cast<float32>( Data.SrcY + Data.SrcH ) / Image.Height;
			if ( View.Has( FDrawFlag::FlipX ) )
			{
				const float32 Old = UV[0]; // 左右を交換する。
				UV[0] = UV[2];
				UV[2] = Old;
			}
			if ( View.Has( FDrawFlag::FlipY ) )
			{
				const float32 Old = UV[1]; // 上下を交換する。
				UV[1] = UV[3];
				UV[3] = Old;
			}
			const float32 Points[8] = { Data.X, Data.Y, Data.X + static_cast<float32>( Data.SrcW ), Data.Y, Data.X + static_cast<float32>( Data.SrcW ), Data.Y + static_cast<float32>( Data.SrcH ), Data.X, Data.Y + static_cast<float32>( Data.SrcH ) }; // 描画先の4点。
			if ( ValidPoints( Points ) )
			{
				Canvas->Image( Image, Points, UV, Color( FDrawRgb() ) );
			}
			break;
		}
		case EDrawCommand::ImagePartStretch:
		{
			if ( !View.CanRead<FDrawImagePartStretchData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImagePartStretchData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !Textures || !Textures->GetTextureDrawView( Data.Handle, Image ) || Image.Width <= 0 || Image.Height <= 0 )
			{
				break;
			}
			float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。
			if ( Data.SrcX < 0 || Data.SrcY < 0 || Data.SrcW <= 0 || Data.SrcH <= 0 || static_cast<int64>( Data.SrcX ) + Data.SrcW > Image.Width || static_cast<int64>( Data.SrcY ) + Data.SrcH > Image.Height )
			{
				break;
			}
			UV[0] = static_cast<float32>( Data.SrcX ) / Image.Width;
			UV[1] = static_cast<float32>( Data.SrcY ) / Image.Height;
			UV[2] = static_cast<float32>( Data.SrcX + Data.SrcW ) / Image.Width;
			UV[3] = static_cast<float32>( Data.SrcY + Data.SrcH ) / Image.Height;
			if ( View.Has( FDrawFlag::FlipX ) )
			{
				const float32 Old = UV[0]; // 左右を交換する。
				UV[0] = UV[2];
				UV[2] = Old;
			}
			if ( View.Has( FDrawFlag::FlipY ) )
			{
				const float32 Old = UV[1]; // 上下を交換する。
				UV[1] = UV[3];
				UV[3] = Old;
			}
			const float32 Points[8] = { Data.X0, Data.Y0, Data.X1, Data.Y0, Data.X1, Data.Y1, Data.X0, Data.Y1 }; // 描画先の4点。
			if ( ValidPoints( Points ) )
			{
				Canvas->Image( Image, Points, UV, Color( FDrawRgb() ) );
			}
			break;
		}
		case EDrawCommand::ImagePartQuad:
		{
			if ( !View.CanRead<FDrawImagePartQuadData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImagePartQuadData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !Textures || !Textures->GetTextureDrawView( Data.Handle, Image ) || Image.Width <= 0 || Image.Height <= 0 )
			{
				break;
			}
			float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。
			if ( Data.SrcX < 0 || Data.SrcY < 0 || Data.SrcW <= 0 || Data.SrcH <= 0 || static_cast<int64>( Data.SrcX ) + Data.SrcW > Image.Width || static_cast<int64>( Data.SrcY ) + Data.SrcH > Image.Height )
			{
				break;
			}
			UV[0] = static_cast<float32>( Data.SrcX ) / Image.Width;
			UV[1] = static_cast<float32>( Data.SrcY ) / Image.Height;
			UV[2] = static_cast<float32>( Data.SrcX + Data.SrcW ) / Image.Width;
			UV[3] = static_cast<float32>( Data.SrcY + Data.SrcH ) / Image.Height;
			if ( View.Has( FDrawFlag::FlipX ) )
			{
				const float32 Old = UV[0]; // 左右を交換する。
				UV[0] = UV[2];
				UV[2] = Old;
			}
			if ( View.Has( FDrawFlag::FlipY ) )
			{
				const float32 Old = UV[1]; // 上下を交換する。
				UV[1] = UV[3];
				UV[3] = Old;
			}
			const float32 Points[8] = { Data.X[0], Data.Y[0], Data.X[1], Data.Y[1], Data.X[2], Data.Y[2], Data.X[3], Data.Y[3] }; // 描画先の4点。
			if ( ValidPoints( Points ) )
			{
				Canvas->Image( Image, Points, UV, Color( FDrawRgb() ) );
			}
			break;
		}
		default:
		{
			const auto Result = Geometry.Execute( View, Bright, Alpha, bAlphaBlend ); // 同じ順序で3Dへ渡す。
			if ( Result == FGeometryCommandExecutor::EResult::Executed )
			{
				break;
			}
			// 未対応の状態を無視して描くと、別の描画先や変換で誤表示するためSpanを止める。
			NBackendSupport::ReportUnsupported( "DrawCommands", *FString::Printf( "Draw command %u", static_cast<unsigned>( View.Type ) ) );
			bStopped = true;
			break;
		}
		}
		Offset += Count;
	}
	while ( ClipDepth > 0 )
	{
		Canvas->PopClip();
		--ClipDepth;
	}
	Canvas->SetAntiAlias( true );
	if ( bStopped )
	{
		NBackendSupport::ReportUnsupported( "DrawCommands", "Incomplete span", "Unsupported state or malformed payload: remaining commands were skipped" );
	}
}
