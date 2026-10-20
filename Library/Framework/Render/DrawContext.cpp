#include "DrawContext.h"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Library/Core/Collection/Array.h"

void FDrawContext::MaterialQuad3D( const FDrawMaterialQuadData& Data )
{
	if ( !NDrawMaterialQuad::IsValid( Data ) ) return;
	Push( EDrawCommand::MaterialQuad3D, 0, Data );
}

void FDrawContext::DistortedMaterialQuad3D( const FDrawDistortedMaterialQuadData& Data )
{
	if ( !NDrawDistortedMaterialQuad::IsValid( Data ) ) return;
	Push( EDrawCommand::DistortedMaterialQuad3D, 0, Data );
}

void FDrawContext::MaterialRibbon3D( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points )
{
	// 入力値が記録先の再確保で変わらないよう、固定設定を先に値として保持する。
	FDrawMaterialRibbonData Header{};
	std::memcpy( &Header, &Data, sizeof( Header ) );
	// 既存命令と同じ設定・点列条件を、記録前に一括で検査する。
	if ( !NDrawMaterialRibbon::IsValidHeader( Header ) || Points == nullptr ) return;
	TArray<FDrawMaterialRibbonData::FPoint> Copied;
	Copied.Append( Points, Header.Ribbon.NumPoints );
	if ( !NDrawMaterialRibbon::IsValid( Header, Copied.GetData() ) ) return;
	// 固定設定と全点を合わせた命令長。MaxPoints検査後なので積は安全な範囲。
	constexpr size_t DataWords = sizeof( FDrawMaterialRibbonData ) / 4;
	// 全点を4byte word単位で記録する長さ。
	const size_t PointWords = static_cast<size_t>( Header.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) / 4;
	// command headerを含む最終word数。
	const size_t CommandWords = 1 + DataWords + PointWords;
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.size() > Buffer->Words.max_size() - CommandWords ) return;
	// PointsやDataが既存bufferを指す場合も、resize前に所有コピーする。
	std::vector<uint32>& Words = Buffer->Words;
	// 追記する新命令の開始word。
	const size_t Begin = Words.size();
	Words.resize( Begin + CommandWords );
	Words[Begin] = NDrawCommand::MakeHeader( EDrawCommand::MaterialRibbon3D, 0, static_cast<int32>( CommandWords ) );
	std::memcpy( &Words[Begin + 1], &Header, sizeof( Header ) );
	std::memcpy( &Words[Begin + 1 + DataWords], Copied.GetData(), static_cast<size_t>( Header.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) );
}

void FDrawContext::DistortedMaterialRibbon3D( const FDrawDistortedMaterialRibbonData& Data, const FDrawDistortedMaterialRibbonData::FPoint* Points )
{
	// buffer再確保で入力headerが変わらないよう、固定設定を先に値で保持する。
	FDrawDistortedMaterialRibbonData Header{};
	std::memcpy( &Header, &Data, sizeof( Header ) );
	if ( Points == nullptr || !NDrawDistortedMaterialRibbon::IsValidHeader( Header ) ) return;
	// 入力配列を所有し、検証後に一括記録するための点列。
	TArray<FDrawDistortedMaterialRibbonData::FPoint> Copied;
	Copied.Append( Points, Header.Material.Ribbon.NumPoints );
	if ( !NDrawDistortedMaterialRibbon::IsValid( Header, Copied.GetData() ) ) return;
	// 固定headerと全点を含む描画命令のword数。
	constexpr size_t DataWords = sizeof( FDrawDistortedMaterialRibbonData ) / 4;
	// 1024点以下の可変payload word数。
	const size_t PointWords = static_cast<size_t>( Header.Material.Ribbon.NumPoints ) * sizeof( FDrawDistortedMaterialRibbonData::FPoint ) / 4;
	// command header込みの合計word数。
	const size_t CommandWords = 1 + DataWords + PointWords;
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.size() > Buffer->Words.max_size() - CommandWords ) return;
	// 追記する描画命令が入る共有word buffer。
	std::vector<uint32>& Words = Buffer->Words;
	// 新命令を記録する開始位置。
	const size_t Begin = Words.size();
	Words.resize( Begin + CommandWords );
	Words[Begin] = NDrawCommand::MakeHeader( EDrawCommand::DistortedMaterialRibbon3D, 0, static_cast<int32>( CommandWords ) );
	std::memcpy( &Words[Begin + 1], &Header, sizeof( Header ) );
	std::memcpy( &Words[Begin + 1 + DataWords], Copied.GetData(), static_cast<size_t>( Header.Material.Ribbon.NumPoints ) * sizeof( FDrawDistortedMaterialRibbonData::FPoint ) );
}

void FDrawContext::MaterialMesh3D( const FDrawMaterialMeshData& Data, const FMeshVertex* Vertices, const uint32* Indices )
{
	// buffer再確保で入力headerが変わらないよう、固定設定を先に値として保持する。
	FDrawMaterialMeshData Header{};
	std::memcpy( &Header, &Data, sizeof( Header ) );
	if ( Vertices == nullptr || Indices == nullptr || !NDrawMaterialMesh::IsValidHeader( Header ) ) return;
	// 追記前に頂点payloadを所有し、入力aliasを切り離す配列。
	TArray<FMeshVertex> CopiedVertices;
	CopiedVertices.Append( Vertices, Header.NumVertices );
	// 追記前にindex payloadを所有し、入力aliasを切り離す配列。
	TArray<uint32> CopiedIndices;
	CopiedIndices.Append( Indices, Header.NumIndices );
	if ( !NDrawMaterialMesh::IsValid( Header, CopiedVertices.GetData(), CopiedIndices.GetData() ) ) return;
	// 固定headerのword数。
	constexpr size_t DataWords = sizeof( FDrawMaterialMeshData ) / 4;
	// 頂点payloadのword数。
	const size_t VertexWords = static_cast<size_t>( Header.NumVertices ) * sizeof( FMeshVertex ) / 4;
	// index payloadのword数。
	const size_t IndexWords = static_cast<size_t>( Header.NumIndices ) * sizeof( uint32 ) / 4;
	// command headerを含めた全word数。
	const size_t CommandWords = 1 + DataWords + VertexWords + IndexWords;
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.size() > Buffer->Words.max_size() - CommandWords ) return;
	// 所有コピー済みデータを一度のresizeで追記するword列。
	std::vector<uint32>& Words = Buffer->Words;
	// 新しい命令を書き始める位置。
	const size_t Begin = Words.size();
	Words.resize( Begin + CommandWords );
	Words[Begin] = NDrawCommand::MakeHeader( EDrawCommand::MaterialMesh3D, 0, static_cast<int32>( CommandWords ) );
	std::memcpy( &Words[Begin + 1], &Header, sizeof( Header ) );
	std::memcpy( &Words[Begin + 1 + DataWords], CopiedVertices.GetData(), static_cast<size_t>( Header.NumVertices ) * sizeof( FMeshVertex ) );
	std::memcpy( &Words[Begin + 1 + DataWords + VertexWords], CopiedIndices.GetData(), static_cast<size_t>( Header.NumIndices ) * sizeof( uint32 ) );
}

void FDrawContext::Ribbon3D( const FDrawRibbonData::FPoint* Points, int32 NumPoints, int32 Texture, float32 UVRepeat, int32 JoinFlags )
{
	if ( Points == nullptr || NumPoints < 2 || NumPoints > FDrawRibbonData::MaxPoints || Texture < -1 || !std::isfinite( UVRepeat ) || UVRepeat <= 0.0f || JoinFlags < 0 || ( JoinFlags & ~( FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint ) ) != 0 || NumPoints - static_cast<int32>( ( JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) - static_cast<int32>( ( JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) < 2 ) return;
	// テクスチャを反復する際に必要な、分割後の区間数。
	int32 TextureSections = 0;
	// 点列の途中だけを落として、不連続な経路へ変えてしまわないよう全点を先に確かめる。
	for ( int32 Index = 0; Index < NumPoints; ++Index )
	{
		// 検査してから複写する、今回の経路の点。
		const FDrawRibbonData::FPoint& Point = Points[Index];
		if ( !std::isfinite( Point.Pos[0] ) || !std::isfinite( Point.Pos[1] ) || !std::isfinite( Point.Pos[2] ) || !std::isfinite( Point.Width ) || Point.Width < 0.0f || !std::isfinite( Point.U ) || Point.U < 0.0f || !std::isfinite( Point.U * UVRepeat ) ) return;
		if ( Index > 0 && Point.U < Points[Index - 1].U ) return;
		// 幅と接線には全点を使い、補助点が作る区間だけを除外する。
		const bool bVisibleSegment = Index > 0 && !( Index == 1 && ( JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) && !( Index + 1 == NumPoints && ( JoinFlags & FDrawRibbonData::NextPoint ) != 0 );
		if ( Texture >= 0 && bVisibleSegment )
		{
			// 始点側で使い始める画像の反復番号。
			const float64 First = std::floor( static_cast<float64>( Points[Index - 1].U * UVRepeat ) );
			// 終点までに必要な反復数。整数境界と一致する終点では余分な区間を作らない。
			const float64 Sections = std::max( 1.0, std::ceil( static_cast<float64>( Point.U * UVRepeat ) ) - First );
			if ( Sections > static_cast<float64>( FDrawRibbonData::MaxTextureSections - TextureSections ) ) return;
			TextureSections += static_cast<int32>( Sections );
		}
	}
	// 命令の固定部分のワード数。
	constexpr size_t DataWords = sizeof( FDrawRibbonData ) / 4;
	// 点列の可変部分のワード数。点数の上限を検査した後に計算する。
	const size_t PointWords = static_cast<size_t>( NumPoints ) * sizeof( FDrawRibbonData::FPoint ) / 4;
	// 命令ヘッダー自身を含む全体のワード数。
	const size_t CommandWords = 1 + DataWords + PointWords;
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.size() > Buffer->Words.max_size() - CommandWords ) return;
	// Pointsが既存の記録領域を参照する場合も、resizeで失効する前に複写する。
	TArray<FDrawRibbonData::FPoint> Copied;
	Copied.Append( Points, NumPoints );
	// 点列に先行する、固定長の描画設定。
	FDrawRibbonData Data;
	Data.Texture = Texture;
	Data.NumPoints = NumPoints;
	Data.UVRepeat = UVRepeat;
	Data.JoinFlags = JoinFlags;
	// 既存の描画命令が入っている、今回の追記先。
	std::vector<uint32>& Words = Buffer->Words;
	// 追加する命令の先頭。
	const size_t Begin = Words.size();
	Words.resize( Begin + CommandWords );
	Words[Begin] = NDrawCommand::MakeHeader( EDrawCommand::Ribbon3D, 0, static_cast<int32>( CommandWords ) );
	std::memcpy( &Words[Begin + 1], &Data, sizeof( Data ) );
	std::memcpy( &Words[Begin + 1 + DataWords], Copied.GetData(), static_cast<size_t>( NumPoints ) * sizeof( FDrawRibbonData::FPoint ) );
}

void FDrawContext::Text( int32 X, int32 Y, const char* String, const FColor32& Color )
{
	constexpr size_t DataWords = sizeof( FDrawTextData ) / 4;
	// ヘッダに収まる大きさまで（それより長い文字列は切る）
	const size_t MaxChars = ( static_cast<size_t>( NDrawCommand::MaxWords ) - 1 - DataWords ) * 4 - 1;
	size_t Length = String != nullptr ? std::strlen( String ) : 0;
	if ( Length > MaxChars ) Length = MaxChars;
	const size_t CharWords = ( Length + 1 + 3 ) / 4;   // '\0' を含めて、4 バイト境界まで

	FDrawTextData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Color = Rgb( Color );
	Data.Length = static_cast<int32>( Length );

	std::vector<uint32>& Words = Buffer->Words;
	const size_t Begin = Words.size();
	Words.resize( Begin + 1 + DataWords + CharWords );   // 0 で埋まる（終端の '\0' と、境界までの隙間）
	Words[Begin] = NDrawCommand::MakeHeader( EDrawCommand::Text, 0, static_cast<int32>( 1 + DataWords + CharWords ) );
	std::memcpy( &Words[Begin + 1], &Data, sizeof( Data ) );
	if ( Length > 0 ) std::memcpy( &Words[Begin + 1 + DataWords], String, Length );
}

void FDrawContext::TextWithFont( int32 FontHandle, int32 X, int32 Y, const char* String, const FColor32& Color, EDrawTextAlign Align, const FColor32& EdgeColor )
{
	constexpr size_t DataWords = sizeof( FDrawTextFontData ) / 4;
	const size_t MaxChars = ( static_cast<size_t>( NDrawCommand::MaxWords ) - 1 - DataWords ) * 4 - 1;
	size_t Length = String != nullptr ? std::strlen( String ) : 0;
	if ( Length > MaxChars ) Length = MaxChars;
	const size_t CharWords = ( Length + 1 + 3 ) / 4;

	FDrawTextFontData Data;
	Data.X = X;
	Data.Y = Y;
	Data.Color = Rgb( Color );
	Data.Length = static_cast<int32>( Length );
	Data.Font = FontHandle;
	Data.EdgeColor = Rgb( EdgeColor );

	uint8 Flags = 0;
	if ( Align == EDrawTextAlign::Center ) Flags = FDrawFlag::AlignCenter;
	else if ( Align == EDrawTextAlign::Right ) Flags = FDrawFlag::AlignRight;

	std::vector<uint32>& Words = Buffer->Words;
	const size_t Begin = Words.size();
	Words.resize( Begin + 1 + DataWords + CharWords );
	Words[Begin] = NDrawCommand::MakeHeader( EDrawCommand::TextFont, Flags, static_cast<int32>( 1 + DataWords + CharWords ) );
	std::memcpy( &Words[Begin + 1], &Data, sizeof( Data ) );
	if ( Length > 0 ) std::memcpy( &Words[Begin + 1 + DataWords], String, Length );
}

void FDrawContext::TextF( int32 X, int32 Y, const FColor32& Color, const char* Format, ... )
{
	char Small[256];

	va_list Args;
	va_start( Args, Format );
	va_list ArgsCopy;
	va_copy( ArgsCopy, Args );
	const int Needed = std::vsnprintf( Small, sizeof( Small ), Format, Args );
	va_end( Args );

	if ( Needed < 0 )
	{
		Text( X, Y, "", Color );
	}
	else if ( Needed < static_cast<int>( sizeof( Small ) ) )
	{
		Text( X, Y, Small, Color );
	}
	else
	{
		std::vector<char> Large( static_cast<size_t>( Needed ) + 1 );
		std::vsnprintf( Large.data(), Large.size(), Format, ArgsCopy );
		Text( X, Y, Large.data(), Color );
	}
	va_end( ArgsCopy );
}
