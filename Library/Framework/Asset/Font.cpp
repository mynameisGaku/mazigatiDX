#include "Font.h"

#include <cstdarg>
#include <cstdio>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Log/Log.h"
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

void CFont::DrawF( int32 X, int32 Y, const FColor32& Color, const char* Format, ... ) const
{
	char Small[512];
	va_list Args;
	va_start( Args, Format );
	va_list ArgsCopy;
	va_copy( ArgsCopy, Args );
	const int Needed = std::vsnprintf( Small, sizeof( Small ), Format, Args );
	va_end( Args );

	if ( Needed >= 0 && Needed < static_cast<int>( sizeof( Small ) ) )
	{
		Draw( X, Y, Small, Color );
	}
	else if ( Needed >= 0 )
	{
		TArray<char> Large;
		Large.SetNum( Needed + 1 );
		std::vsnprintf( Large.GetData(), static_cast<size_t>( Large.Num() ), Format, ArgsCopy );
		Draw( X, Y, Large.GetData(), Color );
	}
	va_end( ArgsCopy );
}
