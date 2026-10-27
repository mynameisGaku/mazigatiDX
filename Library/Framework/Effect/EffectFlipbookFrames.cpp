// SPDX-License-Identifier: MIT

#include "EffectFlipbookFrames.h"


#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/SpriteSheet.h"

namespace
{
	// 選択済みクリップの再生情報と矩形を、資源作成の前に一括で検証する。
	bool ReadClipSnapshot( const FEffectFlipbookDefinition& Definition, CAssetManager& Assets, TSharedPtr<CSpriteSheet>& OutSheet, int32& OutClipIndex, TArray<FSpriteFrame>& OutFrames, TArray<float32>& OutDurations, EAnimLoopMode& OutLoop, FString* OutError )
	{
	// 呼出し側へ根拠を返し、部分状態を残さない。
	auto Fail = [OutError]( const char* Message ) { if ( OutError != nullptr ) *OutError = Message; return false; };
	if ( !Definition.bEnabled || Definition.Sheet.IsEmpty() ) return Fail( "コマ送りが無効か、シートのパスが空です" );
	// 再生設定そのものに保存不可能な値がないか調べる。
	const TArray<FString> DefinitionIssues = Definition.Validate();
		if ( !DefinitionIssues.IsEmpty() ) return Fail( *DefinitionIssues[0] );
		if ( Definition.TimeMode != FEffectFlipbookDefinition::ETimeMode::Lifetime && Definition.TimeMode != FEffectFlipbookDefinition::ETimeMode::Seconds ) return Fail( "再生時間の種類が不正です" );
		if ( Definition.TimeMode == FEffectFlipbookDefinition::ETimeMode::Seconds && ( !NMath::IsFinite( Definition.Speed ) || Definition.Speed < 0.0f || Definition.Speed > 128.0f ) ) return Fail( "秒時計の速度は0から128の範囲で指定してください" );
		OutSheet = Assets.LoadSpriteSheet( Definition.Sheet, false );
	if ( !OutSheet.IsValid() || !OutSheet->IsReady() || !OutSheet->GetTexture().IsValid() || !OutSheet->GetTexture()->IsReady() ) return Fail( "スプライトシートまたは元画像を読み込めません" );
	// sheetが所有するclipと元画像参照を読む定義。
	const FSpriteSheetDefinition& SheetDefinition = OutSheet->GetDefinition();
		if ( SheetDefinition.Clips.IsEmpty() ) return Fail( "スプライトシートにクリップがありません" );
		OutClipIndex = Definition.Clip.IsEmpty() ? 0 : OutSheet->FindClip( Definition.Clip );
	if ( !SheetDefinition.Clips.IsValidIndex( OutClipIndex ) ) return Fail( "指定したクリップがありません" );
	// 選択clipに記録された元画像のframe順と時間。
	const FSpriteClip& Clip = SheetDefinition.Clips[OutClipIndex];
		if ( Clip.Frames.IsEmpty() || Clip.Frames.Num() > 256 ) return Fail( "クリップの元コマ数は1から256にしてください" );
	if ( Clip.Loop != EAnimLoopMode::Once && Clip.Loop != EAnimLoopMode::Loop && Clip.Loop != EAnimLoopMode::PingPong ) return Fail( "クリップの繰り返し方法が不正です" );
	// region矩形の外側を調べる元画像寸法。
	const int32 TextureWidth = OutSheet->GetTexture()->GetWidth();
	const int32 TextureHeight = OutSheet->GetTexture()->GetHeight();
		if ( TextureWidth <= 0 || TextureHeight <= 0 ) return Fail( "元画像の大きさが不正です" );
		OutFrames.Reset();
		OutDurations.Reset();
		OutFrames.Reserve( Clip.Frames.Num() );
		OutDurations.Reserve( Clip.Frames.Num() );
		for ( int32 Step = 0; Step < Clip.Frames.Num(); ++Step )
		{
			// 元クリップのこの段が参照するシート番号。
			const int32 FrameIndex = Clip.Frames[Step];
			const FSpriteFrame* Frame = OutSheet->GetFrame( FrameIndex );
			if ( Frame == nullptr || Frame->X < 0 || Frame->Y < 0 || Frame->Width <= 0 || Frame->Height <= 0 || static_cast<int64>( Frame->X ) + Frame->Width > TextureWidth || static_cast<int64>( Frame->Y ) + Frame->Height > TextureHeight ) return Fail( "クリップに無効な矩形、または画像の外に出るコマがあります" );
			// 明示durationが無い段だけ、有限正のFpsから秒数を求める。
		if ( !Clip.Durations.IsValidIndex( Step ) && ( !NMath::IsFinite( Clip.Fps ) || Clip.Fps <= 0.0f ) ) return Fail( "コマ時間かFPSのどちらかを有限の正数にしてください" );
		// 明示durationは値をそのまま検証し、不正値を既定時間へ置き換えない。
		const float32 Duration = Clip.Durations.IsValidIndex( Step ) ? Clip.Durations[Step] : 1.0f / Clip.Fps;
			if ( !NMath::IsFinite( Duration ) || Duration <= 0.0f ) return Fail( "コマの表示時間は有限の正数にしてください" );
			OutFrames.Add( *Frame );
			OutDurations.Add( Duration );
		}
	OutLoop = Definition.TimeMode == FEffectFlipbookDefinition::ETimeMode::Lifetime ? EAnimLoopMode::Once : Clip.Loop;
	// 検証済みdurationから、粒子時計用の再生順を作る。
	const FAnimTimeline Timeline = FAnimTimeline::Build( OutDurations, OutLoop );
		if ( !NEffectFlipbook::IsValidTimeline( Timeline ) ) return Fail( "作成したコマ送り時間軸が空か不正です" );
		return true;
	}
}

TArray<FString> NEffectFlipbook::ValidateAssets( const FEffectFlipbookDefinition& Definition, CAssetManager& Assets )
{
	TArray<FString> Issues;
	if ( !Definition.bEnabled ) return Issues;
	// GPU資源を作らずに選択clipのmetadataを検証する一時状態。
	TSharedPtr<CSpriteSheet> Sheet;
	// sheet内で解決したclip番号。
	int32 ClipIndex = INDEX_NONE;
	// 検査済みclip順の画像矩形。
	TArray<FSpriteFrame> Frames;
	// 時間軸へ渡す各段の秒数。
	TArray<float32> Durations;
	// LifetimeかSecondsかで構築する再生方式。
	EAnimLoopMode Loop = EAnimLoopMode::Once;
	// metadata検証に失敗した理由。
	FString Error;
	if ( !ReadClipSnapshot( Definition, Assets, Sheet, ClipIndex, Frames, Durations, Loop, &Error ) ) Issues.Add( FString::Printf( "Flipbook: %s", *Error ) );
	return Issues;
}

bool NEffectFlipbook::BuildFrames( const FEffectFlipbookDefinition& Definition, CAssetManager& Assets, FEffectFlipbookFrames& Out, FString* OutError )
{
	// sheetの参照を作成中だけ保持する。
	TSharedPtr<CSpriteSheet> Sheet;
	// sheet内で見つかったclip番号。
	int32 ClipIndex = INDEX_NONE;
	// 元clip順に複写した矩形列。
	TArray<FSpriteFrame> SourceFrames;
	// source frameごとの表示秒数。
	TArray<float32> Durations;
	// clockモードに応じた時間軸の繰り返し方。
	EAnimLoopMode Loop = EAnimLoopMode::Once;
	if ( !ReadClipSnapshot( Definition, Assets, Sheet, ClipIndex, SourceFrames, Durations, Loop, OutError ) ) return false;
	// すべて一時配列へ組み、失敗時に呼出し側の既存snapshotを壊さない。
	FEffectFlipbookFrames Built;
	Built.Timeline = FAnimTimeline::Build( Durations, Loop );
	Built.Frames.Reserve( SourceFrames.Num() );
	for ( const FSpriteFrame& Frame : SourceFrames )
	{
		// 同じ元矩形はAssetManagerの弱参照cacheを通して共有される。
		TSharedPtr<CTexture> Region = Assets.CreateTextureRegion( Sheet->GetTexture(), Frame.X, Frame.Y, Frame.Width, Frame.Height );
		if ( !Region.IsValid() )
		{
			if ( OutError != nullptr ) *OutError = "コマ画像の派生領域を作成できません";
			return false;
		}
		Built.Frames.Add( Region );
	}
	Out = MoveTemp( Built );
	if ( OutError != nullptr ) OutError->Reset();
	return true;
}
