#include "DxLibImageOps.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	// EDrawGraphFilter の順に並べた、DxLib の種類。乗算済みアルファ版が無いものは -1
	constexpr int FilterTypes[] = {
		DX_GRAPH_FILTER_MONO,
		DX_GRAPH_FILTER_GAUSS,
		DX_GRAPH_FILTER_DOWN_SCALE,
		DX_GRAPH_FILTER_BRIGHT_CLIP,
		DX_GRAPH_FILTER_BRIGHT_SCALE,
		DX_GRAPH_FILTER_HSB,
		DX_GRAPH_FILTER_INVERT,
		DX_GRAPH_FILTER_LEVEL,
		DX_GRAPH_FILTER_TWO_COLOR,
		DX_GRAPH_FILTER_GRADIENT_MAP,
		DX_GRAPH_FILTER_REPLACEMENT,
		DX_GRAPH_FILTER_PREMUL_ALPHA,
		DX_GRAPH_FILTER_INTERP_ALPHA,
		DX_GRAPH_FILTER_BICUBIC_SCALE,
		DX_GRAPH_FILTER_LANCZOS3_SCALE,
		DX_GRAPH_FILTER_FLOAT_COLOR_SCALE,
	};
	constexpr int PremultipliedFilterTypes[] = {
		-1,
		-1,
		-1,
		DX_GRAPH_FILTER_PMA_BRIGHT_CLIP,
		DX_GRAPH_FILTER_PMA_BRIGHT_SCALE,
		DX_GRAPH_FILTER_PMA_HSB,
		DX_GRAPH_FILTER_PMA_INVERT,
		DX_GRAPH_FILTER_PMA_LEVEL,
		DX_GRAPH_FILTER_PMA_TWO_COLOR,
		DX_GRAPH_FILTER_PMA_GRADIENT_MAP,
		DX_GRAPH_FILTER_PMA_REPLACEMENT,
		-1,
		-1,
		-1,
		-1,
		-1,
	};
	static_assert( sizeof( FilterTypes ) == sizeof( PremultipliedFilterTypes ), "the two filter tables must have the same length" );
	constexpr int NumFilterTypes = static_cast<int>( sizeof( FilterTypes ) / sizeof( FilterTypes[0] ) );

	// EDrawGraphBlend の順に並べた、DxLib の種類
	constexpr int BlendTypes[] = {
		DX_GRAPH_BLEND_NORMAL,
		DX_GRAPH_BLEND_RGBA_SELECT_MIX,
		DX_GRAPH_BLEND_MULTIPLE,
		DX_GRAPH_BLEND_DIFFERENCE,
		DX_GRAPH_BLEND_ADD,
		DX_GRAPH_BLEND_SCREEN,
		DX_GRAPH_BLEND_OVERLAY,
		DX_GRAPH_BLEND_DODGE,
		DX_GRAPH_BLEND_BURN,
		DX_GRAPH_BLEND_DARKEN,
		DX_GRAPH_BLEND_LIGHTEN,
		DX_GRAPH_BLEND_SOFTLIGHT,
		DX_GRAPH_BLEND_HARDLIGHT,
		DX_GRAPH_BLEND_EXCLUSION,
		DX_GRAPH_BLEND_NORMAL_ALPHACH,
		DX_GRAPH_BLEND_ADD_ALPHACH,
		DX_GRAPH_BLEND_MULTIPLE_A_ONLY,
		DX_GRAPH_BLEND_MASK,
	};
	constexpr int PremultipliedBlendTypes[] = {
		DX_GRAPH_BLEND_PMA_NORMAL,
		DX_GRAPH_BLEND_PMA_RGBA_SELECT_MIX,
		DX_GRAPH_BLEND_PMA_MULTIPLE,
		DX_GRAPH_BLEND_PMA_DIFFERENCE,
		DX_GRAPH_BLEND_PMA_ADD,
		DX_GRAPH_BLEND_PMA_SCREEN,
		DX_GRAPH_BLEND_PMA_OVERLAY,
		DX_GRAPH_BLEND_PMA_DODGE,
		DX_GRAPH_BLEND_PMA_BURN,
		DX_GRAPH_BLEND_PMA_DARKEN,
		DX_GRAPH_BLEND_PMA_LIGHTEN,
		DX_GRAPH_BLEND_PMA_SOFTLIGHT,
		DX_GRAPH_BLEND_PMA_HARDLIGHT,
		DX_GRAPH_BLEND_PMA_EXCLUSION,
		DX_GRAPH_BLEND_PMA_NORMAL_ALPHACH,
		DX_GRAPH_BLEND_PMA_ADD_ALPHACH,
		DX_GRAPH_BLEND_PMA_MULTIPLE_A_ONLY,
		DX_GRAPH_BLEND_PMA_MASK,
	};
	static_assert( sizeof( BlendTypes ) == sizeof( PremultipliedBlendTypes ), "the two blend tables must have the same length" );
	constexpr int NumBlendTypes = static_cast<int>( sizeof( BlendTypes ) / sizeof( BlendTypes[0] ) );

	// 転送先のブレンドの設定を、呼ぶ間だけ決めて、終わりに元（ブレンドなし）へ戻す
	class FTransferBlendScope
	{
	public:
		explicit FTransferBlendScope( bool bAlphaBlend )
		{
			SetGraphFilterBltBlendMode( bAlphaBlend ? DX_BLENDMODE_ALPHA : DX_BLENDMODE_NOBLEND );
		}

		~FTransferBlendScope()
		{
			SetGraphFilterBltBlendMode( DX_BLENDMODE_NOBLEND );
		}

		FTransferBlendScope( const FTransferBlendScope& ) = delete;
		FTransferBlendScope& operator=( const FTransferBlendScope& ) = delete;
	};
}

int NDxLibImageOps::ToDxFilterType( int32 Type, bool bPremultiplied )
{
	if ( Type < 0 || Type >= NumFilterTypes )
	{
		return -1;
	}
	if ( bPremultiplied && PremultipliedFilterTypes[Type] >= 0 )
	{
		return PremultipliedFilterTypes[Type];
	}
	return FilterTypes[Type];
}

int NDxLibImageOps::ToDxBlendType( int32 Type, bool bPremultiplied )
{
	if ( Type < 0 || Type >= NumBlendTypes )
	{
		return -1;
	}
	return bPremultiplied ? PremultipliedBlendTypes[Type] : BlendTypes[Type];
}

int NDxLibImageOps::ToDxMaskChannel( int32 Channel )
{
	switch ( static_cast<EDrawMaskChannel>( Channel ) )
	{
	case EDrawMaskChannel::Red: return DX_MASKGRAPH_CH_R;
	case EDrawMaskChannel::Green: return DX_MASKGRAPH_CH_G;
	case EDrawMaskChannel::Blue: return DX_MASKGRAPH_CH_B;
	default: return DX_MASKGRAPH_CH_A;
	}
}

bool NDxLibImageOps::ApplyFilter( const FDrawImageFilterData& Data )
{
	const int Type = ToDxFilterType( Data.Type, Data.Premultiplied != 0 );
	if ( Type < 0 || Data.Source < 0 || Data.Dest < 0 )
	{
		return false;
	}
	const bool bInPlace = Data.Source == Data.Dest && Data.HasRect == 0;
	// 転送先に重ねるのは、その場でない（転送先が別にある）ときだけ
	const FTransferBlendScope Blend( Data.Blend != 0 && !bInPlace );

	// 3 つの関数（その場・転送・矩形つき転送）の、種類より後ろの引数（種類ごとに違う）を、同じ形で渡すための窓口
	const auto Run = [&]( auto... Arguments ) -> int
	{
		if ( bInPlace )
		{
			return GraphFilter( Data.Source, Type, Arguments... );
		}
		if ( Data.HasRect == 0 )
		{
			return GraphFilterBlt( Data.Source, Data.Dest, Type, Arguments... );
		}
		return GraphFilterRectBlt( Data.Source, Data.Dest, Data.SrcX, Data.SrcY, Data.SrcX + Data.SrcW, Data.SrcY + Data.SrcH, Data.DestX, Data.DestY, Type, Arguments... );
	};
	const int* const P = Data.Params;
	int Result = -1;
	switch ( static_cast<EDrawGraphFilter>( Data.Type ) )
	{
	case EDrawGraphFilter::Mono:
	case EDrawGraphFilter::Gauss:
	case EDrawGraphFilter::BrightScale:
	case EDrawGraphFilter::BicubicScale:
	case EDrawGraphFilter::Lanczos3Scale:
		Result = Run( P[0], P[1] );
		break;
	case EDrawGraphFilter::DownScale:
		Result = Run( P[0] );
		break;
	case EDrawGraphFilter::BrightClip:
		// Params: 暗いほうをクリップするか、しきい値、塗るか、塗る色（0xRRGGBB）、塗るアルファ
		Result = Run( P[0] != 0 ? static_cast<int>( DX_CMP_LESS ) : static_cast<int>( DX_CMP_GREATER ), P[1], P[2] != 0 ? TRUE : FALSE, static_cast<unsigned int>( P[3] ), P[4] );
		break;
	case EDrawGraphFilter::Hsb:
		Result = Run( P[0], P[1], P[2], P[3] );
		break;
	case EDrawGraphFilter::Invert:
	case EDrawGraphFilter::PremulAlpha:
	case EDrawGraphFilter::InterpAlpha:
		Result = Run();
		break;
	case EDrawGraphFilter::Level:
		Result = Run( P[0], P[1], P[2], P[3], P[4] );
		break;
	case EDrawGraphFilter::TwoColor:
		Result = Run( P[0], static_cast<unsigned int>( P[1] ), P[2], static_cast<unsigned int>( P[3] ), P[4] );
		break;
	case EDrawGraphFilter::GradientMap:
		if ( Data.Map < 0 )
		{
			return false;
		}
		Result = Run( Data.Map, P[0] != 0 ? TRUE : FALSE );
		break;
	case EDrawGraphFilter::Replacement:
		Result = Run( P[0], P[1], P[2], P[3], P[4], P[5], P[6], P[7] );
		break;
	case EDrawGraphFilter::FloatColorScale:
	{
		// float は、ビットのまま入っている（FGraphFilter::FloatColorScale）。可変長引数では double に広がって渡る
		float32 F[8] = {};
		for ( int Index = 0; Index < 8; ++Index )
		{
			FMemory::Memcpy( &F[Index], &P[Index], sizeof( float32 ) );
		}
		Result = Run( F[0], F[1], F[2], F[3], F[4], F[5], F[6], F[7] );
		break;
	}
	}
	return Result == 0;
}

bool NDxLibImageOps::BlendImages( const FDrawImageBlendData& Data )
{
	const int Type = ToDxBlendType( Data.Type, Data.Premultiplied != 0 );
	if ( Type < 0 || Data.Base < 0 || Data.Blend < 0 || Data.Dest < 0 )
	{
		return false;
	}
	const int Ratio = NMath::Clamp( Data.Ratio, 0, 255 );
	// 結果は、転送先を置き換える
	const FTransferBlendScope Blend( false );
	const bool bInPlace = Data.Base == Data.Dest;
	const bool bSelect = Data.Type == static_cast<int32>( EDrawGraphBlend::RgbaSelectMix );
	if ( bSelect )
	{
		// 出力の R / G / B / A になる成分（EDrawRgbaSelect の値は、DX_RGBA_SELECT_* と同じ並び）
		const int* const S = Data.Select;
		if ( bInPlace )
		{
			return GraphBlend( Data.Base, Data.Blend, Ratio, Type, S[0], S[1], S[2], S[3] ) == 0;
		}
		return GraphBlendBlt( Data.Base, Data.Blend, Data.Dest, Ratio, Type, S[0], S[1], S[2], S[3] ) == 0;
	}
	if ( bInPlace )
	{
		return GraphBlend( Data.Base, Data.Blend, Ratio, Type ) == 0;
	}
	return GraphBlendBlt( Data.Base, Data.Blend, Data.Dest, Ratio, Type ) == 0;
}
