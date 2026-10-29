#include "Font.h"

#include <cstdarg>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Framework/Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC( LogFont, Log )

CFont::CFont( const FString& InKey, int32 InHandle, const FFontDesc& InDesc, const FString& InFontFilePath )
	: CAsset( EAssetType::Font, InKey )
	, Handle( InHandle )
	, Desc( InDesc )
	, FontFilePath( InFontFilePath )
{
}

CFont::~CFont()
{
	NAssetService::ReleaseFont( Handle );
	if ( !FontFilePath.IsEmpty() ) NAssetService::RemoveFontFile( FontFilePath );
}

FString CFont::Describe() const
{
	return FString::Printf( "%s %dpx%s", Desc.Face.IsEmpty() ? "(default)" : *Desc.Face, Desc.Size, Desc.EdgeSize > 0 ? " edge" : "" );
}

int64 CFont::GetMemoryBytes() const
{
	const int64 Side = static_cast<int64>( Desc.Size ) + static_cast<int64>( Desc.EdgeSize ) * 2;
	return Side * Side * 4 * 256;
}

int32 CFont::MeasureWidth( const char* Text ) const
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr ? Backend->MeasureText( Handle, Text ) : 0;
}

void CFont::Record( FDrawContext& Draw, int32 X, int32 Y, const char* Text, const FColor32& Color, EDrawTextAlign Align, const FColor32& EdgeColor ) const
{
	Draw.TextWithFont( Handle, X, Y, Text, Color, Align, EdgeColor );
}

void CFont::Draw( int32 X, int32 Y, const char* Text, const FColor32& Color, EDrawTextAlign Align, const FColor32& EdgeColor ) const
{
	IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
	if ( Executor == nullptr ) return;

	// 記録して、すぐ実行する（使い回す入れ物。メインスレッドだけで使う）
	static FDrawBuffer Buffer;
	Buffer.Clear();
	FDrawContext Context( Buffer );
	Record( Context, X, Y, Text, Color, Align, EdgeColor );
	Executor->Execute( FDrawSpan { Buffer.Words.GetData(), Buffer.Words.Num() } );
}

namespace
{
	// バックエンドの文字の測定を、折り返しの計算（ITextMeasurer）に渡す
	class FFontMeasurer final : public ITextMeasurer
	{
	public:
		explicit FFontMeasurer( int32 InHandle )
			: Handle( InHandle )
		{
		}

		bool GetGlyphs( const char* Text, int32 /*Length*/, TArray<FTextGlyph>& OutGlyphs ) const override
		{
			IAssetBackend* Backend = NAssetService::GetBackend();
			return Backend != nullptr && Backend->GetTextGlyphs( Handle, Text, OutGlyphs );
		}

	private:
		int32 Handle;
	};

	// 記録して、すぐ実行する（使い回す入れ物。メインスレッドだけで使う）
	template<typename TRecord>
	void RecordAndExecute( TRecord&& Record )
	{
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
		if ( Executor == nullptr )
		{
			return;
		}
		static FDrawBuffer Buffer;
		Buffer.Clear();
		FDrawContext Context( Buffer );
		Record( Context );
		Executor->Execute( FDrawSpan { Buffer.Words.GetData(), Buffer.Words.Num() } );
	}
}

FTextSize CFont::MeasureSize( const char* Text ) const
{
	FTextSize Size;
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || Text == nullptr || !Backend->MeasureTextSize( Handle, Text, Size ) )
	{
		return FTextSize();
	}
	return Size;
}

bool CFont::GetGlyphs( const char* Text, TArray<FTextGlyph>& OutGlyphs ) const
{
	OutGlyphs.Reset();
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && Text != nullptr && Backend->GetTextGlyphs( Handle, Text, OutGlyphs );
}

FTextLayout CFont::Layout( const char* Text, const FTextLayoutOptions& Options ) const
{
	return NTextLayout::Build( Text, Options, FFontMeasurer( Handle ) );
}

void CFont::RecordLayout( FDrawContext& Draw, const FTextLayout& Layout, int32 X, int32 Y, int32 BoxWidth, const FColor32& Color, EDrawTextAlign Align, const FColor32& EdgeColor ) const
{
	const int32 LineHeight = GetLineHeight();
	// 各行は、箱の左端・中央・右端を基準点にして描く（TextWithFont の位置揃え）
	int32 AnchorX = X;
	if ( Align == EDrawTextAlign::Center )
	{
		AnchorX = X + BoxWidth / 2;
	}
	else if ( Align == EDrawTextAlign::Right )
	{
		AnchorX = X + BoxWidth;
	}
	for ( int32 Index = 0; Index < Layout.Lines.Num(); ++Index )
	{
		const FTextLine& Line = Layout.Lines[Index];
		if ( Line.End <= Line.Begin )
		{
			continue;
		}
		const int64 LineY = static_cast<int64>( Y ) + static_cast<int64>( Index ) * LineHeight;
		const FString LineText = Layout.GetLineText( Index );
		Draw.TextWithFont( Handle, AnchorX, static_cast<int32>( NMath::Clamp<int64>( LineY, -10000000, 10000000 ) ), *LineText, Color, Align, EdgeColor );
	}
}

void CFont::RecordWrapped( FDrawContext& Draw, int32 X, int32 Y, int32 MaxWidth, const char* Text, const FColor32& Color, EDrawTextAlign Align, const FColor32& EdgeColor, int32 MaxLines ) const
{
	FTextLayoutOptions Options;
	Options.MaxWidth = MaxWidth;
	Options.MaxLines = MaxLines;
	RecordLayout( Draw, Layout( Text, Options ), X, Y, MaxWidth, Color, Align, EdgeColor );
}

void CFont::DrawWrapped( int32 X, int32 Y, int32 MaxWidth, const char* Text, const FColor32& Color, EDrawTextAlign Align, const FColor32& EdgeColor, int32 MaxLines ) const
{
	RecordAndExecute( [&]( FDrawContext& Context ) { RecordWrapped( Context, X, Y, MaxWidth, Text, Color, Align, EdgeColor, MaxLines ); } );
}

bool CFont::RecordStyled( FDrawContext& Draw, float32 X, float32 Y, const char* Text, const FColor32& Color, const FTextStyle& Style ) const
{
	return Draw.TextStyled( Handle, X, Y, Text, Color, Style );
}

void CFont::DrawStyled( float32 X, float32 Y, const char* Text, const FColor32& Color, const FTextStyle& Style ) const
{
	RecordAndExecute( [&]( FDrawContext& Context ) { RecordStyled( Context, X, Y, Text, Color, Style ); } );
}

bool CFont::AddCharImage( const char* Character, const IImage& Image, int32 Advance, int32 DrawX, int32 DrawY )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	const FImageHandle ImageHandle = Image.GetImageHandle();
	return Backend != nullptr && Character != nullptr && ImageHandle.IsValid() && Backend->AddFontCharImage( Handle, Character, ImageHandle.Value, DrawX, DrawY, Advance );
}

bool CFont::AddCharImage( const char* Character, const TSharedPtr<CTexture>& Image, int32 Advance, int32 DrawX, int32 DrawY )
{
	if ( !Image.IsValid() || !AddCharImage( Character, *Image, Advance, DrawX, DrawY ) )
	{
		return false;
	}
	OwnedCharImages.Add( FString( Character ), Image );
	return true;
}

void CFont::RemoveCharImage( const char* Character )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend != nullptr && Character != nullptr )
	{
		Backend->RemoveFontCharImage( Handle, Character );
		OwnedCharImages.Remove( FString( Character ) );
	}
}

void CFont::DrawF( int32 X, int32 Y, const FColor32& Color, const char* Format, ... ) const
{
	char Small[512];
	va_list Args;
	va_start( Args, Format );
	va_list ArgsCopy;
	va_copy( ArgsCopy, Args );
	const int Needed = NCString::FormatV( Small, sizeof( Small ), Format, Args );
	va_end( Args );

	if ( Needed >= 0 && Needed < static_cast<int>( sizeof( Small ) ) )
	{
		Draw( X, Y, Small, Color );
	}
	else if ( Needed >= 0 )
	{
		TArray<char> Large;
		Large.SetNum( Needed + 1 );
		NCString::FormatV( Large.GetData(), static_cast<size_t>( Large.Num() ), Format, ArgsCopy );
		Draw( X, Y, Large.GetData(), Color );
	}
	va_end( ArgsCopy );
}
