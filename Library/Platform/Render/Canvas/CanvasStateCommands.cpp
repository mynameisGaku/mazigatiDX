// SPDX-License-Identifier: MIT
#include "CanvasStateCommands.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/BackendSupport.h"

namespace
{
	using NCanvasValidation::ValidPoints;
	using FMaskState = FCanvasMaskState;
}

CCanvasStateCommands::CCanvasStateCommands( FCanvasExecution& InRun )
	: Run( InRun )
	, Surface( Run.Surface )
	, Canvas( Run.Canvas )
	, Geometry( Run.Geometry )
	, bStopped( Run.bStopped )
	, bLinearFilter( Run.State.bLinearFilter )
	, bExplicitFilter( Run.State.bExplicitFilter )
	, bAlphaBlend( Run.State.bAlphaBlend )
	, Bright( Run.State.Bright )
	, Alpha( Run.State.Alpha )
	, AddColorValue( Run.State.AddColorValue )
	, bAlphaTest( Run.State.bAlphaTest )
	, AlphaTestMode( Run.State.AlphaTestMode )
	, AlphaTestReference( Run.State.AlphaTestReference )
	, ClipDepth( Run.State.ClipDepth )
	, Transform( Run.State.Transform )
	, MaskStack( Run.State.MaskStack )
{
}

bool CCanvasStateCommands::GetImage( int32 Handle, FTextureDrawView& Image )
{
	return Run.GetImage( Handle, Image );
}

void CCanvasStateCommands::ApplyMask()
{
	Run.ApplyMask();
}

bool CCanvasStateCommands::AlphaTestPasses( int32 Value )
{
	return Run.State.AlphaTestPasses( Value );
}

bool CCanvasStateCommands::AlphaTestMayPass()
{
	return Run.State.AlphaTestMayPass();
}

// アルファテストで、この命令を描かないか。
bool CCanvasStateCommands::ShouldSkipForAlphaTest( const FDrawCommandView& View )
{
	if ( bAlphaTest )
	{
		// アルファテスト: 平らな図形は、アルファが1つに決まるので、通らなければ丸ごと飛ばす。画像・文字は、通りうる画素が無いときだけ飛ばす（画素ごとの比べは、まだ無い）。
		bool bFlatShape = false;
		bool bImageLike = false;
		switch ( View.Type )
		{
		case EDrawCommand::Box:
		case EDrawCommand::BoxAA:
		case EDrawCommand::Line:
		case EDrawCommand::LineAA:
		case EDrawCommand::Circle:
		case EDrawCommand::CircleAA:
		case EDrawCommand::Oval:
		case EDrawCommand::RoundBox:
		case EDrawCommand::Pixel:
		case EDrawCommand::Triangle:
		case EDrawCommand::Quad:
			bFlatShape = true;
			break;
		case EDrawCommand::Text:
		case EDrawCommand::TextFont:
		case EDrawCommand::TextStyled:
		case EDrawCommand::Image:
		case EDrawCommand::ImageStretch:
		case EDrawCommand::ImageQuad:
		case EDrawCommand::ImagePart:
		case EDrawCommand::ImagePartStretch:
		case EDrawCommand::ImagePartQuad:
		case EDrawCommand::Sprite:
		case EDrawCommand::SpriteRect:
		case EDrawCommand::SpritePivot:
		case EDrawCommand::Tile:
		case EDrawCommand::CircleGauge:
			bImageLike = true;
			break;
		default:
			break;
		}
		if ( bFlatShape && !AlphaTestPasses( Alpha ) )
		{
			return true;
		}
		if ( bImageLike )
		{
			if ( !AlphaTestMayPass() )
			{
				return true;
			}
			NBackendSupport::ReportUnsupported( "DrawCommands", "AlphaTest (image / text)", "AlphaTest is applied to flat shapes only; images and text are drawn without a per-pixel test" );
		}
	}
	return false;
}

bool CCanvasStateCommands::Execute( const FDrawCommandView& View )
{
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
		EGeometryBlend Mode = EGeometryBlend::Alpha; // 実行先の混ぜ方。
		switch ( static_cast<EDrawBlend>( Data.Mode ) )
		{
		case EDrawBlend::Opaque:
		case EDrawBlend::Alpha:
			break;
		case EDrawBlend::Add:
			Mode = EGeometryBlend::Add;
			break;
		case EDrawBlend::Subtract:
			Mode = EGeometryBlend::Subtract;
			break;
		case EDrawBlend::Multiply:
			Mode = EGeometryBlend::Multiply;
			break;
		case EDrawBlend::Invert:
		case EDrawBlend::PmaInvert: // 乗算済みアルファの反転は、DxLib では、通常の反転と同じ結果（実測）。
			Mode = EGeometryBlend::Invert; // 描く色を反転して、アルファブレンドする（図形・画像・文字・3D のすべて）。
			break;
		case EDrawBlend::MultiplyAlpha:
			Mode = EGeometryBlend::MultiplyAlpha; // 描画先 x ( 1 ＋ ( 描く色 － 1 ) x アルファ )。
			break;
		case EDrawBlend::PmaAlpha:
			Mode = EGeometryBlend::PremultipliedAlpha;
			break;
		case EDrawBlend::PmaAdd:
			Mode = EGeometryBlend::PremultipliedAdd;
			break;
		case EDrawBlend::PmaSubtract:
			Mode = EGeometryBlend::PremultipliedSubtract;
			break;
		default:
			// 反転などは、まだ無い。通常の合成で描く（色は変わるが、形は出る）。
			NBackendSupport::ReportUnsupported( "DrawCommands", "Blend mode", "An unknown blend mode is drawn with Alpha" );
			break;
		}
		bAlphaBlend = Data.Mode != static_cast<int32>( EDrawBlend::Opaque );
		Alpha = Data.Mode == static_cast<int32>( EDrawBlend::Opaque ) ? 255 : NMath::Clamp( Data.Param, 0, 255 );
		Canvas->SetBlend( Mode );
		Geometry.SetBlend( Mode );
		break;
	}
	case EDrawCommand::CustomBlend:
	{
		if ( !View.CanRead<FDrawCustomBlendData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawCustomBlendData>(); // 固定部を所有コピーする。
		// 係数の番号は、EDrawBlendFactor / EDrawBlendOp と同じ並び。範囲の外は、通常の合成で描く。
		const auto Factor = [&]( int32 Value ) { return static_cast<EGeometryBlendFactor>( NMath::Clamp( Value, 0, static_cast<int32>( EGeometryBlendFactor::SrcAlphaSaturate ) ) ); };
		const auto Op = [&]( int32 Value ) { return static_cast<EGeometryBlendOp>( NMath::Clamp( Value, 0, static_cast<int32>( EGeometryBlendOp::Max ) ) ); };
		FGeometryCustomBlend Custom; // 利用者が決めた混ぜ方。
		Custom.SrcColor = Factor( Data.SrcColor );
		Custom.DestColor = Factor( Data.DestColor );
		Custom.ColorOp = Op( Data.ColorOp );
		Custom.SrcAlpha = Factor( Data.SrcAlpha );
		Custom.DestAlpha = Factor( Data.DestAlpha );
		Custom.AlphaOp = Op( Data.AlphaOp );
		bAlphaBlend = true;
		Alpha = NMath::Clamp( Data.Param, 0, 255 );
		Canvas->SetCustomBlend( Custom );
		Geometry.SetCustomBlend( Custom );
		Geometry.SetBlend( EGeometryBlend::Custom );
		break;
	}
	case EDrawCommand::Filter:
	{
		if ( !View.CanRead<FDrawFilterData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawFilterData>(); // 2Dと3Dで同じ設定を使う。
		if ( Data.Mode < 0 || Data.Mode > static_cast<int32>( EDrawFilter::Anisotropic ) )
		{
			bStopped = true;
			break;
		}
		bLinearFilter = Data.Mode >= 1; // 異方性は、線形として扱う。
		bExplicitFilter = true;
		FDrawFilterData Simple = Data; // 3D側は、異方性も受け取る。
		Simple.Mode = Data.Mode;
		FDrawCommandView SimpleView = View; // 値を差し替えた命令。
		SimpleView.Data = reinterpret_cast<const uint32*>( &Simple );
		Geometry.Execute( SimpleView, Bright, Alpha, bAlphaBlend );
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
	case EDrawCommand::Transform2D:
	{
		if ( !View.CanRead<FDrawMatrixData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawMatrixData>(); // 固定部を所有コピーする。
		if ( !ValidPoints( Data.M, 12 ) )
		{
			bStopped = true;
			break;
		}
		Transform.XX = Data.M[0];
		Transform.XY = Data.M[1];
		Transform.YX = Data.M[3];
		Transform.YY = Data.M[4];
		Transform.TX = Data.M[9];
		Transform.TY = Data.M[10];
		Transform.bIdentity = Transform.XX == 1 && Transform.XY == 0 && Transform.YX == 0 && Transform.YY == 1 && Transform.TX == 0 && Transform.TY == 0;
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
		if ( ClipDepth > 0 )
		{
			Canvas->PopClip();
			--ClipDepth;
		}
		break;
	}
	case EDrawCommand::AddColor:
	{
		if ( !View.CanRead<FDrawAddColorData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawAddColorData>(); // 固定部を所有コピーする。
		AddColorValue[0] = NMath::Clamp( Data.R, -255, 255 );
		AddColorValue[1] = NMath::Clamp( Data.G, -255, 255 );
		AddColorValue[2] = NMath::Clamp( Data.B, -255, 255 );
		break;
	}
	case EDrawCommand::AlphaTest:
	{
		if ( !View.CanRead<FDrawAlphaTestData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawAlphaTestData>(); // 固定部を所有コピーする。
		bAlphaTest = Data.Enabled != 0;
		AlphaTestMode = static_cast<EDrawCompare>( NMath::Clamp( Data.Mode, 0, static_cast<int32>( EDrawCompare::Always ) ) );
		AlphaTestReference = NMath::Clamp( Data.Reference, 0, 255 );
		break;
	}
	case EDrawCommand::PushMask:
	{
		if ( !View.CanRead<FDrawMaskData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawMaskData>(); // 固定部を所有コピーする。
		FMaskState Mask;
		FTextureDrawView Image; // マスクの画像。
		// DxLibと同じく、マスクにできない画像は、マスクなしで描く（そのマスクも、1 つに数える）。2Dの描画だけが、マスクで薄まる。
		if ( GetImage( Data.Handle, Image ) )
		{
			Mask.View = Image;
		}
		Mask.Channel = NMath::Clamp( Data.Channel, 0, 3 );
		Mask.bInvert = Data.Invert != 0;
		MaskStack.Add( Mask );
		ApplyMask();
		break;
	}
	case EDrawCommand::PopMask:
	{
		if ( MaskStack.IsEmpty() )
		{
			break;
		}
		MaskStack.Pop();
		ApplyMask();
		break;
	}
	default:
		return false;
	}
	return true;
}
