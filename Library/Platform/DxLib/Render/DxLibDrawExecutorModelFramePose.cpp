// SPDX-License-Identifier: MIT
#include "DxLibDrawExecutorSubsystem.h"
#include "Library/Platform/DxLib/Render/DxLibRenderLog.h"

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Render/Draw/DrawModelFrameOverrideScope.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"
#include "Library/Platform/DxLib/Asset/DxLibAssetBackendSubsystem.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	// 12個のaffine値をDxLib行列へ写す。
	MATRIX ToModelFramePoseMatrix( const float32 ( &Values )[12] )
	{
		MATRIX Result{}; // DxLibへ渡すaffine変換行列。
		// 保存値を読む行番号。
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			// 各行で最後の列を除いて写すcolumn番号。
			for ( int32 Column = 0; Column < 3; ++Column )
			{
				Result.m[Row][Column] = Values[Row * 3 + Column];
			}
			Result.m[Row][3] = Row == 3 ? 1.0f : 0.0f;
		}
		return Result;
	}

	// 一時poseを設定した個体を一度描き、native failureを返す。
	bool DrawModelFramePose( const FDrawModelFramePoseData& Data )
	{
		if ( MV1SetMatrix( Data.Handle, ToModelFramePoseMatrix( Data.M ) ) < 0 )
		{
			return false;
		}
		if ( Data.AnimIndex >= 0 && MV1SetAttachAnimTime( Data.Handle, Data.AnimIndex, Data.AnimTime ) < 0 )
		{
			return false;
		}
		return MV1DrawModel( Data.Handle ) >= 0;
	}
}

void CDxLibDrawExecutorSubsystem::ExecuteModelFramePose( const FDrawCommandView& Command )
{
	static bool bInvalidLogged = false; // payload不正を一度だけ知らせる状態。
	static bool bBackendUnavailableLogged = false; // backend未準備を一度だけ知らせる状態。
	static bool bApplyFailedLogged = false; // 一時pose適用失敗を一度だけ知らせる状態。
	static bool bModelDrawFailedLogged = false; // native描画失敗を一度だけ知らせる状態。
	static bool bRestoreFailedLogged = false; // pose復元失敗を一度だけ知らせる状態。
	FDrawModelFramePoseData Data{}; // payloadから読む固定model pose値。
	if ( !NDrawModelFramePose::IsValidCommand( Command, Data ) )
	{
		if ( !bInvalidLogged )
		{
			LOG( LogDxLibDraw, Warning, "モデル骨pose命令の設定値またはpayload長が不正なため描画を読み飛ばしました。" );
			bInvalidLogged = true;
		}
		return;
	}
	if ( Data.NumOverrides == 0 )
	{
		if ( !DrawModelFramePose( Data ) && !bModelDrawFailedLogged )
		{
			LOG( LogDxLibDraw, Warning, "一時骨pose付きモデル描画のnative処理に失敗しました (model %d)。", Data.Handle );
			bModelDrawFailedLogged = true;
		}
		return;
	}
	if ( AssetBackend == nullptr )
	{
		if ( !bBackendUnavailableLogged )
		{
			LOG( LogDxLibDraw, Warning, "一時骨pose描画に必要なasset backendが無いため描画を読み飛ばしました (model %d)。", Data.Handle );
			bBackendUnavailableLogged = true;
		}
		return;
	}
	// command bufferからscopeが所有する一時pose配列へ複写する。
	TArray<FDrawModelFrameOverrideData> Overrides;
	Overrides.Reserve( Data.NumOverrides );
	// command末尾のposeを順にaligned localへ読む位置。
	for ( int32 Index = 0; Index < Data.NumOverrides; ++Index )
	{
		FDrawModelFrameOverrideData Override; // 一時pose適用へ渡す安全な値copy。
		if ( !NDrawModelFramePose::ReadOverride( Command, Index, Override ) )
		{
			return;
		}
		Overrides.Add( Override );
	}
	FModelFrameOverrideScope OverrideScope( *AssetBackend, Data.Handle, Overrides.GetData(), Overrides.Num() ); // 描画中だけposeを適用するscope。
	if ( !OverrideScope.IsApplied() )
	{
		if ( !bApplyFailedLogged )
		{
			LOG( LogDxLibDraw, Warning, "一時骨poseを設定できないためモデル描画を読み飛ばしました (model %d)。", Data.Handle );
			bApplyFailedLogged = true;
		}
		return;
	}
	const bool bDrawSucceeded = DrawModelFramePose( Data ); // native model処理が成功したか。
	const bool bRestored = OverrideScope.Restore(); // native処理後のpose復元結果。
	if ( !bDrawSucceeded && !bModelDrawFailedLogged )
	{
		LOG( LogDxLibDraw, Warning, "一時骨pose付きモデル描画のnative処理に失敗しました (model %d)。", Data.Handle );
		bModelDrawFailedLogged = true;
	}
	if ( !bRestored && !bRestoreFailedLogged )
	{
		LOG( LogDxLibDraw, Warning, "一時骨poseを元の状態へ戻せませんでした (model %d)。", Data.Handle );
		bRestoreFailedLogged = true;
	}
}
