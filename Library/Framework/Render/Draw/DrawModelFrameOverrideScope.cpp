// SPDX-License-Identifier: MIT
#include "DrawModelFrameOverrideScope.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"

DEFINE_LOG_CATEGORY_STATIC( LogModelFrameOverrideScope, Log )

namespace
{
	// 描画命令の16値からbackendへ渡すmatrixを作る。
	FMatrix4x4 ToMatrix( const float32 ( &Values )[16] )
	{
		FMatrix4x4 Matrix; // backendへ渡す親基準行列。
		// 行優先値を行列の各列へ写す行番号。
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			// 現在行で写す列番号。
			for ( int32 Column = 0; Column < 4; ++Column )
			{
				Matrix.m[Row][Column] = Values[Row * 4 + Column];
			}
		}
		return Matrix;
	}
}

FModelFrameOverrideScope::FModelFrameOverrideScope( IAssetBackend& InBackend, int32 InModel, const FDrawModelFrameOverrideData* Overrides, int32 InNumOverrides )
	: Backend( &InBackend )
	, Model( InModel )
{
	if ( InModel < 0 )
	{
		return;
	}
	if ( InNumOverrides == 0 )
	{
		bApplied = true;
		return;
	}
	if ( !NDrawModelFramePose::IsValidOverrides( Overrides, InNumOverrides ) )
	{
		return;
	}
	RequestedOverrides.Append( Overrides, InNumOverrides );
	PreviousStates.Reserve( InNumOverrides );
	// 全件queryを終えるまで一時設定へ進まないframe位置。
	for ( int32 Index = 0; Index < RequestedOverrides.Num(); ++Index )
	{
		FFrameState Previous; // 現在frameの適用前状態。
		Previous.Frame = RequestedOverrides[Index].Frame;
		if ( !Backend->GetModelFrameLocalMatrixOverride( Model, Previous.Frame, Previous.bOverridden, Previous.Matrix ) )
		{
			LOG( LogModelFrameOverrideScope, Warning, "一時骨poseの現在状態を取得できませんでした (model %d, frame %d)。", Model, Previous.Frame );
			PreviousStates.Empty();
			return;
		}
		PreviousStates.Add( Previous );
	}
	// saved状態と同順で一時poseを適用する位置。
	for ( int32 Index = 0; Index < RequestedOverrides.Num(); ++Index )
	{
		const FDrawModelFrameOverrideData& Requested = RequestedOverrides[Index]; // 次に適用するpose値。
		if ( !Backend->SetModelFrameLocalMatrixOverride( Model, Requested.Frame, ToMatrix( Requested.M ) ) )
		{
			LOG( LogModelFrameOverrideScope, Warning, "一時骨poseを設定できませんでした (model %d, frame %d)。", Model, Requested.Frame );
			bRestoreStarted = true;
			RestorePending();
			return;
		}
		PreviousStates[Index].bPendingRestore = true;
	}
	bApplied = true;
}

FModelFrameOverrideScope::~FModelFrameOverrideScope()
{
	if ( HasPendingRestore() )
	{
		RestorePending();
	}
}

bool FModelFrameOverrideScope::IsApplied() const
{
	return bApplied && !bRestoreStarted;
}

bool FModelFrameOverrideScope::Restore()
{
	bRestoreStarted = true;
	bApplied = false;
	return RestorePending();
}

bool FModelFrameOverrideScope::RestorePending()
{
	bool bSucceeded = true; // 全frame復元が成功したかを集める値。
	// 最後に適用したframeから残っている一時poseを戻す位置。
	for ( int32 Index = PreviousStates.Num() - 1; Index >= 0; --Index )
	{
		const FFrameState Previous = PreviousStates[Index]; // backend callback中も独立して読む復元値。
		if ( !Previous.bPendingRestore )
		{
			continue;
		}
		const bool bRestored = Previous.bOverridden
			? Backend->SetModelFrameLocalMatrixOverride( Model, Previous.Frame, Previous.Matrix )
			: Backend->ClearModelFrameLocalMatrixOverride( Model, Previous.Frame );
		if ( !bRestored )
		{
			LOG( LogModelFrameOverrideScope, Warning, "モデルの一時骨pose復元に失敗しました (model %d, frame %d)。", Model, Previous.Frame );
			bSucceeded = false;
		}
		else
		{
			PreviousStates[Index].bPendingRestore = false;
		}
	}
	return bSucceeded;
}

bool FModelFrameOverrideScope::HasPendingRestore() const
{
	// 復元未完了frameを探す配列位置。
	for ( int32 Index = 0; Index < PreviousStates.Num(); ++Index )
	{
		if ( PreviousStates[Index].bPendingRestore )
		{
			return true;
		}
	}
	return false;
}
