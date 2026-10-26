#pragma once
#include <string>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/DefinitionDocument.h"
#include "Library/Framework/Asset/ModelAnimationAsset.h"
#include "Library/Framework/Asset/SpriteSheet.h"

//=============================================================================
// データアセット（FSpriteSheetDefinition / FModelAnimationDefinition）の編集
//   エディタ（ImGui）に依存しない、編集の操作と履歴。エディタの画面は、ここの関数を呼ぶだけにして、
//   「どう編集されるか」（番号のずれの補正・名前の重複・元に戻す）はここでテストできる。
//=============================================================================

using TSpriteSheetDocument = TDefinitionDocument<FSpriteSheetDefinition>;
using TModelAnimationDocument = TDefinitionDocument<FModelAnimationDefinition>;

//-----------------------------------------------------------------------------
// 名前: 同じ名前があれば "Name 2"、"Name 3" ... にする
//-----------------------------------------------------------------------------
namespace NDefinitionEdit
{
	FString MakeUniqueName( const TArray<FString>& ExistingNames, const FString& Wanted, const FString& Fallback );
}

//-----------------------------------------------------------------------------
// スプライトシートの編集
//   コマの番号は、格子（定義した順）、続けて自由な矩形の順。格子を足したり消したりすると、あとの番号がずれるので、
//   クリップの中の番号も一緒に直す。
//-----------------------------------------------------------------------------
namespace NSpriteSheetEdit
{
	void SetTexturePath( TSpriteSheetDocument& Document, const FString& Path );

	// 格子
	int32 AddGrid( TSpriteSheetDocument& Document, const FSpriteSheetGrid& Grid );
	void SetGrid( TSpriteSheetDocument& Document, int32 GridIndex, const FSpriteSheetGrid& Grid, const char* CoalesceKey = nullptr );
	void RemoveGrid( TSpriteSheetDocument& Document, int32 GridIndex );

	// 自由な矩形。FrameIndex はスプライトシート全体の番号（格子のコマの後ろ）
	int32 AddFrame( TSpriteSheetDocument& Document, const FSpriteFrame& Frame );
	void SetFrame( TSpriteSheetDocument& Document, int32 FrameIndex, const FSpriteFrame& Frame, const char* CoalesceKey = nullptr );
	void RemoveFrame( TSpriteSheetDocument& Document, int32 FrameIndex );

	// クリップ
	int32 AddClip( TSpriteSheetDocument& Document, const FString& Name, int32 FirstFrame = INDEX_NONE );
	void RemoveClip( TSpriteSheetDocument& Document, int32 ClipIndex );
	// 同じ名前があれば番号をつける。実際の名前を返す
	FString RenameClip( TSpriteSheetDocument& Document, int32 ClipIndex, const FString& Name );
	void SetClipFps( TSpriteSheetDocument& Document, int32 ClipIndex, float32 Fps, const char* CoalesceKey = nullptr );
	void SetClipLoop( TSpriteSheetDocument& Document, int32 ClipIndex, EAnimLoopMode Loop );

	// クリップのコマ（ステップ）。出来事は、ステップの番号に合わせて動く
	void InsertStep( TSpriteSheetDocument& Document, int32 ClipIndex, int32 At, int32 SheetFrame );
	void AppendStep( TSpriteSheetDocument& Document, int32 ClipIndex, int32 SheetFrame );
	void RemoveStep( TSpriteSheetDocument& Document, int32 ClipIndex, int32 Step );
	void MoveStep( TSpriteSheetDocument& Document, int32 ClipIndex, int32 From, int32 To );
	void SetStepFrame( TSpriteSheetDocument& Document, int32 ClipIndex, int32 Step, int32 SheetFrame );
	// 範囲（両端を含む）でクリップのコマを入れ替える（出来事は、ステップの数に収まるものだけ残る）
	void SetStepRange( TSpriteSheetDocument& Document, int32 ClipIndex, int32 First, int32 Last );
	// 表示時間（秒）。全部同じ値に戻ったら、Durations を空にする
	void SetStepDuration( TSpriteSheetDocument& Document, int32 ClipIndex, int32 Step, float32 Seconds, const char* CoalesceKey = nullptr );
	void ResetDurations( TSpriteSheetDocument& Document, int32 ClipIndex );

	// 出来事
	int32 AddEvent( TSpriteSheetDocument& Document, int32 ClipIndex, int32 Step, const FString& Name );
	void SetEvent( TSpriteSheetDocument& Document, int32 ClipIndex, int32 EventIndex, int32 Step, const FString& Name, const char* CoalesceKey = nullptr );
	void RemoveEvent( TSpriteSheetDocument& Document, int32 ClipIndex, int32 EventIndex );

	// 問題の一覧（空なら問題なし）。TextureWidth / Height が 0 より大きければ、コマが画像の外に出ていないかも確かめる
	TArray<FString> Validate( const FSpriteSheetDefinition& Definition, int32 TextureWidth, int32 TextureHeight );
}

//-----------------------------------------------------------------------------
// モデルのアニメーション定義の編集
//-----------------------------------------------------------------------------
namespace NModelAnimationEdit
{
	void SetModelPath( TModelAnimationDocument& Document, const FString& Path );
	// エディタのプレビュー用のモデル（モデルを持たないアニメーション集のとき）
	void SetPreviewModelPath( TModelAnimationDocument& Document, const FString& Path );
	void SetUnitsPerSecond( TModelAnimationDocument& Document, float32 UnitsPerSecond, const char* CoalesceKey = nullptr );
	void SetPlacement( TModelAnimationDocument& Document, const FVector3& Offset, const FVector3& RotationDegrees, const FVector3& Scale, const char* CoalesceKey = nullptr );

	// 同じ名前があれば番号をつける。追加した番号を返す
	int32 AddClip( TModelAnimationDocument& Document, const FModelClipDef& Clip );
	// 何本かをまとめて足す（1 回の取り消しで全部戻る）。足したクリップの最初の番号を返す。Clips が空なら INDEX_NONE
	int32 AddClips( TModelAnimationDocument& Document, const TArray<FModelClipDef>& Clips );
	void RemoveClip( TModelAnimationDocument& Document, int32 ClipIndex );
	void MoveClip( TModelAnimationDocument& Document, int32 From, int32 To );
	FString RenameClip( TModelAnimationDocument& Document, int32 ClipIndex, const FString& Name );
	// 名前・出来事以外の設定（Source / AnimIndex / AnimName / Loop / Speed / Fade）を入れ替える
	void SetClipSettings( TModelAnimationDocument& Document, int32 ClipIndex, const FModelClipDef& Clip, const char* CoalesceKey = nullptr );

	// 出来事。Time はクリップの長さを 1 とした位置（0..1）。時刻の順に並べて保つ。追加・移動した出来事の番号を返す
	int32 AddEvent( TModelAnimationDocument& Document, int32 ClipIndex, float32 Time, const FString& Name );
	int32 SetEvent( TModelAnimationDocument& Document, int32 ClipIndex, int32 EventIndex, float32 Time, const FString& Name, const char* CoalesceKey = nullptr );
	void RemoveEvent( TModelAnimationDocument& Document, int32 ClipIndex, int32 EventIndex );

	TArray<FString> Validate( const FModelAnimationDefinition& Definition );
}
