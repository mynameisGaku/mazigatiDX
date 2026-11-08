// SPDX-License-Identifier: MIT
#include "UIBrush.h"

#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Material/MaterialSubsystem.h"

#include "Library/Core/Types/Numeric.h"

namespace
{
	// マテリアルのブラシへ渡す、部品の状態（スレッドごと）
	thread_local FUIMaterialState CurrentMaterialState;
}

FUIMaterialStateScope::FUIMaterialStateScope( const FUIMaterialState& State )
	: Previous( CurrentMaterialState )
{
	CurrentMaterialState = State;
}

FUIMaterialStateScope::~FUIMaterialStateScope()
{
	CurrentMaterialState = Previous;
}

FUIBrush FUIBrush::MakeMaterial( const TSharedPtr<CMaterial>& InMaterial, const FColor32& InTint )
{
	FUIBrush Brush;
	Brush.Kind = EUIBrushKind::Material;
	Brush.Material = InMaterial;
	Brush.Color = InTint;
	return Brush;
}

namespace
{
	// 画像の大きさと貼り方から、画面に描く矩形と、元の画像の切り出し範囲（画素）を決める
	struct FImagePlacement
	{
		float32 X0 = 0.0f;
		float32 Y0 = 0.0f;
		float32 X1 = 0.0f;
		float32 Y1 = 0.0f;
		int32 SrcX = 0;
		int32 SrcY = 0;
		int32 SrcW = 0;
		int32 SrcH = 0;
	};

	int32 RoundToInt( float32 Value )
	{
		return static_cast<int32>( Value >= 0.0f ? Value + 0.5f : Value - 0.5f );
	}

	void SetBlend( FDrawContext& CommandList, int32 Alpha )
	{
		CommandList.Blend( EDrawBlend::Alpha, Alpha );
	}

	void DrawSolid( FDrawContext& CommandList, const FUIBrush& Brush, const FUIRect& Rect, float32 Opacity )
	{
		const float32 Radius = NMath::Clamp( Brush.CornerRadius, 0.0f, NMath::Min( Rect.Size.x, Rect.Size.y ) * 0.5f );
		const int32 FillAlpha = NUIBrush::ToAlpha( Brush.Color, Opacity );
		if ( FillAlpha > 0 )
		{
			SetBlend( CommandList, FillAlpha );
			if ( Radius > 0.0f )
			{
				CommandList.RoundBoxAA( Rect.Left(), Rect.Top(), Rect.Right(), Rect.Bottom(), Radius, Radius, Brush.Color, true );
			}
			else
			{
				CommandList.BoxAA( Rect.Left(), Rect.Top(), Rect.Right(), Rect.Bottom(), Brush.Color, true );
			}
		}

		const int32 OutlineAlpha = NUIBrush::ToAlpha( Brush.OutlineColor, Opacity );
		if ( Brush.OutlineThickness > 0.0f && OutlineAlpha > 0 )
		{
			SetBlend( CommandList, OutlineAlpha );
			// 枠は、矩形の内側に収まるよう、太さの半分だけ縮めた線で描く
			const float32 Half = Brush.OutlineThickness * 0.5f;
			const float32 Left = Rect.Left() + Half;
			const float32 Top = Rect.Top() + Half;
			const float32 Right = Rect.Right() - Half;
			const float32 Bottom = Rect.Bottom() - Half;
			if ( Right > Left && Bottom > Top )
			{
				if ( Radius > 0.0f )
				{
					const float32 OutlineRadius = NMath::Max( 0.0f, Radius - Half );
					CommandList.RoundBoxAA( Left, Top, Right, Bottom, OutlineRadius, OutlineRadius, Brush.OutlineColor, false, Brush.OutlineThickness );
				}
				else
				{
					CommandList.BoxAA( Left, Top, Right, Bottom, Brush.OutlineColor, false, Brush.OutlineThickness );
				}
			}
		}
	}

	void DrawNineSlice( FDrawContext& CommandList, const FUIBrush& Brush, const FUIRect& Rect, FImageHandle Handle )
	{
		const int32 ImageWidth = RoundToInt( Brush.ImageSize.x );
		const int32 ImageHeight = RoundToInt( Brush.ImageSize.y );

		// 余白は、画像の大きさを超えない（左右の和が幅を、上下の和が高さを超えたら、比を保って縮める）
		float32 Left = NMath::Max( 0.0f, Brush.NineSliceMargin.Left );
		float32 Right = NMath::Max( 0.0f, Brush.NineSliceMargin.Right );
		float32 Top = NMath::Max( 0.0f, Brush.NineSliceMargin.Top );
		float32 Bottom = NMath::Max( 0.0f, Brush.NineSliceMargin.Bottom );
		if ( Left + Right > static_cast<float32>( ImageWidth ) && Left + Right > 0.0f )
		{
			const float32 Scale = static_cast<float32>( ImageWidth ) / ( Left + Right );
			Left *= Scale;
			Right *= Scale;
		}
		if ( Top + Bottom > static_cast<float32>( ImageHeight ) && Top + Bottom > 0.0f )
		{
			const float32 Scale = static_cast<float32>( ImageHeight ) / ( Top + Bottom );
			Top *= Scale;
			Bottom *= Scale;
		}
		const int32 SrcLeft = RoundToInt( Left );
		const int32 SrcRight = RoundToInt( Right );
		const int32 SrcTop = RoundToInt( Top );
		const int32 SrcBottom = RoundToInt( Bottom );

		// 画面側の余白は、矩形が小さいとき、比を保って縮める（四隅が重ならないように）
		float32 DestLeft = static_cast<float32>( SrcLeft );
		float32 DestRight = static_cast<float32>( SrcRight );
		float32 DestTop = static_cast<float32>( SrcTop );
		float32 DestBottom = static_cast<float32>( SrcBottom );
		if ( DestLeft + DestRight > Rect.Size.x && DestLeft + DestRight > 0.0f )
		{
			const float32 Scale = Rect.Size.x / ( DestLeft + DestRight );
			DestLeft *= Scale;
			DestRight *= Scale;
		}
		if ( DestTop + DestBottom > Rect.Size.y && DestTop + DestBottom > 0.0f )
		{
			const float32 Scale = Rect.Size.y / ( DestTop + DestBottom );
			DestTop *= Scale;
			DestBottom *= Scale;
		}

		// 列・行の、元の画像の範囲と、画面の範囲
		const int32 SrcColumnStart[3] = { 0, SrcLeft, ImageWidth - SrcRight };
		const int32 SrcColumnSize[3] = { SrcLeft, ImageWidth - SrcLeft - SrcRight, SrcRight };
		const int32 SrcRowStart[3] = { 0, SrcTop, ImageHeight - SrcBottom };
		const int32 SrcRowSize[3] = { SrcTop, ImageHeight - SrcTop - SrcBottom, SrcBottom };
		const float32 DestColumnStart[3] = { Rect.Left(), Rect.Left() + DestLeft, Rect.Right() - DestRight };
		const float32 DestColumnEnd[3] = { Rect.Left() + DestLeft, Rect.Right() - DestRight, Rect.Right() };
		const float32 DestRowStart[3] = { Rect.Top(), Rect.Top() + DestTop, Rect.Bottom() - DestBottom };
		const float32 DestRowEnd[3] = { Rect.Top() + DestTop, Rect.Bottom() - DestBottom, Rect.Bottom() };

		for ( int32 Row = 0; Row < 3; ++Row )
		{
			for ( int32 Column = 0; Column < 3; ++Column )
			{
				if ( SrcColumnSize[Column] <= 0 || SrcRowSize[Row] <= 0 || DestColumnEnd[Column] <= DestColumnStart[Column] || DestRowEnd[Row] <= DestRowStart[Row] )
				{
					continue;
				}
				CommandList.ImagePartStretch( DestColumnStart[Column], DestRowStart[Row], DestColumnEnd[Column], DestRowEnd[Row], SrcColumnStart[Column], SrcRowStart[Row], SrcColumnSize[Column],
											  SrcRowSize[Row], Handle );
			}
		}
	}

	void DrawImage( FDrawContext& CommandList, const FUIBrush& Brush, const FUIRect& Rect, float32 Opacity )
	{
		if ( !Brush.Image )
		{
			return;
		}
		const FImageHandle Handle = Brush.Image->GetImageHandle();
		const int32 ImageWidth = RoundToInt( Brush.ImageSize.x );
		const int32 ImageHeight = RoundToInt( Brush.ImageSize.y );
		if ( !Handle.IsValid() || ImageWidth <= 0 || ImageHeight <= 0 )
		{
			return;
		}
		const int32 Alpha = NUIBrush::ToAlpha( Brush.Color, Opacity );
		if ( Alpha <= 0 )
		{
			return;
		}

		SetBlend( CommandList, Alpha );
		CommandList.Tint( FColor32( Brush.Color.r, Brush.Color.g, Brush.Color.b ) );

		switch ( Brush.ImageMode )
		{
		case EUIImageMode::Stretch:
			CommandList.ImageStretch( Rect.Left(), Rect.Top(), Rect.Right(), Rect.Bottom(), Handle );
			break;

		case EUIImageMode::Fit:
		{
			const float32 Scale = NMath::Min( Rect.Size.x / static_cast<float32>( ImageWidth ), Rect.Size.y / static_cast<float32>( ImageHeight ) );
			const float32 Width = static_cast<float32>( ImageWidth ) * Scale;
			const float32 Height = static_cast<float32>( ImageHeight ) * Scale;
			const float32 X = Rect.Left() + ( Rect.Size.x - Width ) * 0.5f;
			const float32 Y = Rect.Top() + ( Rect.Size.y - Height ) * 0.5f;
			CommandList.ImageStretch( X, Y, X + Width, Y + Height, Handle );
			break;
		}

		case EUIImageMode::Fill:
		{
			// 矩形を覆う拡大率にして、はみ出す分は、元の画像の側で切り落とす（クリップ命令を使わない）
			const float32 Scale = NMath::Max( Rect.Size.x / static_cast<float32>( ImageWidth ), Rect.Size.y / static_cast<float32>( ImageHeight ) );
			const float32 VisibleWidth = NMath::Min( static_cast<float32>( ImageWidth ), Rect.Size.x / Scale );
			const float32 VisibleHeight = NMath::Min( static_cast<float32>( ImageHeight ), Rect.Size.y / Scale );
			const int32 SrcW = NMath::Max( 1, RoundToInt( VisibleWidth ) );
			const int32 SrcH = NMath::Max( 1, RoundToInt( VisibleHeight ) );
			const int32 SrcX = NMath::Clamp( ( ImageWidth - SrcW ) / 2, 0, ImageWidth - SrcW );
			const int32 SrcY = NMath::Clamp( ( ImageHeight - SrcH ) / 2, 0, ImageHeight - SrcH );
			CommandList.ImagePartStretch( Rect.Left(), Rect.Top(), Rect.Right(), Rect.Bottom(), SrcX, SrcY, SrcW, SrcH, Handle );
			break;
		}

		case EUIImageMode::Tile:
			CommandList.Tile( RoundToInt( Rect.Left() ), RoundToInt( Rect.Top() ), RoundToInt( Rect.Right() ), RoundToInt( Rect.Bottom() ), Handle );
			break;

		case EUIImageMode::NineSlice:
			DrawNineSlice( CommandList, Brush, Rect, Handle );
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// FUIBrush
//-----------------------------------------------------------------------------
bool FUIBrush::IsEmpty() const
{
	switch ( Kind )
	{
	case EUIBrushKind::Solid:
	{
		const bool bFill = Color.a > 0;
		const bool bOutline = OutlineThickness > 0.0f && OutlineColor.a > 0;
		return !bFill && !bOutline;
	}
	case EUIBrushKind::Image:
		return !Image || Color.a == 0;
	case EUIBrushKind::Callback:
		return !Paint;
	case EUIBrushKind::Material:
		return !Material || Color.a == 0;
	case EUIBrushKind::Layered:
	{
		for ( int32 Index = 0; Index < Layers.Num(); ++Index )
		{
			if ( Layers[Index] && !Layers[Index]->IsEmpty() )
			{
				return false;
			}
		}
		return true;
	}
	case EUIBrushKind::None:
	default:
		return true;
	}
}

FUIBrush FUIBrush::MakeNone()
{
	return FUIBrush();
}

FUIBrush FUIBrush::MakeSolid( const FColor32& InColor, float32 InCornerRadius )
{
	FUIBrush Brush;
	Brush.Kind = EUIBrushKind::Solid;
	Brush.Color = InColor;
	Brush.CornerRadius = NMath::Max( 0.0f, InCornerRadius );
	return Brush;
}

FUIBrush FUIBrush::MakeSolidWithOutline( const FColor32& InColor, float32 InCornerRadius, const FColor32& InOutlineColor, float32 InOutlineThickness )
{
	FUIBrush Brush = MakeSolid( InColor, InCornerRadius );
	Brush.OutlineColor = InOutlineColor;
	Brush.OutlineThickness = NMath::Max( 0.0f, InOutlineThickness );
	return Brush;
}

FUIBrush FUIBrush::MakeImage( const TSharedPtr<IImage>& InImage, const FVector2& InImageSize, EUIImageMode InMode, const FColor32& InTint )
{
	FUIBrush Brush;
	Brush.Kind = EUIBrushKind::Image;
	Brush.Image = InImage;
	Brush.ImageSize = InImageSize;
	Brush.ImageMode = InMode;
	Brush.Color = InTint;
	return Brush;
}

FUIBrush FUIBrush::MakeNineSlice( const TSharedPtr<IImage>& InImage, const FVector2& InImageSize, const FUIMargin& InMargin, const FColor32& InTint )
{
	FUIBrush Brush = MakeImage( InImage, InImageSize, EUIImageMode::NineSlice, InTint );
	Brush.NineSliceMargin = InMargin;
	return Brush;
}

FUIBrush FUIBrush::MakeCallback( const FPaintFunction& InPaint )
{
	FUIBrush Brush;
	Brush.Kind = EUIBrushKind::Callback;
	Brush.Paint = InPaint;
	return Brush;
}

FUIBrush FUIBrush::MakeLayered( const TArray<FUIBrush>& InLayers )
{
	FUIBrush Brush;
	Brush.Kind = EUIBrushKind::Layered;
	for ( int32 Index = 0; Index < InLayers.Num(); ++Index )
	{
		Brush.Layers.Add( MakeShared<FUIBrush>( InLayers[Index] ) );
	}
	return Brush;
}

//-----------------------------------------------------------------------------
// NUIBrush
//-----------------------------------------------------------------------------
int32 NUIBrush::ToAlpha( const FColor32& Color, float32 Opacity )
{
	const float32 Clamped = NMath::Clamp( Opacity, 0.0f, 1.0f );
	return NMath::Clamp( RoundToInt( static_cast<float32>( Color.a ) * Clamped ), 0, 255 );
}

void NUIBrush::Draw( FDrawContext& CommandList, const FUIBrush& Brush, const FUIRect& Rect, float32 Opacity )
{
	if ( Opacity <= 0.0f || Rect.Size.x <= 0.0f || Rect.Size.y <= 0.0f )
	{
		return;
	}

	switch ( Brush.Kind )
	{
	case EUIBrushKind::Solid:
		DrawSolid( CommandList, Brush, Rect, Opacity );
		break;
	case EUIBrushKind::Image:
		DrawImage( CommandList, Brush, Rect, Opacity );
		break;
	case EUIBrushKind::Callback:
		if ( Brush.Paint )
		{
			Brush.Paint( CommandList, Rect, NMath::Clamp( Opacity, 0.0f, 1.0f ) );
		}
		break;
	case EUIBrushKind::Material:
		if ( Brush.Material )
		{
			// 時間は、マテリアルのサブシステムから（無ければ 0）。部品の状態は、FUIMaterialStateScope から
			FMaterialFrameState Frame;
			if ( const CMaterialSubsystem* Materials = GetAppSubsystem<CMaterialSubsystem>() )
			{
				Frame = Materials->MakeFrameState();
			}
			Frame.Hover = CurrentMaterialState.Hover;
			Frame.Press = CurrentMaterialState.Press;
			Frame.Value = CurrentMaterialState.Value;
			Frame.Enabled = CurrentMaterialState.Enabled;
			CommandList.Blend( EDrawBlend::Alpha, 255 );
			Brush.Material->DrawRect( CommandList, Rect.Left(), Rect.Top(), Rect.Size.x, Rect.Size.y, FColor32( Brush.Color.r, Brush.Color.g, Brush.Color.b, static_cast<uint8>( ToAlpha( Brush.Color, Opacity ) ) ), Frame );
		}
		break;
	case EUIBrushKind::Layered:
		for ( int32 Index = 0; Index < Brush.Layers.Num(); ++Index )
		{
			if ( Brush.Layers[Index] )
			{
				Draw( CommandList, *Brush.Layers[Index], Rect, Opacity );
			}
		}
		break;
	case EUIBrushKind::None:
	default:
		break;
	}
}
