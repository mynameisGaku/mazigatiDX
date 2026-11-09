// SPDX-License-Identifier: MIT
#include "WicImageDecoder.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment( lib, "windowscodecs.lib" )
#pragma comment( lib, "ole32.lib" )

bool FWicImageDecoder::Decode( const void* FileImage, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels )
{
	if ( !FileImage || Size == 0 || Size > 256 * 1024 * 1024 )
	{
		return false;
	}
	const HRESULT Initialized = CoInitializeEx( nullptr, COINIT_MULTITHREADED ); // この呼び出しだけで必要なCOM接続。
	if ( FAILED( Initialized ) && Initialized != RPC_E_CHANGED_MODE )
	{
		return false;
	}
	bool bSuccess = false; // 出力を書き換える前の結果。
	{
		using Microsoft::WRL::ComPtr;
		ComPtr<IWICImagingFactory> Factory; // OSの画像読み込み。
		ComPtr<IWICStream> Stream; // 借用するファイル内容。
		ComPtr<IWICBitmapDecoder> Decoder; // 形式ごとの展開器。
		ComPtr<IWICBitmapFrameDecode> Frame; // 最初の画像。
		ComPtr<IWICFormatConverter> Converter; // RGBA8への変換。
		UINT Width = 0, Height = 0; // OSが返した大きさ。
		if ( SUCCEEDED( CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &Factory ) ) )
			&& SUCCEEDED( Factory->CreateStream( &Stream ) )
			&& SUCCEEDED( Stream->InitializeFromMemory( static_cast<BYTE*>( const_cast<void*>( FileImage ) ), static_cast<DWORD>( Size ) ) )
			&& SUCCEEDED( Factory->CreateDecoderFromStream( Stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &Decoder ) )
			&& SUCCEEDED( Decoder->GetFrame( 0, &Frame ) ) && SUCCEEDED( Frame->GetSize( &Width, &Height ) )
			&& Width > 0 && Height > 0 && Width <= 16384 && Height <= 16384 && static_cast<uint64>( Width ) * Height <= 64 * 1024 * 1024
			&& SUCCEEDED( Factory->CreateFormatConverter( &Converter ) )
			&& SUCCEEDED( Converter->Initialize( Frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom ) ) )
		{
			TArray<FColor32> Pixels; // 成功するまで出力から分離する。
			Pixels.SetNum( static_cast<int32>( Width * Height ) );
			if ( SUCCEEDED( Converter->CopyPixels( nullptr, Width * 4, Width * Height * 4, reinterpret_cast<BYTE*>( Pixels.GetData() ) ) ) )
			{
				OutWidth = static_cast<int32>( Width );
				OutHeight = static_cast<int32>( Height );
				OutPixels = MoveTemp( Pixels );
				bSuccess = true;
			}
		}
	}
	if ( SUCCEEDED( Initialized ) )
	{
		CoUninitialize();
	}
	return bSuccess;
}
