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
	// 検証と記録で共通して使う、所有済み経路設定と材質設定。
	const FDrawRibbonData& Ribbon = Header.Ribbon;
	const FDrawMaterialRibbonData::FMaterialSettings& Material = Header.Material;
	if ( Points == nullptr || Ribbon.NumPoints < 2 || Ribbon.NumPoints > FDrawRibbonData::MaxPoints || Ribbon.Texture < 0 || Material.MaskTexture < -1 || !std::isfinite( Ribbon.UVRepeat ) || Ribbon.UVRepeat <= 0.0f || Ribbon.JoinFlags < 0 || ( Ribbon.JoinFlags & ~( FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint ) ) != 0 || Ribbon.NumPoints - static_cast<int32>( ( Ribbon.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) - static_cast<int32>( ( Ribbon.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) < 2 ) return;
	if ( !std::isfinite( Material.UVScale[0] ) || !std::isfinite( Material.UVScale[1] ) || Material.UVScale[0] <= 0.0f || Material.UVScale[1] <= 0.0f || Material.UVScale[0] > 128.0f || Material.UVScale[1] > 128.0f || !std::isfinite( Material.MaskScale ) || Material.MaskScale <= 0.0f || Material.MaskScale > 128.0f || !std::isfinite( Material.EdgeWidth ) || Material.EdgeWidth < 0.0f || Material.EdgeWidth > 1.0f || !std::isfinite( Material.EdgeEmission ) || Material.EdgeEmission < 0.0f || Material.EdgeEmission > 32.0f || ( Material.bDissolve != 0 && Material.bDissolve != 1 ) || ( Material.DepthTest != 0 && Material.DepthTest != 1 ) || ( Material.DepthWrite != 0 && Material.DepthWrite != 1 ) ) return;
	// 任意のbyte列から来た点を、通常のFPoint寿命を持つ所有配列へ移す。
	TArray<FDrawMaterialRibbonData::FPoint> Copied;
	Copied.SetNum( Ribbon.NumPoints );
	std::memcpy( Copied.GetData(), Points, static_cast<size_t>( Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) );
	// テクスチャ反復の上限を、旧Ribbonと同じ経路区間単位で数える。
	int32 TextureSections = 0;
	// 位置・距離・材質値を検査する点の番号。
	for ( int32 Index = 0; Index < Ribbon.NumPoints; ++Index )
	{
		// 形状と材質を一緒に補間する今回の点。
		const FDrawMaterialRibbonData::FPoint& Point = Copied[Index];
		// UVRepeat適用後の連続横座標。
		const float32 BaseU = Point.Path.U * Ribbon.UVRepeat;
		if ( !std::isfinite( Point.Path.Pos[0] ) || !std::isfinite( Point.Path.Pos[1] ) || !std::isfinite( Point.Path.Pos[2] ) || !std::isfinite( Point.Path.Width ) || Point.Path.Width < 0.0f || !std::isfinite( Point.Path.U ) || Point.Path.U < 0.0f || !std::isfinite( BaseU ) || !std::isfinite( BaseU * Material.UVScale[0] + Point.UVOffset[0] ) || !std::isfinite( BaseU * Material.MaskScale ) || !std::isfinite( Point.Dissolve ) || Point.Dissolve < 0.0f || Point.Dissolve > 1.0f || !std::isfinite( Point.Emission ) || Point.Emission < 0.0f || Point.Emission > 32.0f || !std::isfinite( Point.UVOffset[0] ) || !std::isfinite( Point.UVOffset[1] ) || std::abs( Point.UVOffset[0] ) > 1.0e6f || std::abs( Point.UVOffset[1] ) > 1.0e6f ) return;
		if ( Index > 0 && Point.Path.U < Copied[Index - 1].Path.U ) return;
		// 接線補助用の端点を除く、表示区間だけの分割数。
		const bool bVisibleSegment = Index > 0 && !( Index == 1 && ( Ribbon.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) && !( Index + 1 == Ribbon.NumPoints && ( Ribbon.JoinFlags & FDrawRibbonData::NextPoint ) != 0 );
		if ( bVisibleSegment )
		{
			// この表示区間で必要な画像反復数。
			const float64 FirstRepeat = std::floor( static_cast<float64>( Copied[Index - 1].Path.U * Ribbon.UVRepeat ) );
			const float64 Sections = std::max( 1.0, std::ceil( static_cast<float64>( Point.Path.U * Ribbon.UVRepeat ) ) - FirstRepeat );
			if ( !std::isfinite( Sections ) || Sections > static_cast<float64>( FDrawRibbonData::MaxTextureSections - TextureSections ) ) return;
			TextureSections += static_cast<int32>( Sections );
		}
	}
		// 固定設定と全点を合わせた命令長。MaxPoints検査後なので積は安全な範囲。
	constexpr size_t DataWords = sizeof( FDrawMaterialRibbonData ) / 4;
	// 全点を4byte word単位で記録する長さ。
	const size_t PointWords = static_cast<size_t>( Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) / 4;
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
	std::memcpy( &Words[Begin + 1 + DataWords], Copied.GetData(), static_cast<size_t>( Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) );
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
