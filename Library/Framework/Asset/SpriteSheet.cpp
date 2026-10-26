#include "SpriteSheet.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Json/JsonHelpers.h"

DEFINE_LOG_CATEGORY_STATIC( LogSpriteSheet, Log )

//=============================================================================
// スプライトシートの定義ファイル（JSON、拡張子は .sheet.json）の書式
//
//   {
//     "Version": 1,
//     "Texture": "Sprites/Hero.png",                     // 必須。CAssetManager のルートからのパス
//     "Grid": {                                          // 省略可。格子状のコマ（複数なら配列 [ {...}, {...} ]）
//       "Columns": 8, "Rows": 4,                         // 分割数（横・縦）
//       "CellWidth": 64, "CellHeight": 64,               // 1 コマのピクセル数。省略／0 なら画像の大きさから自動
//       "NumFrames": 30,                                 // 使うコマの数。省略なら Columns * Rows
//       "MarginX": 0, "MarginY": 0,                      // 画像の外側の余白
//       "SpacingX": 0, "SpacingY": 0,                    // コマの間の隙間
//       "Order": "RowMajor",                             // "RowMajor"（既定）または "ColumnMajor"
//       "Pivot": [32, 60]                                // 基準点（コマの左上が原点）。省略ならコマの中央
//     },
//     "Frames": [                                        // 省略可。自由な矩形を、Grid のコマの後ろに足す
//       { "X": 0, "Y": 256, "W": 100, "H": 80, "PivotX": 50, "PivotY": 78 }
//     ],
//     "Clips": [                                         // 省略可
//       {
//         "Name": "Run",
//         "Range": [8, 15],                              // コマの番号の範囲（両端を含む。逆順も可）。または "Frames": [8, 9, 10, 9]
//         "Fps": 14,                                     // 省略なら 12
//         "Loop": "Loop",                                // "Once" / "Loop" / "PingPong"（省略なら Loop）
//         "Durations": [0.1, 0.1, 0.2],                  // コマごとの表示時間（秒）。省略可。足りない分は 1 / Fps
//         "Events": [ { "Step": 2, "Name": "Footstep" } ] // 省略可。Step は、クリップの何コマ目（0 から）に入ったときか
//       }
//     ]
//   }
//=============================================================================

namespace
{
	NlohmannJson GridToJson( const FSpriteSheetGrid& Grid )
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Columns"] = Grid.Columns;
		Json["Rows"] = Grid.Rows;
		if ( Grid.CellWidth > 0 ) Json["CellWidth"] = Grid.CellWidth;
		if ( Grid.CellHeight > 0 ) Json["CellHeight"] = Grid.CellHeight;
		if ( Grid.NumFrames > 0 ) Json["NumFrames"] = Grid.NumFrames;
		if ( Grid.MarginX != 0 ) Json["MarginX"] = Grid.MarginX;
		if ( Grid.MarginY != 0 ) Json["MarginY"] = Grid.MarginY;
		if ( Grid.SpacingX != 0 ) Json["SpacingX"] = Grid.SpacingX;
		if ( Grid.SpacingY != 0 ) Json["SpacingY"] = Grid.SpacingY;
		if ( Grid.Order == ESpriteGridOrder::ColumnMajor ) Json["Order"] = "ColumnMajor";
		if ( Grid.bCustomPivot ) Json["Pivot"] = NlohmannJson::array( { NJson::Round4( Grid.PivotX ), NJson::Round4( Grid.PivotY ) } );
		return Json;
	}

	FSpriteSheetGrid GridFromJson( const NlohmannJson& Json )
	{
		FSpriteSheetGrid Grid;
		Grid.Columns = NMath::Max( NJson::GetOr<int32>( Json, "Columns", 1 ), 1 );
		Grid.Rows = NMath::Max( NJson::GetOr<int32>( Json, "Rows", 1 ), 1 );
		Grid.CellWidth = NJson::GetOr<int32>( Json, "CellWidth", 0 );
		Grid.CellHeight = NJson::GetOr<int32>( Json, "CellHeight", 0 );
		Grid.NumFrames = NJson::GetOr<int32>( Json, "NumFrames", 0 );
		Grid.MarginX = NJson::GetOr<int32>( Json, "MarginX", 0 );
		Grid.MarginY = NJson::GetOr<int32>( Json, "MarginY", 0 );
		Grid.SpacingX = NJson::GetOr<int32>( Json, "SpacingX", 0 );
		Grid.SpacingY = NJson::GetOr<int32>( Json, "SpacingY", 0 );
		Grid.Order = NJson::GetOr<FString>( Json, "Order", FString( "RowMajor" ) ).ToLower() == "columnmajor" ? ESpriteGridOrder::ColumnMajor : ESpriteGridOrder::RowMajor;
		if ( Json.contains( "Pivot" ) && Json["Pivot"].is_array() && Json["Pivot"].size() >= 2 )
		{
			Grid.bCustomPivot = true;
			Grid.PivotX = Json["Pivot"][0].get<float32>();
			Grid.PivotY = Json["Pivot"][1].get<float32>();
		}
		return Grid;
	}

	NlohmannJson ClipToJson( const FSpriteClip& Clip )
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Name"] = Clip.Name;

		// 連続した番号（昇順 / 降順）は Range で書くと読みやすい
		bool bRange = Clip.Frames.Num() >= 3;
		for ( int32 i = 1; bRange && i < Clip.Frames.Num(); ++i )
		{
			const int32 Step = Clip.Frames[1] - Clip.Frames[0];
			bRange = ( Step == 1 || Step == -1 ) && Clip.Frames[i] - Clip.Frames[i - 1] == Step;
		}
		if ( bRange ) Json["Range"] = NlohmannJson::array( { Clip.Frames.First(), Clip.Frames.Last() } );
		else
		{
			NlohmannJson Frames = NlohmannJson::array();
			for ( const int32 Frame : Clip.Frames ) Frames.push_back( Frame );
			Json["Frames"] = Frames;
		}

		Json["Fps"] = NJson::Round4( Clip.Fps );
		Json["Loop"] = ToString( Clip.Loop );
		if ( !Clip.Durations.IsEmpty() )
		{
			NlohmannJson Durations = NlohmannJson::array();
			for ( const float32 Duration : Clip.Durations ) Durations.push_back( NJson::Round4( Duration ) );
			Json["Durations"] = Durations;
		}
		if ( !Clip.Events.IsEmpty() )
		{
			NlohmannJson Events = NlohmannJson::array();
			for ( const FSpriteAnimEvent& Event : Clip.Events )
			{
				NlohmannJson Item = NlohmannJson::object();
				Item["Step"] = Event.Step;
				Item["Name"] = Event.Name;
				Events.push_back( Item );
			}
			Json["Events"] = Events;
		}
		return Json;
	}

	bool ClipFromJson( const NlohmannJson& Json, FSpriteClip& OutClip )
	{
		FSpriteClip Clip;
		Clip.Name = NJson::GetOr<FString>( Json, "Name", FString() );
		Clip.Fps = NJson::GetOr<float32>( Json, "Fps", 12.0f );
		Clip.Loop = ParseLoopMode( NJson::GetOr<FString>( Json, "Loop", FString( "Loop" ) ), EAnimLoopMode::Loop );

		if ( Json.contains( "Range" ) && Json["Range"].is_array() && Json["Range"].size() >= 2 )
		{
			Clip.Frames = FSpriteClip::FromRange( Clip.Name, Json["Range"][0].get<int32>(), Json["Range"][1].get<int32>() ).Frames;
		}
		else if ( Json.contains( "Frames" ) && Json["Frames"].is_array() )
		{
			for ( const NlohmannJson& Frame : Json["Frames"] ) Clip.Frames.Add( Frame.get<int32>() );
		}
		if ( Json.contains( "Durations" ) && Json["Durations"].is_array() )
		{
			for ( const NlohmannJson& Duration : Json["Durations"] ) Clip.Durations.Add( Duration.get<float32>() );
		}
		if ( Json.contains( "Events" ) && Json["Events"].is_array() )
		{
			for ( const NlohmannJson& Event : Json["Events"] ) Clip.AddEvent( NJson::GetOr<int32>( Event, "Step", 0 ), NJson::GetOr<FString>( Event, "Name", FString() ) );
		}

		if ( Clip.Name.IsEmpty() || Clip.Frames.IsEmpty() ) return false;
		OutClip = Clip;
		return true;
	}
}

//=============================================================================
// FSpriteSheetGrid / FSpriteClip
//=============================================================================
int32 FSpriteSheetGrid::GetNumFrames() const
{
	const int32 Total = NMath::Max( Columns, 1 ) * NMath::Max( Rows, 1 );
	return NumFrames > 0 ? NMath::Min( NumFrames, Total ) : Total;
}

FSpriteClip FSpriteClip::FromRange( const FString& InName, int32 First, int32 Last, float32 InFps, EAnimLoopMode InLoop )
{
	FSpriteClip Clip;
	Clip.Name = InName;
	Clip.Fps = InFps;
	Clip.Loop = InLoop;
	const int32 Step = First <= Last ? 1 : -1;
	for ( int32 Frame = First;; Frame += Step )
	{
		Clip.Frames.Add( Frame );
		if ( Frame == Last ) break;
	}
	return Clip;
}

FSpriteClip FSpriteClip::FromFrames( const FString& InName, std::initializer_list<int32> InFrames, float32 InFps, EAnimLoopMode InLoop )
{
	FSpriteClip Clip;
	Clip.Name = InName;
	Clip.Fps = InFps;
	Clip.Loop = InLoop;
	for ( const int32 Frame : InFrames ) Clip.Frames.Add( Frame );
	return Clip;
}

FSpriteClip& FSpriteClip::SetDuration( int32 Step, float32 Seconds )
{
	if ( Step < 0 || Step >= Frames.Num() ) return *this;
	const float32 Default = Fps > 0.0f ? 1.0f / Fps : 0.1f;
	while ( Durations.Num() < Frames.Num() ) Durations.Add( Default );
	Durations[Step] = Seconds;
	return *this;
}

FSpriteClip& FSpriteClip::AddEvent( int32 Step, const FString& EventName )
{
	FSpriteAnimEvent Event;
	Event.Step = Step;
	Event.Name = EventName;
	Events.Add( Event );
	return *this;
}

float32 FSpriteClip::GetDuration( int32 Step ) const
{
	if ( Durations.IsValidIndex( Step ) ) return Durations[Step];
	return Fps > 0.0f ? 1.0f / Fps : 0.1f;
}

float32 FSpriteClip::GetTotalDuration() const
{
	float32 Total = 0.0f;
	for ( int32 Step = 0; Step < Frames.Num(); ++Step ) Total += GetDuration( Step );
	return Total;
}

void FSpriteClip::NormalizeDurations()
{
	if ( Durations.IsEmpty() ) return;
	const float32 Default = Fps > 0.0f ? 1.0f / Fps : 0.1f;
	for ( const float32 Duration : Durations )
	{
		if ( NMath::Abs( Duration - Default ) > 1.0e-5f ) return;
	}
	Durations.Empty();
}

//=============================================================================
// FSpriteSheetDefinition
//=============================================================================
int32 FSpriteSheetDefinition::GetNumGridFrames() const
{
	int32 Total = 0;
	for ( const FSpriteSheetGrid& Grid : Grids ) Total += Grid.GetNumFrames();
	return Total;
}

NlohmannJson FSpriteSheetDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	Json["Version"] = 1;
	Json["Texture"] = TexturePath;

	if ( Grids.Num() == 1 )
	{
		Json["Grid"] = GridToJson( Grids[0] );
	}
	else if ( Grids.Num() > 1 )
	{
		NlohmannJson Array = NlohmannJson::array();
		for ( const FSpriteSheetGrid& Grid : Grids ) Array.push_back( GridToJson( Grid ) );
		Json["Grid"] = Array;
	}

	if ( !Frames.IsEmpty() )
	{
		NlohmannJson Array = NlohmannJson::array();
		for ( const FSpriteFrame& Frame : Frames )
		{
			NlohmannJson Item = NlohmannJson::object();
			Item["X"] = Frame.X;
			Item["Y"] = Frame.Y;
			Item["W"] = Frame.Width;
			Item["H"] = Frame.Height;
			Item["PivotX"] = NJson::Round4( Frame.PivotX );
			Item["PivotY"] = NJson::Round4( Frame.PivotY );
			Array.push_back( Item );
		}
		Json["Frames"] = Array;
	}

	if ( !Clips.IsEmpty() )
	{
		NlohmannJson Array = NlohmannJson::array();
		for ( const FSpriteClip& Clip : Clips ) Array.push_back( ClipToJson( Clip ) );
		Json["Clips"] = Array;
	}
	return Json;
}

bool FSpriteSheetDefinition::FromJson( const NlohmannJson& Json, FSpriteSheetDefinition& OutDefinition )
{
	if ( !Json.is_object() ) return false;

	FSpriteSheetDefinition Definition;
	Definition.TexturePath = NJson::GetOr<FString>( Json, "Texture", FString() );

	if ( Json.contains( "Grid" ) )
	{
		const NlohmannJson& GridJson = Json["Grid"];
		if ( GridJson.is_array() )
		{
			for ( const NlohmannJson& Item : GridJson ) Definition.Grids.Add( GridFromJson( Item ) );
		}
		else if ( GridJson.is_object() )
		{
			Definition.Grids.Add( GridFromJson( GridJson ) );
		}
	}

	if ( Json.contains( "Frames" ) && Json["Frames"].is_array() )
	{
		for ( const NlohmannJson& Item : Json["Frames"] )
		{
			FSpriteFrame Frame;
			Frame.X = NJson::GetOr<int32>( Item, "X", 0 );
			Frame.Y = NJson::GetOr<int32>( Item, "Y", 0 );
			Frame.Width = NJson::GetOr<int32>( Item, "W", 0 );
			Frame.Height = NJson::GetOr<int32>( Item, "H", 0 );
			Frame.PivotX = NJson::GetOr<float32>( Item, "PivotX", static_cast<float32>( Frame.Width ) * 0.5f );
			Frame.PivotY = NJson::GetOr<float32>( Item, "PivotY", static_cast<float32>( Frame.Height ) * 0.5f );
			Definition.Frames.Add( Frame );
		}
	}

	if ( Json.contains( "Clips" ) && Json["Clips"].is_array() )
	{
		for ( const NlohmannJson& Item : Json["Clips"] )
		{
			FSpriteClip Clip;
			if ( !ClipFromJson( Item, Clip ) )
			{
				LOG( LogSpriteSheet, Warning, "A clip without a Name or frames (\"Range\" / \"Frames\") was skipped." );
				continue;
			}
			Definition.Clips.Add( Clip );
		}
	}

	OutDefinition = Definition;
	return true;
}

//=============================================================================
// CSpriteSheet
//=============================================================================
CSpriteSheet::CSpriteSheet( const FString& InName, const TSharedPtr<CTexture>& InTexture )
	: CAsset( EAssetType::SpriteSheet, InName )
	, Texture( InTexture )
{
	if ( Texture.IsValid() ) Definition.TexturePath = Texture->GetPath();
}

void CSpriteSheet::SetTexture( const TSharedPtr<CTexture>& InTexture, const FString& DefinitionTexturePath )
{
	Texture = InTexture;
	if ( !DefinitionTexturePath.IsEmpty() ) Definition.TexturePath = DefinitionTexturePath;
	else if ( Texture.IsValid() ) Definition.TexturePath = Texture->GetPath();
	RebuildFrames();
}

bool CSpriteSheet::IsReady() const
{
	if ( !Texture.IsValid() || !Texture->IsReady() ) return false;
	Prepare();
	return !bPendingSize;
}

FString CSpriteSheet::Describe() const
{
	return FString::Printf( "%d frame(s), %d clip(s), texture '%s'", GetNumFrames(), GetNumClips(), Texture.IsValid() ? *Texture->GetPath() : "(none)" );
}

int64 CSpriteSheet::GetMemoryBytes() const
{
	int64 Bytes = static_cast<int64>( sizeof( *this ) ) + static_cast<int64>( Frames.Num() ) * static_cast<int64>( sizeof( FSpriteFrame ) );
	for ( const FSpriteClip& Clip : Definition.Clips )
	{
		Bytes += static_cast<int64>( sizeof( FSpriteClip ) ) + Clip.Frames.Num() * 4 + Clip.Durations.Num() * 4 + Clip.Events.Num() * static_cast<int64>( sizeof( FSpriteAnimEvent ) );
	}
	return Bytes;
}

void CSpriteSheet::Prepare() const
{
	if ( bPendingSize && Texture.IsValid() && Texture->IsReady() ) RebuildFrames();
}

void CSpriteSheet::ComputeGridFrames( const FSpriteSheetGrid& Grid, int32 TextureWidth, int32 TextureHeight, TArray<FSpriteFrame>& OutFrames )
{
	OutFrames.Reset();
	const int32 Columns = NMath::Max( Grid.Columns, 1 );
	const int32 Rows = NMath::Max( Grid.Rows, 1 );
	const int32 Num = Grid.GetNumFrames();

	// 自動: 余白と隙間を引いた残りを、分割数で等分する
	int32 CellWidth = Grid.CellWidth;
	int32 CellHeight = Grid.CellHeight;
	if ( CellWidth <= 0 ) CellWidth = ( TextureWidth - 2 * Grid.MarginX - Grid.SpacingX * ( Columns - 1 ) ) / Columns;
	if ( CellHeight <= 0 ) CellHeight = ( TextureHeight - 2 * Grid.MarginY - Grid.SpacingY * ( Rows - 1 ) ) / Rows;

	for ( int32 Index = 0; Index < Num; ++Index )
	{
		const int32 Column = Grid.Order == ESpriteGridOrder::RowMajor ? Index % Columns : Index / Rows;
		const int32 Row = Grid.Order == ESpriteGridOrder::RowMajor ? Index / Columns : Index % Rows;

		FSpriteFrame Frame;
		Frame.X = Grid.MarginX + Column * ( CellWidth + Grid.SpacingX );
		Frame.Y = Grid.MarginY + Row * ( CellHeight + Grid.SpacingY );
		Frame.Width = CellWidth;
		Frame.Height = CellHeight;
		Frame.PivotX = Grid.bCustomPivot ? Grid.PivotX : static_cast<float32>( CellWidth ) * 0.5f;
		Frame.PivotY = Grid.bCustomPivot ? Grid.PivotY : static_cast<float32>( CellHeight ) * 0.5f;
		OutFrames.Add( Frame );
	}
}

void CSpriteSheet::FillGrid( const FSpriteSheetGrid& Grid, int32 StartIndex, int32 TextureWidth, int32 TextureHeight ) const
{
	TArray<FSpriteFrame> Computed;
	ComputeGridFrames( Grid, TextureWidth, TextureHeight, Computed );

	for ( int32 Index = 0; Index < Computed.Num(); ++Index )
	{
		FSpriteFrame Frame = Computed[Index];

		// 画像の外にはみ出していたら、警告して空のコマにする（描いても何も出ない）
		if ( Frame.Width <= 0 || Frame.Height <= 0 || Frame.X < 0 || Frame.Y < 0 || Frame.X + Frame.Width > TextureWidth || Frame.Y + Frame.Height > TextureHeight )
		{
			LOG( LogSpriteSheet, Warning, "Frame %d of '%s' (%d,%d %dx%d) is outside the texture (%dx%d).", StartIndex + Index, *GetPath(), Frame.X, Frame.Y, Frame.Width, Frame.Height,
				 TextureWidth, TextureHeight );
			Frame.Width = 0;
			Frame.Height = 0;
		}
		Frames[StartIndex + Index] = Frame;
	}
}

void CSpriteSheet::RebuildFrames() const
{
	bPendingSize = false;
	Frames.Empty();

	const bool bTextureReady = Texture.IsValid() && Texture->IsReady();
	const int32 TextureWidth = bTextureReady ? Texture->GetWidth() : 0;
	const int32 TextureHeight = bTextureReady ? Texture->GetHeight() : 0;

	for ( const FSpriteSheetGrid& Grid : Definition.Grids )
	{
		const int32 StartIndex = Frames.Num();
		Frames.SetNum( StartIndex + Grid.GetNumFrames() );   // 番号は、画像が読み込まれていなくても決まっている
		if ( bTextureReady ) FillGrid( Grid, StartIndex, TextureWidth, TextureHeight );
		else bPendingSize = true;
	}
	if ( !Texture.IsValid() && !Definition.Grids.IsEmpty() )
	{
		LOG( LogSpriteSheet, Warning, "'%s' has no texture. The grid frames stay empty.", *GetPath() );
	}
	for ( const FSpriteFrame& Frame : Definition.Frames ) Frames.Add( Frame );
}

void CSpriteSheet::SetDefinition( const FSpriteSheetDefinition& InDefinition )
{
	Definition = InDefinition;
	RebuildFrames();
	RebuildTimelines();
}

void CSpriteSheet::BuildGrid( const FSpriteSheetGrid& Grid )
{
	Definition.Grids.Add( Grid );
	RebuildFrames();
}

int32 CSpriteSheet::AddFrame( int32 X, int32 Y, int32 Width, int32 Height )
{
	FSpriteFrame Frame;
	Frame.X = X;
	Frame.Y = Y;
	Frame.Width = Width;
	Frame.Height = Height;
	Frame.PivotX = static_cast<float32>( Width ) * 0.5f;
	Frame.PivotY = static_cast<float32>( Height ) * 0.5f;
	return AddFrame( Frame );
}

int32 CSpriteSheet::AddFrame( const FSpriteFrame& Frame )
{
	Definition.Frames.Add( Frame );
	RebuildFrames();
	return Frames.Num() - 1;
}

int32 CSpriteSheet::GetNumFrames() const
{
	return Frames.Num();
}

const FSpriteFrame* CSpriteSheet::GetFrame( int32 Index ) const
{
	return Frames.IsValidIndex( Index ) ? &Frames[Index] : nullptr;
}

FAnimTimeline CSpriteSheet::MakeTimeline( const FSpriteClip& Clip )
{
	TArray<float32> Durations;
	for ( int32 Step = 0; Step < Clip.Frames.Num(); ++Step ) Durations.Add( Clip.GetDuration( Step ) );
	return FAnimTimeline::Build( Durations, Clip.Loop );
}

void CSpriteSheet::RebuildTimelines()
{
	Timelines.Empty();
	for ( const FSpriteClip& Clip : Definition.Clips ) Timelines.Add( MakeTimeline( Clip ) );
}

int32 CSpriteSheet::AddClip( const FSpriteClip& Clip )
{
	const int32 Existing = FindClip( Clip.Name );
	if ( Existing != INDEX_NONE )
	{
		Definition.Clips[Existing] = Clip;
		Timelines[Existing] = MakeTimeline( Clip );
		return Existing;
	}
	Timelines.Add( MakeTimeline( Clip ) );
	return Definition.Clips.Add( Clip );
}

int32 CSpriteSheet::FindClip( const FString& Name ) const
{
	return Definition.Clips.IndexOfByPredicate( [&Name]( const FSpriteClip& Clip ) { return Clip.Name == Name; } );
}

const FSpriteClip* CSpriteSheet::GetClip( int32 Index ) const
{
	return Definition.Clips.IsValidIndex( Index ) ? &Definition.Clips[Index] : nullptr;
}

const FAnimTimeline* CSpriteSheet::GetTimeline( int32 Index ) const
{
	return Timelines.IsValidIndex( Index ) ? &Timelines[Index] : nullptr;
}
