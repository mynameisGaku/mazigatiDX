#include "DefinitionEditing.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Types/Numeric.h"

//=============================================================================
// 名前
//=============================================================================
FString NDefinitionEdit::MakeUniqueName( const TArray<FString>& ExistingNames, const FString& Wanted, const FString& Fallback )
{
	const FString Base = Wanted.TrimStartAndEnd().IsEmpty() ? Fallback : Wanted.TrimStartAndEnd();
	if ( !ExistingNames.Contains( Base ) ) return Base;
	for ( int32 Number = 2;; ++Number )
	{
		const FString Candidate = FString::Printf( "%s %d", *Base, Number );
		if ( !ExistingNames.Contains( Candidate ) ) return Candidate;
	}
}

//=============================================================================
// スプライトシート
//=============================================================================
namespace
{
	TArray<FString> ClipNames( const TArray<FSpriteClip>& Clips, int32 ExceptIndex = INDEX_NONE )
	{
		TArray<FString> Names;
		for ( int32 i = 0; i < Clips.Num(); ++i )
		{
			if ( i != ExceptIndex ) Names.Add( Clips[i].Name );
		}
		return Names;
	}

	void RemoveStepInternal( FSpriteClip& Clip, int32 Step )
	{
		if ( !Clip.Frames.IsValidIndex( Step ) ) return;
		Clip.Frames.RemoveAt( Step );
		if ( Clip.Durations.IsValidIndex( Step ) ) Clip.Durations.RemoveAt( Step );
		Clip.Events.RemoveAll( [Step]( const FSpriteAnimEvent& Event ) { return Event.Step == Step; } );
		for ( FSpriteAnimEvent& Event : Clip.Events )
		{
			if ( Event.Step > Step ) --Event.Step;
		}
	}

	void InsertStepInternal( FSpriteClip& Clip, int32 At, int32 SheetFrame )
	{
		At = NMath::Clamp( At, 0, Clip.Frames.Num() );
		const bool bHasDurations = !Clip.Durations.IsEmpty();
		const float32 Default = Clip.Fps > 0.0f ? 1.0f / Clip.Fps : 0.1f;
		if ( bHasDurations )
		{
			while ( Clip.Durations.Num() < Clip.Frames.Num() ) Clip.Durations.Add( Default );
			Clip.Durations.Insert( Default, At );
		}
		Clip.Frames.Insert( SheetFrame, At );
		for ( FSpriteAnimEvent& Event : Clip.Events )
		{
			if ( Event.Step >= At ) ++Event.Step;
		}
	}

	// [Start, Start + OldCount) の番号のコマが NewCount 個になった（後ろの番号はずれる）ので、クリップの中の番号を直す。
	// 範囲の中で、新しい個数に収まらなくなったコマは、クリップから外す
	void RemapFrames( TArray<FSpriteClip>& Clips, int32 Start, int32 OldCount, int32 NewCount )
	{
		for ( FSpriteClip& Clip : Clips )
		{
			for ( int32 Step = Clip.Frames.Num() - 1; Step >= 0; --Step )
			{
				const int32 Frame = Clip.Frames[Step];
				if ( Frame >= Start + OldCount ) Clip.Frames[Step] = Frame + ( NewCount - OldCount );
				else if ( Frame >= Start && Frame - Start >= NewCount ) RemoveStepInternal( Clip, Step );
			}
		}
	}

	int32 GridStartIndex( const FSpriteSheetDefinition& Definition, int32 GridIndex )
	{
		int32 Start = 0;
		for ( int32 i = 0; i < GridIndex && i < Definition.Grids.Num(); ++i ) Start += Definition.Grids[i].GetNumFrames();
		return Start;
	}
}

void NSpriteSheetEdit::SetTexturePath( TSpriteSheetDocument& Document, const FString& Path )
{
	Document.BeginEdit().TexturePath = Path;
}

int32 NSpriteSheetEdit::AddGrid( TSpriteSheetDocument& Document, const FSpriteSheetGrid& Grid )
{
	FSpriteSheetDefinition& Definition = Document.BeginEdit();
	const int32 Start = Definition.GetNumGridFrames();
	RemapFrames( Definition.Clips, Start, 0, Grid.GetNumFrames() );   // 自由な矩形の番号が、格子のぶんだけずれる
	return Definition.Grids.Add( Grid );
}

void NSpriteSheetEdit::SetGrid( TSpriteSheetDocument& Document, int32 GridIndex, const FSpriteSheetGrid& Grid, const char* CoalesceKey )
{
	if ( !Document.Get().Grids.IsValidIndex( GridIndex ) ) return;
	FSpriteSheetDefinition& Definition = Document.BeginEdit( CoalesceKey );
	const int32 Start = GridStartIndex( Definition, GridIndex );
	const int32 OldCount = Definition.Grids[GridIndex].GetNumFrames();
	Definition.Grids[GridIndex] = Grid;
	if ( Grid.GetNumFrames() != OldCount ) RemapFrames( Definition.Clips, Start, OldCount, Grid.GetNumFrames() );
}

void NSpriteSheetEdit::RemoveGrid( TSpriteSheetDocument& Document, int32 GridIndex )
{
	if ( !Document.Get().Grids.IsValidIndex( GridIndex ) ) return;
	FSpriteSheetDefinition& Definition = Document.BeginEdit();
	RemapFrames( Definition.Clips, GridStartIndex( Definition, GridIndex ), Definition.Grids[GridIndex].GetNumFrames(), 0 );
	Definition.Grids.RemoveAt( GridIndex );
}

int32 NSpriteSheetEdit::AddFrame( TSpriteSheetDocument& Document, const FSpriteFrame& Frame )
{
	FSpriteSheetDefinition& Definition = Document.BeginEdit();
	Definition.Frames.Add( Frame );
	return Definition.GetNumFrames() - 1;
}

void NSpriteSheetEdit::SetFrame( TSpriteSheetDocument& Document, int32 FrameIndex, const FSpriteFrame& Frame, const char* CoalesceKey )
{
	const int32 FreeIndex = FrameIndex - Document.Get().GetNumGridFrames();
	if ( !Document.Get().Frames.IsValidIndex( FreeIndex ) ) return;   // 格子のコマは、格子の設定で変える
	Document.BeginEdit( CoalesceKey ).Frames[FreeIndex] = Frame;
}

void NSpriteSheetEdit::RemoveFrame( TSpriteSheetDocument& Document, int32 FrameIndex )
{
	const int32 FreeIndex = FrameIndex - Document.Get().GetNumGridFrames();
	if ( !Document.Get().Frames.IsValidIndex( FreeIndex ) ) return;
	FSpriteSheetDefinition& Definition = Document.BeginEdit();
	RemapFrames( Definition.Clips, FrameIndex, 1, 0 );
	Definition.Frames.RemoveAt( FreeIndex );
}

int32 NSpriteSheetEdit::AddClip( TSpriteSheetDocument& Document, const FString& Name, int32 FirstFrame )
{
	FSpriteSheetDefinition& Definition = Document.BeginEdit();
	FSpriteClip Clip;
	Clip.Name = NDefinitionEdit::MakeUniqueName( ClipNames( Definition.Clips ), Name, "Clip" );
	Clip.Frames.Add( FirstFrame >= 0 ? FirstFrame : 0 );
	return Definition.Clips.Add( Clip );
}

void NSpriteSheetEdit::RemoveClip( TSpriteSheetDocument& Document, int32 ClipIndex )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	Document.BeginEdit().Clips.RemoveAt( ClipIndex );
}

FString NSpriteSheetEdit::RenameClip( TSpriteSheetDocument& Document, int32 ClipIndex, const FString& Name )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return FString();
	FSpriteSheetDefinition& Definition = Document.BeginEdit();
	Definition.Clips[ClipIndex].Name = NDefinitionEdit::MakeUniqueName( ClipNames( Definition.Clips, ClipIndex ), Name, "Clip" );
	return Definition.Clips[ClipIndex].Name;
}

void NSpriteSheetEdit::SetClipFps( TSpriteSheetDocument& Document, int32 ClipIndex, float32 Fps, const char* CoalesceKey )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	FSpriteSheetDefinition& Definition = Document.BeginEdit( CoalesceKey );
	FSpriteClip& Clip = Definition.Clips[ClipIndex];
	// コマごとの表示時間を持っていないクリップだけ、速さが変わる。持っているクリップは、その時間のまま
	Clip.Fps = NMath::Max( Fps, 0.01f );
}

void NSpriteSheetEdit::SetClipLoop( TSpriteSheetDocument& Document, int32 ClipIndex, EAnimLoopMode Loop )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	Document.BeginEdit().Clips[ClipIndex].Loop = Loop;
}

void NSpriteSheetEdit::InsertStep( TSpriteSheetDocument& Document, int32 ClipIndex, int32 At, int32 SheetFrame )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	InsertStepInternal( Document.BeginEdit().Clips[ClipIndex], At, SheetFrame );
}

void NSpriteSheetEdit::AppendStep( TSpriteSheetDocument& Document, int32 ClipIndex, int32 SheetFrame )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	InsertStep( Document, ClipIndex, Document.Get().Clips[ClipIndex].Frames.Num(), SheetFrame );
}

void NSpriteSheetEdit::RemoveStep( TSpriteSheetDocument& Document, int32 ClipIndex, int32 Step )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) || !Document.Get().Clips[ClipIndex].Frames.IsValidIndex( Step ) ) return;
	RemoveStepInternal( Document.BeginEdit().Clips[ClipIndex], Step );
}

void NSpriteSheetEdit::MoveStep( TSpriteSheetDocument& Document, int32 ClipIndex, int32 From, int32 To )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	const FSpriteClip& Current = Document.Get().Clips[ClipIndex];
	if ( !Current.Frames.IsValidIndex( From ) || !Current.Frames.IsValidIndex( To ) || From == To ) return;

	FSpriteClip& Clip = Document.BeginEdit().Clips[ClipIndex];
	const int32 Frame = Clip.Frames[From];
	Clip.Frames.RemoveAt( From );
	Clip.Frames.Insert( Frame, To );
	if ( Clip.Durations.Num() == Clip.Frames.Num() )
	{
		const float32 Duration = Clip.Durations[From];
		Clip.Durations.RemoveAt( From );
		Clip.Durations.Insert( Duration, To );
	}
	for ( FSpriteAnimEvent& Event : Clip.Events )
	{
		if ( Event.Step == From ) Event.Step = To;
		else if ( From < To && Event.Step > From && Event.Step <= To ) --Event.Step;
		else if ( From > To && Event.Step >= To && Event.Step < From ) ++Event.Step;
	}
}

void NSpriteSheetEdit::SetStepFrame( TSpriteSheetDocument& Document, int32 ClipIndex, int32 Step, int32 SheetFrame )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) || !Document.Get().Clips[ClipIndex].Frames.IsValidIndex( Step ) ) return;
	Document.BeginEdit().Clips[ClipIndex].Frames[Step] = SheetFrame;
}

void NSpriteSheetEdit::SetStepRange( TSpriteSheetDocument& Document, int32 ClipIndex, int32 First, int32 Last )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	FSpriteClip& Clip = Document.BeginEdit().Clips[ClipIndex];
	Clip.Frames = FSpriteClip::FromRange( Clip.Name, First, Last ).Frames;
	Clip.Durations.Empty();
	Clip.Events.RemoveAll( [&Clip]( const FSpriteAnimEvent& Event ) { return Event.Step >= Clip.Frames.Num(); } );
}

void NSpriteSheetEdit::SetStepDuration( TSpriteSheetDocument& Document, int32 ClipIndex, int32 Step, float32 Seconds, const char* CoalesceKey )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) || !Document.Get().Clips[ClipIndex].Frames.IsValidIndex( Step ) ) return;
	FSpriteClip& Clip = Document.BeginEdit( CoalesceKey ).Clips[ClipIndex];
	Clip.SetDuration( Step, NMath::Max( Seconds, 0.001f ) );
	Clip.NormalizeDurations();
}

void NSpriteSheetEdit::ResetDurations( TSpriteSheetDocument& Document, int32 ClipIndex )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	Document.BeginEdit().Clips[ClipIndex].Durations.Empty();
}

int32 NSpriteSheetEdit::AddEvent( TSpriteSheetDocument& Document, int32 ClipIndex, int32 Step, const FString& Name )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) || Document.Get().Clips[ClipIndex].Frames.IsEmpty() ) return INDEX_NONE;
	FSpriteClip& Clip = Document.BeginEdit().Clips[ClipIndex];
	FSpriteAnimEvent Event;
	Event.Step = NMath::Clamp( Step, 0, Clip.Frames.Num() - 1 );
	Event.Name = Name;
	return Clip.Events.Add( Event );
}

void NSpriteSheetEdit::SetEvent( TSpriteSheetDocument& Document, int32 ClipIndex, int32 EventIndex, int32 Step, const FString& Name, const char* CoalesceKey )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) || !Document.Get().Clips[ClipIndex].Events.IsValidIndex( EventIndex ) ) return;
	FSpriteClip& Clip = Document.BeginEdit( CoalesceKey ).Clips[ClipIndex];
	Clip.Events[EventIndex].Step = NMath::Clamp( Step, 0, NMath::Max( Clip.Frames.Num() - 1, 0 ) );
	Clip.Events[EventIndex].Name = Name;
}

void NSpriteSheetEdit::RemoveEvent( TSpriteSheetDocument& Document, int32 ClipIndex, int32 EventIndex )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) || !Document.Get().Clips[ClipIndex].Events.IsValidIndex( EventIndex ) ) return;
	Document.BeginEdit().Clips[ClipIndex].Events.RemoveAt( EventIndex );
}

TArray<FString> NSpriteSheetEdit::Validate( const FSpriteSheetDefinition& Definition, int32 TextureWidth, int32 TextureHeight )
{
	TArray<FString> Problems;
	if ( Definition.TexturePath.IsEmpty() ) Problems.Add( "No texture is set." );

	for ( int32 GridIndex = 0; GridIndex < Definition.Grids.Num(); ++GridIndex )
	{
		const FSpriteSheetGrid& Grid = Definition.Grids[GridIndex];
		if ( Grid.Columns < 1 || Grid.Rows < 1 ) Problems.Add( FString::Printf( "Grid %d: Columns and Rows must be at least 1.", GridIndex ) );
		if ( TextureWidth > 0 && TextureHeight > 0 )
		{
			TArray<FSpriteFrame> Frames;
			CSpriteSheet::ComputeGridFrames( Grid, TextureWidth, TextureHeight, Frames );
			int32 NumOutside = 0;
			for ( const FSpriteFrame& Frame : Frames )
			{
				if ( Frame.Width <= 0 || Frame.Height <= 0 || Frame.X < 0 || Frame.Y < 0 || Frame.X + Frame.Width > TextureWidth || Frame.Y + Frame.Height > TextureHeight ) ++NumOutside;
			}
			if ( NumOutside > 0 ) Problems.Add( FString::Printf( "Grid %d: %d cell(s) are outside the texture (%dx%d).", GridIndex, NumOutside, TextureWidth, TextureHeight ) );
		}
	}
	if ( TextureWidth > 0 && TextureHeight > 0 )
	{
		for ( int32 i = 0; i < Definition.Frames.Num(); ++i )
		{
			const FSpriteFrame& Frame = Definition.Frames[i];
			if ( Frame.Width <= 0 || Frame.Height <= 0 || Frame.X < 0 || Frame.Y < 0 || Frame.X + Frame.Width > TextureWidth || Frame.Y + Frame.Height > TextureHeight )
			{
				Problems.Add( FString::Printf( "Free frame %d is empty or outside the texture.", Definition.GetNumGridFrames() + i ) );
			}
		}
	}

	const int32 NumFrames = Definition.GetNumFrames();
	TArray<FString> SeenNames;
	for ( const FSpriteClip& Clip : Definition.Clips )
	{
		if ( Clip.Name.IsEmpty() ) Problems.Add( "A clip has no name." );
		else if ( SeenNames.Contains( Clip.Name ) ) Problems.Add( FString::Printf( "Clip name '%s' is used more than once.", *Clip.Name ) );
		SeenNames.Add( Clip.Name );

		if ( Clip.Frames.IsEmpty() ) Problems.Add( FString::Printf( "Clip '%s' has no frames (it is skipped when loading).", *Clip.Name ) );
		for ( int32 Step = 0; Step < Clip.Frames.Num(); ++Step )
		{
			if ( Clip.Frames[Step] < 0 || Clip.Frames[Step] >= NumFrames )
			{
				Problems.Add( FString::Printf( "Clip '%s', step %d uses frame %d, but there are only %d frame(s).", *Clip.Name, Step, Clip.Frames[Step], NumFrames ) );
				break;
			}
		}
		if ( Clip.Fps <= 0.0f ) Problems.Add( FString::Printf( "Clip '%s' has a non-positive fps.", *Clip.Name ) );
		for ( const FSpriteAnimEvent& Event : Clip.Events )
		{
			if ( Event.Name.IsEmpty() ) Problems.Add( FString::Printf( "Clip '%s' has an event without a name.", *Clip.Name ) );
			if ( Event.Step < 0 || Event.Step >= Clip.Frames.Num() ) Problems.Add( FString::Printf( "Clip '%s': event '%s' is on step %d, which does not exist.", *Clip.Name, *Event.Name, Event.Step ) );
		}
	}
	return Problems;
}

//=============================================================================
// モデルのアニメーション定義
//=============================================================================
namespace
{
	TArray<FString> ClipNames( const TArray<FModelClipDef>& Clips, int32 ExceptIndex = INDEX_NONE )
	{
		TArray<FString> Names;
		for ( int32 i = 0; i < Clips.Num(); ++i )
		{
			if ( i != ExceptIndex ) Names.Add( Clips[i].Name );
		}
		return Names;
	}

	// 時刻の順に並べて（同じ時刻なら、元の順を保つ）、指定した出来事が動いた先の番号を返す
	int32 SortEvents( TArray<FModelEventDef>& Events, int32 TrackedIndex )
	{
		struct FEntry
		{
			FModelEventDef Event;
			int32 OriginalIndex;
		};
		TArray<FEntry> Entries;
		for ( int32 i = 0; i < Events.Num(); ++i ) Entries.Add( FEntry { Events[i], i } );
		Entries.StableSort( []( const FEntry& A, const FEntry& B ) { return A.Event.Time < B.Event.Time; } );

		int32 NewIndex = INDEX_NONE;
		for ( int32 i = 0; i < Entries.Num(); ++i )
		{
			Events[i] = Entries[i].Event;
			if ( Entries[i].OriginalIndex == TrackedIndex ) NewIndex = i;
		}
		return NewIndex;
	}
}

void NModelAnimationEdit::SetModelPath( TModelAnimationDocument& Document, const FString& Path )
{
	Document.BeginEdit().ModelPath = Path;
}

void NModelAnimationEdit::SetPreviewModelPath( TModelAnimationDocument& Document, const FString& Path )
{
	Document.BeginEdit().PreviewModelPath = Path;
}

void NModelAnimationEdit::SetUnitsPerSecond( TModelAnimationDocument& Document, float32 UnitsPerSecond, const char* CoalesceKey )
{
	Document.BeginEdit( CoalesceKey ).UnitsPerSecond = NMath::Max( UnitsPerSecond, 0.001f );
}

void NModelAnimationEdit::SetPlacement( TModelAnimationDocument& Document, const FVector3& Offset, const FVector3& RotationDegrees, const FVector3& Scale, const char* CoalesceKey )
{
	FModelAnimationDefinition& Definition = Document.BeginEdit( CoalesceKey );
	Definition.Offset = Offset;
	Definition.RotationDegrees = RotationDegrees;
	Definition.Scale = Scale;
}

int32 NModelAnimationEdit::AddClip( TModelAnimationDocument& Document, const FModelClipDef& Clip )
{
	FModelAnimationDefinition& Definition = Document.BeginEdit();
	FModelClipDef Added = Clip;
	Added.Name = NDefinitionEdit::MakeUniqueName( ClipNames( Definition.Clips ), Clip.Name, "Clip" );
	return Definition.Clips.Add( Added );
}

int32 NModelAnimationEdit::AddClips( TModelAnimationDocument& Document, const TArray<FModelClipDef>& Clips )
{
	if ( Clips.IsEmpty() ) return INDEX_NONE;
	FModelAnimationDefinition& Definition = Document.BeginEdit();
	const int32 First = Definition.Clips.Num();
	for ( const FModelClipDef& Clip : Clips )
	{
		FModelClipDef Added = Clip;
		Added.Name = NDefinitionEdit::MakeUniqueName( ClipNames( Definition.Clips ), Clip.Name, "Clip" );
		Definition.Clips.Add( Added );
	}
	return First;
}

void NModelAnimationEdit::RemoveClip( TModelAnimationDocument& Document, int32 ClipIndex )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	Document.BeginEdit().Clips.RemoveAt( ClipIndex );
}

void NModelAnimationEdit::MoveClip( TModelAnimationDocument& Document, int32 From, int32 To )
{
	const TArray<FModelClipDef>& Current = Document.Get().Clips;
	if ( !Current.IsValidIndex( From ) || !Current.IsValidIndex( To ) || From == To ) return;
	TArray<FModelClipDef>& Clips = Document.BeginEdit().Clips;
	const FModelClipDef Clip = Clips[From];
	Clips.RemoveAt( From );
	Clips.Insert( Clip, To );
}

FString NModelAnimationEdit::RenameClip( TModelAnimationDocument& Document, int32 ClipIndex, const FString& Name )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return FString();
	FModelAnimationDefinition& Definition = Document.BeginEdit();
	Definition.Clips[ClipIndex].Name = NDefinitionEdit::MakeUniqueName( ClipNames( Definition.Clips, ClipIndex ), Name, "Clip" );
	return Definition.Clips[ClipIndex].Name;
}

void NModelAnimationEdit::SetClipSettings( TModelAnimationDocument& Document, int32 ClipIndex, const FModelClipDef& Clip, const char* CoalesceKey )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return;
	FModelClipDef& Target = Document.BeginEdit( CoalesceKey ).Clips[ClipIndex];
	Target.SourcePath = Clip.SourcePath;
	Target.AnimIndex = Clip.AnimIndex;
	Target.AnimName = Clip.AnimName;
	Target.Loop = Clip.Loop;
	Target.Speed = Clip.Speed;
	Target.FadeTime = NMath::Max( Clip.FadeTime, 0.0f );
}

int32 NModelAnimationEdit::AddEvent( TModelAnimationDocument& Document, int32 ClipIndex, float32 Time, const FString& Name )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) ) return INDEX_NONE;
	TArray<FModelEventDef>& Events = Document.BeginEdit().Clips[ClipIndex].Events;
	FModelEventDef Event;
	Event.Time = NMath::Clamp( Time, 0.0f, 1.0f );
	Event.Name = Name;
	const int32 Added = Events.Add( Event );
	return SortEvents( Events, Added );
}

int32 NModelAnimationEdit::SetEvent( TModelAnimationDocument& Document, int32 ClipIndex, int32 EventIndex, float32 Time, const FString& Name, const char* CoalesceKey )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) || !Document.Get().Clips[ClipIndex].Events.IsValidIndex( EventIndex ) ) return INDEX_NONE;
	TArray<FModelEventDef>& Events = Document.BeginEdit( CoalesceKey ).Clips[ClipIndex].Events;
	Events[EventIndex].Time = NMath::Clamp( Time, 0.0f, 1.0f );
	Events[EventIndex].Name = Name;
	return SortEvents( Events, EventIndex );
}

void NModelAnimationEdit::RemoveEvent( TModelAnimationDocument& Document, int32 ClipIndex, int32 EventIndex )
{
	if ( !Document.Get().Clips.IsValidIndex( ClipIndex ) || !Document.Get().Clips[ClipIndex].Events.IsValidIndex( EventIndex ) ) return;
	Document.BeginEdit().Clips[ClipIndex].Events.RemoveAt( EventIndex );
}

TArray<FString> NModelAnimationEdit::Validate( const FModelAnimationDefinition& Definition )
{
	TArray<FString> Problems;
	if ( Definition.ModelPath.IsEmpty() && Definition.Clips.IsEmpty() ) Problems.Add( "No model is set and there are no clips." );
	if ( Definition.UnitsPerSecond <= 0.0f ) Problems.Add( "Units per second must be positive." );

	TArray<FString> SeenNames;
	for ( const FModelClipDef& Clip : Definition.Clips )
	{
		if ( Clip.Name.IsEmpty() ) Problems.Add( "A clip has no name." );
		else if ( SeenNames.Contains( Clip.Name ) ) Problems.Add( FString::Printf( "Clip name '%s' is used more than once.", *Clip.Name ) );
		SeenNames.Add( Clip.Name );

		// モデルを持たない集（モーション専用）では、アニメーションの入ったファイルが必須
		if ( Definition.ModelPath.IsEmpty() && Clip.SourcePath.IsEmpty() ) Problems.Add( FString::Printf( "Clip '%s' has no animation file, and the set has no model.", *Clip.Name ) );
		if ( Clip.AnimName.IsEmpty() && Clip.AnimIndex < 0 ) Problems.Add( FString::Printf( "Clip '%s' has neither an animation name nor a valid index.", *Clip.Name ) );
		if ( Clip.Speed == 0.0f ) Problems.Add( FString::Printf( "Clip '%s' has speed 0, so it never moves.", *Clip.Name ) );
		for ( const FModelEventDef& Event : Clip.Events )
		{
			if ( Event.Name.IsEmpty() ) Problems.Add( FString::Printf( "Clip '%s' has an event without a name.", *Clip.Name ) );
			if ( Event.Time < 0.0f || Event.Time > 1.0f ) Problems.Add( FString::Printf( "Clip '%s': event '%s' is at %.3f, outside 0..1.", *Clip.Name, *Event.Name, Event.Time ) );
		}
	}
	return Problems;
}
