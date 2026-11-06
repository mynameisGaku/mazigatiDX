// SPDX-License-Identifier: MIT
#include "DxLibDrawExecutorSubsystem.h"

#include "Library/Platform/DxLib/DxLibInclude.h"

//=============================================================================
// CDxLibDrawExecutorSubsystem のうち、撮影のための描き直し（高解像度のスクリーンショット・被写体だけの撮影）
//   大きな描画先（MakeScreen）へ替え、2D の変換に撮影の倍率を入れておく（Execute の中の Apply2DTransform が、命令の変換と合わせる）。
//   3D は、描画先を替えたときに DxLib がカメラを描画先の大きさに合わせるので、そのまま細かくなる。
//=============================================================================
namespace
{
	constexpr int32 MaxCaptureSize = 16384;
}

bool CDxLibDrawExecutorSubsystem::BeginOffscreenCapture( const FOffscreenCaptureDesc& Desc )
{
	if ( bOffscreenCapture || Desc.Width < 1 || Desc.Height < 1 || Desc.Width > MaxCaptureSize || Desc.Height > MaxCaptureSize || !( Desc.Scale > 0.0f ) )
	{
		return false;
	}
	// 溜まっている描画を、いまの描画先へ描き切ってから替える
	RenderVertex();
	const int Screen = MakeScreen( Desc.Width, Desc.Height, TRUE );
	if ( Screen < 0 )
	{
		return false;
	}
	CapturePreviousScreen = GetDrawScreen();
	RECT PreviousArea = {};
	GetDrawArea( &PreviousArea );
	CapturePreviousArea[0] = PreviousArea.left;
	CapturePreviousArea[1] = PreviousArea.top;
	CapturePreviousArea[2] = PreviousArea.right;
	CapturePreviousArea[3] = PreviousArea.bottom;
	if ( SetDrawScreen( Screen ) != 0 )
	{
		DeleteGraph( Screen );
		return false;
	}
	CaptureScreen = Screen;
	CaptureWidth = Desc.Width;
	CaptureHeight = Desc.Height;
	CaptureScale = Desc.Scale;
	bOffscreenCapture = true;
	SetDrawAreaFull();
	// 塗る色で埋める（透明度も 255）
	SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 255 );
	DrawBox( 0, 0, Desc.Width, Desc.Height, GetColor( Desc.ClearColor.r, Desc.ClearColor.g, Desc.ClearColor.b ), TRUE );
	if ( CaptureScale != 1.0f )
	{
		const MATRIX Scale = MGetScale( VGet( CaptureScale, CaptureScale, 1.0f ) );
		SetTransformTo2D( &Scale );
	}
	return true;
}

bool CDxLibDrawExecutorSubsystem::EndOffscreenCapture( TArray<FColor32>& Out )
{
	if ( !bOffscreenCapture )
	{
		return false;
	}
	RenderVertex();
	bool bRead = false;
	const int Soft = MakeARGB8ColorSoftImage( CaptureWidth, CaptureHeight );
	if ( Soft >= 0 && GetDrawScreenSoftImage( 0, 0, CaptureWidth, CaptureHeight, Soft ) == 0 )
	{
		const uint8* Bytes = static_cast<const uint8*>( GetImageAddressSoftImage( Soft ) );
		const int32 Pitch = GetPitchSoftImage( Soft );
		if ( Bytes != nullptr && Pitch >= CaptureWidth * 4 )
		{
			// ARGB8 の CPU 側の画像は、1 画素 4 バイト（B, G, R, A の順）
			Out.SetNum( CaptureWidth * CaptureHeight );
			for ( int32 Y = 0; Y < CaptureHeight; ++Y )
			{
				const uint8* Row = Bytes + static_cast<usize>( Y ) * static_cast<usize>( Pitch );
				for ( int32 X = 0; X < CaptureWidth; ++X )
				{
					Out[Y * CaptureWidth + X] = FColor32( Row[X * 4 + 2], Row[X * 4 + 1], Row[X * 4 + 0], Row[X * 4 + 3] );
				}
			}
			bRead = true;
		}
	}
	if ( Soft >= 0 )
	{
		DeleteSoftImage( Soft );
	}
	// 元の描画先へ戻す（変換・描画範囲も）
	ResetTransformTo2D();
	SetDrawScreen( CapturePreviousScreen );
	SetDrawArea( CapturePreviousArea[0], CapturePreviousArea[1], CapturePreviousArea[2], CapturePreviousArea[3] );
	DeleteGraph( CaptureScreen );
	CaptureScreen = -1;
	CaptureScale = 1.0f;
	bOffscreenCapture = false;
	return bRead;
}
