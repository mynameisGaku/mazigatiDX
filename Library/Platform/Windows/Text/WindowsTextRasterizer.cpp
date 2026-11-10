// SPDX-License-Identifier: MIT
#include "WindowsTextRasterizer.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "user32.lib" )

FWindowsTextRasterizer::~FWindowsTextRasterizer()
{
	if ( Context && PreviousFont )
	{
		SelectObject( Context, PreviousFont );
	}
	if ( Font )
	{
		DeleteObject( Font );
	}
	if ( Context )
	{
		DeleteDC( Context );
	}
}

bool FWindowsTextRasterizer::Initialize()
{
	if ( Context )
	{
		return Font != nullptr;
	}
	HDC CreatedContext = CreateCompatibleDC( nullptr ); // 完成するまで所有先へ渡さない文脈。
	HFONT CreatedFont = CreateFontW( -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas" ); // ゲーム用の既定文字。
	if ( !CreatedContext || !CreatedFont )
	{
		if ( CreatedFont )
		{
			DeleteObject( CreatedFont );
		}
		if ( CreatedContext )
		{
			DeleteDC( CreatedContext );
		}
		return false;
	}
	HGDIOBJ Previous = SelectObject( CreatedContext, CreatedFont ); // 完成後の破棄で戻す元のフォント。
	if ( !Previous || Previous == HGDI_ERROR )
	{
		DeleteObject( CreatedFont );
		DeleteDC( CreatedContext );
		return false;
	}
	Context = CreatedContext;
	Font = CreatedFont;
	PreviousFont = Previous;
	return true;
}

bool FWindowsTextRasterizer::RasterizeText( TArrayView<const char> Text, TArray<FColor32> &OutPixels, int32 &OutWidth, int32 &OutHeight )
{
	if ( !Context || !Font || !Text.GetData() || Text.Num() <= 0 || Text.Num() > 65536 )
	{
		return false;
	}
	const int32 Count = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, Text.GetData(), Text.Num(), nullptr, 0 ); // 指定範囲のUTF-8だけを変換する。
	if ( Count <= 0 )
	{
		return false;
	}
	TArray<wchar_t> Wide; // OS呼出中だけ保持する文字列。
	Wide.SetNum( Count );
	if ( MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, Text.GetData(), Text.Num(), Wide.GetData(), Count ) != Count )
	{
		return false;
	}
	RECT Bounds = { 0, 0, 0, 0 }; // 改行を含む文字列全体の寸法。
	const UINT Flags = DT_NOPREFIX | DT_EXPANDTABS | DT_LEFT | DT_TOP; // &を文字として扱い、改行は保持する。
	if ( DrawTextW( Context, Wide.GetData(), Count, &Bounds, Flags | DT_CALCRECT ) <= 0 || Bounds.right < 0 || Bounds.bottom <= 0 || Bounds.right > 4096 || Bounds.bottom > 4096 || static_cast<int64>( Bounds.right ) * Bounds.bottom > 4 * 1024 * 1024 )
	{
		return false;
	}
	Bounds.right = NMath::Max( 1L, Bounds.right ); // 改行だけの有効な文字列も、透明な一列として保持する。
	BITMAPINFO Info = {}; // 左上から行順の32bit画像。
	Info.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
	Info.bmiHeader.biWidth = Bounds.right;
	Info.bmiHeader.biHeight = -Bounds.bottom;
	Info.bmiHeader.biPlanes = 1;
	Info.bmiHeader.biBitCount = 32;
	Info.bmiHeader.biCompression = BI_RGB;
	void *Bytes = nullptr; // OSが所有し、この呼出中だけ読み出す画像。
	HBITMAP Bitmap = CreateDIBSection( Context, &Info, DIB_RGB_COLORS, &Bytes, nullptr, 0 ); // GPUを介さない文字の描画先。
	if ( !Bitmap || !Bytes )
	{
		if ( Bitmap )
		{
			DeleteObject( Bitmap );
		}
		return false;
	}
	HGDIOBJ PreviousBitmap = SelectObject( Context, Bitmap ); // 読み出し後に必ず元へ戻す。
	if ( !PreviousBitmap || PreviousBitmap == HGDI_ERROR )
	{
		DeleteObject( Bitmap );
		return false;
	}
	FMemory::Memzero( Bytes, static_cast<usize>( Bounds.right ) * Bounds.bottom * 4 );
	SetTextColor( Context, RGB( 255, 255, 255 ) );
	SetBkMode( Context, TRANSPARENT );
	const bool bDrawn = DrawTextW( Context, Wide.GetData(), Count, &Bounds, Flags ) > 0 && GdiFlush(); // CPUで画素を読む前にOS側の描画を完了させる。
	TArray<FColor32> Pixels; // 成功した画像だけを呼出側へ渡す。
	if ( bDrawn )
	{
		Pixels.SetNum( Bounds.right * Bounds.bottom );
		const auto *Source = static_cast<const uint8 *>( Bytes ); // OSのBGRを白と被覆率へ変換する。
		for ( int32 Index = 0; Index < Pixels.Num(); ++Index )
		{
			const uint8 Coverage = NMath::Max( Source[Index * 4], Source[Index * 4 + 1], Source[Index * 4 + 2] ); // 色のにじみをゲームの色へ混ぜない。
			Pixels[Index] = FColor32( 255, 255, 255, Coverage );
		}
	}
	SelectObject( Context, PreviousBitmap );
	DeleteObject( Bitmap );
	if ( !bDrawn )
	{
		return false;
	}
	OutPixels = MoveTemp( Pixels );
	OutWidth = Bounds.right;
	OutHeight = Bounds.bottom;
	return true;
}
