// SPDX-License-Identifier: MIT
#include "WicImageEncoder.h"
#include "Library/Framework/Render/Types/RenderTargetTypes.h"
#include "Library/Core/File/File.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment( lib, "windowscodecs.lib" )
#pragma comment( lib, "ole32.lib" )

bool FWicImageEncoder::Save( int32 Width, int32 Height, const FColor32* Pixels, const FString& Path, const FImageSaveOptions& Options )
{
	if ( !Pixels || Path.IsEmpty() || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || static_cast<uint64>( Width ) * Height * 4 > 256ull * 1024 * 1024 || ( Options.Format != EImageFileFormat::Png && Options.Format != EImageFileFormat::Bmp && Options.Format != EImageFileFormat::Jpeg ) || ( Options.Format == EImageFileFormat::Png && Options.PngCompression != -1 ) )
	{
		return false;
	}
	const HRESULT Initialized = CoInitializeEx( nullptr, COINIT_MULTITHREADED ); // 既存のCOM接続方式もそのまま使える。
	if ( FAILED( Initialized ) && Initialized != RPC_E_CHANGED_MODE )
	{
		return false;
	}
	bool bSuccess = false; // 画像全体を符号化してからファイルへ書く。
	{
		using Microsoft::WRL::ComPtr;
		ComPtr<IWICImagingFactory> Factory; // OSに同梱された画像形式を使う。
		ComPtr<IWICBitmap> Source; // 呼出中だけ借用するRGBA画素の画像。
		ComPtr<IWICFormatConverter> Converter; // 形式ごとの格納順と透明度へ変換する。
		ComPtr<IWICBitmapEncoder> Encoder; // 保存形式の符号化器。
		ComPtr<IWICBitmapFrameEncode> Frame; // 一枚だけの保存先。
		ComPtr<IPropertyBag2> Properties; // JPEGの品質を渡す場所。
		ComPtr<IStream> Stream; // 符号化の途中では保存先を変更しない。
		const GUID& Container = Options.Format == EImageFileFormat::Png ? GUID_ContainerFormatPng : Options.Format == EImageFileFormat::Bmp ? GUID_ContainerFormatBmp : GUID_ContainerFormatJpeg; // 明示した形式を使う。
		WICPixelFormatGUID Format = Options.Format == EImageFileFormat::Png ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR; // PNG以外は透明度を持たない。
		bool bReady = SUCCEEDED( CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &Factory ) ) ) && SUCCEEDED( CreateStreamOnHGlobal( nullptr, TRUE, &Stream ) ) && SUCCEEDED( Factory->CreateEncoder( Container, nullptr, &Encoder ) ) && SUCCEEDED( Encoder->Initialize( Stream.Get(), WICBitmapEncoderNoCache ) ) && SUCCEEDED( Encoder->CreateNewFrame( &Frame, &Properties ) ); // 準備の失敗ではファイルを作らない。
		if ( bReady && Options.Format == EImageFileFormat::Jpeg )
		{
			PROPBAG2 Property = {}; // JPEGの可逆性とは別の品質値。
			Property.pstrName = const_cast<wchar_t*>( L"ImageQuality" );
			VARIANT Value = {}; // 文字列を所有しない数値だけを渡す。
			Value.vt = VT_R4;
			Value.fltVal = NMath::Clamp( Options.JpegQuality, 0, 100 ) / 100.0f;
			bReady = Properties && SUCCEEDED( Properties->Write( 1, &Property, &Value ) );
		}
		if ( bReady && SUCCEEDED( Frame->Initialize( Properties.Get() ) ) && SUCCEEDED( Frame->SetSize( Width, Height ) ) && SUCCEEDED( Frame->SetPixelFormat( &Format ) ) && SUCCEEDED( Factory->CreateBitmapFromMemory( Width, Height, GUID_WICPixelFormat32bppRGBA, Width * 4, Width * Height * 4, reinterpret_cast<BYTE*>( const_cast<FColor32*>( Pixels ) ), &Source ) ) && SUCCEEDED( Factory->CreateFormatConverter( &Converter ) ) && SUCCEEDED( Converter->Initialize( Source.Get(), Format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom ) ) && SUCCEEDED( Frame->WriteSource( Converter.Get(), nullptr ) ) && SUCCEEDED( Frame->Commit() ) && SUCCEEDED( Encoder->Commit() ) )
		{
			HGLOBAL Memory = nullptr; // 完成したファイルのバイト列を借用する。
			STATSTG Info = {}; // 実際に書かれた長さだけを保存する。
			if ( SUCCEEDED( GetHGlobalFromStream( Stream.Get(), &Memory ) ) && SUCCEEDED( Stream->Stat( &Info, STATFLAG_NONAME ) ) && Info.cbSize.QuadPart > 0 && Info.cbSize.QuadPart <= 512ull * 1024 * 1024 )
			{
				const void* Bytes = GlobalLock( Memory ); // Streamが解放する前にファイルへ所有コピーする。
				if ( Bytes )
				{
					bSuccess = NFile::WriteAllBytes( Path, Bytes, static_cast<usize>( Info.cbSize.QuadPart ) );
					GlobalUnlock( Memory );
				}
			}
		}
	}
	if ( SUCCEEDED( Initialized ) )
	{
		CoUninitialize();
	}
	return bSuccess;
}
