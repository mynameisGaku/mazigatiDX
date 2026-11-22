// SPDX-License-Identifier: MIT
#include "CanvasExecution.h"
#include "Library/Platform/System/BackendSupport.h"

namespace
{
	// 未対応の命令の名前（警告に出す）。
	FString CommandName( EDrawCommand Type )
	{
		return FString::Printf( "Draw command %u", static_cast<unsigned>( Type ) );
	}
}

FColor32 FCanvasDrawState::MakeColor( FDrawRgb Value ) const
{
	return FColor32( static_cast<uint8>( Value.R * Bright.R / 255 ), static_cast<uint8>( Value.G * Bright.G / 255 ),
		static_cast<uint8>( Value.B * Bright.B / 255 ), static_cast<uint8>( Alpha ) );
}

// 平らな図形の色（描く色に、AddColor の値を足す。DxLibと同じく、明るさの乗算のあとに足す）。
FColor32 FCanvasDrawState::MakeShapeColor( FDrawRgb Value ) const
{
	const FColor32 Base = MakeColor( Value );
	FColor32 Result( static_cast<uint8>( NMath::Clamp( static_cast<int32>( Base.r ) + AddColorValue[0], 0, 255 ) ), static_cast<uint8>( NMath::Clamp( static_cast<int32>( Base.g ) + AddColorValue[1], 0, 255 ) ),
		static_cast<uint8>( NMath::Clamp( static_cast<int32>( Base.b ) + AddColorValue[2], 0, 255 ) ), Base.a );
	return Result;
}

// アルファテストを、あるアルファが通るか。
bool FCanvasDrawState::AlphaTestPasses( int32 Value ) const
{
	switch ( AlphaTestMode )
	{
	case EDrawCompare::Never:
		return false;
	case EDrawCompare::Less:
		return Value < AlphaTestReference;
	case EDrawCompare::Equal:
		return Value == AlphaTestReference;
	case EDrawCompare::LessEqual:
		return Value <= AlphaTestReference;
	case EDrawCompare::Greater:
		return Value > AlphaTestReference;
	case EDrawCompare::NotEqual:
		return Value != AlphaTestReference;
	case EDrawCompare::GreaterEqual:
		return Value >= AlphaTestReference;
	default:
		return true;
	}
}

// 画素ごとにアルファが違う図（画像・文字）が、1画素でも通りうるか（0から、いまの不透明度まで）。
bool FCanvasDrawState::AlphaTestMayPass() const
{
	for ( int32 Value = 0; Value <= Alpha; ++Value )
	{
		if ( AlphaTestPasses( Value ) )
		{
			return true;
		}
	}
	return false;
}

FCanvasExecution::FCanvasExecution( IRenderSurface& InSurface, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers, IStaticModelSource* InModels, IFontDrawSource* InFonts, IRenderTargetDrawSource* InTargets, IShaderDrawSource* InShaders, const FDrawSpan& InSpan, IRenderCanvas2D* InCanvas )
	: Surface( InSurface )
	, Textures( InTextures )
	, Buffers( InBuffers )
	, Models( InModels )
	, Fonts( InFonts )
	, Targets( InTargets )
	, Shaders( InShaders )
	, Span( InSpan )
	, Canvas( InCanvas )
	, Geometry( InSurface.GetGeometry3D(), InTextures, InBuffers, InModels )
	, ShaderExecutor( Geometry, InShaders )
	, MaterialExecutor( Geometry, ShaderExecutor )
{
	Geometry.SetShaderSource( InShaders ); // 影の描画にも、同じ実行先を使う。
}

void FCanvasExecution::Unsupported( const FDrawCommandView& View, const char* Reason )
{
	NBackendSupport::ReportUnsupported( "DrawCommands", *CommandName( View.Type ), Reason );
}

bool FCanvasExecution::GetImage( int32 Handle, FTextureDrawView& Image )
{
	return Textures && Textures->GetTextureDrawView( Handle, Image ) && Image.Width > 0 && Image.Height > 0;
}

void FCanvasExecution::ApplyMask()
{
	if ( !Canvas )
	{
		return;
	}
	if ( State.MaskStack.IsEmpty() || State.MaskStack[State.MaskStack.Num() - 1].View.Binding == 0 )
	{
		Canvas->SetMask( nullptr, 0, false );
		return;
	}
	const FCanvasMaskState& Top = State.MaskStack[State.MaskStack.Num() - 1];
	Canvas->SetMask( &Top.View, Top.Channel, Top.bInvert );
}
